#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/CmdTypes.h"
#include "Audio/CmdMusic.h"
#include "CmdAudioActor.generated.h"

class UCmdAudioMixerComponent;
class UAudioComponent;

/**
 * One per level. Owns the software mixer and keeps the soundtrack in step with the
 * game: menu theme, briefing tension, battle music that swells with combat, and
 * victory/defeat stings.
 */
UCLASS()
class COMMANDERS_API ACmdAudioActor : public AActor
{
	GENERATED_BODY()

public:
	ACmdAudioActor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UCmdAudioMixerComponent* GetMixer() const { return Mixer; }

private:
	ECmdMusicMood MoodForFlow(ECmdFlowState Flow) const;
	void ApplyMusicAsset(ECmdMusicMood Mood);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCmdAudioMixerComponent> Mixer;

	/** Plays an imported music track when Audio.json supplies one for the current mood. */
	UPROPERTY()
	TObjectPtr<UAudioComponent> MusicAsset;

	ECmdFlowState LastFlow = ECmdFlowState::None;
	ECmdMusicMood CurrentMood = ECmdMusicMood::Silence;
	bool bAssetMusicActive = false;
};
