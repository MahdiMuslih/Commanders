#include "Game/CmdGameMode.h"
#include "Audio/CmdAudioSubsystem.h"
#include "Game/CmdGameState.h"
#include "Game/CmdGameInstance.h"
#include "Game/CmdPlayerState.h"
#include "World/CmdWorldBuilder.h"
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
#include "GameFramework/PlayerState.h"

ACmdGameMode::ACmdGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	GameStateClass = ACmdGameState::StaticClass();
	PlayerControllerClass = ACmdPlayerController::StaticClass();
	PlayerStateClass = ACmdPlayerState::StaticClass();
	DefaultPawnClass = ACmdCameraPawn::StaticClass();
	HUDClass = ACmdHUD::StaticClass();
}

void ACmdGameMode::StartPlay()
{
	// Let the engine dispatch BeginPlay first, so every entity we spawn below
	// begins play immediately (and registers with the grid / game state).
	Super::StartPlay();
	if (UCmdAudioSubsystem* Audio = UCmdAudioSubsystem::Get(this))
	{
		Audio->EnsureForWorld(GetWorld());
	}

	UCmdGameInstance* GI = Cast<UCmdGameInstance>(GetGameInstance());
	const UCmdDataRegistry* Data = UCmdDataRegistry::Get(this);
	SessionMode = GI ? GI->Mode : ECmdSessionMode::Menu;

	if (SessionMode == ECmdSessionMode::MultiplayerHost)
	{
		StartLobby();
		return;
	}
	if (SessionMode == ECmdSessionMode::MultiplayerClient)
	{
		// We are a client that lost its server and fell back to a local level.
		SessionMode = ECmdSessionMode::Menu;
	}

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

	SetupTeams(Scenario, bIsMenu);

	const bool bShroud = !bIsMenu && CmdJson::Bool(Scenario, TEXT("shroud"), true);
	GS->bShroudEnabled = bShroud;
	const FCmdBuiltWorld Built = CmdWorldBuilder::BuildEnvironment(World, Scenario, bShroud, GS->GetLocalAlliance(), GS->LocalTeamId);
	Terrain = Built.Terrain;
	Environment = Built.Environment;
	CameraStart = Built.CameraStart;
	Grid->SetTrackOreChanges(GS->IsNetworked());

	SpawnEntities(Scenario);

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
	Director->InitMission(Scenario, SessionMode != ECmdSessionMode::Campaign);
	Director->bMultiplayer = IsMultiplayer();

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
		T.bIsHuman = CmdJson::Bool(J, TEXT("human"), T.bIsLocalPlayer);
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

// ---------------------------------------------------------------------------
// Multiplayer: lobby, joining and leaving
// ---------------------------------------------------------------------------

void ACmdGameMode::StartLobby()
{
	ACmdGameState* GS = GetGameState<ACmdGameState>();
	UCmdGameInstance* GI = Cast<UCmdGameInstance>(GetGameInstance());
	if (!GS)
	{
		return;
	}
	GS->Flow = ECmdFlowState::Lobby;
	GS->bShroudEnabled = false;
	if (GI)
	{
		GS->LobbySettings = GI->Skirmish;
		GS->LobbySettings.NumEnemies = FMath::Clamp(GS->LobbySettings.NumEnemies, 0, CmdConst::MaxPlayers - 1);
	}
	// A sky over the empty lobby world.
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Environment = GetWorld()->SpawnActor<ACmdEnvironmentActor>(ACmdEnvironmentActor::StaticClass(), FTransform::Identity, Params);
	if (Environment)
	{
		Environment->ApplyTheme(FCmdMapTheme::FromName(TEXT("dusk")));
	}
	UE_LOG(LogCommanders, Log, TEXT("Hosting a multiplayer lobby."));
}

void ACmdGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
	if (!ErrorMessage.IsEmpty())
	{
		return;
	}
	const ACmdGameState* GS = GetGameState<ACmdGameState>();
	if (!IsMultiplayer())
	{
		ErrorMessage = TEXT("This game is not hosting.");
	}
	else if (!GS || GS->Flow != ECmdFlowState::Lobby)
	{
		ErrorMessage = TEXT("The match has already started.");
	}
	else if (GetNumPlayers() >= CmdConst::MaxPlayers)
	{
		ErrorMessage = TEXT("The game is full.");
	}
}

void ACmdGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);
	ACmdGameState* GS = GetGameState<ACmdGameState>();
	ACmdPlayerState* PS = NewPlayer ? NewPlayer->GetPlayerState<ACmdPlayerState>() : nullptr;
	if (!IsMultiplayer() || !GS || !PS)
	{
		return;
	}
	PS->bIsHost = NewPlayer->IsLocalController();
	PS->bReady = PS->bIsHost;

	// Free-for-all by default: the lowest side nobody has picked yet.
	TSet<int32> Taken;
	for (APlayerState* Other : GS->PlayerArray)
	{
		if (const ACmdPlayerState* O = Cast<ACmdPlayerState>(Other))
		{
			if (O != PS)
			{
				Taken.Add(O->Alliance);
			}
		}
	}
	PS->Alliance = 0;
	while (Taken.Contains(PS->Alliance) && PS->Alliance < CmdConst::MaxPlayers - 1)
	{
		++PS->Alliance;
	}
	if (PS->bIsHost)
	{
		if (const UCmdGameInstance* GI = Cast<UCmdGameInstance>(GetGameInstance()))
		{
			PS->Faction = GI->Skirmish.PlayerFaction;
			if (!GI->GetPlayerName().IsEmpty())
			{
				ChangeName(NewPlayer, GI->GetPlayerName(), false);
			}
		}
	}
	if (PS->GetPlayerName().IsEmpty())
	{
		ChangeName(NewPlayer, FString::Printf(TEXT("Commander %d"), GetNumPlayers()), false);
	}
	if (!PS->bIsHost)
	{
		GS->AddMessage(INDEX_NONE, FString::Printf(TEXT("%s joined the game."), *PS->GetPlayerName()), FLinearColor(0.6f, 0.9f, 1.f), TEXT("message"));
	}
}

void ACmdGameMode::Logout(AController* Exiting)
{
	ACmdGameState* GS = GetGameState<ACmdGameState>();
	const ACmdPlayerState* PS = Exiting ? Exiting->GetPlayerState<ACmdPlayerState>() : nullptr;
	if (IsMultiplayer() && GS && PS && !PS->bIsHost)
	{
		FString Text = FString::Printf(TEXT("%s left the game."), *PS->GetPlayerName());
		// Mid-match: an AI commander takes over the abandoned side so the battle stays fair.
		FCmdTeam* Team = GS->GetTeam(PS->SlotTeam);
		if (Team && !Team->bDefeated && GS->Flow == ECmdFlowState::Playing)
		{
			Team->bIsAI = true;
			Team->bIsHuman = false;
			UCmdAICommander* AI = NewObject<UCmdAICommander>(this);
			AI->Init(GetWorld(), Team->Id, 1);
			AICommanders.Add(AI);
			Text += TEXT(" The AI takes command of their forces.");
		}
		GS->AddMessage(INDEX_NONE, Text, FLinearColor(1.f, 0.7f, 0.4f), TEXT("message"));
	}
	Super::Logout(Exiting);
}

