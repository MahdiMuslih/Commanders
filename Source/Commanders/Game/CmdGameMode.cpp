#include "Game/CmdGameMode.h"
#include "Game/CmdGameState.h"
#include "Game/CmdGameInstance.h"
#include "Player/CmdPlayerController.h"
#include "Player/CmdCameraPawn.h"
#include "UI/CmdHUD.h"
#include "AI/CmdAICommander.h"
#include "Mission/CmdMissionDirector.h"
#include "World/CmdGridSubsystem.h"
#include "World/CmdTerrainActor.h"
#include "World/CmdEnvironmentActor.h"
#include "World/CmdMapGenerator.h"
#include "Gameplay/CmdSpawner.h"
#include "Gameplay/CmdEntity.h"
#include "Data/CmdDataRegistry.h"
#include "Core/CmdJson.h"
#include "Commanders.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/DirectionalLight.h"

ACmdGameMode::ACmdGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	GameStateClass = ACmdGameState::StaticClass();
	PlayerControllerClass = ACmdPlayerController::StaticClass();
	DefaultPawnClass = ACmdCameraPawn::StaticClass();
	HUDClass = ACmdHUD::StaticClass();
}

void ACmdGameMode::StartPlay()
{
	// Let the engine dispatch BeginPlay first, so every entity we spawn below
	// begins play immediately (and registers with the grid / game state).
	Super::StartPlay();

	UCmdGameInstance* GI = Cast<UCmdGameInstance>(GetGameInstance());
	const UCmdDataRegistry* Data = UCmdDataRegistry::Get(this);
	SessionMode = GI ? GI->Mode : ECmdSessionMode::Menu;

	TSharedPtr<FJsonObject> Scenario;
	if (SessionMode == ECmdSessionMode::Campaign && GI && Data)
	{
		const FCmdMissionInfo* Info = Data->GetMission(GI->CurrentMission);
		Scenario = Info ? Info->Json : nullptr;
		if (!Scenario.IsValid())
		{
			UE_LOG(LogCommanders, Error, TEXT("Mission %s missing - falling back to menu."), *GI->CurrentMission.ToString());
			SessionMode = ECmdSessionMode::Menu;
		}
	}
	else if (SessionMode == ECmdSessionMode::Skirmish && GI)
	{
		Scenario = CmdMapGenerator::GenerateSkirmish(GI->Skirmish, Data);
	}

	if (SessionMode == ECmdSessionMode::Menu || !Scenario.IsValid())
	{
		SessionMode = ECmdSessionMode::Menu;
		Scenario = CmdMapGenerator::GenerateMenuBattle(FMath::RandRange(1, 100000));
	}

	ActiveScenario = Scenario;
	BuildScenario(Scenario, SessionMode == ECmdSessionMode::Menu);
}

