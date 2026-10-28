#pragma once

#include "CoreMinimal.h"

/** Shared, immutable mono PCM buffer (float, CmdSynth::SampleRate). Safe to hand to the audio thread. */
using FCmdSoundBuffer = TSharedPtr<const TArray<float>, ESPMode::ThreadSafe>;

/**
 * Offline synthesis of every sound effect in the game. Nothing is loaded from disk:
 * explosions, gunfire, energy weapons, UI blips and announcer chimes are built from
 * noise, oscillators, filters and envelopes when the game starts.
 */
namespace CmdSynth
{
	constexpr int32 SampleRate = 48000;

	/** Builds the full sound bank: id -> buffer. */
	COMMANDERS_API void BuildBank(TMap<FName, FCmdSoundBuffer>& OutBank);
}
