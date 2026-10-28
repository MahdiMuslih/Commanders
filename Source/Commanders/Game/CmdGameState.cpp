#include "Game/CmdGameState.h"
#include "Audio/CmdAudioSubsystem.h"
#include "Gameplay/CmdEntity.h"
#include "Gameplay/CmdBuilding.h"
#include "Gameplay/CmdUnit.h"
#include "Gameplay/CmdSpawner.h"
#include "Mission/CmdMissionDirector.h"
#include "Data/CmdDataRegistry.h"
#include "World/CmdGridSubsystem.h"
#include "Gameplay/CmdEffect.h"
#include "Gameplay/CmdProjectile.h"
#include "Game/CmdPlayerState.h"
#include "Player/CmdPlayerController.h"
#include "World/CmdMapGenerator.h"
#include "World/CmdWorldBuilder.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "World/CmdTerrainActor.h"
#include "Net/UnrealNetwork.h"

ACmdGameState::ACmdGameState()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	PrimaryActorTick.bTickEvenWhenPaused = true;	// lobby UI and late world builds
	NetUpdateFrequency = 10.f;
}

void ACmdGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACmdGameState, Teams);
	DOREPLIFETIME(ACmdGameState, Flow);
	DOREPLIFETIME(ACmdGameState, MatchTime);
	DOREPLIFETIME(ACmdGameState, MatchResultText);
	DOREPLIFETIME(ACmdGameState, LobbySettings);
	DOREPLIFETIME(ACmdGameState, MatchConfig);
}

void ACmdGameState::BeginPlay()
{
	Super::BeginPlay();
	// Network clients have no game mode, so they start their own audio here.
	if (!HasAuthority())
	{
		if (UCmdAudioSubsystem* Audio = UCmdAudioSubsystem::Get(this))
		{
			Audio->EnsureForWorld(GetWorld());
		}
	}
}

ECmdFlowState ACmdGameState::GetDisplayFlow() const
{
	if ((Flow == ECmdFlowState::Victory || Flow == ECmdFlowState::Defeat) && IsNetworked())
	{
		const APlayerController* PC = GetWorld()->GetFirstPlayerController();
		const ACmdPlayerState* PS = PC ? PC->GetPlayerState<ACmdPlayerState>() : nullptr;
		if (PS && PS->MatchResult != ECmdMatchResult::None)
		{
			return PS->MatchResult == ECmdMatchResult::Victory ? ECmdFlowState::Victory : ECmdFlowState::Defeat;
		}
	}
	return Flow;
}

// ---------------------------------------------------------------------------
// Teams
// ---------------------------------------------------------------------------

int32 ACmdGameState::AddTeam(const FCmdTeam& Team)
{
	FCmdTeam Copy = Team;
	Copy.Id = Teams.Num();
	return Teams.Add(Copy);
}

int32 ACmdGameState::GetAlliance(int32 TeamId) const
{
	const FCmdTeam* T = GetTeam(TeamId);
	return T ? T->Alliance : -1;
}

bool ACmdGameState::AreAllied(int32 A, int32 B) const
{
	if (A == B)
	{
		return true;
	}
	const FCmdTeam* TA = GetTeam(A);
	const FCmdTeam* TB = GetTeam(B);
	if (!TA || !TB)
	{
		return false;
	}
	return TA->Alliance == TB->Alliance;
}

bool ACmdGameState::IsNeutralTeam(int32 TeamId) const
{
	const FCmdTeam* T = GetTeam(TeamId);
	return T && T->bIsNeutral;
}

FLinearColor ACmdGameState::GetTeamColor(int32 TeamId) const
{
	const FCmdTeam* T = GetTeam(TeamId);
	return T ? T->Color : FLinearColor(0.4f, 0.4f, 0.4f);
}

void ACmdGameState::AddCredits(int32 TeamId, float Amount, bool bHarvested)
{
	if (FCmdTeam* T = GetTeam(TeamId))
	{
		T->Credits = FMath::Max(0.f, T->Credits + Amount);
		if (bHarvested)
		{
			T->Stats.CreditsHarvested += Amount;
		}
	}
}

// ---------------------------------------------------------------------------
// Entity registry and events
// ---------------------------------------------------------------------------

void ACmdGameState::RegisterEntity(ACmdEntity* Entity)
{
	if (!Entity)
	{
		return;
	}
	Entities.AddUnique(Entity);
	if (!Entity->MissionTag.IsNone())
	{
		SeenTags.Add(Entity->MissionTag);
	}
	if (FCmdTeam* T = GetTeam(Entity->TeamId))
	{
		T->Owned.FindOrAdd(Entity->DefId)++;
	}
	RecalculatePower();
	if (!HasAuthority() && bClientWorldBuilt)
	{
		Entity->RefreshTeamColor();	// teams are known by now
	}
	// Reveal immediately so freshly spawned units do not pop out of the shroud a frame late.
	if (UCmdGridSubsystem* Grid = GetWorld()->GetSubsystem<UCmdGridSubsystem>())
	{
		if (Entity->Def && !IsNeutralTeam(Entity->TeamId))
		{
			Grid->RevealCircle(GetAlliance(Entity->TeamId), Entity->GetActorLocation(), Entity->Def->Sight);
		}
		Entity->UpdateShroudVisibility(bShroudEnabled ? GetLocalAlliance() : -1);
	}
}

