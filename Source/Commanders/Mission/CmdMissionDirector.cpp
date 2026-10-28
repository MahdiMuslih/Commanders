#include "Mission/CmdMissionDirector.h"
#include "Game/CmdPlayerState.h"
#include "Audio/CmdAudioSubsystem.h"
#include "Mission/CmdCinematicDirector.h"
#include "Game/CmdGameState.h"
#include "Game/CmdGameMode.h"
#include "Game/CmdGameInstance.h"
#include "AI/CmdAICommander.h"
#include "Gameplay/CmdEntity.h"
#include "Gameplay/CmdUnit.h"
#include "Gameplay/CmdBuilding.h"
#include "Gameplay/CmdSpawner.h"
#include "Gameplay/CmdEffect.h"
#include "Player/CmdPlayerController.h"
#include "World/CmdGridSubsystem.h"
#include "World/CmdEnvironmentActor.h"
#include "Data/CmdDataRegistry.h"
#include "Core/CmdJson.h"
#include "Commanders.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"

ACmdMissionDirector::ACmdMissionDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
}

ACmdGameState* ACmdMissionDirector::GS() const
{
	return GetWorld() ? GetWorld()->GetGameState<ACmdGameState>() : nullptr;
}

// ---------------------------------------------------------------------------
// Setup / flow
// ---------------------------------------------------------------------------

void ACmdMissionDirector::InitMission(const TSharedPtr<FJsonObject>& InMission, bool bInSkirmish)
{
	Mission = InMission;
	bSkirmish = bInSkirmish;
	MissionId = CmdJson::Name(Mission, TEXT("id"));
	Title = CmdJson::Str(Mission, TEXT("title"), TEXT("Skirmish"));
	Subtitle = CmdJson::Str(Mission, TEXT("subtitle"));
	Location = CmdJson::Str(Mission, TEXT("location"));
	Date = CmdJson::Str(Mission, TEXT("date"));
	BriefingParagraphs = CmdJson::Strings(Mission, TEXT("briefing"));
	VictoryText = CmdJson::Str(Mission, TEXT("victory_text"), TEXT("Mission accomplished."));
	DefeatText = CmdJson::Str(Mission, TEXT("defeat_text"), TEXT("Mission failed."));
	if (const ACmdGameState* State = GS())
	{
		PlayerTeam = State->LocalTeamId;
	}

	for (const TSharedPtr<FJsonValue>& V : CmdJson::Arr(Mission, TEXT("objectives")))
	{
		AddObjectiveFromJson(V->AsObject());
	}
	if (Objectives.Num() == 0)
	{
		FCmdObjective O;
		O.Id = TEXT("destroy_all");
		O.Text = TEXT("Destroy all enemy forces.");
		O.Type = TEXT("destroy_all");
		Objectives.Add(O);
	}

	for (const TSharedPtr<FJsonValue>& V : CmdJson::Arr(Mission, TEXT("triggers")))
	{
		const TSharedPtr<FJsonObject> J = V->AsObject();
		FCmdTrigger T;
		T.Id = CmdJson::Name(J, TEXT("id"));
		T.Condition = CmdJson::Obj(J, TEXT("when"));
		T.Actions = CmdJson::Arr(J, TEXT("actions"));
		T.bEnabled = CmdJson::Bool(J, TEXT("enabled"), true);
		T.bRepeat = CmdJson::Bool(J, TEXT("repeat"), false);
		T.Interval = CmdJson::Num(J, TEXT("interval"), 0.0);
		T.Delay = CmdJson::Num(J, TEXT("delay"), 0.0);
		Triggers.Add(T);
	}

	FActorSpawnParameters Params;
	Params.Owner = this;
	Cinematic = GetWorld()->SpawnActor<ACmdCinematicDirector>(ACmdCinematicDirector::StaticClass(), FTransform::Identity, Params);
	if (Cinematic)
	{
		Cinematic->Director = this;
	}
}

void ACmdMissionDirector::AddObjectiveFromJson(const TSharedPtr<FJsonObject>& J)
{
	if (!J.IsValid())
	{
		return;
	}
	FCmdObjective O;
	O.Id = CmdJson::Name(J, TEXT("id"));
	O.Text = CmdJson::Str(J, TEXT("text"));
	O.Type = CmdJson::Str(J, TEXT("type"), TEXT("custom"));
	O.Json = J;
	O.bPrimary = CmdJson::Bool(J, TEXT("primary"), true);
	O.bVisible = !CmdJson::Bool(J, TEXT("hidden"), false);
	O.ActivatedAt = MissionTime;
	if (FCmdObjective* Existing = FindObjective(O.Id))
	{
		*Existing = O;
	}
	else
	{
		Objectives.Add(O);
	}
	if (O.bVisible && bPlaying)
	{
		if (ACmdGameState* State = GS())
		{
			State->AddMessage(PlayerTeam, FString::Printf(TEXT("New objective: %s"), *O.Text), FLinearColor(0.5f, 0.9f, 1.f));
		}
	}
	const float Limit = CmdJson::Num(J, TEXT("time_limit"), 0.0);
	if (Limit > 0.f)
	{
		FCmdNamedTimer& T = Timers.FindOrAdd(FName(*(O.Id.ToString() + TEXT("_limit"))));
		T.Label = CmdJson::Str(J, TEXT("timer_label"), TEXT("Time remaining"));
		T.Remaining = Limit;
		T.bVisible = O.bVisible;
		T.bExpired = false;
		VisibleTimer = FName(*(O.Id.ToString() + TEXT("_limit")));
	}
}

