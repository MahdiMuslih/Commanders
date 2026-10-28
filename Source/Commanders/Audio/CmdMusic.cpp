#include "Audio/CmdMusic.h"

namespace
{
	constexpr float TwoPi = 6.28318530718f;

	float Frac(float X) { return X - FMath::FloorToFloat(X); }

	float SoftClip(float X)
	{
		const float C = FMath::Clamp(X, -3.f, 3.f);
		return C * (27.f + C * C) / (27.f + 9.f * C * C);
	}

	void AdvanceTime(float& Time, float Dt)
	{
		if (Time < 30.f)
		{
			Time += Dt;
		}
	}
}

FCmdMusicEngine::FCmdMusicEngine()
{
	SetSampleRate(48000.f);
}

void FCmdMusicEngine::SetSampleRate(float InSampleRate)
{
	SampleRate = FMath::Max(8000.f, InSampleRate);
	DelayL.Init(0.f, FMath::CeilToInt(SampleRate * 2.f));
	DelayR.Init(0.f, FMath::CeilToInt(SampleRate * 2.f));
	DelayIndex = 0;
	ResetForMood();
}

float FCmdMusicEngine::NoteFreq(int32 Semitones) const
{
	return 110.f * FMath::Pow(2.f, Semitones / 12.f);
}

float FCmdMusicEngine::Coef(float Hz) const
{
	return 1.f - FMath::Exp(-TwoPi * FMath::Clamp(Hz, 10.f, SampleRate * 0.45f) / SampleRate);
}

float FCmdMusicEngine::Noise()
{
	Rng ^= Rng << 13;
	Rng ^= Rng >> 17;
	Rng ^= Rng << 5;
	return static_cast<float>(Rng & 0xFFFFFF) / 8388608.f - 1.f;
}

int32 FCmdMusicEngine::ChordRoot() const
{
	return Progression.IsValidIndex(ChordIndex) ? Progression[ChordIndex] : 0;
}

int32 FCmdMusicEngine::ChordThird() const
{
	return (ChordMinor.IsValidIndex(ChordIndex) && !ChordMinor[ChordIndex]) ? 4 : 3;
}

void FCmdMusicEngine::TriggerBass(int32 Semitones, float Velocity)
{
	BassFreq = NoteFreq(Root + ChordRoot() + Semitones - 12);
	BassTime = 0.f;
	BassVelocity = Velocity;
}

void FCmdMusicEngine::TriggerLead(int32 Semitones, float Velocity)
{
	LeadFreq = NoteFreq(Root + ChordRoot() + Semitones + 12);
	LeadTime = 0.f;
	LeadVelocity = Velocity;
}

void FCmdMusicEngine::TriggerBell(int32 Semitones, float Velocity)
{
	BellFreq = NoteFreq(Root + Semitones + 24);
	BellTime = 0.f;
	BellVelocity = Velocity;
	BellPan = Noise() * 0.6f;
}

void FCmdMusicEngine::ResetForMood()
{
	Bar = -1;
	StepIndex = 0;
	StepCounter = 0.f;
	ChordIndex = 0;
	bFill = false;
	BassDecay = 3.f;

	switch (CurrentMood)
	{
	case ECmdMusicMood::Menu:
		Tempo = 76.f; Root = 5; bMinor = true;
		Progression = { 0, 8, 5, 7 }; ChordMinor = { true, false, true, false };
		PadLevel = 0.55f; DroneLevel = 0.35f; BassDecay = 0.8f;
		break;
	case ECmdMusicMood::Tension:
		Tempo = 90.f; Root = -5; bMinor = true;
		Progression = { 0, 1, 0, 10 }; ChordMinor = { true, false, true, false };
		PadLevel = 0.3f; DroneLevel = 0.7f; BassDecay = 5.f;
		break;
	case ECmdMusicMood::Battle:
		Tempo = 128.f; Root = 0; bMinor = true;
		Progression = { 0, 8, 3, 10 }; ChordMinor = { true, false, false, false };
		PadLevel = 0.35f; DroneLevel = 0.f; BassDecay = 4.f;
		break;
	case ECmdMusicMood::Victory:
		Tempo = 104.f; Root = 3; bMinor = false;
		Progression = { 0, 7, 9, 5 }; ChordMinor = { false, false, true, false };
		PadLevel = 0.5f; DroneLevel = 0.f; BassDecay = 3.f;
		break;
	case ECmdMusicMood::Defeat:
		Tempo = 60.f; Root = -12; bMinor = true;
		Progression = { 0, 8, 5, 7 }; ChordMinor = { true, false, true, false };
		PadLevel = 0.4f; DroneLevel = 0.5f; BassDecay = 0.6f;
		break;
	default:
		Tempo = 100.f; Root = 0;
		Progression = { 0 }; ChordMinor = { true };
		PadLevel = 0.f; DroneLevel = 0.f;
		break;
	}
	SamplesPerStep = SampleRate * 60.f / Tempo / 4.f;
	DelayLength = FMath::Clamp(FMath::RoundToInt(SamplesPerStep * 3.f), 100, DelayL.Num() - 1);
	Reroll();
}

