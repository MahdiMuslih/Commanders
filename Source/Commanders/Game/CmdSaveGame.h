#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "CmdSaveGame.generated.h"

UCLASS()
class COMMANDERS_API UCmdSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	static const FString SlotName;

	UPROPERTY()
	TArray<FName> CompletedMissions;

	UPROPERTY()
	TArray<FName> SeenCampaignEndings;

	UPROPERTY()
	int32 SkirmishWins = 0;

	UPROPERTY()
	int32 SkirmishLosses = 0;

	UPROPERTY()
	float EdgeScrollSpeed = 1.f;

	UPROPERTY()
	float MusicVolume = 0.6f;

	UPROPERTY()
	float SfxVolume = 0.85f;

	UPROPERTY()
	FString PlayerName;

	UPROPERTY()
	FString LastJoinAddress = TEXT("127.0.0.1");
};
