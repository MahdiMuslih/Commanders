#include "Game/CmdGameInstance.h"
#include "Game/CmdSaveGame.h"
#include "Data/CmdDataRegistry.h"
#include "Commanders.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"

const FString UCmdSaveGame::SlotName = TEXT("CommandersProgress");

void UCmdGameInstance::Init()
{
	Super::Init();

	if (UGameplayStatics::DoesSaveGameExist(UCmdSaveGame::SlotName, 0))
	{
		Save = Cast<UCmdSaveGame>(UGameplayStatics::LoadGameFromSlot(UCmdSaveGame::SlotName, 0));
	}
	if (!Save)
	{
		Save = Cast<UCmdSaveGame>(UGameplayStatics::CreateSaveGameObject(UCmdSaveGame::StaticClass()));
	}
	if (GEngine)
	{
		GEngine->OnNetworkFailure().AddUObject(this, &UCmdGameInstance::HandleNetworkFailure);
		GEngine->OnTravelFailure().AddUObject(this, &UCmdGameInstance::HandleTravelFailure);
	}
}

void UCmdGameInstance::Shutdown()
{
	if (GEngine)
	{
		GEngine->OnNetworkFailure().RemoveAll(this);
		GEngine->OnTravelFailure().RemoveAll(this);
	}
	Super::Shutdown();
}

// ---------------------------------------------------------------------------
// Multiplayer
// ---------------------------------------------------------------------------

void UCmdGameInstance::HostGame()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	Mode = ECmdSessionMode::MultiplayerHost;
	CurrentMission = NAME_None;
	NetworkError.Reset();
	const FString MapPath = UWorld::RemovePIEPrefix(World->GetOutermost()->GetName());
	UE_LOG(LogCommanders, Log, TEXT("Hosting on %s"), *MapPath);
	UGameplayStatics::OpenLevel(World, FName(*MapPath), true, TEXT("listen"));
}

void UCmdGameInstance::JoinGame(const FString& Address)
{
	UWorld* World = GetWorld();
	const FString Clean = Address.TrimStartAndEnd();
	if (!World || Clean.IsEmpty())
	{
		return;
	}
	if (Save)
	{
		Save->LastJoinAddress = Clean;
		SaveProgress();
	}
	Mode = ECmdSessionMode::MultiplayerClient;
	CurrentMission = NAME_None;
	NetworkError.Reset();
	FString URL = Clean;
	const FString Name = GetPlayerName();
	if (!Name.IsEmpty())
	{
		URL += TEXT("?Name=") + Name.Replace(TEXT(" "), TEXT("_")).Replace(TEXT("?"), TEXT("")).Replace(TEXT("#"), TEXT(""));
	}
	UE_LOG(LogCommanders, Log, TEXT("Joining %s"), *URL);
	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		PC->ClientTravel(URL, TRAVEL_Absolute);
	}
}

void UCmdGameInstance::LeaveGame(const FString& Reason)
{
	NetworkError = Reason;
	ReturnToMenu(TEXT("multiplayer"));
}

FString UCmdGameInstance::GetPlayerName() const
{
	return Save ? Save->PlayerName : FString();
}

void UCmdGameInstance::SetPlayerName(const FString& Name)
{
	if (Save)
	{
		Save->PlayerName = Name.TrimStartAndEnd().Left(20);
		SaveProgress();
	}
}

FString UCmdGameInstance::GetLastJoinAddress() const
{
	return Save ? Save->LastJoinAddress : TEXT("127.0.0.1");
}

void UCmdGameInstance::HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString)
{
	if (Mode != ECmdSessionMode::MultiplayerClient && Mode != ECmdSessionMode::MultiplayerHost)
	{
		return;
	}
	if (World && World->GetNetMode() == NM_ListenServer)
	{
		return;	// one client dropping is handled by the game mode, the host keeps going
	}
	UE_LOG(LogCommanders, Warning, TEXT("Network failure: %s"), *ErrorString);
	// The engine sends us back to the default map; our game mode builds the menu there.
	Mode = ECmdSessionMode::Menu;
	PendingMenuPage = TEXT("multiplayer");
	NetworkError = ErrorString.IsEmpty() ? TEXT("Lost connection to the game.") : ErrorString;
}

void UCmdGameInstance::HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString)
{
	HandleNetworkFailure(World, nullptr, ENetworkFailure::ConnectionLost, ErrorString.IsEmpty() ? TEXT("Could not reach the host.") : ErrorString);
}