void ACmdGameState::UnregisterEntity(ACmdEntity* Entity)
{
	if (!Entity)
	{
		return;
	}
	if (Entities.Remove(Entity) > 0)
	{
		if (FCmdTeam* T = GetTeam(Entity->TeamId))
		{
			if (int32* Count = T->Owned.Find(Entity->DefId))
			{
				*Count = FMath::Max(0, *Count - 1);
			}
		}
		RecalculatePower();
	}
}

void ACmdGameState::OnEntityDamaged(ACmdEntity* Victim, ACmdEntity* Attacker)
{
	FCmdTeam* Team = Victim ? GetTeam(Victim->TeamId) : nullptr;
	if (!Team || !Attacker || Flow != ECmdFlowState::Playing || Team->bIsAI || Team->bIsNeutral)
	{
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now - Team->LastAttackWarning > 20.f)
	{
		Team->LastAttackWarning = Now;
		AddMessage(Victim->TeamId, Victim->IsBuilding() ? TEXT("Our base is under attack!") : TEXT("Unit under attack."), FLinearColor(1.f, 0.35f, 0.25f), TEXT("alarm"));
	}
}

void ACmdGameState::OnEntityKilled(ACmdEntity* Victim, ACmdEntity* Killer)
{
	if (!Victim)
	{
		return;
	}
	if (FCmdTeam* T = GetTeam(Victim->TeamId))
	{
		Victim->IsBuilding() ? ++T->Stats.BuildingsLost : ++T->Stats.UnitsLost;
	}
	if (Killer)
	{
		if (FCmdTeam* K = GetTeam(Killer->TeamId))
		{
			++K->Stats.Kills;
		}
	}
	if (Flow == ECmdFlowState::Playing)
	{
		if (Victim->IsBuilding())
		{
			AddMessage(Victim->TeamId, FString::Printf(TEXT("Structure lost: %s"), Victim->Def ? *Victim->Def->Name : TEXT("")), FLinearColor(1.f, 0.4f, 0.3f), TEXT("objective_failed"));
		}
	}
	if (MissionDirector)
	{
		MissionDirector->OnEntityDestroyed(Victim, Killer);
	}
	UnregisterEntity(Victim);
}

void ACmdGameState::OnEntitySold(ACmdBuilding* Building)
{
	UnregisterEntity(Building);
	if (MissionDirector)
	{
		MissionDirector->OnEntityDestroyed(Building, nullptr);
	}
}

void ACmdGameState::OnEntityChangedTeam(ACmdEntity* Entity, int32 OldTeam)
{
	if (!Entity)
	{
		return;
	}
	if (FCmdTeam* Old = GetTeam(OldTeam))
	{
		if (int32* Count = Old->Owned.Find(Entity->DefId))
		{
			*Count = FMath::Max(0, *Count - 1);
		}
	}
	if (FCmdTeam* New = GetTeam(Entity->TeamId))
	{
		New->Owned.FindOrAdd(Entity->DefId)++;
	}
	RecalculatePower();
	if (MissionDirector)
	{
		MissionDirector->OnEntityCaptured(Entity, OldTeam);
	}
}

void ACmdGameState::OnStructureCompleted(ACmdBuilding* Building)
{
	if (!Building || !Building->Def)
	{
		return;
	}
	if (FCmdTeam* T = GetTeam(Building->TeamId))
	{
		++T->Stats.BuildingsBuilt;
	}
	if (!Building->Def->FreeUnit.IsNone())
	{
		const FIntPoint Dock = Building->GetDockCell();
		CmdSpawner::SpawnAtCell(GetWorld(), Building->Def->FreeUnit, Building->TeamId, Dock, NAME_None, 90.f);
	}
	if (MissionDirector)
	{
		MissionDirector->OnStructureBuilt(Building);
	}
}

int32 ACmdGameState::CountTeamEntities(int32 TeamId, bool bIncludeBuildings, bool bIncludeUnits) const
{
	int32 Count = 0;
	for (const ACmdEntity* E : Entities)
	{
		if (!E || !E->IsAlive() || E->TeamId != TeamId || (E->Def && E->Def->bIgnoreForVictory))
		{
			continue;
		}
		if (E->IsBuilding() ? bIncludeBuildings : bIncludeUnits)
		{
			++Count;
		}
	}
	return Count;
}