void ACmdMissionDirector::StartSession()
{
	ACmdGameState* State = GS();
	if (!State)
	{
		return;
	}
	if (bSkirmish)
	{
		BeginPlaying();
		return;
	}
	State->Flow = ECmdFlowState::Briefing;
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		PC->SetPause(true);
	}
}

void ACmdMissionDirector::ConfirmBriefing()
{
	ACmdGameState* State = GS();
	if (!State || State->Flow != ECmdFlowState::Briefing)
	{
		return;
	}
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		PC->SetPause(false);
	}
	const FName Intro = CmdJson::Name(Mission, TEXT("intro_cinematic"));
	if (!Intro.IsNone() && PlayCinematic(Intro, [this]() { BeginPlaying(); }))
	{
		return;
	}
	BeginPlaying();
}

void ACmdMissionDirector::BeginPlaying()
{
	ACmdGameState* State = GS();
	if (!State || bPlaying)
	{
		return;
	}
	bPlaying = true;
	State->Flow = ECmdFlowState::Playing;
	if (bMultiplayer)
	{
		Objectives.Reset();	// last side standing wins; results are per player
	}
	for (FCmdObjective& O : Objectives)
	{
		O.ActivatedAt = MissionTime;
	}
	if (ACmdGameMode* GM = GetWorld()->GetAuthGameMode<ACmdGameMode>())
	{
		GM->OnMissionStarted();
	}
	// Opening dialogue/actions declared directly on the mission.
	ExecuteActions(CmdJson::Arr(Mission, TEXT("on_start")));
}

bool ACmdMissionDirector::PlayCinematic(FName Id, TFunction<void()> OnFinished)
{
	if (!Cinematic || Id.IsNone())
	{
		return false;
	}
	TSharedPtr<FJsonObject> Json = CmdJson::Obj(CmdJson::Obj(Mission, TEXT("cinematics")), *Id.ToString());
	if (!Json.IsValid())
	{
		if (const UCmdDataRegistry* Data = UCmdDataRegistry::Get(this))
		{
			Json = Data->GetGlobalCinematic(Id);
		}
	}
	if (!Json.IsValid())
	{
		UE_LOG(LogCommanders, Warning, TEXT("Cinematic '%s' not found."), *Id.ToString());
		return false;
	}

	ACmdGameState* State = GS();
	const ECmdFlowState Previous = State ? State->Flow : ECmdFlowState::Playing;
	if (State)
	{
		State->Flow = ECmdFlowState::Cinematic;
	}
	Cinematic->Play(Json, [this, Previous, OnFinished]()
	{
		if (ACmdGameState* S = GS())
		{
			if (S->Flow == ECmdFlowState::Cinematic)
			{
				S->Flow = (Previous == ECmdFlowState::Briefing || Previous == ECmdFlowState::Cinematic) ? ECmdFlowState::Playing : Previous;
			}
		}
		if (OnFinished)
		{
			OnFinished();
		}
	});
	return true;
}

void ACmdMissionDirector::SkipCinematic()
{
	if (Cinematic && Cinematic->IsPlaying())
	{
		Cinematic->Skip();
	}
}

void ACmdMissionDirector::EndMission(bool bInVictory, const FString& Reason)
{
	if (bEnded)
	{
		return;
	}
	bEnded = true;
	bVictory = bInVictory;
	ResultReason = Reason.IsEmpty() ? (bVictory ? VictoryText : DefeatText) : Reason;
	DialogueQueue.Reset();

	if (UCmdGameInstance* GI = Cast<UCmdGameInstance>(GetGameInstance()))
	{
		GI->ReportResult(bVictory);
	}

	const FName Outro = CmdJson::Name(Mission, bVictory ? TEXT("outro_cinematic") : TEXT("defeat_cinematic"));
	if (!Outro.IsNone() && PlayCinematic(Outro, [this]() { FinishMission(); }))
	{
		return;
	}
	EndDelay = 2.5f;	// let the last explosions play out
}

void ACmdMissionDirector::FinishMission()
{
	ACmdGameState* State = GS();
	if (!State)
	{
		return;
	}
	State->Flow = bVictory ? ECmdFlowState::Victory : ECmdFlowState::Defeat;
	if (ACmdPlayerController* PC = Cast<ACmdPlayerController>(GetWorld()->GetFirstPlayerController()))
	{
		PC->OnMissionEnded();
	}
}

// ---------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------

void ACmdMissionDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (GetWorld()->IsPaused())
	{
		return;
	}

	if (EndDelay > 0.f)
	{
		EndDelay -= DeltaSeconds;
		if (EndDelay <= 0.f)
		{
			FinishMission();
		}
	}

	TickDialogue(DeltaSeconds);
	if (!bPlaying)
	{
		return;
	}

	MissionTime += DeltaSeconds;
	TickTimers(DeltaSeconds);
	TickPending(DeltaSeconds);

	EvalTimer -= DeltaSeconds;
	if (EvalTimer <= 0.f)
	{
		EvalTimer = 0.25f;
		TickTriggers();
		if (!bEnded && bMultiplayer)
		{
			CheckMultiplayerEnd();
		}
		else if (!bEnded)
		{
			UpdateObjectives();
			CheckEndConditions();
		}
	}
}

