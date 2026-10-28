#include "AI/CmdAICommander.h"
#include "Game/CmdGameState.h"
#include "Gameplay/CmdUnit.h"
#include "Gameplay/CmdBuilding.h"
#include "Data/CmdDataRegistry.h"
#include "World/CmdGridSubsystem.h"
#include "Core/CmdJson.h"
#include "Engine/World.h"

void UCmdAICommander::Init(UWorld* InWorld, int32 InTeamId, int32 InDifficulty)
{
	World = InWorld;
	TeamId = InTeamId;
	Difficulty = FMath::Clamp(InDifficulty, 0, 2);
	Rng.Initialize(1337 + TeamId * 97);

	static const float FirstAttack[3] = { 420.f, 300.f, 200.f };
	static const float Interval[3] = { 240.f, 170.f, 120.f };
	static const int32 MinSize[3] = { 4, 6, 9 };
	static const float Bonus[3] = { 0.f, 1.5f, 5.f };
	FirstAttackDelay = FirstAttack[Difficulty];
	AttackInterval = Interval[Difficulty];
	MinAttackSize = MinSize[Difficulty];
	IncomeBonus = Bonus[Difficulty];
	AttackTimer = FirstAttackDelay;
	ThinkTimer = Rng.FRandRange(0.f, 1.f);

	if (ACmdGameState* State = GS())
	{
		if (FCmdTeam* T = State->GetTeam(TeamId))
		{
			static const float Speed[3] = { 0.75f, 1.f, 1.25f };
			T->BuildSpeed = Speed[Difficulty];
			T->bIsAI = true;
		}
	}
}

void UCmdAICommander::Configure(const TSharedPtr<FJsonObject>& Json)
{
	if (!Json.IsValid())
	{
		return;
	}
	if (CmdJson::Has(Json, TEXT("difficulty")))
	{
		const FString D = CmdJson::Str(Json, TEXT("difficulty"));
		Init(World.Get(), TeamId, D == TEXT("easy") ? 0 : (D == TEXT("hard") ? 2 : 1));
	}
	bEnabled = CmdJson::Bool(Json, TEXT("enabled"), bEnabled);
	bBuildBase = CmdJson::Bool(Json, TEXT("build"), bBuildBase);
	bProduceUnits = CmdJson::Bool(Json, TEXT("produce"), bProduceUnits);
	bAttack = CmdJson::Bool(Json, TEXT("attack"), bAttack);
	bDefend = CmdJson::Bool(Json, TEXT("defend"), bDefend);
	if (CmdJson::Has(Json, TEXT("first_attack")))
	{
		FirstAttackDelay = CmdJson::Num(Json, TEXT("first_attack"), FirstAttackDelay);
		AttackTimer = FirstAttackDelay;
	}
	AttackInterval = CmdJson::Num(Json, TEXT("attack_interval"), AttackInterval);
	MinAttackSize = CmdJson::Int(Json, TEXT("attack_size"), MinAttackSize);
	IncomeBonus = CmdJson::Num(Json, TEXT("income_bonus"), IncomeBonus);
	FixedAttackTarget = CmdJson::Cell(Json, TEXT("attack_target"), FixedAttackTarget);
}

ACmdGameState* UCmdAICommander::GS() const
{
	return World.IsValid() ? World->GetGameState<ACmdGameState>() : nullptr;
}

int32 UCmdAICommander::Count(FName Id) const
{
	const ACmdGameState* State = GS();
	const FCmdTeam* T = State ? State->GetTeam(TeamId) : nullptr;
	return T ? T->CountOwned(Id) + State->CountQueued(TeamId, Id) : 0;
}

void UCmdAICommander::Tick(float DeltaSeconds)
{
	ACmdGameState* State = GS();
	FCmdTeam* Team = State ? State->GetTeam(TeamId) : nullptr;
	if (!bEnabled || !Team || Team->bDefeated)
	{
		return;
	}
	if (State->Flow != ECmdFlowState::Playing && State->Flow != ECmdFlowState::MainMenu)
	{
		return;
	}

	Team->Credits += IncomeBonus * DeltaSeconds;

	ThinkTimer -= DeltaSeconds;
	if (ThinkTimer <= 0.f)
	{
		ThinkTimer = 1.f;
		if (bBuildBase)
		{
			ThinkDeploy();
			PlaceReadyStructure();
			ThinkEconomy();
		}
		if (bProduceUnits)
		{
			ThinkProduction();
		}
	}

	AttackTimer -= DeltaSeconds;
	DefendTimer -= DeltaSeconds;
	ThinkArmy();
}

// ---------------------------------------------------------------------------
// Economy and base building
// ---------------------------------------------------------------------------