TArray<ACmdEntity*> ACmdGameState::GetEntitiesWithTag(FName Tag) const
{
	TArray<ACmdEntity*> Out;
	for (ACmdEntity* E : Entities)
	{
		if (E && E->IsAlive() && E->MissionTag == Tag)
		{
			Out.Add(E);
		}
	}
	return Out;
}

FVector ACmdGameState::GetTeamBaseLocation(int32 TeamId) const
{
	FVector Sum = FVector::ZeroVector;
	int32 Count = 0;
	for (const ACmdEntity* E : Entities)
	{
		const ACmdBuilding* B = Cast<ACmdBuilding>(E);
		if (B && B->TeamId == TeamId && B->IsAlive())
		{
			if (B->Def && B->Def->bConstructionYard)
			{
				return B->GetActorLocation();
			}
			Sum += B->GetActorLocation();
			++Count;
		}
	}
	if (Count > 0)
	{
		return Sum / Count;
	}
	const FCmdTeam* T = GetTeam(TeamId);
	return T ? Cmd::CellToWorld(T->StartCell) : FVector::ZeroVector;
}

// ---------------------------------------------------------------------------
// Production
// ---------------------------------------------------------------------------

bool ACmdGameState::HasPrerequisites(int32 TeamId, const FCmdEntityDef& Def, FString* OutMissing) const
{
	const FCmdTeam* T = GetTeam(TeamId);
	if (!T)
	{
		return false;
	}
	const UCmdDataRegistry* Data = UCmdDataRegistry::Get(this);
	bool bOk = true;
	for (const FName& Req : Def.Prereqs)
	{
		if (T->CountOwned(Req) <= 0)
		{
			bOk = false;
			if (OutMissing)
			{
				const FCmdEntityDef* ReqDef = Data ? Data->GetEntity(Req) : nullptr;
				if (!OutMissing->IsEmpty()) OutMissing->Append(TEXT(", "));
				OutMissing->Append(ReqDef ? ReqDef->Name : Req.ToString());
			}
		}
	}
	return bOk;
}

bool ACmdGameState::HasProducer(int32 TeamId, ECmdCategory Category) const
{
	return FindProducer(TeamId, Category) != nullptr;
}

ACmdBuilding* ACmdGameState::FindProducer(int32 TeamId, ECmdCategory Category) const
{
	ACmdBuilding* Fallback = nullptr;
	for (ACmdEntity* E : Entities)
	{
		ACmdBuilding* B = Cast<ACmdBuilding>(E);
		if (B && B->TeamId == TeamId && B->IsAlive() && B->IsConstructed() && B->Produces(Category))
		{
			if (B->bPrimary)
			{
				return B;
			}
			if (!Fallback)
			{
				Fallback = B;
			}
		}
	}
	return Fallback;
}

void ACmdGameState::SetPrimary(ACmdBuilding* Building)
{
	if (!Building || !Building->Def)
	{
		return;
	}
	for (ACmdEntity* E : Entities)
	{
		ACmdBuilding* B = Cast<ACmdBuilding>(E);
		if (B && B->TeamId == Building->TeamId && B->Def)
		{
			for (ECmdCategory C : Building->Def->Produces)
			{
				if (B->Produces(C))
				{
					B->bPrimary = false;
				}
			}
		}
	}
	Building->bPrimary = true;
	AddMessage(Building->TeamId, FString::Printf(TEXT("%s set as primary."), *Building->Def->Name));
}

bool ACmdGameState::HasRadar(int32 TeamId) const
{
	const FCmdTeam* T = GetTeam(TeamId);
	if (!T || T->HasLowPower())
	{
		return false;
	}
	for (const ACmdEntity* E : Entities)
	{
		if (E && E->TeamId == TeamId && E->IsAlive() && E->Def && E->Def->bRadar)
		{
			return true;
		}
	}
	return false;
}

bool ACmdGameState::IsVisibleInSidebar(int32 TeamId, const FCmdEntityDef& Def) const
{
	const FCmdTeam* T = GetTeam(TeamId);
	if (!T || !Def.bBuildable || T->TechLock.Contains(Def.Id))
	{
		return false;
	}
	if (Def.Faction != TEXT("any") && Def.Faction != T->Faction)
	{
		return false;
	}
	if (Def.bIsBuilding && !T->bCanBuild)
	{
		return false;
	}
	return HasPrerequisites(TeamId, Def);
}

bool ACmdGameState::CanTeamBuild(int32 TeamId, const FCmdEntityDef& Def) const
{
	return IsVisibleInSidebar(TeamId, Def) && HasProducer(TeamId, Def.Category);
}