void ACmdMissionDirector::TickDialogue(float DeltaSeconds)
{
	if (bHasDialogue)
	{
		DialogueAge += DeltaSeconds;
		if (DialogueAge >= CurrentDialogue.Duration)
		{
			bHasDialogue = false;
		}
	}
	if (!bHasDialogue && DialogueQueue.Num() > 0)
	{
		CurrentDialogue = DialogueQueue[0];
		DialogueQueue.RemoveAt(0);
		DialogueAge = 0.f;
		bHasDialogue = true;
		UCmdAudioSubsystem::Play2D(this, TEXT("radio"));
	}
}

void ACmdMissionDirector::TickTimers(float DeltaSeconds)
{
	for (auto& Pair : Timers)
	{
		FCmdNamedTimer& T = Pair.Value;
		if (!T.bExpired)
		{
			T.Remaining -= DeltaSeconds;
			if (T.Remaining <= 0.f)
			{
				T.Remaining = 0.f;
				T.bExpired = true;
			}
		}
	}
}

void ACmdMissionDirector::TickPending(float DeltaSeconds)
{
	TArray<TArray<TSharedPtr<FJsonValue>>> Due;
	for (int32 i = Pending.Num() - 1; i >= 0; --i)
	{
		Pending[i].Remaining -= DeltaSeconds;
		if (Pending[i].Remaining <= 0.f)
		{
			Due.Add(Pending[i].Actions);
			Pending.RemoveAt(i);
		}
	}
	for (int32 i = Due.Num() - 1; i >= 0; --i)
	{
		ExecuteActions(Due[i]);
	}
}

void ACmdMissionDirector::TickTriggers()
{
	for (int32 i = 0; i < Triggers.Num(); ++i)
	{
		FCmdTrigger& T = Triggers[i];
		if (!T.bEnabled || (T.bFired && !T.bRepeat) || MissionTime < T.NextAllowed)
		{
			continue;
		}
		if (!EvaluateCondition(T.Condition))
		{
			continue;
		}
		T.bFired = true;
		T.NextAllowed = MissionTime + FMath::Max(1.f, T.Interval);
		const TArray<TSharedPtr<FJsonValue>> Actions = T.Actions;	// copy: actions may add triggers
		if (T.Delay > 0.f)
		{
			FPendingActions P;
			P.Remaining = T.Delay;
			P.Actions = Actions;
			Pending.Add(P);
		}
		else
		{
			ExecuteActions(Actions);
		}
		if (bEnded)
		{
			return;
		}
	}
}

// ---------------------------------------------------------------------------
// Objectives
// ---------------------------------------------------------------------------

FCmdObjective* ACmdMissionDirector::FindObjective(FName Id)
{
	return Objectives.FindByPredicate([Id](const FCmdObjective& O) { return O.Id == Id; });
}

void ACmdMissionDirector::SetObjectiveState(FName Id, ECmdObjectiveState State)
{
	FCmdObjective* O = FindObjective(Id);
	if (!O || O->State == State)
	{
		return;
	}
	O->State = State;
	if (ACmdGameState* S = GS())
	{
		if (O->bVisible)
		{
			if (State == ECmdObjectiveState::Completed)
			{
				S->AddMessage(PlayerTeam, FString::Printf(TEXT("Objective complete: %s"), *O->Text), FLinearColor(0.4f, 1.f, 0.4f), TEXT("objective"));
			}
			else if (State == ECmdObjectiveState::Failed)
			{
				S->AddMessage(PlayerTeam, FString::Printf(TEXT("Objective failed: %s"), *O->Text), FLinearColor(1.f, 0.3f, 0.3f), TEXT("objective_failed"));
			}
		}
	}
	// A completed objective stops its time limit.
	if (FCmdNamedTimer* T = Timers.Find(FName(*(Id.ToString() + TEXT("_limit")))))
	{
		T->bVisible = false;
		T->bExpired = false;
		T->Remaining = 1e9f;
	}
}

bool ACmdMissionDirector::IsTagDestroyed(FName Tag) const
{
	const ACmdGameState* State = GS();
	return State && State->HasEverSeenTag(Tag) && State->GetEntitiesWithTag(Tag).Num() == 0;
}