FName UCmdAICommander::ChooseNextStructure() const
{
	const ACmdGameState* State = GS();
	const FCmdTeam* Team = State->GetTeam(TeamId);
	const UCmdDataRegistry* Data = UCmdDataRegistry::Get(World.Get());
	if (!Team || !Data)
	{
		return NAME_None;
	}

	auto Can = [&](FName Id)
	{
		const FCmdEntityDef* Def = Data->GetEntity(Id);
		return Def && State->CanTeamBuild(TeamId, *Def);
	};

	const int32 Margin = Team->PowerProduced - Team->PowerConsumed;
	if ((Margin < 25 || Count(TEXT("power")) == 0) && Can(TEXT("power")))
	{
		return TEXT("power");
	}
	if (Count(TEXT("refinery")) == 0 && Can(TEXT("refinery"))) return TEXT("refinery");
	if (Count(TEXT("barracks")) == 0 && Can(TEXT("barracks"))) return TEXT("barracks");
	if (Count(TEXT("factory")) == 0 && Can(TEXT("factory"))) return TEXT("factory");
	if (Count(TEXT("refinery")) < 2 && Can(TEXT("refinery"))) return TEXT("refinery");
	if (Count(TEXT("radar")) == 0 && Can(TEXT("radar"))) return TEXT("radar");

	// Faction defences.
	const FCmdFactionDef* Faction = Data->GetFaction(Team->Faction);
	const int32 WantedDefences = 2 + Difficulty * 2 + WaveCount / 2;
	if (Faction)
	{
		int32 Defences = 0;
		for (const FName& D : Faction->AIDefenses)
		{
			Defences += Team->CountOwned(D);
		}
		if (Defences < WantedDefences)
		{
			// Rotate through the faction's defence list, best available first.
			for (int32 i = Faction->AIDefenses.Num() - 1; i >= 0; --i)
			{
				const FName D = Faction->AIDefenses[(i + Defences) % Faction->AIDefenses.Num()];
				if (Can(D) && Team->Credits > 900.f)
				{
					return D;
				}
			}
		}
	}

	if (Count(TEXT("techcenter")) == 0 && Can(TEXT("techcenter")) && Team->Credits > 1500.f) return TEXT("techcenter");
	if (Count(TEXT("helipad")) == 0 && Can(TEXT("helipad")) && Team->Credits > 1500.f && Difficulty > 0) return TEXT("helipad");
	if (Count(TEXT("refinery")) < 3 && Can(TEXT("refinery")) && Team->Credits > 2500.f) return TEXT("refinery");
	if (Count(TEXT("factory")) < 2 && Can(TEXT("factory")) && Team->Credits > 3000.f && Difficulty == 2) return TEXT("factory");
	if (Margin < 60 && Can(TEXT("power"))) return TEXT("power");
	return NAME_None;
}

void UCmdAICommander::ThinkDeploy()
{
	// No Construction Yard? Unpack an MCV, nudging it to open ground if it doesn't fit.
	ACmdGameState* State = GS();
	const FCmdTeam* Team = State->GetTeam(TeamId);
	if (!Team || State->HasProducer(TeamId, ECmdCategory::Structure))
	{
		return;
	}
	TArray<ACmdUnit*> MCVs;
	for (ACmdEntity* E : State->Entities)
	{
		ACmdUnit* U = Cast<ACmdUnit>(E);
		if (U && U->TeamId == TeamId && U->IsAlive() && U->Def && !U->Def->DeploysInto.IsNone())
		{
			MCVs.Add(U);
		}
	}
	for (ACmdUnit* U : MCVs)
	{
		if (U->CanDeploy())
		{
			U->TryDeploy();
			return;
		}
		if (!U->IsMoving())
		{
			const FIntPoint Offset(Rng.RandRange(-4, 4), Rng.RandRange(-4, 4));
			U->OrderMove(U->GetCell() + Offset, false);
		}
	}
}

void UCmdAICommander::ThinkEconomy()
{
	ACmdGameState* State = GS();
	const FCmdTeam* Team = State->GetTeam(TeamId);
	if (!Team || !Team->bCanBuild)
	{
		return;
	}
	const FCmdProductionQueue& Q = Team->Queues[static_cast<int32>(ECmdCategory::Structure)];
	if (Q.Items.Num() == 0)
	{
		const FName Next = ChooseNextStructure();
		if (!Next.IsNone())
		{
			State->QueueProduction(TeamId, Next, false);
		}
	}

	// Keep harvesters topped up.
	const UCmdDataRegistry* Data = UCmdDataRegistry::Get(World.Get());
	const FCmdFactionDef* Faction = Data ? Data->GetFaction(Team->Faction) : nullptr;
	const FName Harvester = Faction ? Faction->Harvester : FName(TEXT("harvester"));
	const int32 Refineries = Team->CountOwned(TEXT("refinery"));
	if (Refineries > 0 && Count(Harvester) < Refineries + (Difficulty > 0 ? 1 : 0))
	{
		State->QueueProduction(TeamId, Harvester, false);
	}

	// Repair damaged structures when rich enough.
	if (Team->Credits > 800.f)
	{
		for (ACmdEntity* E : State->Entities)
		{
			ACmdBuilding* B = Cast<ACmdBuilding>(E);
			if (B && B->TeamId == TeamId && B->GetHealthPct() < 0.6f && !B->bRepairing)
			{
				B->ToggleRepair();
			}
		}
	}
}