TArray<const FCmdEntityDef*> ACmdGameState::GetBuildOptions(int32 TeamId, ECmdCategory Category) const
{
	TArray<const FCmdEntityDef*> Out;
	const UCmdDataRegistry* Data = UCmdDataRegistry::Get(this);
	if (!Data)
	{
		return Out;
	}
	for (const FName& Id : Data->GetEntityOrder())
	{
		const FCmdEntityDef* Def = Data->GetEntity(Id);
		if (Def && Def->Category == Category && IsVisibleInSidebar(TeamId, *Def))
		{
			Out.Add(Def);
		}
	}
	return Out;
}

int32 ACmdGameState::CountQueued(int32 TeamId, FName DefId) const
{
	const FCmdTeam* T = GetTeam(TeamId);
	if (!T)
	{
		return 0;
	}
	int32 Count = 0;
	for (const FCmdProductionQueue& Q : T->Queues)
	{
		for (const FName& Item : Q.Items)
		{
			Count += Item == DefId ? 1 : 0;
		}
	}
	return Count;
}

bool ACmdGameState::QueueProduction(int32 TeamId, FName DefId, bool bNotify)
{
	FCmdTeam* T = GetTeam(TeamId);
	const UCmdDataRegistry* Data = UCmdDataRegistry::Get(this);
	const FCmdEntityDef* Def = Data ? Data->GetEntity(DefId) : nullptr;
	if (!T || !Def || !CanTeamBuild(TeamId, *Def))
	{
		return false;
	}
	FCmdProductionQueue& Q = T->Queues[static_cast<int32>(Def->Category)];
	if (Def->bIsBuilding)
	{
		if (Q.Items.Num() > 0)
		{
			if (bNotify) AddMessage(TeamId, TEXT("Unable to comply, building in progress."), FLinearColor(0.9f, 0.9f, 0.6f), TEXT("deny"));
			return false;
		}
	}
	else if (Q.Items.Num() >= 10)
	{
		return false;
	}
	if (Q.Items.Num() == 0)
	{
		Q.Progress = 0.f;
		Q.Spent = 0.f;
		Q.bOnHold = false;
		Q.bReady = false;
	}
	Q.Items.Add(DefId);
	if (bNotify && Def->bIsBuilding)
	{
		AddMessage(TeamId, TEXT("Building."), FLinearColor(0.9f, 0.9f, 0.6f), TEXT("queue"));
	}
	else if (bNotify && Q.Items.Num() == 1)
	{
		AddMessage(TeamId, TEXT("Training."), FLinearColor(0.9f, 0.9f, 0.6f), TEXT("queue"));
	}
	return true;
}

void ACmdGameState::CancelProduction(int32 TeamId, FName DefId)
{
	FCmdTeam* T = GetTeam(TeamId);
	const UCmdDataRegistry* Data = UCmdDataRegistry::Get(this);
	const FCmdEntityDef* Def = Data ? Data->GetEntity(DefId) : nullptr;
	if (!T || !Def)
	{
		return;
	}
	FCmdProductionQueue& Q = T->Queues[static_cast<int32>(Def->Category)];
	const int32 LastIndex = Q.Items.FindLast(DefId);
	if (LastIndex == INDEX_NONE)
	{
		return;
	}
	if (LastIndex == 0)
	{
		// Holding the front item first, cancelling a held item refunds it (classic two-click cancel).
		if (!Q.bOnHold && !Q.bReady && Q.Spent > 0.f && Q.Items.Num() == 1)
		{
			Q.bOnHold = true;
			AddMessage(TeamId, TEXT("On hold."), FLinearColor(0.9f, 0.9f, 0.6f), TEXT("click"));
			return;
		}
		T->Credits += Q.Spent;
		T->Stats.CreditsSpent -= Q.Spent;
		Q.Spent = 0.f;
		Q.Progress = 0.f;
		Q.bReady = false;
		Q.bOnHold = false;
	}
	Q.Items.RemoveAt(LastIndex);
	AddMessage(TeamId, TEXT("Cancelled."), FLinearColor(0.9f, 0.9f, 0.6f), TEXT("click"));
}

void ACmdGameState::ToggleHold(int32 TeamId, ECmdCategory Category)
{
	if (FCmdTeam* T = GetTeam(TeamId))
	{
		FCmdProductionQueue& Q = T->Queues[static_cast<int32>(Category)];
		if (Q.Items.Num() > 0 && !Q.bReady)
		{
			Q.bOnHold = !Q.bOnHold;
		}
	}
}

FName ACmdGameState::GetReadyStructure(int32 TeamId) const
{
	const FCmdTeam* T = GetTeam(TeamId);
	if (!T)
	{
		return NAME_None;
	}
	const FCmdProductionQueue& Q = T->Queues[static_cast<int32>(ECmdCategory::Structure)];
	return (Q.bReady && Q.Items.Num() > 0) ? Q.Items[0] : NAME_None;
}