void ACmdGameMode::BuildScenario(const TSharedPtr<FJsonObject>& Scenario, bool bIsMenu)
{
	UWorld* World = GetWorld();
	ACmdGameState* GS = GetGameState<ACmdGameState>();
	UCmdGridSubsystem* Grid = World->GetSubsystem<UCmdGridSubsystem>();
	const UCmdDataRegistry* Data = UCmdDataRegistry::Get(this);
	if (!GS || !Grid || !Data)
	{
		return;
	}

	const TSharedPtr<FJsonObject> Map = CmdJson::Obj(Scenario, TEXT("map"));
	Grid->InitGrid(CmdJson::Int(Map, TEXT("width"), 64), CmdJson::Int(Map, TEXT("height"), 64));

	SetupTeams(Scenario, bIsMenu);

	// Keep decoration away from where entities start.
	TArray<FIntRect> KeepClear;
	for (const TSharedPtr<FJsonValue>& V : CmdJson::Arr(Scenario, TEXT("entities")))
	{
		const TSharedPtr<FJsonObject> E = V->AsObject();
		const FIntPoint At = CmdJson::Cell(E, TEXT("at"), FIntPoint(0, 0));
		const FCmdEntityDef* Def = Data->GetEntity(CmdJson::Name(E, TEXT("type")));
		const FIntPoint Size = Def ? Def->Footprint : FIntPoint(1, 1);
		KeepClear.Add(FIntRect(At - FIntPoint(1, 1), At + Size + FIntPoint(1, 2)));
	}
	BuildTerrain(Map, KeepClear);

	const bool bShroud = !bIsMenu && CmdJson::Bool(Scenario, TEXT("shroud"), true);
	GS->bShroudEnabled = bShroud;
	Grid->SetShroudViewer(bShroud ? GS->GetLocalAlliance() : -1);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FCmdMapTheme Theme = FCmdMapTheme::FromName(CmdJson::Str(Map, TEXT("theme"), TEXT("temperate")));
	Terrain = World->SpawnActor<ACmdTerrainActor>(ACmdTerrainActor::StaticClass(), FTransform::Identity, Params);
	Terrain->Build(Theme, bShroud);

	bool bHasSun = false;
	for (TActorIterator<ADirectionalLight> It(World); It; ++It)
	{
		bHasSun = true;
	}
	if (!bHasSun)
	{
		Environment = World->SpawnActor<ACmdEnvironmentActor>(ACmdEnvironmentActor::StaticClass(), FTransform::Identity, Params);
		Environment->ApplyTheme(Theme);
	}

	SpawnEntities(Scenario);

	// Initial camera: explicit, else the player's first entity, else the map centre.
	const FIntPoint CamCell = CmdJson::Cell(Scenario, TEXT("camera"));
	if (CamCell.X >= 0)
	{
		CameraStart = Cmd::CellToWorld(CamCell);
	}
	else if (const FCmdTeam* Local = GS->GetLocalTeam())
	{
		CameraStart = Cmd::CellToWorld(Local->StartCell);
	}
	else
	{
		CameraStart = Grid->GetMapCenter();
	}

	// AI commanders.
	for (const FCmdTeam& T : GS->Teams)
	{
		if (!T.bIsAI || T.bIsNeutral)
		{
			continue;
		}
		UCmdAICommander* AI = NewObject<UCmdAICommander>(this);
		const TSharedPtr<FJsonObject> TeamJson = CmdJson::Arr(Scenario, TEXT("teams"))[T.Id]->AsObject();
		const FString Diff = CmdJson::Str(TeamJson, TEXT("difficulty"), TEXT("normal"));
		AI->Init(World, T.Id, Diff == TEXT("easy") ? 0 : (Diff == TEXT("hard") ? 2 : 1));
		AI->Configure(CmdJson::Obj(TeamJson, TEXT("ai_config")));
		AICommanders.Add(AI);
	}

	if (ACmdPlayerController* PC = Cast<ACmdPlayerController>(World->GetFirstPlayerController()))
	{
		PC->OnSessionBuilt(CameraStart, bIsMenu);
	}

	if (bIsMenu)
	{
		GS->Flow = ECmdFlowState::MainMenu;
		bAIActive = true;
		// The attract-mode battle should get going quickly.
		TSharedPtr<FJsonObject> Fast = MakeShared<FJsonObject>();
		Fast->SetNumberField(TEXT("first_attack"), 45.0);
		Fast->SetNumberField(TEXT("attack_interval"), 60.0);
		Fast->SetNumberField(TEXT("attack_size"), 4.0);
		for (UCmdAICommander* AI : AICommanders)
		{
			AI->Configure(Fast);
		}
		return;
	}

	FActorSpawnParameters DirectorParams;
	DirectorParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Director = World->SpawnActor<ACmdMissionDirector>(ACmdMissionDirector::StaticClass(), FTransform::Identity, DirectorParams);
	GS->MissionDirector = Director;
	Director->InitMission(Scenario, SessionMode == ECmdSessionMode::Skirmish);

	// Start the flow on the next tick so the player controller and HUD exist.
	FTimerHandle Handle;
	World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		if (Director)
		{
			Director->StartSession();
		}
	}), 0.1f, false);
}

