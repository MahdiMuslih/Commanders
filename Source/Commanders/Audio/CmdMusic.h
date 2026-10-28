#pragma once

#include "CoreMinimal.h"

enum class ECmdMusicMood : uint8
{
	Silence,
	Menu,		// brooding, slow, minor key
	Tension,	// briefings and cutscenes: drones and timpani
	Battle,		// driving industrial march; intensity adds drums and lead
	Victory,	// major-key triumph
	Defeat		// slow, falling, minor
};

/**
 * Real-time procedural soundtrack. A 16th-note step sequencer drives drums, bass,
 * a detuned pad, an arpeggiated lead with a ping-pong delay, and bells, all
 * synthesised sample by sample. Every eight bars it re-rolls its arrangement
 * (bass pattern, lead on/off, fills), so the music never loops exactly.
 *
 * Pure DSP with no engine objects: Render() is called from the audio thread.
 */
class COMMANDERS_API FCmdMusicEngine
{
public:
	FCmdMusicEngine();

	void SetSampleRate(float InSampleRate);

	/** Requests a mood; the engine fades out, switches and fades back in. */
	void SetMood(ECmdMusicMood Mood) { TargetMood = Mood; }

	/** 0 = calm, 1 = full combat. Only affects the Battle mood. */
	void SetIntensity(float Value) { TargetIntensity = FMath::Clamp(Value, 0.f, 1.f); }

	/** Adds Frames of interleaved stereo audio into Out (L, R, L, R...). */
	void Render(float* Out, int32 Frames, float Gain);

private:
	struct FDrum
	{
		float Time = 100.f;
		float Velocity = 0.f;
		float Phase = 0.f;
		void Trigger(float InVelocity) { Time = 0.f; Velocity = InVelocity; Phase = 0.f; }
	};

	struct FFilter
	{
		float Z = 0.f;
		float LowPass(float X, float Coefficient) { Z += Coefficient * (X - Z); return Z; }
	};

	void ResetForMood();
	void Reroll();
	void Step();
	void TriggerBass(int32 Semitones, float Velocity);
	void TriggerLead(int32 Semitones, float Velocity);
	void TriggerBell(int32 Semitones, float Velocity);
	int32 ChordRoot() const;
	int32 ChordThird() const;
	void RenderFrame(float& L, float& R);
	float NoteFreq(int32 Semitones) const;
	float Coef(float Hz) const;
	float Noise();

	float SampleRate = 48000.f;
	ECmdMusicMood CurrentMood = ECmdMusicMood::Silence;
	ECmdMusicMood TargetMood = ECmdMusicMood::Silence;
	float TargetIntensity = 0.f;
	float Intensity = 0.f;
	float Fade = 0.f;

	// Sequencer
	float Tempo = 120.f;
	float SamplesPerStep = 6000.f;
	float StepCounter = 0.f;
	int32 StepIndex = 0;
	int32 Bar = 0;
	int32 Root = 0;				// semitones relative to A2 (110 Hz)
	bool bMinor = true;
	TArray<int32> Progression;
	TArray<bool> ChordMinor;
	int32 ChordIndex = 0;
	int32 BassPattern = 0;
	bool bLead = false;
	bool bFill = false;
	uint32 Rng = 0x1234567u;

	// Voices
	FDrum Kick;
	FDrum Snare;
	FDrum Hat;
	FDrum Crash;
	FDrum Tom;
	FDrum Anvil;
	FFilter SnareFilter;
	FFilter HatFilter;
	FFilter CrashFilter;

	float BassFreq = 55.f;
	float BassPhase = 0.f;
	float BassTime = 100.f;
	float BassVelocity = 0.f;
	float BassDecay = 3.f;
	FFilter BassFilter;

	float PadFreqs[3] = { 110.f, 130.8f, 164.8f };
	float PadTargets[3] = { 110.f, 130.8f, 164.8f };
	float PadPhases[6] = {};
	float PadLevel = 0.f;
	FFilter PadFilterL;
	FFilter PadFilterR;

	float LeadFreq = 440.f;
	float LeadPhase = 0.f;
	float LeadTime = 100.f;
	float LeadVelocity = 0.f;
	FFilter LeadFilter;

	float BellFreq = 880.f;
	float BellTime = 100.f;
	float BellVelocity = 0.f;
	float BellPan = 0.f;

	float DroneLevel = 0.f;
	float DronePhase[2] = {};

	TArray<float> DelayL;
	TArray<float> DelayR;
	int32 DelayIndex = 0;
	int32 DelayLength = 9000;
};