bool ACmdGameState::PlaceReadyStructure(int32 TeamId, const FIntPoint& Origin)
{
	FCmdTeam* T = GetTeam(TeamId);
	const FName Ready = GetReadyStructure(TeamId);
	const UCmdDataRegistry* Data = UCmdDataRegistry::Get(this);
	const FCmdEntityDef* Def = Data ? Data->GetEntity(Ready) : nullptr;
	const UCmdGridSubsystem* Grid = GetWorld()->GetSubsystem<UCmdGridSubsystem>();
	if (!T || !Def || !Grid)
	{
		return false;
	}
	if (!Grid->CanPlaceStructure(TeamId, *Def, Origin, true))
	{
		return false;
	}
	FCmdProductionQueue& Q = T->Queues[static_cast<int32>(ECmdCategory::Structure)];
	Q.Items.RemoveAt(0);
	Q.bReady = false;
	Q.Progress = 0.f;
	Q.Spent = 0.f;
	CmdSpawner::SpawnBuilding(GetWorld(), Def->Id, TeamId, Origin, NAME_None, true);
	return true;
}

void ACmdGameState::TickProduction(float DeltaSeconds)
{
	const UCmdDataRegistry* Data = UCmdDataRegistry::Get(this);
	if (!Data)
	{
		return;
	}
	for (FCmdTeam& Team : Teams)
	{
		if (Team.bDefeated || Team.bIsNeutral)
		{
			continue;
		}
		for (int32 Cat = 0; Cat < CmdConst::CategoryCount; ++Cat)
		{
			FCmdProductionQueue& Q = Team.Queues[Cat];
			if (Q.Items.Num() == 0 || Q.bOnHold || Q.bReady)
			{
				continue;
			}
			const FCmdEntityDef* Def = Data->GetEntity(Q.Items[0]);
			if (!Def)
			{
				Q.Items.RemoveAt(0);
				continue;
			}
			// Lost the factory or the tech: production pauses.
			if (!HasProducer(Team.Id, static_cast<ECmdCategory>(Cat)))
			{
				continue;
			}

			int32 Factories = 0;
			for (const ACmdEntity* E : Entities)
			{
				const ACmdBuilding* B = Cast<ACmdBuilding>(E);
				if (B && B->TeamId == Team.Id && B->IsConstructed() && B->Produces(static_cast<ECmdCategory>(Cat)))
				{
					++Factories;
				}
			}
			const float SpeedMultiplier = (1.f + 0.4f * FMath::Max(0, Factories - 1)) * Team.PowerFactor() * Team.BuildSpeed;
			const float DeltaProgress = DeltaSeconds * SpeedMultiplier / FMath::Max(0.5f, Def->BuildTime);
			const float StepProgress = FMath::Min(DeltaProgress, 1.f - Q.Progress);
			const float StepCost = Def->Cost * StepProgress;
			if (Team.Credits < StepCost)
			{
				continue;	// stalls until money arrives
			}
			Team.Credits -= StepCost;
			Team.Stats.CreditsSpent += StepCost;
			Q.Spent += StepCost;
			Q.Progress += StepProgress;

			if (Q.Progress >= 1.f - KINDA_SMALL_NUMBER)
			{
				FinishProduction(Team, static_cast<ECmdCategory>(Cat));
			}
		}
	}
}

void ACmdGameState::FinishProduction(FCmdTeam& Team, ECmdCategory Category)
{
	FCmdProductionQueue& Q = Team.Queues[static_cast<int32>(Category)];
	const UCmdDataRegistry* Data = UCmdDataRegistry::Get(this);
	const FCmdEntityDef* Def = Data ? Data->GetEntity(Q.Items[0]) : nullptr;
	if (!Def)
	{
		return;
	}
	if (Def->bIsBuilding)
	{
		Q.bReady = true;
		Q.Progress = 1.f;
		AddMessage(Team.Id, TEXT("Construction complete."), FLinearColor(0.5f, 1.f, 0.5f), TEXT("build_complete"));
		return;
	}
	Q.Items.RemoveAt(0);
	Q.Progress = 0.f;
	Q.Spent = 0.f;
	SpawnProducedUnit(Team.Id, *Def);
	++Team.Stats.UnitsBuilt;
	AddMessage(Team.Id, TEXT("Unit ready."), FLinearColor(0.5f, 1.f, 0.5f), TEXT("unit_ready"));
}

