#include "Game/CmdPlayerState.h"
#include "Net/UnrealNetwork.h"

ACmdPlayerState::ACmdPlayerState()
{
	NetUpdateFrequency = 5.f;
}

void ACmdPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACmdPlayerState, Faction);
	DOREPLIFETIME(ACmdPlayerState, Alliance);
	DOREPLIFETIME(ACmdPlayerState, bReady);
	DOREPLIFETIME(ACmdPlayerState, bIsHost);
	DOREPLIFETIME(ACmdPlayerState, SlotTeam);
	DOREPLIFETIME(ACmdPlayerState, MatchResult);
}