void ACmdMissionDirector::UpdateObjectives()
{
	ACmdGameState* State = GS();
	if (!State)
	{
		return;
	}
	for (FCmdObjective& O : Objectives)
	{
		if (O.State != ECmdObjectiveState::Active)
		{
			continue;
		}
		const FName Tag = CmdJson::Name(O.Json, TEXT("tag"));

		// Time limits fail any objective type.
		if (const FCmdNamedTimer* Limit = Timers.Find(FName(*(O.Id.ToString() + TEXT("_limit")))))
		{
			if (Limit->bExpired)
			{
				SetObjectiveState(O.Id, ECmdObjectiveState::Failed);
				continue;
			}
		}

		if (O.Type == TEXT("destroy_all"))
		{
			bool bAnyLeft = false;
			for (const FCmdTeam& T : State->Teams)
			{
				if (!T.bIsNeutral && State->AreHostile(PlayerTeam, T.Id) && State->CountTeamEntities(T.Id) > 0)
				{
					bAnyLeft = true;
					break;
				}
			}
			if (!bAnyLeft)
			{
				SetObjectiveState(O.Id, ECmdObjectiveState::Completed);
			}
		}
		else if (O.Type == TEXT("destroy"))
		{
			if (IsTagDestroyed(Tag))
			{
				SetObjectiveState(O.Id, ECmdObjectiveState::Completed);
			}
		}
		else if (O.Type == TEXT("protect"))
		{
			bool bLost = IsTagDestroyed(Tag);
			for (const ACmdEntity* E : State->GetEntitiesWithTag(Tag))
			{
				bLost |= !State->AreAllied(E->TeamId, PlayerTeam) && !State->IsNeutralTeam(E->TeamId);
			}
			if (bLost)
			{
				SetObjectiveState(O.Id, ECmdObjectiveState::Failed);
			}
		}
		else if (O.Type == TEXT("reach"))
		{
			if (IsAreaOccupied(O.Json))
			{
				SetObjectiveState(O.Id, ECmdObjectiveState::Completed);
			}
		}
		else if (O.Type == TEXT("survive"))
		{
			if (MissionTime - O.ActivatedAt >= CmdJson::Num(O.Json, TEXT("seconds"), 300.0))
			{
				SetObjectiveState(O.Id, ECmdObjectiveState::Completed);
			}
		}
		else if (O.Type == TEXT("capture"))
		{
			for (const ACmdEntity* E : State->GetEntitiesWithTag(Tag))
			{
				if (State->AreAllied(E->TeamId, PlayerTeam))
				{
					SetObjectiveState(O.Id, ECmdObjectiveState::Completed);
					break;
				}
			}
			if (O.State == ECmdObjectiveState::Active && IsTagDestroyed(Tag))
			{
				SetObjectiveState(O.Id, ECmdObjectiveState::Failed);
			}
		}
		else if (O.Type == TEXT("build"))
		{
			const FCmdTeam* T = State->GetTeam(PlayerTeam);
			const FName Type = CmdJson::Name(O.Json, TEXT("entity"));
			if (T && T->CountOwned(Type) >= CmdJson::Int(O.Json, TEXT("count"), 1))
			{
				SetObjectiveState(O.Id, ECmdObjectiveState::Completed);
			}
		}
		else if (O.Type == TEXT("eliminate"))
		{
			const int32 Team = ResolveTeam(O.Json, TEXT("team"), 1);
			if (State->CountTeamEntities(Team) == 0)
			{
				SetObjectiveState(O.Id, ECmdObjectiveState::Completed);
			}
		}
	}
}

void ACmdMissionDirector::CheckEndConditions()
{
	ACmdGameState* State = GS();
	if (!State || bEnded)
	{
		return;
	}

	// Lose when the player has nothing left.
	if (State->CountTeamEntities(PlayerTeam) == 0)
	{
		EndMission(false, TEXT("All of your forces have been destroyed."));
		return;
	}

	bool bAllDone = true;
	bool bAnyNonProtect = false;
	for (const FCmdObjective& O : Objectives)
	{
		if (!O.bPrimary || !O.bVisible)
		{
			continue;
		}
		if (O.State == ECmdObjectiveState::Failed)
		{
			EndMission(false, CmdJson::Str(O.Json, TEXT("fail_text"), FString::Printf(TEXT("Objective failed: %s"), *O.Text)));
			return;
		}
		if (O.Type == TEXT("protect"))
		{
			continue;	// protection objectives just must not fail
		}
		bAnyNonProtect = true;
		bAllDone &= O.State == ECmdObjectiveState::Completed;
	}
	if (bAnyNonProtect && bAllDone && !CmdJson::Bool(Mission, TEXT("manual_victory"), false))
	{
		for (FCmdObjective& O : Objectives)
		{
			if (O.Type == TEXT("protect") && O.State == ECmdObjectiveState::Active)
			{
				O.State = ECmdObjectiveState::Completed;
			}
		}
		EndMission(true, FString());
	}
}

void ACmdMissionDirector::CheckMultiplayerEnd()
{
	ACmdGameState* State = GS();
	if (!State || bEnded)
	{
		return;
	}

	// Players whose side has been wiped out lose now and may keep watching.
	for (APlayerState* P : State->PlayerArray)
	{
		ACmdPlayerState* PS = Cast<ACmdPlayerState>(P);
		const FCmdTeam* Team = PS ? State->GetTeam(PS->SlotTeam) : nullptr;
		if (Team && Team->bDefeated && PS->MatchResult == ECmdMatchResult::None)
		{
			PS->MatchResult = ECmdMatchResult::Defeat;
			State->AddMessage(Team->Id, TEXT("You have been defeated. Keep watching, or leave from the menu (Esc)."), FLinearColor(1.f, 0.35f, 0.3f), TEXT("objective_failed"));
		}
	}

	TSet<int32> AliveSides;
	for (const FCmdTeam& T : State->Teams)
	{
		if (!T.bIsNeutral && !T.bDefeated)
		{
			AliveSides.Add(T.Alliance);
		}
	}
	if (AliveSides.Num() > 1)
	{
		return;
	}

	bool bHostWon = false;
	FString Winners;
	for (const FCmdTeam& T : State->Teams)
	{
		if (!T.bIsNeutral && AliveSides.Contains(T.Alliance))
		{
			Winners += Winners.IsEmpty() ? T.Name : FString::Printf(TEXT(", %s"), *T.Name);
		}
	}
	for (APlayerState* P : State->PlayerArray)
	{
		ACmdPlayerState* PS = Cast<ACmdPlayerState>(P);
		const FCmdTeam* Team = PS ? State->GetTeam(PS->SlotTeam) : nullptr;
		if (!Team)
		{
			continue;
		}
		if (PS->MatchResult == ECmdMatchResult::None)
		{
			PS->MatchResult = AliveSides.Contains(Team->Alliance) ? ECmdMatchResult::Victory : ECmdMatchResult::Defeat;
		}
		if (PS->bIsHost)
		{
			bHostWon = PS->MatchResult == ECmdMatchResult::Victory;
		}
	}
	State->MatchResultText = Winners.IsEmpty() ? TEXT("No side survived.") : FString::Printf(TEXT("Victory to %s."), *Winners);
	EndMission(bHostWon, Winners.IsEmpty() ? TEXT("No side survived.") : FString::Printf(TEXT("Victory to %s."), *Winners));
}