void ACmdGameState::SpawnProducedUnit(int32 TeamId, const FCmdEntityDef& Def)
{
	ACmdBuilding* Producer = FindProducer(TeamId, Def.Category);
	if (!Producer)
	{
		return;
	}
	ACmdUnit* Unit = nullptr;
	if (Def.bAircraft)
	{
		FVector L = Producer->GetActorLocation();
		L.Z = 120.f;
		Unit = CmdSpawner::SpawnUnit(GetWorld(), Def.Id, TeamId, L, 90.f, NAME_None);
		if (Unit)
		{
			Unit->SetInitialAltitude(120.f);
		}
	}
	else
	{
		Unit = Cast<ACmdUnit>(CmdSpawner::SpawnAtCell(GetWorld(), Def.Id, TeamId, Producer->GetExitCell(), NAME_None, 90.f));
	}
	if (Unit && !Def.bHarvester)
	{
		const UCmdGridSubsystem* Grid = GetWorld()->GetSubsystem<UCmdGridSubsystem>();
		if (Producer->RallyCell.X >= 0 && Grid && Grid->IsInside(Producer->RallyCell))
		{
			Unit->OrderMove(Producer->RallyCell, false);
		}
	}
	if (Unit && MissionDirector)
	{
		MissionDirector->OnUnitProduced(Unit);
	}
}

void ACmdGameState::RecalculatePower()
{
	for (FCmdTeam& T : Teams)
	{
		T.PowerProduced = 0;
		T.PowerConsumed = 0;
	}
	for (const ACmdEntity* E : Entities)
	{
		if (!E || !E->IsAlive() || !E->Def || !E->IsBuilding())
		{
			continue;
		}
		if (FCmdTeam* T = GetTeam(E->TeamId))
		{
			if (E->Def->Power > 0)
			{
				// Damaged power plants produce less.
				T->PowerProduced += FMath::RoundToInt(E->Def->Power * FMath::Lerp(0.5f, 1.f, E->GetHealthPct()));
			}
			else
			{
				T->PowerConsumed -= E->Def->Power;
			}
		}
	}
}

// ---------------------------------------------------------------------------
// Messages
// ---------------------------------------------------------------------------

void ACmdGameState::AddMessage(int32 TeamId, const FString& Text, const FLinearColor& Color, FName Sound)
{
	const ENetMode NetMode = GetNetMode();
	if (NetMode == NM_ListenServer || NetMode == NM_DedicatedServer)
	{
		// Route to the machines of the players who own that team (the host included).
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			ACmdPlayerController* PC = Cast<ACmdPlayerController>(It->Get());
			if (PC && (TeamId == INDEX_NONE || PC->GetCommandTeam() == TeamId))
			{
				PC->ClientShowMessage(Text, Color, Sound);
			}
		}
		return;
	}
	if (TeamId == LocalTeamId || TeamId == INDEX_NONE)
	{
		ShowMessageLocal(Text, Color, Sound);
	}
}

void ACmdGameState::ShowMessageLocal(const FString& Text, const FLinearColor& Color, FName Sound)
{
	UCmdAudioSubsystem::Play2D(this, Sound);
	// Collapse duplicates.
	if (Messages.Num() > 0 && Messages.Last().Text == Text)
	{
		Messages.Last().Time = GetWorld()->GetTimeSeconds();
		return;
	}
	FCmdMessage M;
	M.Text = Text;
	M.Color = Color;
	M.Time = GetWorld()->GetTimeSeconds();
	Messages.Add(M);
	ChatLog.Add(M);
	while (ChatLog.Num() > 10)
	{
		ChatLog.RemoveAt(0);
	}
	while (Messages.Num() > 6)
	{
		Messages.RemoveAt(0);
	}
}

// ---------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------

void ACmdGameState::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Clean out anything that was destroyed without telling us.
	Entities.RemoveAll([](const TObjectPtr<ACmdEntity>& E) { return !IsValid(E); });

	if (!HasAuthority())
	{
		// Clients only derive what they display; the server simulates.
		TryBuildClientWorld();
		UpdateSpectatorView();
		TickShroud(DeltaSeconds);
		PowerTimer -= DeltaSeconds;
		if (PowerTimer <= 0.f)
		{
			PowerTimer = 1.f;
			RecalculatePower();
		}
		const float ClientNow = GetWorld()->GetTimeSeconds();
		Messages.RemoveAll([ClientNow](const FCmdMessage& M) { return ClientNow - M.Time > 7.f; });
		return;
	}
	if (GetWorld()->IsPaused())
	{
		return;
	}
	UpdateSpectatorView();

	const bool bSimulating = Flow == ECmdFlowState::Playing || Flow == ECmdFlowState::MainMenu || Flow == ECmdFlowState::Cinematic;
	if (Flow == ECmdFlowState::Playing)
	{
		MatchTime += DeltaSeconds;
	}
	if (bSimulating && Flow != ECmdFlowState::Cinematic)
	{
		TickProduction(DeltaSeconds);
	}
	TickShroud(DeltaSeconds);

	PowerTimer -= DeltaSeconds;
	if (PowerTimer <= 0.f)
	{
		PowerTimer = 1.f;
		RecalculatePower();
		for (FCmdTeam& T : Teams)
		{
			if (T.bIsAI || T.bIsNeutral)
			{
				continue;
			}
			if (T.HasLowPower() && !T.bLowPowerWarned && Flow == ECmdFlowState::Playing)
			{
				AddMessage(T.Id, TEXT("Low power."), FLinearColor(1.f, 0.6f, 0.2f), TEXT("low_power"));
			}
			T.bLowPowerWarned = T.HasLowPower();
		}
	}

	OreTimer -= DeltaSeconds;
	if (OreTimer <= 0.f)
	{
		OreTimer = 15.f;
		if (UCmdGridSubsystem* Grid = GetWorld()->GetSubsystem<UCmdGridSubsystem>())
		{
			Grid->GrowOre();
		}
	}
	if (IsNetworked())
	{
		OreFlushTimer -= DeltaSeconds;
		if (OreFlushTimer <= 0.f)
		{
			OreFlushTimer = 0.4f;
			FlushOreChanges();
		}
	}

	if (bSimulating)
	{
		DefeatTimer -= DeltaSeconds;
		if (DefeatTimer <= 0.f)
		{
			DefeatTimer = 1.f;
			CheckDefeat();
		}
	}

	const float Now = GetWorld()->GetTimeSeconds();
	Messages.RemoveAll([Now](const FCmdMessage& M) { return Now - M.Time > 7.f; });
}