FCmdMatchConfig ACmdGameMode::BuildMatchConfig(FString& OutError) const
{
	FCmdMatchConfig Config;
	const ACmdGameState* GS = GetGameState<ACmdGameState>();
	if (!GS)
	{
		OutError = TEXT("No game state.");
		return Config;
	}
	const FCmdSkirmishSettings& S = GS->LobbySettings;

	// Humans first, host leading, then everyone in join order.
	TArray<const ACmdPlayerState*> Humans;
	for (APlayerState* P : GS->PlayerArray)
	{
		if (const ACmdPlayerState* PS = Cast<ACmdPlayerState>(P))
		{
			Humans.Add(PS);
		}
	}
	Humans.StableSort([](const ACmdPlayerState& A, const ACmdPlayerState& B) { return A.bIsHost && !B.bIsHost; });
	for (const ACmdPlayerState* PS : Humans)
	{
		if (!PS->bReady)
		{
			OutError = FString::Printf(TEXT("Waiting for %s to get ready."), *PS->GetPlayerName());
			return Config;
		}
		FCmdMatchSlot Slot;
		Slot.Name = PS->GetPlayerName();
		Slot.Faction = PS->Faction;
		Slot.Alliance = PS->Alliance;
		Slot.bHuman = true;
		Slot.PlayerId = PS->GetPlayerId();
		Slot.Difficulty = S.Difficulty;
		Config.Slots.Add(Slot);
	}

	const int32 AICount = FMath::Clamp(S.NumEnemies, 0, CmdConst::MaxPlayers - Config.Slots.Num());
	for (int32 i = 0; i < AICount; ++i)
	{
		FCmdMatchSlot Slot;
		Slot.Name = FString::Printf(TEXT("AI Commander %d"), i + 1);
		Slot.Faction = (i % 2 == 0) ? FName(TEXT("pact")) : FName(TEXT("concord"));
		Slot.Alliance = S.bAlliedAI ? CmdConst::MaxPlayers : CmdConst::MaxPlayers + i;	// sides 4+ belong to the AI
		Slot.Difficulty = S.Difficulty;
		Config.Slots.Add(Slot);
	}

	TSet<int32> Sides;
	for (const FCmdMatchSlot& Slot : Config.Slots)
	{
		Sides.Add(Slot.Alliance);
	}
	if (Sides.Num() < 2)
	{
		OutError = TEXT("Everyone is on the same side: add an AI opponent or pick different teams.");
		return Config;
	}

	Config.bValid = true;
	Config.Seed = FMath::RandRange(1, 999999);
	Config.MapSize = S.MapSize;
	Config.Theme = S.Theme;
	Config.StartingCredits = S.StartingCredits;
	return Config;
}

FString ACmdGameMode::StartMatchFromLobby()
{
	ACmdGameState* GS = GetGameState<ACmdGameState>();
	if (!IsMultiplayer() || !GS || GS->Flow != ECmdFlowState::Lobby)
	{
		return TEXT("Not in a lobby.");
	}
	FString Error;
	const FCmdMatchConfig Config = BuildMatchConfig(Error);
	if (!Config.bValid)
	{
		return Error;
	}

	// Seat everyone, then build the same world the clients will generate from the config.
	int32 HostSeat = INDEX_NONE;
	for (int32 i = 0; i < Config.Slots.Num(); ++i)
	{
		for (APlayerState* P : GS->PlayerArray)
		{
			ACmdPlayerState* PS = Cast<ACmdPlayerState>(P);
			if (PS && Config.Slots[i].bHuman && PS->GetPlayerId() == Config.Slots[i].PlayerId)
			{
				PS->SlotTeam = i;
				PS->MatchResult = ECmdMatchResult::None;
				if (PS->bIsHost)
				{
					HostSeat = i;
				}
			}
		}
	}
	if (Environment)
	{
		Environment->Destroy();	// the match brings its own theatre
		Environment = nullptr;
	}
	if (UCmdGameInstance* GI = Cast<UCmdGameInstance>(GetGameInstance()))
	{
		GI->Skirmish = GS->LobbySettings;	// remembered for the rematch lobby
	}
	GS->Flow = ECmdFlowState::None;
	GS->MatchConfig = Config;
	const TSharedPtr<FJsonObject> Scenario = CmdMapGenerator::GenerateMatch(Config, UCmdDataRegistry::Get(this), HostSeat);
	ActiveScenario = Scenario;
	BuildScenario(Scenario, false);
	return FString();
}

void ACmdGameMode::ReturnToLobby()
{
	if (!IsMultiplayer())
	{
		return;
	}
	// Reload the level as a listen server; connected clients travel with us.
	const FString MapPath = UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName());
	GetWorld()->ServerTravel(MapPath + TEXT("?listen"), true);
}
