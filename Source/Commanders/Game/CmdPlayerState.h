#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "Core/CmdTypes.h"
#include "CmdPlayerState.generated.h"

/** Per-player lobby choices and match seat, replicated to everyone. */
UCLASS()
class COMMANDERS_API ACmdPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	ACmdPlayerState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Lobby: chosen faction. */
	UPROPERTY(Replicated)
	FName Faction = TEXT("concord");

	/** Lobby: chosen side (0..3). Players with the same number are allies. */
	UPROPERTY(Replicated)
	int32 Alliance = 0;

	UPROPERTY(Replicated)
	bool bReady = false;

	UPROPERTY(Replicated)
	bool bIsHost = false;

	/** The team this player commands once the match has started (INDEX_NONE in the lobby). */
	UPROPERTY(Replicated)
	int32 SlotTeam = INDEX_NONE;

	UPROPERTY(Replicated)
	ECmdMatchResult MatchResult = ECmdMatchResult::None;
};