void ACmdGameState::TickShroud(float DeltaSeconds)
{
	ShroudTimer -= DeltaSeconds;
	if (ShroudTimer > 0.f)
	{
		return;
	}
	ShroudTimer = 0.25f;
	UCmdGridSubsystem* Grid = GetWorld()->GetSubsystem<UCmdGridSubsystem>();
	if (!Grid || !Grid->IsInitialized())
	{
		return;
	}
	for (ACmdEntity* E : Entities)
	{
		if (E && E->IsAlive() && E->Def && !IsNeutralTeam(E->TeamId))
		{
			Grid->RevealCircle(GetAlliance(E->TeamId), E->GetActorLocation(), E->Def->Sight);
		}
	}
	const int32 Viewer = bShroudEnabled ? GetLocalAlliance() : -1;
	for (ACmdEntity* E : Entities)
	{
		if (E)
		{
			E->UpdateShroudVisibility(Viewer);
		}
	}
}

void ACmdGameState::CheckDefeat()
{
	for (FCmdTeam& T : Teams)
	{
		if (T.bDefeated || T.bIsNeutral)
		{
			continue;
		}
		if (CountTeamEntities(T.Id) == 0)
		{
			T.bDefeated = true;
			if ((T.Id != LocalTeamId || IsNetworked()) && Flow == ECmdFlowState::Playing)
			{
				AddMessage(INDEX_NONE, FString::Printf(TEXT("%s has been eliminated."), *T.Name), FLinearColor(0.6f, 0.9f, 1.f), TEXT("objective"));
			}
			if (MissionDirector)
			{
				MissionDirector->OnTeamDefeated(T.Id);
			}
		}
	}
}

// ---------------------------------------------------------------------------
// Networking
// ---------------------------------------------------------------------------

void ACmdGameState::BroadcastSound(FName Sound, const FVector& Location)
{
	if (IsNetworked() && HasAuthority())
	{
		MulticastSound(Sound, Location);
	}
	else
	{
		UCmdAudioSubsystem::PlayAt(this, Sound, Location);
	}
}

void ACmdGameState::MulticastSound_Implementation(FName Sound, FVector_NetQuantize Location)
{
	UCmdAudioSubsystem::PlayAt(this, Sound, Location);
}

void ACmdGameState::MulticastShot_Implementation(FName WeaponId, FVector_NetQuantize Muzzle, ACmdEntity* Target, FVector_NetQuantize TargetLocation)
{
	const UCmdDataRegistry* Data = UCmdDataRegistry::Get(this);
	const FCmdWeaponDef* Weapon = Data ? Data->GetWeapon(WeaponId) : nullptr;
	UWorld* World = GetWorld();
	if (!Weapon || !World)
	{
		return;
	}
	if (Weapon->MuzzleFlash > 0.f)
	{
		ACmdEffect::SpawnFlashLocal(World, Muzzle, Weapon->Color, 0.25f * Weapon->MuzzleFlash);
	}
	UCmdAudioSubsystem::PlayWeapon(this, *Weapon, Muzzle);

	if (Weapon->ProjectileSpeed <= 0.f)
	{
		for (int32 i = 0; i < Weapon->Burst; ++i)
		{
			ACmdEffect::SpawnBeamLocal(World, Muzzle, TargetLocation, Weapon->Color);
		}
		ACmdProjectile::SpawnImpactEffects(World, *Weapon, TargetLocation);
		return;
	}
	// The server flies real projectiles; everyone else gets harmless look-alikes.
	if (!HasAuthority())
	{
		for (int32 i = 0; i < Weapon->Burst; ++i)
		{
			const FVector Spread = Weapon->Burst > 1 ? FVector(FMath::FRandRange(-40.f, 40.f), FMath::FRandRange(-40.f, 40.f), 0.f) : FVector::ZeroVector;
			ACmdProjectile::Launch(World, nullptr, *Weapon, FVector(Muzzle) + Spread * 0.3f, Target, FVector(TargetLocation) + Spread, i * 0.08f, true);
		}
	}
}