void UCmdAICommander::PlaceReadyStructure()
{
	ACmdGameState* State = GS();
	const FName Ready = State->GetReadyStructure(TeamId);
	if (Ready.IsNone())
	{
		return;
	}
	const UCmdDataRegistry* Data = UCmdDataRegistry::Get(World.Get());
	const FCmdEntityDef* Def = Data ? Data->GetEntity(Ready) : nullptr;
	FIntPoint Origin;
	if (Def && FindPlacement(*Def, Origin))
	{
		State->PlaceReadyStructure(TeamId, Origin);
	}
	else if (Def)
	{
		State->CancelProduction(TeamId, Ready);
	}
}

bool UCmdAICommander::FindPlacement(const FCmdEntityDef& Def, FIntPoint& OutOrigin) const
{
	const ACmdGameState* State = GS();
	const UCmdGridSubsystem* Grid = World->GetSubsystem<UCmdGridSubsystem>();
	if (!State || !Grid)
	{
		return false;
	}
	const FIntPoint Base = Cmd::WorldToCell(State->GetTeamBaseLocation(TeamId));
	const bool bDefence = Def.Weapon != NAME_None;

	// Defences face the map centre (where attacks come from); everything else hugs the base.
	FIntPoint Bias = FIntPoint::ZeroValue;
	if (bDefence)
	{
		const FVector Center = Grid->GetMapCenter();
		const FVector BaseW = Cmd::CellToWorld(Base);
		const FVector Dir = (Center - BaseW).GetSafeNormal2D();
		Bias = FIntPoint(FMath::RoundToInt(Dir.X * 5.f), FMath::RoundToInt(Dir.Y * 5.f));
	}

	TArray<FIntPoint> Candidates;
	for (int32 R = 2; R <= 14; ++R)
	{
		for (int32 Y = -R; Y <= R; ++Y)
		{
			for (int32 X = -R; X <= R; ++X)
			{
				if (FMath::Abs(X) != R && FMath::Abs(Y) != R)
				{
					continue;
				}
				const FIntPoint Origin = Base + Bias + FIntPoint(X, Y);
				// Leave a one-cell lane on the bottom edge so factories can unload.
				if (Grid->CanPlaceStructure(TeamId, Def, Origin, true) && Grid->IsBuildableCell(Origin + FIntPoint(Def.Footprint.X / 2, Def.Footprint.Y)))
				{
					Candidates.Add(Origin);
				}
			}
		}
		if (Candidates.Num() >= 3)
		{
			break;
		}
	}
	if (Candidates.Num() == 0)
	{
		return false;
	}
	OutOrigin = Candidates[Rng.RandRange(0, Candidates.Num() - 1)];
	return true;
}

// ---------------------------------------------------------------------------
// Army
// ---------------------------------------------------------------------------

FName UCmdAICommander::ChooseUnit(int32 Category) const
{
	const ACmdGameState* State = GS();
	TArray<const FCmdEntityDef*> Options = State->GetBuildOptions(TeamId, static_cast<ECmdCategory>(Category));
	TArray<const FCmdEntityDef*> Combat;
	for (const FCmdEntityDef* Def : Options)
	{
		if (Def->Weapon != NAME_None && !Def->bHarvester && Def->DeploysInto.IsNone())
		{
			Combat.Add(Def);
		}
	}
	if (Combat.Num() == 0)
	{
		return NAME_None;
	}
	// Weighted toward the more expensive end of the menu as the game goes on.
	Combat.Sort([](const FCmdEntityDef& A, const FCmdEntityDef& B) { return A.Cost < B.Cost; });
	const float Bias = FMath::Clamp(0.3f + WaveCount * 0.12f + Difficulty * 0.1f, 0.f, 1.f);
	const int32 Index = FMath::Clamp(FMath::RoundToInt(FMath::Pow(Rng.FRand(), 1.f - Bias * 0.7f) * (Combat.Num() - 1)), 0, Combat.Num() - 1);
	return Combat[Index]->Id;
}