bool ACmdMissionDirector::GetVisibleTimer(FString& OutLabel, float& OutRemaining) const
{
	const FCmdNamedTimer* T = Timers.Find(VisibleTimer);
	if (!T || !T->bVisible || T->Remaining > 1e8f)
	{
		return false;
	}
	OutLabel = T->Label;
	OutRemaining = T->Remaining;
	return true;
}

// ---------------------------------------------------------------------------
// Conditions
// ---------------------------------------------------------------------------

int32 ACmdMissionDirector::ResolveTeam(const TSharedPtr<FJsonObject>& Json, const TCHAR* Key, int32 Default) const
{
	const TSharedPtr<FJsonValue> V = CmdJson::Field(Json, Key);
	if (!V.IsValid())
	{
		return Default;
	}
	if (V->Type == EJson::String)
	{
		return V->AsString() == TEXT("player") ? PlayerTeam : FCString::Atoi(*V->AsString());
	}
	if (V->Type == EJson::Number)
	{
		return FMath::RoundToInt(V->AsNumber());
	}
	return Default;
}

bool ACmdMissionDirector::IsAreaOccupied(const TSharedPtr<FJsonObject>& Json)
{
	const ACmdGameState* State = GS();
	if (!State)
	{
		return false;
	}
	const FIntRect Area = CmdJson::Rect(Json, TEXT("area"));
	const int32 Team = ResolveTeam(Json, TEXT("team"), PlayerTeam);
	const FName Tag = CmdJson::Name(Json, TEXT("tag"));
	const int32 Needed = CmdJson::Int(Json, TEXT("min"), 1);
	const bool bAllied = CmdJson::Bool(Json, TEXT("allied"), true);
	int32 Count = 0;
	for (const ACmdEntity* E : State->Entities)
	{
		if (!E || !E->IsAlive() || E->IsBuilding())
		{
			continue;
		}
		if (bAllied ? !State->AreAllied(E->TeamId, Team) : E->TeamId != Team)
		{
			continue;
		}
		if (!Tag.IsNone() && E->MissionTag != Tag)
		{
			continue;
		}
		const FIntPoint C = E->GetCell();
		if (C.X >= Area.Min.X && C.X < Area.Max.X && C.Y >= Area.Min.Y && C.Y < Area.Max.Y)
		{
			if (++Count >= Needed)
			{
				return true;
			}
		}
	}
	return false;
}