void ACmdGameState::MulticastEffect_Implementation(uint8 Kind, FVector_NetQuantize A, FVector_NetQuantize B, FColor Color, float Size, bool bFlag)
{
	UWorld* World = GetWorld();
	switch (static_cast<ECmdNetEffect>(Kind))
	{
	case ECmdNetEffect::Explosion: ACmdEffect::SpawnExplosionLocal(World, A, Size, bFlag); break;
	case ECmdNetEffect::Flash: ACmdEffect::SpawnFlashLocal(World, A, FLinearColor(Color), Size); break;
	case ECmdNetEffect::Beam: ACmdEffect::SpawnBeamLocal(World, A, B, FLinearColor(Color)); break;
	case ECmdNetEffect::Smoke: ACmdEffect::SpawnSmokeLocal(World, A, Size); break;
	default: break;
	}
}

void ACmdGameState::FlushOreChanges()
{
	UCmdGridSubsystem* Grid = GetWorld()->GetSubsystem<UCmdGridSubsystem>();
	if (!Grid)
	{
		return;
	}
	TArray<FCmdOreDelta> Changes;
	Grid->ConsumeOreChanges(Changes);
	if (Changes.Num() > 0)
	{
		MulticastOre(Changes);
	}
}

void ACmdGameState::MulticastOre_Implementation(const TArray<FCmdOreDelta>& Changes)
{
	if (HasAuthority())
	{
		return;	// the server's grid is the source of these changes
	}
	if (UCmdGridSubsystem* Grid = GetWorld()->GetSubsystem<UCmdGridSubsystem>())
	{
		for (const FCmdOreDelta& Change : Changes)
		{
			Grid->ApplyOreDelta(Change.Index, Change.Amount);
		}
	}
}

void ACmdGameState::OnRep_MatchConfig()
{
	TryBuildClientWorld();
}

void ACmdGameState::TryBuildClientWorld()
{
	if (HasAuthority() || bClientWorldBuilt || !MatchConfig.bValid)
	{
		return;
	}
	// Which seat is ours? Our PlayerState may arrive after the config does.
	ACmdPlayerController* PC = Cast<ACmdPlayerController>(GetWorld()->GetFirstPlayerController());
	const APlayerState* PS = PC ? PC->PlayerState.Get() : nullptr;
	if (!PS)
	{
		return;
	}
	int32 Seat = INDEX_NONE;
	for (int32 i = 0; i < MatchConfig.Slots.Num(); ++i)
	{
		if (MatchConfig.Slots[i].bHuman && MatchConfig.Slots[i].PlayerId == PS->GetPlayerId())
		{
			Seat = i;
			break;
		}
	}
	if (Seat == INDEX_NONE || !Teams.IsValidIndex(Seat))
	{
		return;	// teams not replicated yet, try again next tick
	}

	bClientWorldBuilt = true;
	LocalTeamId = Seat;
	bShroudEnabled = true;
	const TSharedPtr<FJsonObject> Scenario = CmdMapGenerator::GenerateMatch(MatchConfig, UCmdDataRegistry::Get(this), Seat);
	const FCmdBuiltWorld Built = CmdWorldBuilder::BuildEnvironment(GetWorld(), Scenario, true, GetAlliance(Seat), Seat);

	// Entities that replicated in before the grid and team list existed need their
	// footprints and team colours.
	if (UCmdGridSubsystem* Grid = GetWorld()->GetSubsystem<UCmdGridSubsystem>())
	{
		for (ACmdEntity* E : Entities)
		{
			if (E)
			{
				E->RefreshTeamColor();
			}
			if (ACmdBuilding* B = Cast<ACmdBuilding>(E))
			{
				if (B->Def)
				{
					Grid->SetBuilding(B->Origin, B->Def->Footprint, B);
				}
			}
		}
	}
	PC->OnSessionBuilt(Built.CameraStart, false);
}

void ACmdGameState::UpdateSpectatorView()
{
	const FCmdTeam* Local = GetTeam(LocalTeamId);
	if (bSpectatorView || !IsNetworked() || !Local || !Local->bDefeated || Flow != ECmdFlowState::Playing)
	{
		return;
	}
	bSpectatorView = true;
	bShroudEnabled = false;
	if (UCmdGridSubsystem* Grid = GetWorld()->GetSubsystem<UCmdGridSubsystem>())
	{
		Grid->SetShroudViewer(-1);
	}
	for (TActorIterator<ACmdTerrainActor> It(GetWorld()); It; ++It)
	{
		It->SetShroudVisible(false);
	}
}