void FCmdMusicEngine::Reroll()
{
	BassPattern = static_cast<int32>((Rng >> 3) % 3u);
	Noise();
	switch (CurrentMood)
	{
	case ECmdMusicMood::Battle:
		bLead = Noise() > -0.3f;
		// Alternate between two progressions for variety.
		if (Noise() > 0.f)
		{
			Progression = { 0, 8, 3, 10 }; ChordMinor = { true, false, false, false };
		}
		else
		{
			Progression = { 0, 5, 8, 7 }; ChordMinor = { true, true, false, false };
		}
		break;
	case ECmdMusicMood::Victory:
		bLead = true;
		break;
	default:
		bLead = false;
		break;
	}
}

void FCmdMusicEngine::Step()
{
	const int32 S = StepIndex;
	if (S == 0)
	{
		++Bar;
		if (Bar > 0 && Bar % 8 == 0)
		{
			Reroll();
		}
		bFill = Bar % 8 == 7;
		ChordIndex = Progression.Num() > 0 ? Bar % Progression.Num() : 0;
		const int32 Chord = Root + ChordRoot() + 12;
		PadTargets[0] = NoteFreq(Chord);
		PadTargets[1] = NoteFreq(Chord + ChordThird());
		PadTargets[2] = NoteFreq(Chord + 7);
	}

	const int32 Level = Intensity < 0.25f ? 0 : (Intensity < 0.6f ? 1 : 2);
	switch (CurrentMood)
	{
	case ECmdMusicMood::Battle:
	{
		// Drums scale with intensity.
		if (Level == 0)
		{
			if (S == 0 || S == 8) Kick.Trigger(0.7f);
			if (S % 4 == 2) Hat.Trigger(0.25f);
		}
		else
		{
			if (S % 4 == 0) Kick.Trigger(0.95f);
			if (S == 4 || S == 12) Snare.Trigger(0.8f);
			Hat.Trigger(S % 2 == 0 ? 0.35f : 0.18f);
			if (Level == 2)
			{
				if (S == 7 || S == 10) Kick.Trigger(0.7f);
				if (S == 0 && Bar % 8 == 0) Crash.Trigger(0.9f);
				if (bFill && S >= 12)
				{
					Snare.Trigger(0.5f + 0.12f * (S - 12));
				}
				if (bFill && (S == 12 || S == 14)) Tom.Trigger(0.8f);
			}
		}
		if (S == 14 && Bar % 2 == 1) Anvil.Trigger(0.5f);

		// Bass ostinato.
		static const int32 Synco[16] = { 1, 0, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 1 };
		switch (BassPattern)
		{
		case 0: if (S % 4 != 3) TriggerBass(S == 14 ? 7 : 0, 0.8f); break;
		case 1: if (S % 2 == 0) TriggerBass(S % 8 == 6 ? 12 : 0, 0.85f); break;
		default: if (Synco[S] != 0) TriggerBass(S == 10 ? 7 : 0, 0.85f); break;
		}

		// Arpeggiated lead once the fighting heats up.
		if (bLead && Level >= 1)
		{
			const int32 Third = ChordThird();
			const int32 Arp[6] = { 0, Third, 7, 12, 7, Third };
			TriggerLead(Arp[S % 6], S % 2 == 0 ? 0.9f : 0.55f);
		}
		PadLevel = 0.3f + 0.25f * Intensity;
		break;
	}
	case ECmdMusicMood::Menu:
		if (S == 0) Kick.Trigger(0.45f);
		if (S == 0) TriggerBass(0, 0.7f);
		if (S == 10) TriggerBass(7, 0.5f);
		if ((S == 0 && Noise() > -0.2f) || (S == 8 && Noise() > 0.4f) || (S == 12 && Noise() > 0.7f))
		{
			static const int32 Pentatonic[7] = { 0, 3, 5, 7, 10, 12, 15 };
			TriggerBell(ChordRoot() + Pentatonic[(Rng >> 5) % 7u], 0.55f);
		}
		break;

	case ECmdMusicMood::Tension:
		if (S == 0) Kick.Trigger(0.55f);
		if ((S == 0 && Bar % 2 == 0) || S == 10) Tom.Trigger(0.75f);
		if (S % 2 == 0) TriggerBass(0, 0.45f);
		if (S == 12 && Bar % 2 == 1) Anvil.Trigger(0.35f);
		break;

	case ECmdMusicMood::Victory:
		if (S == 0 || S == 8) Kick.Trigger(0.8f);
		if (S == 4 || S == 12) Snare.Trigger(0.5f);
		if (S % 2 == 0) Hat.Trigger(0.22f);
		if (S % 4 == 0) TriggerBass(0, 0.8f);
		if (S == 6) TriggerBass(7, 0.6f);
		{
			const int32 Third = ChordThird();
			const int32 Arp[4] = { 0, Third, 7, 12 };
			TriggerLead(Arp[S % 4] + (S >= 8 ? 12 : 0), S % 2 == 0 ? 0.7f : 0.45f);
		}
		if (S == 0) TriggerBell(ChordRoot() + 12, 0.5f);
		break;

	case ECmdMusicMood::Defeat:
		if (S == 0 && Bar % 2 == 0) Tom.Trigger(0.5f);
		if (S == 0) TriggerBass(0, 0.6f);
		if (S == 0 || S == 8)
		{
			static const int32 Falling[8] = { 12, 10, 8, 7, 5, 3, 2, 0 };
			TriggerBell(Falling[(Bar * 2 + S / 8) % 8], 0.45f);
		}
		break;

	default:
		break;
	}

	StepIndex = (StepIndex + 1) % 16;
}

