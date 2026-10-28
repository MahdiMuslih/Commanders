#pragma once

#include "CoreMinimal.h"
#include "Core/CmdTypes.h"
#include "CmdNetTypes.generated.h"

class ACmdEntity;

/** One seat in a match: a human player or an AI commander. Index in the config = team id. */
USTRUCT(BlueprintType)
struct FCmdMatchSlot
{
	GENERATED_BODY()

	UPROPERTY() FString Name;
	UPROPERTY() FName Faction = TEXT("concord");
	UPROPERTY() int32 Alliance = 0;
	UPROPERTY() bool bHuman = false;
	UPROPERTY() int32 PlayerId = INDEX_NONE;	// APlayerState::GetPlayerId() for humans
	UPROPERTY() int32 Difficulty = 1;
};

/**
 * Everything needed to generate a skirmish battlefield. The server replicates this once
 * when the match starts; every machine runs the same deterministic generator on it,
 * so terrain and ore never travel over the network.
 */
USTRUCT(BlueprintType)
struct FCmdMatchConfig
{
	GENERATED_BODY()

	UPROPERTY() bool bValid = false;
	UPROPERTY() int32 Seed = 1;
	UPROPERTY() int32 MapSize = 80;
	UPROPERTY() int32 Theme = 0;
	UPROPERTY() int32 StartingCredits = 5000;
	UPROPERTY() TArray<FCmdMatchSlot> Slots;
};

/** A player order, sent client -> server. */
USTRUCT(BlueprintType)
struct FCmdCommand
{
	GENERATED_BODY()

	UPROPERTY() ECmdCommandType Type = ECmdCommandType::Stop;
	UPROPERTY() TArray<TObjectPtr<ACmdEntity>> Units;
	UPROPERTY() TObjectPtr<ACmdEntity> Target = nullptr;
	UPROPERTY() FIntPoint Cell = FIntPoint(-1, -1);
	UPROPERTY() FName Id;
	UPROPERTY() int32 Param = 0;
};

/** A changed ore cell, broadcast in batches by the server. */
USTRUCT()
struct FCmdOreDelta
{
	GENERATED_BODY()

	UPROPERTY() int32 Index = 0;
	UPROPERTY() uint8 Amount = 0;
};
