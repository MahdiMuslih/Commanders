#pragma once

#include "CoreMinimal.h"
#include "Components/SynthComponent.h"
#include "Containers/Queue.h"
#include "Audio/CmdSynth.h"
#include "Audio/CmdMusic.h"
#include <atomic>
#include "CmdAudioMixerComponent.generated.h"

/**
 * The game's software mixer. One synth component renders the procedural soundtrack
 * plus every sound effect voice, so the whole soundscape needs no audio assets.
 *
 * Threading: the Play/Set functions are called on the game thread and only push to a
 * lock-free queue or store atomics; OnGenerateAudio runs on the audio render thread
 * and owns the voices and the music engine.
 */
UCLASS()
class COMMANDERS_API UCmdAudioMixerComponent : public USynthComponent
{
	GENERATED_BODY()

public:
	UCmdAudioMixerComponent(const FObjectInitializer& ObjectInitializer);

	/** Queues a one-shot. Pan is -1 (left) .. 1 (right). */
	void PlayVoice(const FCmdSoundBuffer& Buffer, float Volume, float Pan, float Pitch);

	void SetMood(ECmdMusicMood Mood) { PendingMood.store(static_cast<int32>(Mood)); }
	void SetIntensity(float Value) { PendingIntensity.store(Value); }
	void SetVolumes(float MusicLevel, float EffectsLevel) { MusicVolume.store(MusicLevel); SfxVolume.store(EffectsLevel); }

protected:
	virtual bool Init(int32& SampleRate) override;
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;

private:
	struct FVoiceCommand
	{
		FCmdSoundBuffer Buffer;
		float GainL = 1.f;
		float GainR = 1.f;
		float Pitch = 1.f;
	};

	struct FVoice
	{
		FCmdSoundBuffer Buffer;
		double Position = 0.0;
		float GainL = 1.f;
		float GainR = 1.f;
		float Step = 1.f;
	};

	static constexpr int32 MaxVoices = 40;

	TQueue<FVoiceCommand, EQueueMode::Mpsc> Commands;

	// Audio-thread state.
	TArray<FVoice> Voices;
	TArray<float> Scratch;
	FCmdMusicEngine Music;
	float DeviceRate = 48000.f;
	float LimiterGain = 1.f;

	// Shared state.
	std::atomic<int32> PendingMood{ 0 };
	std::atomic<float> PendingIntensity{ 0.f };
	std::atomic<float> MusicVolume{ 0.6f };
	std::atomic<float> SfxVolume{ 0.85f };
};