void FCmdMusicEngine::RenderFrame(float& OutL, float& OutR)
{
	const float Dt = 1.f / SampleRate;
	float L = 0.f;
	float R = 0.f;

	// --- Drums ------------------------------------------------------------------
	if (Kick.Time < 1.f)
	{
		const float F = 45.f + 120.f * FMath::Exp(-Kick.Time * 35.f);
		Kick.Phase = Frac(Kick.Phase + F * Dt);
		const float K = FMath::Sin(TwoPi * Kick.Phase) * FMath::Exp(-Kick.Time * 7.f) * Kick.Velocity * 0.9f;
		L += K;
		R += K;
	}
	if (Snare.Time < 0.6f)
	{
		const float N = Noise();
		const float Sn = ((N - SnareFilter.LowPass(N, Coef(1200.f))) * FMath::Exp(-Snare.Time * 16.f) * 0.6f
			+ FMath::Sin(TwoPi * 190.f * Snare.Time) * FMath::Exp(-Snare.Time * 22.f) * 0.35f) * Snare.Velocity;
		L += Sn;
		R += Sn;
	}
	if (Hat.Time < 0.2f)
	{
		const float N = Noise();
		const float H = (N - HatFilter.LowPass(N, Coef(6000.f))) * FMath::Exp(-Hat.Time * 55.f) * Hat.Velocity * 0.35f;
		L += H * 0.7f;
		R += H;
	}
	if (Crash.Time < 2.5f)
	{
		const float N = Noise();
		const float C = (N - CrashFilter.LowPass(N, Coef(3500.f))) * FMath::Exp(-Crash.Time * 2.2f) * Crash.Velocity * 0.22f;
		L += C;
		R += C * 0.8f;
	}
	if (Tom.Time < 1.5f)
	{
		const float F = 92.f * (1.f + 0.5f * FMath::Exp(-Tom.Time * 25.f));
		Tom.Phase = Frac(Tom.Phase + F * Dt);
		const float T = FMath::Sin(TwoPi * Tom.Phase) * FMath::Exp(-Tom.Time * 4.5f) * Tom.Velocity * 0.7f;
		L += T;
		R += T;
	}
	if (Anvil.Time < 1.f)
	{
		const float At = Anvil.Time;
		const float A = (FMath::Sin(TwoPi * 340.f * At) + 0.6f * FMath::Sin(TwoPi * 819.4f * At) + 0.4f * FMath::Sin(TwoPi * 1315.8f * At))
			* FMath::Exp(-At * 7.f) * Anvil.Velocity * 0.22f;
		L += A * 0.8f;
		R += A;
	}
	AdvanceTime(Kick.Time, Dt);
	AdvanceTime(Snare.Time, Dt);
	AdvanceTime(Hat.Time, Dt);
	AdvanceTime(Crash.Time, Dt);
	AdvanceTime(Tom.Time, Dt);
	AdvanceTime(Anvil.Time, Dt);

	// --- Bass -------------------------------------------------------------------
	if (BassTime < 8.f)
	{
		BassPhase = Frac(BassPhase + BassFreq * Dt);
		const float Saw = BassPhase * 2.f - 1.f;
		const float Cut = 220.f + 1300.f * FMath::Exp(-BassTime * 12.f);
		const float B = BassFilter.LowPass(Saw, Coef(Cut)) * FMath::Exp(-BassTime * BassDecay) * BassVelocity * 0.45f;
		L += B;
		R += B;
		AdvanceTime(BassTime, Dt);
	}

	// --- Pad: three chord notes, two detuned saws each --------------------------
	if (PadLevel > 0.001f)
	{
		float PL = 0.f;
		float PR = 0.f;
		for (int32 i = 0; i < 3; ++i)
		{
			PadFreqs[i] += (PadTargets[i] - PadFreqs[i]) * 0.0015f;
			PadPhases[i * 2] = Frac(PadPhases[i * 2] + PadFreqs[i] * 0.996f * Dt);
			PadPhases[i * 2 + 1] = Frac(PadPhases[i * 2 + 1] + PadFreqs[i] * 1.004f * Dt);
			PL += PadPhases[i * 2] * 2.f - 1.f;
			PR += PadPhases[i * 2 + 1] * 2.f - 1.f;
		}
		const float PadCoef = Coef(1100.f);
		L += PadFilterL.LowPass(PL, PadCoef) * PadLevel * 0.09f;
		R += PadFilterR.LowPass(PR, PadCoef) * PadLevel * 0.09f;
	}

	// --- Drone ------------------------------------------------------------------
	if (DroneLevel > 0.001f)
	{
		DronePhase[0] = Frac(DronePhase[0] + NoteFreq(Root - 12) * Dt);
		DronePhase[1] = Frac(DronePhase[1] + NoteFreq(Root - 5) * Dt);
		const float D = (FMath::Sin(TwoPi * DronePhase[0]) + 0.5f * FMath::Sin(TwoPi * DronePhase[1])) * DroneLevel * 0.12f;
		L += D;
		R += D;
	}

	// --- Lead + ping-pong delay -------------------------------------------------
	float LeadSignal = 0.f;
	if (LeadTime < 2.f)
	{
		LeadPhase = Frac(LeadPhase + LeadFreq * Dt);
		const float Pulse = LeadPhase < 0.35f ? 1.f : -1.f;
		LeadSignal = LeadFilter.LowPass(Pulse, Coef(2600.f)) * FMath::Exp(-LeadTime * 9.f) * LeadVelocity * 0.11f;
		AdvanceTime(LeadTime, Dt);
	}
	{
		const int32 ReadIndex = (DelayIndex - DelayLength + DelayL.Num()) % DelayL.Num();
		const float DL = DelayL[ReadIndex];
		const float DR = DelayR[ReadIndex];
		DelayL[DelayIndex] = LeadSignal + DR * 0.42f;
		DelayR[DelayIndex] = DL * 0.42f;
		DelayIndex = (DelayIndex + 1) % DelayL.Num();
		L += LeadSignal + DL * 0.55f;
		R += LeadSignal + DR * 0.55f;
	}

	// --- Bell -------------------------------------------------------------------
	if (BellTime < 4.f)
	{
		const float Bt = BellTime;
		const float B = (FMath::Sin(TwoPi * BellFreq * Bt) + 0.3f * FMath::Sin(TwoPi * BellFreq * 2.76f * Bt) * FMath::Exp(-Bt * 5.f))
			* FMath::Exp(-Bt * 1.8f) * BellVelocity * 0.16f;
		L += B * (1.f - BellPan);
		R += B * (1.f + BellPan);
		AdvanceTime(BellTime, Dt);
	}

	OutL = L;
	OutR = R;
}

void FCmdMusicEngine::Render(float* Out, int32 Frames, float Gain)
{
	const float Dt = 1.f / SampleRate;
	for (int32 i = 0; i < Frames; ++i)
	{
		// Mood changes: fade out, switch arrangement, fade back in.
		if (TargetMood != CurrentMood)
		{
			Fade -= Dt / 1.5f;
			if (Fade <= 0.f)
			{
				Fade = 0.f;
				CurrentMood = TargetMood;
				ResetForMood();
			}
		}
		else if (Fade < 1.f)
		{
			Fade = FMath::Min(1.f, Fade + Dt / 2.f);
		}
		Intensity += (TargetIntensity - Intensity) * Dt * 0.6f;

		if (CurrentMood == ECmdMusicMood::Silence)
		{
			continue;
		}

		StepCounter -= 1.f;
		if (StepCounter <= 0.f)
		{
			StepCounter += SamplesPerStep;
			Step();
		}

		float L = 0.f;
		float R = 0.f;
		RenderFrame(L, R);
		const float G = Gain * Fade;
		Out[i * 2] += SoftClip(L * 1.1f) * G;
		Out[i * 2 + 1] += SoftClip(R * 1.1f) * G;
	}
}