void UCmdGameInstance::StartMission(FName MissionId)
{
	Mode = ECmdSessionMode::Campaign;
	CurrentMission = MissionId;
	TravelToBattlefield();
}

void UCmdGameInstance::StartSkirmish(const FCmdSkirmishSettings& Settings)
{
	Mode = ECmdSessionMode::Skirmish;
	Skirmish = Settings;
	if (Skirmish.Seed == 0)
	{
		Skirmish.Seed = FMath::RandRange(1, 999999);
	}
	CurrentMission = NAME_None;
	TravelToBattlefield();
}

void UCmdGameInstance::ReturnToMenu(FName MenuPage)
{
	Mode = ECmdSessionMode::Menu;
	PendingMenuPage = MenuPage;
	TravelToBattlefield();
}

void UCmdGameInstance::RestartSession()
{
	TravelToBattlefield();
}

void UCmdGameInstance::TravelToBattlefield()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	// The battlefield is generated at runtime, so we simply reload whatever level we are in.
	// (Opening a level without "listen" also closes any network connection.)
	const FString MapPath = UWorld::RemovePIEPrefix(World->GetPackage()->GetName());
	UE_LOG(LogCommanders, Log, TEXT("Travelling to %s (mode %d, mission %s)"), *MapPath, static_cast<int32>(Mode), *CurrentMission.ToString());
	UGameplayStatics::OpenLevel(World, FName(*MapPath), true);
}

void UCmdGameInstance::ReportResult(bool bVictory)
{
	if (!Save)
	{
		return;
	}
	if (Mode == ECmdSessionMode::Campaign && bVictory && !CurrentMission.IsNone())
	{
		Save->CompletedMissions.AddUnique(CurrentMission);
	}
	else if (Mode == ECmdSessionMode::Skirmish)
	{
		if (bVictory)
		{
			++Save->SkirmishWins;
		}
		else
		{
			++Save->SkirmishLosses;
		}
	}
	SaveProgress();
}

bool UCmdGameInstance::IsMissionCompleted(FName MissionId) const
{
	return Save && Save->CompletedMissions.Contains(MissionId);
}

bool UCmdGameInstance::IsMissionUnlocked(FName MissionId) const
{
	const UCmdDataRegistry* Data = GetSubsystem<UCmdDataRegistry>();
	const FCmdMissionInfo* Info = Data ? Data->GetMission(MissionId) : nullptr;
	if (!Info)
	{
		return false;
	}
	if (Info->Index == 0 || IsMissionCompleted(MissionId))
	{
		return true;
	}
	const FCmdCampaignDef* Campaign = Data->GetCampaign(Info->Campaign);
	if (!Campaign || !Campaign->Missions.IsValidIndex(Info->Index - 1))
	{
		return false;
	}
	return IsMissionCompleted(Campaign->Missions[Info->Index - 1]);
}

FName UCmdGameInstance::GetNextMission(FName MissionId) const
{
	const UCmdDataRegistry* Data = GetSubsystem<UCmdDataRegistry>();
	const FCmdMissionInfo* Info = Data ? Data->GetMission(MissionId) : nullptr;
	const FCmdCampaignDef* Campaign = Info ? Data->GetCampaign(Info->Campaign) : nullptr;
	if (Campaign && Campaign->Missions.IsValidIndex(Info->Index + 1))
	{
		return Campaign->Missions[Info->Index + 1];
	}
	return NAME_None;
}

bool UCmdGameInstance::IsLastMissionOfCampaign(FName MissionId) const
{
	return GetNextMission(MissionId).IsNone();
}

void UCmdGameInstance::SaveProgress()
{
	if (Save)
	{
		UGameplayStatics::SaveGameToSlot(Save, UCmdSaveGame::SlotName, 0);
	}
}

void UCmdGameInstance::ResetProgress()
{
	if (Save)
	{
		Save->CompletedMissions.Reset();
		Save->SeenCampaignEndings.Reset();
		SaveProgress();
	}
}

void UCmdGameInstance::UnlockAll()
{
	const UCmdDataRegistry* Data = GetSubsystem<UCmdDataRegistry>();
	if (!Data || !Save)
	{
		return;
	}
	for (const FCmdCampaignDef& C : Data->GetCampaigns())
	{
		for (const FName& M : C.Missions)
		{
			Save->CompletedMissions.AddUnique(M);
		}
	}
	SaveProgress();
}