void ACmdGameMode::SetupTeams(const TSharedPtr<FJsonObject>& Scenario, bool bIsMenu)
{
	ACmdGameState* GS = GetGameState<ACmdGameState>();
	const UCmdDataRegistry* Data = UCmdDataRegistry::Get(this);
	GS->LocalTeamId = INDEX_NONE;

	int32 Index = 0;
	for (const TSharedPtr<FJsonValue>& V : CmdJson::Arr(Scenario, TEXT("teams")))
	{
		const TSharedPtr<FJsonObject> J = V->AsObject();
		FCmdTeam T;
		T.Faction = CmdJson::Name(J, TEXT("faction"), TEXT("concord"));
		const FCmdFactionDef* Faction = Data->GetFaction(T.Faction);
		T.Name = CmdJson::Str(J, TEXT("name"), Faction ? Faction->Name : TEXT("Team"));
		T.Color = CmdJson::Color(J, TEXT("color"), Faction ? Faction->Color : FLinearColor::Gray);
		T.Alliance = FMath::Clamp(CmdJson::Int(J, TEXT("alliance"), Index), 0, CmdConst::MaxAlliances - 1);
		T.Credits = CmdJson::Num(J, TEXT("credits"), 0.0);
		T.bIsLocalPlayer = !bIsMenu && CmdJson::Bool(J, TEXT("player"), false);
		T.bIsAI = CmdJson::Bool(J, TEXT("ai"), false);
		T.bIsNeutral = CmdJson::Bool(J, TEXT("neutral"), false);
		T.bCanBuild = CmdJson::Bool(J, TEXT("can_build"), true);
		T.TechLock = CmdJson::Names(J, TEXT("locked"));

		// "allowed": everything else is locked (used for the early, tutorial-style missions).
		const TArray<FName> Allowed = CmdJson::Names(J, TEXT("allowed"));
		if (Allowed.Num() > 0)
		{
			for (const FName& Id : Data->GetEntityOrder())
			{
				if (!Allowed.Contains(Id))
				{
					T.TechLock.AddUnique(Id);
				}
			}
		}

		const int32 Id = GS->AddTeam(T);
		if (T.bIsLocalPlayer && GS->LocalTeamId == INDEX_NONE)
		{
			GS->LocalTeamId = Id;
		}
		++Index;
	}
}

void ACmdGameMode::BuildTerrain(const TSharedPtr<FJsonObject>& Map, const TArray<FIntRect>& KeepClear)
{
	UCmdGridSubsystem* Grid = GetWorld()->GetSubsystem<UCmdGridSubsystem>();

	auto ForEachRect = [&Map](const TCHAR* Key, TFunctionRef<void(const FIntRect&)> Fn)
	{
		for (const TSharedPtr<FJsonValue>& V : CmdJson::Arr(Map, Key))
		{
			const TArray<double> N = CmdJson::Numbers(V);
			if (N.Num() >= 4)
			{
				const int32 X = FMath::FloorToInt(N[0]);
				const int32 Y = FMath::FloorToInt(N[1]);
				Fn(FIntRect(X, Y, X + FMath::Max(1, FMath::FloorToInt(N[2])), Y + FMath::Max(1, FMath::FloorToInt(N[3]))));
			}
			else if (N.Num() >= 2)
			{
				const int32 X = FMath::FloorToInt(N[0]);
				const int32 Y = FMath::FloorToInt(N[1]);
				Fn(FIntRect(X, Y, X + 1, Y + 1));
			}
		}
	};

	auto IsKeptClear = [&KeepClear](const FIntPoint& C)
	{
		for (const FIntRect& R : KeepClear)
		{
			if (C.X >= R.Min.X && C.X < R.Max.X && C.Y >= R.Min.Y && C.Y < R.Max.Y)
			{
				return true;
			}
		}
		return false;
	};

	auto Paint = [&](const FIntRect& R, ECmdTerrain Type)
	{
		for (int32 Y = R.Min.Y; Y < R.Max.Y; ++Y)
		{
			for (int32 X = R.Min.X; X < R.Max.X; ++X)
			{
				const FIntPoint C(X, Y);
				if (Type == ECmdTerrain::Road || !IsKeptClear(C))
				{
					Grid->SetTerrain(C, Type);
				}
			}
		}
	};

	// Roads go last so they can bridge rivers.
	ForEachRect(TEXT("water"), [&](const FIntRect& R) { Paint(R, ECmdTerrain::Water); });
	ForEachRect(TEXT("rocks"), [&](const FIntRect& R) { Paint(R, ECmdTerrain::Rock); });
	ForEachRect(TEXT("trees"), [&](const FIntRect& R) { Paint(R, ECmdTerrain::Tree); });
	ForEachRect(TEXT("roads"), [&](const FIntRect& R) { Paint(R, ECmdTerrain::Road); });

	// Optional random scatter for hand-made maps.
	FRandomStream Rng(CmdJson::Int(Map, TEXT("seed"), 1));
	const double TreeDensity = CmdJson::Num(Map, TEXT("random_trees"), 0.0);
	const double RockDensity = CmdJson::Num(Map, TEXT("random_rocks"), 0.0);
	if (TreeDensity > 0.0 || RockDensity > 0.0)
	{
		TArray<FIntRect> Clear;
		ForEachRect(TEXT("clear"), [&](const FIntRect& R) { Clear.Add(R); });
		for (int32 Y = 1; Y < Grid->GetHeight() - 1; ++Y)
		{
			for (int32 X = 1; X < Grid->GetWidth() - 1; ++X)
			{
				const FIntPoint C(X, Y);
				if (IsKeptClear(C) || Grid->GetCell(C)->Terrain != ECmdTerrain::Clear)
				{
					continue;
				}
				bool bInClear = false;
				for (const FIntRect& R : Clear)
				{
					bInClear |= C.X >= R.Min.X && C.X < R.Max.X && C.Y >= R.Min.Y && C.Y < R.Max.Y;
				}
				if (bInClear)
				{
					continue;
				}
				const float Roll = Rng.FRand();
				if (Roll < TreeDensity)
				{
					Grid->SetTerrain(C, ECmdTerrain::Tree);
				}
				else if (Roll < TreeDensity + RockDensity)
				{
					Grid->SetTerrain(C, ECmdTerrain::Rock);
				}
			}
		}
	}

	for (const TSharedPtr<FJsonValue>& V : CmdJson::Arr(Map, TEXT("ore")))
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		Grid->AddOreField(CmdJson::Cell(O, TEXT("at"), FIntPoint(0, 0)), CmdJson::Num(O, TEXT("radius"), 3.0), CmdJson::Int(O, TEXT("richness"), 8), CmdJson::Bool(O, TEXT("gems"), false));
	}

	// Border cliffs keep units on the map.
	if (CmdJson::Bool(Map, TEXT("border"), false))
	{
		Grid->FillTerrain(FIntRect(0, 0, Grid->GetWidth(), 1), ECmdTerrain::Rock);
		Grid->FillTerrain(FIntRect(0, Grid->GetHeight() - 1, Grid->GetWidth(), Grid->GetHeight()), ECmdTerrain::Rock);
		Grid->FillTerrain(FIntRect(0, 0, 1, Grid->GetHeight()), ECmdTerrain::Rock);
		Grid->FillTerrain(FIntRect(Grid->GetWidth() - 1, 0, Grid->GetWidth(), Grid->GetHeight()), ECmdTerrain::Rock);
	}
}