void UCmdAICommander::ThinkProduction()
{
	ACmdGameState* State = GS();
	const FCmdTeam* Team = State->GetTeam(TeamId);
	if (!Team)
	{
		return;
	}
	const int32 ArmySize = GetArmy(false).Num();
	const int32 MaxArmy = 14 + Difficulty * 10 + WaveCount * 2;
	if (ArmySize >= MaxArmy)
	{
		return;
	}
	for (int32 Cat = static_cast<int32>(ECmdCategory::Infantry); Cat < CmdConst::CategoryCount; ++Cat)
	{
		const FCmdProductionQueue& Q = Team->Queues[Cat];
		if (Q.Items.Num() >= 2 || !State->HasProducer(TeamId, static_cast<ECmdCategory>(Cat)))
		{
			continue;
		}
		// Don't starve the build order on low money.
		if (Team->Credits < 400.f && Cat != static_cast<int32>(ECmdCategory::Infantry))
		{
			continue;
		}
		const FName Unit = ChooseUnit(Cat);
		if (!Unit.IsNone())
		{
			State->QueueProduction(TeamId, Unit, false);
		}
	}
}

TArray<ACmdUnit*> UCmdAICommander::GetArmy(bool bOnlyIdle) const
{
	TArray<ACmdUnit*> Out;
	const ACmdGameState* State = GS();
	for (ACmdEntity* E : State->Entities)
	{
		ACmdUnit* U = Cast<ACmdUnit>(E);
		if (!U || U->TeamId != TeamId || !U->IsAlive() || !U->HasWeapon() || U->IsHarvester() || !U->MissionTag.IsNone())
		{
			continue;	// tagged units belong to the mission script
		}
		if (bOnlyIdle && U->GetOrder() != ECmdOrder::Idle && U->GetOrder() != ECmdOrder::Guard)
		{
			continue;
		}
		Out.Add(U);
	}
	return Out;
}

ACmdEntity* UCmdAICommander::PickAttackTarget() const
{
	const ACmdGameState* State = GS();
	const FVector Home = State->GetTeamBaseLocation(TeamId);
	ACmdEntity* Best = nullptr;
	float BestScore = TNumericLimits<float>::Max();
	for (ACmdEntity* E : State->Entities)
	{
		if (!E || !E->IsAlive() || !State->AreHostile(TeamId, E->TeamId) || State->IsNeutralTeam(E->TeamId))
		{
			continue;
		}
		// Go for the economy first, then anything else.
		float Score = FVector::Dist2D(Home, E->GetActorLocation());
		if (E->Def && (E->Def->bRefinery || E->Def->bHarvester)) Score *= 0.6f;
		if (!E->IsBuilding()) Score *= 1.3f;
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = E;
		}
	}
	return Best;
}

void UCmdAICommander::LaunchAttack(const FIntPoint& TargetCell)
{
	FIntPoint Dest = TargetCell;
	if (Dest.X < 0)
	{
		if (FixedAttackTarget.X >= 0)
		{
			Dest = FixedAttackTarget;
		}
		else if (ACmdEntity* T = PickAttackTarget())
		{
			Dest = T->GetCell();
		}
		else
		{
			return;
		}
	}
	const UCmdGridSubsystem* Grid = World->GetSubsystem<UCmdGridSubsystem>();
	TSet<FIntPoint> Used;
	for (ACmdUnit* U : GetArmy(true))
	{
		FIntPoint Cell = Grid ? Grid->FindNearestPassable(Dest, 8, &Used) : Dest;
		Used.Add(Cell);
		U->OrderMove(Cell, true);
	}
	++WaveCount;
}

void UCmdAICommander::ThinkArmy()
{
	ACmdGameState* State = GS();

	// Defend: anything of ours hit recently pulls idle units toward the attacker.
	if (bDefend && DefendTimer <= 0.f)
	{
		DefendTimer = 2.f;
		const float Now = World->GetTimeSeconds();
		for (ACmdEntity* E : State->Entities)
		{
			if (!E || E->TeamId != TeamId || !E->IsBuilding() || Now - E->LastDamagedTime > 4.f)
			{
				continue;
			}
			ACmdEntity* Attacker = E->LastAttacker.Get();
			if (!Attacker || !Attacker->IsAlive())
			{
				continue;
			}
			for (ACmdUnit* U : GetArmy(true))
			{
				if (FVector::Dist2D(U->GetActorLocation(), E->GetActorLocation()) < 22.f * CmdConst::CellSize)
				{
					U->OrderMove(Attacker->GetCell(), true);
				}
			}
			break;
		}
	}

	if (bAttack && AttackTimer <= 0.f)
	{
		const int32 Wanted = MinAttackSize + WaveCount * (1 + Difficulty);
		if (GetArmy(true).Num() >= Wanted)
		{
			AttackTimer = AttackInterval;
			LaunchAttack(FIntPoint(-1, -1));
		}
		else
		{
			AttackTimer = 15.f;	// check again soon
		}
	}
}