bool ACmdMissionDirector::EvaluateCondition(const TSharedPtr<FJsonObject>& C)
{
	if (!C.IsValid())
	{
		return false;
	}
	ACmdGameState* State = GS();
	if (!State)
	{
		return false;
	}
	const FString Type = CmdJson::Str(C, TEXT("type"));

	if (Type == TEXT("time"))
	{
		return MissionTime >= CmdJson::Num(C, TEXT("seconds"), 0.0);
	}
	if (Type == TEXT("area"))
	{
		return IsAreaOccupied(C);
	}
	if (Type == TEXT("destroyed"))
	{
		return IsTagDestroyed(CmdJson::Name(C, TEXT("tag")));
	}
	if (Type == TEXT("captured"))
	{
		const int32 Team = ResolveTeam(C, TEXT("team"), PlayerTeam);
		for (const ACmdEntity* E : State->GetEntitiesWithTag(CmdJson::Name(C, TEXT("tag"))))
		{
			if (State->AreAllied(E->TeamId, Team))
			{
				return true;
			}
		}
		return false;
	}
	if (Type == TEXT("objective"))
	{
		const FCmdObjective* O = FindObjective(CmdJson::Name(C, TEXT("id")));
		const FString Wanted = CmdJson::Str(C, TEXT("state"), TEXT("completed"));
		if (!O) return false;
		return Wanted == TEXT("failed") ? O->State == ECmdObjectiveState::Failed : O->State == ECmdObjectiveState::Completed;
	}
	if (Type == TEXT("count_below"))
	{
		const int32 Team = ResolveTeam(C, TEXT("team"), 1);
		const FString What = CmdJson::Str(C, TEXT("what"), TEXT("all"));
		const int32 Count = State->CountTeamEntities(Team, What != TEXT("units"), What != TEXT("buildings"));
		return Count < CmdJson::Int(C, TEXT("count"), 1);
	}
	if (Type == TEXT("built"))
	{
		const FCmdTeam* T = State->GetTeam(ResolveTeam(C, TEXT("team"), PlayerTeam));
		return T && T->CountOwned(CmdJson::Name(C, TEXT("entity"))) >= CmdJson::Int(C, TEXT("count"), 1);
	}
	if (Type == TEXT("produced"))
	{
		const int32* Count = ProducedCounts.Find(CmdJson::Name(C, TEXT("entity")));
		return Count && *Count >= CmdJson::Int(C, TEXT("count"), 1);
	}
	if (Type == TEXT("timer"))
	{
		const FCmdNamedTimer* T = Timers.Find(CmdJson::Name(C, TEXT("name")));
		return T && T->bExpired;
	}
	if (Type == TEXT("flag"))
	{
		return Flags.Contains(CmdJson::Name(C, TEXT("name")));
	}
	if (Type == TEXT("damaged"))
	{
		for (const ACmdEntity* E : State->GetEntitiesWithTag(CmdJson::Name(C, TEXT("tag"))))
		{
			if (E->GetHealthPct() < CmdJson::Num(C, TEXT("below"), 0.99))
			{
				return true;
			}
		}
		return false;
	}
	if (Type == TEXT("team_defeated"))
	{
		const FCmdTeam* T = State->GetTeam(ResolveTeam(C, TEXT("team"), 1));
		return T && T->bDefeated;
	}
	if (Type == TEXT("credits"))
	{
		const FCmdTeam* T = State->GetTeam(ResolveTeam(C, TEXT("team"), PlayerTeam));
		return T && T->Credits >= CmdJson::Num(C, TEXT("min"), 0.0);
	}
	if (Type == TEXT("all") || Type == TEXT("any"))
	{
		const bool bAll = Type == TEXT("all");
		for (const TSharedPtr<FJsonValue>& V : CmdJson::Arr(C, TEXT("conditions")))
		{
			const bool bResult = EvaluateCondition(V->AsObject());
			if (bAll && !bResult) return false;
			if (!bAll && bResult) return true;
		}
		return bAll;
	}
	if (Type == TEXT("not"))
	{
		return !EvaluateCondition(CmdJson::Obj(C, TEXT("condition")));
	}
	if (Type == TEXT("start"))
	{
		return true;
	}
	UE_LOG(LogCommanders, Warning, TEXT("Unknown trigger condition '%s'"), *Type);
	return false;
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

void ACmdMissionDirector::ExecuteActions(const TArray<TSharedPtr<FJsonValue>>& Actions)
{
	for (const TSharedPtr<FJsonValue>& V : Actions)
	{
		if (V.IsValid() && V->Type == EJson::Object)
		{
			ExecuteAction(V->AsObject());
		}
	}
}

void ACmdMissionDirector::QueueDialogue(const TArray<TSharedPtr<FJsonValue>>& Lines)
{
	for (const TSharedPtr<FJsonValue>& V : Lines)
	{
		const TSharedPtr<FJsonObject> L = V->AsObject();
		FCmdDialogueLine Line;
		Line.Speaker = CmdJson::Str(L, TEXT("speaker"));
		Line.Text = CmdJson::Str(L, TEXT("text"));
		Line.Color = CmdJson::Color(L, TEXT("color"), FLinearColor(0.85f, 0.85f, 0.85f));
		Line.Duration = CmdJson::Num(L, TEXT("duration"), FMath::Clamp(Line.Text.Len() * 0.065f, 3.f, 9.f));
		DialogueQueue.Add(Line);
	}
}

TArray<ACmdEntity*> ACmdMissionDirector::ResolveEntities(const TSharedPtr<FJsonObject>& Json) const
{
	TArray<ACmdEntity*> Out;
	const ACmdGameState* State = GS();
	if (!State)
	{
		return Out;
	}
	const FName Tag = CmdJson::Name(Json, TEXT("tag"));
	if (!Tag.IsNone())
	{
		return State->GetEntitiesWithTag(Tag);
	}
	if (CmdJson::Has(Json, TEXT("team")))
	{
		const int32 Team = ResolveTeam(Json, TEXT("team"), 1);
		for (ACmdEntity* E : State->Entities)
		{
			if (E && E->IsAlive() && E->TeamId == Team)
			{
				Out.Add(E);
			}
		}
	}
	return Out;
}

void ACmdMissionDirector::IssueOrders(const TArray<ACmdEntity*>& Entities, const TSharedPtr<FJsonObject>& Json)
{
	const FIntPoint AttackMove = CmdJson::Cell(Json, TEXT("attack_move"));
	const FIntPoint MoveTo = CmdJson::Cell(Json, TEXT("move_to"));
	const FName AttackTag = CmdJson::Name(Json, TEXT("attack_tag"));
	ACmdEntity* AttackTarget = nullptr;
	if (!AttackTag.IsNone())
	{
		const TArray<ACmdEntity*> Targets = GS()->GetEntitiesWithTag(AttackTag);
		AttackTarget = Targets.Num() > 0 ? Targets[0] : nullptr;
	}
	const UCmdGridSubsystem* Grid = GetWorld()->GetSubsystem<UCmdGridSubsystem>();
	TSet<FIntPoint> Used;
	for (ACmdEntity* E : Entities)
	{
		ACmdUnit* U = Cast<ACmdUnit>(E);
		if (!U || U->IsHarvester())
		{
			continue;
		}
		if (AttackTarget && U->HasWeapon() && U->CanTarget(AttackTarget))
		{
			U->OrderAttack(AttackTarget);
		}
		else if (AttackMove.X >= 0 || MoveTo.X >= 0)
		{
			const FIntPoint Dest = AttackMove.X >= 0 ? AttackMove : MoveTo;
			FIntPoint Cell = Dest;
			if (Grid)
			{
				Cell = Grid->FindNearestPassable(Dest, 8, &Used);
				Used.Add(Cell);
			}
			U->OrderMove(Cell, AttackMove.X >= 0);
		}
		else if (CmdJson::Bool(Json, TEXT("guard")))
		{
			U->OrderGuard();
		}
		else if (CmdJson::Bool(Json, TEXT("stop")))
		{
			U->OrderStop();
		}
	}
}

void ACmdMissionDirector::ExecuteAction(const TSharedPtr<FJsonObject>& A)
{
	if (!A.IsValid())
	{
		return;
	}
	ACmdGameState* State = GS();
	UWorld* World = GetWorld();
	ACmdGameMode* GM = World->GetAuthGameMode<ACmdGameMode>();
	UCmdGridSubsystem* Grid = World->GetSubsystem<UCmdGridSubsystem>();
	const FString Type = CmdJson::Str(A, TEXT("type"));

	if (Type == TEXT("dialogue"))
	{
		QueueDialogue(CmdJson::Arr(A, TEXT("lines")));
	}
	else if (Type == TEXT("message"))
	{
		if (State) State->AddMessage(PlayerTeam, CmdJson::Str(A, TEXT("text")), CmdJson::Color(A, TEXT("color"), FLinearColor(0.9f, 0.9f, 0.6f)));
	}
	else if (Type == TEXT("spawn"))
	{
		const int32 Team = ResolveTeam(A, TEXT("team"), 1);
		const FIntPoint At = CmdJson::Cell(A, TEXT("at"), FIntPoint(0, 0));
		const FName Tag = CmdJson::Name(A, TEXT("tag"));
		const float Yaw = CmdJson::Num(A, TEXT("yaw"), 90.0);
		TArray<ACmdEntity*> Spawned;
		TSet<FIntPoint> Used;
		for (const FString& Id : CmdJson::Strings(A, TEXT("units")))
		{
			const UCmdDataRegistry* Data = UCmdDataRegistry::Get(this);
			const FCmdEntityDef* Def = Data ? Data->GetEntity(FName(*Id)) : nullptr;
			if (!Def)
			{
				UE_LOG(LogCommanders, Warning, TEXT("spawn: unknown entity %s"), *Id);
				continue;
			}
			FIntPoint Cell = At;
			if (!Def->bIsBuilding && !Def->bAircraft && Grid)
			{
				Cell = Grid->FindNearestPassable(At, 10, &Used);
				Used.Add(Cell);
			}
			else if (Def->bAircraft)
			{
				Cell = At + FIntPoint(Used.Num() % 3, Used.Num() / 3);
				Used.Add(Cell);
			}
			if (ACmdEntity* E = CmdSpawner::SpawnAtCell(World, FName(*Id), Team, Cell, Tag, Yaw))
			{
				Spawned.Add(E);
			}
		}
		IssueOrders(Spawned, A);
		if (CmdJson::Bool(A, TEXT("notify"), false) && State)
		{
			State->AddMessage(PlayerTeam, TEXT("Reinforcements have arrived."), FLinearColor(0.5f, 0.9f, 1.f), TEXT("reinforce"));
		}
	}
	else if (Type == TEXT("order"))
	{
		IssueOrders(ResolveEntities(A), A);
	}
	else if (Type == TEXT("reveal"))
	{
		if (Grid && State)
		{
			const int32 Alliance = State->GetAlliance(ResolveTeam(A, TEXT("team"), PlayerTeam));
			if (CmdJson::Bool(A, TEXT("all")))
			{
				Grid->RevealAll(Alliance);
			}
			else
			{
				const FIntPoint At = CmdJson::Cell(A, TEXT("at"), FIntPoint(0, 0));
				Grid->RevealCircle(Alliance, Cmd::CellToWorld(At), CmdJson::Num(A, TEXT("radius"), 6.0));
			}
		}
	}
	else if (Type == TEXT("camera"))
	{
		if (ACmdPlayerController* PC = Cast<ACmdPlayerController>(World->GetFirstPlayerController()))
		{
			PC->FocusCameraOn(Cmd::CellToWorld(CmdJson::Cell(A, TEXT("at"), FIntPoint(0, 0))));
		}
	}
	else if (Type == TEXT("cinematic"))
	{
		PlayCinematic(CmdJson::Name(A, TEXT("id")));
	}
	else if (Type == TEXT("objective_add"))
	{
		AddObjectiveFromJson(CmdJson::Obj(A, TEXT("objective")));
	}
	else if (Type == TEXT("objective_show"))
	{
		if (FCmdObjective* O = FindObjective(CmdJson::Name(A, TEXT("id"))))
		{
			O->bVisible = true;
			O->ActivatedAt = MissionTime;
			if (State) State->AddMessage(PlayerTeam, FString::Printf(TEXT("New objective: %s"), *O->Text), FLinearColor(0.5f, 0.9f, 1.f));
		}
	}
	else if (Type == TEXT("objective_complete"))
	{
		SetObjectiveState(CmdJson::Name(A, TEXT("id")), ECmdObjectiveState::Completed);
	}
	else if (Type == TEXT("objective_fail"))
	{
		SetObjectiveState(CmdJson::Name(A, TEXT("id")), ECmdObjectiveState::Failed);
	}
	else if (Type == TEXT("timer"))
	{
		const FName Name = CmdJson::Name(A, TEXT("name"), TEXT("timer"));
		FCmdNamedTimer& T = Timers.FindOrAdd(Name);
		T.Label = CmdJson::Str(A, TEXT("label"), TEXT("Time remaining"));
		T.Remaining = CmdJson::Num(A, TEXT("seconds"), 60.0);
		T.bVisible = CmdJson::Bool(A, TEXT("visible"), true);
		T.bExpired = false;
		if (T.bVisible)
		{
			VisibleTimer = Name;
		}
	}
	else if (Type == TEXT("timer_stop"))
	{
		if (FCmdNamedTimer* T = Timers.Find(CmdJson::Name(A, TEXT("name"))))
		{
			T->bVisible = false;
			T->Remaining = 1e9f;
		}
	}
	else if (Type == TEXT("flag"))
	{
		Flags.Add(CmdJson::Name(A, TEXT("name")));
	}
	else if (Type == TEXT("trigger_enable") || Type == TEXT("trigger_disable"))
	{
		const FName Id = CmdJson::Name(A, TEXT("id"));
		for (FCmdTrigger& T : Triggers)
		{
			if (T.Id == Id)
			{
				T.bEnabled = Type == TEXT("trigger_enable");
			}
		}
	}
	else if (Type == TEXT("credits"))
	{
		if (State) State->AddCredits(ResolveTeam(A, TEXT("team"), PlayerTeam), CmdJson::Num(A, TEXT("amount"), 1000.0), false);
	}
	else if (Type == TEXT("victory"))
	{
		EndMission(true, CmdJson::Str(A, TEXT("reason")));
	}
	else if (Type == TEXT("defeat"))
	{
		EndMission(false, CmdJson::Str(A, TEXT("reason")));
	}
	else if (Type == TEXT("transfer"))
	{
		const int32 Team = ResolveTeam(A, TEXT("to"), PlayerTeam);
		for (ACmdEntity* E : ResolveEntities(A))
		{
			E->SetTeam(Team);
		}
	}
	else if (Type == TEXT("destroy"))
	{
		for (ACmdEntity* E : ResolveEntities(A))
		{
			E->bInvulnerable = false;
			E->ApplyDamage(1e7f, ECmdWarhead::HighExplosive, nullptr);
		}
	}
	else if (Type == TEXT("damage"))
	{
		for (ACmdEntity* E : ResolveEntities(A))
		{
			E->Health = FMath::Max(1.f, E->GetMaxHealth() * CmdJson::Num(A, TEXT("health"), 0.5));
		}
	}
	else if (Type == TEXT("explosion"))
	{
		const FIntPoint At = CmdJson::Cell(A, TEXT("at"), FIntPoint(0, 0));
		ACmdEffect::SpawnExplosion(World, Cmd::CellToWorld(At, 40.f), CmdJson::Num(A, TEXT("size"), 2.0), true);
	}
	else if (Type == TEXT("invulnerable"))
	{
		for (ACmdEntity* E : ResolveEntities(A))
		{
			E->bInvulnerable = CmdJson::Bool(A, TEXT("value"), true);
		}
	}
	else if (Type == TEXT("ai"))
	{
		if (GM)
		{
			if (UCmdAICommander* AI = GM->GetAICommander(ResolveTeam(A, TEXT("team"), 1)))
			{
				AI->Configure(A);
			}
		}
	}
	else if (Type == TEXT("ai_attack"))
	{
		if (GM)
		{
			if (UCmdAICommander* AI = GM->GetAICommander(ResolveTeam(A, TEXT("team"), 1)))
			{
				AI->LaunchAttack(CmdJson::Cell(A, TEXT("target")));
			}
		}
	}
	else if (Type == TEXT("build_enable"))
	{
		if (State)
		{
			if (FCmdTeam* T = State->GetTeam(ResolveTeam(A, TEXT("team"), PlayerTeam)))
			{
				T->bCanBuild = CmdJson::Bool(A, TEXT("value"), true);
			}
		}
	}
	else if (Type == TEXT("unlock"))
	{
		if (State)
		{
			if (FCmdTeam* T = State->GetTeam(ResolveTeam(A, TEXT("team"), PlayerTeam)))
			{
				for (const FName& Id : CmdJson::Names(A, TEXT("entities")))
				{
					T->TechLock.Remove(Id);
				}
			}
		}
	}
	else if (Type == TEXT("sun"))
	{
		for (TActorIterator<ACmdEnvironmentActor> It(World); It; ++It)
		{
			It->SetSunRotation(CmdJson::Num(A, TEXT("pitch"), -40.0), CmdJson::Num(A, TEXT("yaw"), 35.0));
		}
	}
	else if (Type == TEXT("delay"))
	{
		FPendingActions P;
		P.Remaining = CmdJson::Num(A, TEXT("seconds"), 1.0);
		P.Actions = CmdJson::Arr(A, TEXT("actions"));
		Pending.Add(P);
	}
	else
	{
		UE_LOG(LogCommanders, Warning, TEXT("Unknown mission action '%s'"), *Type);
	}
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

void ACmdMissionDirector::OnEntityDestroyed(ACmdEntity* Entity, ACmdEntity* Killer)
{
	// Conditions are polled, nothing extra to do yet; hook kept for future scoring/achievements.
}

void ACmdMissionDirector::OnEntityCaptured(ACmdEntity* Entity, int32 OldTeam)
{
}

void ACmdMissionDirector::OnStructureBuilt(ACmdBuilding* Building)
{
}

void ACmdMissionDirector::OnUnitProduced(ACmdUnit* Unit)
{
	if (Unit && Unit->TeamId == PlayerTeam)
	{
		ProducedCounts.FindOrAdd(Unit->DefId)++;
	}
}

void ACmdMissionDirector::OnTeamDefeated(int32 TeamId)
{
}