void ACmdGameMode::SpawnEntities(const TSharedPtr<FJsonObject>& Scenario)
{
	ACmdGameState* GS = GetGameState<ACmdGameState>();
	UWorld* World = GetWorld();
	TArray<bool> HasStart;
	HasStart.Init(false, GS->Teams.Num());

	for (const TSharedPtr<FJsonValue>& V : CmdJson::Arr(Scenario, TEXT("entities")))
	{
		const TSharedPtr<FJsonObject> E = V->AsObject();
		const FName Type = CmdJson::Name(E, TEXT("type"));
		const int32 Team = CmdJson::Int(E, TEXT("team"), 0);
		const FIntPoint At = CmdJson::Cell(E, TEXT("at"), FIntPoint(0, 0));
		ACmdEntity* Spawned = CmdSpawner::SpawnAtCell(World, Type, Team, At, CmdJson::Name(E, TEXT("tag")), CmdJson::Num(E, TEXT("yaw"), 90.0));
		if (!Spawned)
		{
			continue;
		}
		if (CmdJson::Has(E, TEXT("health")))
		{
			Spawned->Health = Spawned->GetMaxHealth() * CmdJson::Num(E, TEXT("health"), 1.0);
		}
		Spawned->bInvulnerable = CmdJson::Bool(E, TEXT("invulnerable"), false);
		if (GS->Teams.IsValidIndex(Team) && !HasStart[Team])
		{
			HasStart[Team] = true;
			GS->Teams[Team].StartCell = At;
		}
	}
}

UCmdAICommander* ACmdGameMode::GetAICommander(int32 TeamId) const
{
	for (UCmdAICommander* AI : AICommanders)
	{
		if (AI && AI->TeamId == TeamId)
		{
			return AI;
		}
	}
	return nullptr;
}

void ACmdGameMode::OnMissionStarted()
{
	bAIActive = true;
}

void ACmdGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bAIActive || GetWorld()->IsPaused())
	{
		return;
	}
	for (UCmdAICommander* AI : AICommanders)
	{
		if (AI)
		{
			AI->Tick(DeltaSeconds);
		}
	}
}
