#include "Audio/CmdSynth.h"

namespace
{
	constexpr float SR = static_cast<float>(CmdSynth::SampleRate);
	constexpr float TwoPi = 6.28318530718f;

	/** Small deterministic white-noise source in [-1, 1). */
	struct FNoise
	{
		uint32 State;
		explicit FNoise(uint32 Seed) : State(Seed ? Seed : 0x9E3779B9u) {}
		float Next()
		{
			State ^= State << 13;
			State ^= State >> 17;
			State ^= State << 5;
			return static_cast<float>(State & 0xFFFFFF) / 8388608.f - 1.f;
		}
	};

	/** One-pole low-pass filter; the high-pass output is input minus low-pass. */
	struct FOnePole
	{
		float Z = 0.f;
		float LowPass(float X, float CutoffHz)
		{
			const float A = 1.f - FMath::Exp(-TwoPi * FMath::Clamp(CutoffHz, 10.f, SR * 0.45f) / SR);
			Z += A * (X - Z);
			return Z;
		}
		float HighPass(float X, float CutoffHz) { return X - LowPass(X, CutoffHz); }
	};

	/** Simple band-pass made from two one-poles. */
	struct FBandPass
	{
		FOnePole Lo;
		FOnePole Hi;
		float Process(float X, float CenterHz) { return Lo.LowPass(Hi.HighPass(X, CenterHz * 0.6f), CenterHz * 1.6f); }
	};

	float Env(float T, float Attack, float Decay)
	{
		if (T < 0.f) return 0.f;
		const float A = Attack > 0.f ? FMath::Min(1.f, T / Attack) : 1.f;
		return A * FMath::Exp(-FMath::Max(0.f, T - Attack) / FMath::Max(0.0001f, Decay));
	}

	float ExpSweep(float From, float To, float T, float Time)
	{
		const float A = FMath::Clamp(T / FMath::Max(0.0001f, Time), 0.f, 1.f);
		return From * FMath::Pow(To / From, A);
	}

	/** Rational tanh approximation: smooth saturation without needing <cmath>. */
	float SoftClip(float X)
	{
		const float C = FMath::Clamp(X, -3.f, 3.f);
		return C * (27.f + C * C) / (27.f + 9.f * C * C);
	}

	/** Renders Duration seconds by calling Fn(t) for every sample, then normalises to Peak. */
	template <typename FnType>
	FCmdSoundBuffer Render(float Duration, float Peak, FnType Fn)
	{
		const int32 N = FMath::Max(1, FMath::RoundToInt(Duration * SR));
		TArray<float> Data;
		Data.SetNumUninitialized(N);
		float Max = 0.0001f;
		for (int32 i = 0; i < N; ++i)
		{
			Data[i] = Fn(static_cast<float>(i) / SR);
			Max = FMath::Max(Max, FMath::Abs(Data[i]));
		}
		const float Gain = Peak / Max;
		for (int32 i = 0; i < N; ++i)
		{
			Data[i] *= Gain;
		}
		// Short fade-out so nothing clicks at the end.
		const int32 Fade = FMath::Min(N, 240);
		for (int32 i = 0; i < Fade; ++i)
		{
			Data[N - 1 - i] *= static_cast<float>(i) / Fade;
		}
		return FCmdSoundBuffer(MakeShared<TArray<float>, ESPMode::ThreadSafe>(MoveTemp(Data)));
	}

	// --- Building blocks --------------------------------------------------------

	FCmdSoundBuffer Explosion(float Duration, float Thump, float NoiseStart, float NoiseEnd, float Tau, uint32 Seed, float Crackle)
	{
		FNoise Noise(Seed);
		FOnePole Filter;
		FOnePole Rumble;
		float Phase = 0.f;
		return Render(Duration, 0.95f, [=](float T) mutable
		{
			const float N = Noise.Next();
			const float Cut = ExpSweep(NoiseStart, NoiseEnd, T, Duration * 0.7f);
			float Out = Filter.LowPass(N, Cut) * Env(T, 0.002f, Tau) * 1.6f;
			Out += Rumble.LowPass(N, 90.f) * Env(T, 0.02f, Tau * 2.2f) * 3.f;
			const float F = ExpSweep(Thump * 2.2f, Thump * 0.6f, T, 0.25f);
			Phase += F / SR;
			Out += FMath::Sin(TwoPi * Phase) * Env(T, 0.001f, Tau * 0.6f) * 0.9f;
			if (Crackle > 0.f && T < Duration * 0.5f && Noise.Next() > 1.f - Crackle * 0.004f)
			{
				Out += Noise.Next() * 1.5f;
			}
			return SoftClip(Out * 1.4f);
		});
	}

	FCmdSoundBuffer Gunshot(float Duration, float Tau, float Body, float Crack, uint32 Seed, float Echo)
	{
		FNoise Noise(Seed);
		FOnePole HP;
		FOnePole LP;
		TArray<float> History;
		History.SetNumZeroed(FMath::RoundToInt(SR * 0.13f));
		int32 Index = 0;
		return Render(Duration, 0.85f, [=](float T) mutable
		{
			const float N = Noise.Next();
			float Out = HP.HighPass(N, 1800.f) * Env(T, 0.0005f, Tau * 0.5f) * Crack;
			Out += LP.LowPass(N, Body) * Env(T, 0.001f, Tau) * 2.5f;
			Out += FMath::Sin(TwoPi * 110.f * T) * Env(T, 0.001f, Tau * 0.8f) * 0.4f;
			if (Echo > 0.f)
			{
				const float Delayed = History[Index];
				History[Index] = Out;
				Index = (Index + 1) % History.Num();
				Out += Delayed * Echo;
			}
			return SoftClip(Out);
		});
	}

	FCmdSoundBuffer Tone(float Duration, TArray<float> Freqs, float NoteLen, float Tau, float Square, float Peak = 0.6f)
	{
		return Render(Duration, Peak, [=](float T)
		{
			float Out = 0.f;
			for (int32 i = 0; i < Freqs.Num(); ++i)
			{
				const float Local = T - i * NoteLen;
				if (Local < 0.f)
				{
					continue;
				}
				const float S = FMath::Sin(TwoPi * Freqs[i] * Local);
				const float Shape = FMath::Lerp(S, FMath::Sign(S) * 0.7f, Square);
				Out += (Shape + 0.3f * FMath::Sin(TwoPi * Freqs[i] * 2.f * Local)) * Env(Local, 0.004f, Tau);
			}
			return Out;
		});
	}

	FCmdSoundBuffer Bell(float Duration, TArray<float> Freqs, float NoteLen, float Tau)
	{
		return Render(Duration, 0.6f, [=](float T)
		{
			float Out = 0.f;
			for (int32 i = 0; i < Freqs.Num(); ++i)
			{
				const float Local = T - i * NoteLen;
				if (Local < 0.f)
				{
					continue;
				}
				const float F = Freqs[i];
				Out += (FMath::Sin(TwoPi * F * Local) + 0.35f * FMath::Sin(TwoPi * F * 2.76f * Local) * FMath::Exp(-Local * 6.f)
					+ 0.2f * FMath::Sin(TwoPi * F * 5.4f * Local) * FMath::Exp(-Local * 12.f)) * Env(Local, 0.002f, Tau);
			}
			return Out;
		});
	}

	/** Brass-like note sequence for fanfares: saw through an opening filter. */
	FCmdSoundBuffer Brass(float Duration, TArray<float> Freqs, TArray<float> Starts, float Tau)
	{
		TArray<FOnePole> Filters;
		Filters.SetNum(Freqs.Num());
		TArray<float> Phases;
		Phases.SetNumZeroed(Freqs.Num());
		return Render(Duration, 0.7f, [=](float T) mutable
		{
			float Out = 0.f;
			for (int32 i = 0; i < Freqs.Num(); ++i)
			{
				const float Local = T - Starts[i];
				if (Local < 0.f)
				{
					continue;
				}
				const float Vib = 1.f + 0.004f * FMath::Sin(TwoPi * 5.5f * Local) * FMath::Min(1.f, Local * 2.f);
				Phases[i] = FMath::Fmod(Phases[i] + Freqs[i] * Vib / SR, 1.f);
				const float Saw = Phases[i] * 2.f - 1.f;
				const float Cut = 400.f + 2600.f * FMath::Min(1.f, Local * 8.f) * FMath::Exp(-Local * 1.2f);
				Out += Filters[i].LowPass(Saw, Cut) * Env(Local, 0.03f, Tau);
			}
			return SoftClip(Out * 1.2f);
		});
	}
}

namespace CmdSynth
{
	void BuildBank(TMap<FName, FCmdSoundBuffer>& Bank)
	{
		Bank.Reset();

		// --- Explosions -------------------------------------------------------------
		Bank.Add(TEXT("explode_small"), Explosion(1.0f, 60.f, 3200.f, 220.f, 0.16f, 11, 0.3f));
		Bank.Add(TEXT("explode_medium"), Explosion(1.6f, 50.f, 2800.f, 140.f, 0.3f, 12, 0.6f));
		Bank.Add(TEXT("explode_large"), Explosion(2.6f, 42.f, 2400.f, 70.f, 0.55f, 13, 1.f));
		{
			// Structure collapse: long rumble with secondary bangs.
			FNoise Noise(14);
			FOnePole Rumble;
			FOnePole Body;
			Bank.Add(TEXT("collapse"), Render(3.4f, 0.95f, [=](float T) mutable
			{
				const float N = Noise.Next();
				float Out = Rumble.LowPass(N, 70.f + 60.f * FMath::Exp(-T)) * Env(T, 0.05f, 1.1f) * 5.f;
				for (int32 k = 0; k < 4; ++k)
				{
					const float Local = T - k * 0.42f - 0.05f * k * k;
					Out += Body.LowPass(N, 900.f) * Env(Local, 0.002f, 0.18f) * 1.2f;
					Out += FMath::Sin(TwoPi * 48.f * Local) * Env(Local, 0.002f, 0.2f) * 0.6f;
				}
				return SoftClip(Out);
			}));
		}

		// --- Small arms ---------------------------------------------------------------
		Bank.Add(TEXT("gun_rifle"), Gunshot(0.32f, 0.05f, 1400.f, 0.9f, 21, 0.25f));
		Bank.Add(TEXT("gun_mg"), Gunshot(0.16f, 0.025f, 1700.f, 1.0f, 22, 0.f));
		Bank.Add(TEXT("gun_pistol"), Gunshot(0.22f, 0.03f, 2000.f, 0.8f, 23, 0.15f));
		Bank.Add(TEXT("gun_sniper"), Gunshot(0.9f, 0.06f, 1100.f, 1.3f, 24, 0.45f));
		Bank.Add(TEXT("gun_chain"), Gunshot(0.12f, 0.02f, 2200.f, 1.1f, 25, 0.f));

		// --- Cannons & artillery -------------------------------------------------------
		Bank.Add(TEXT("cannon"), Explosion(1.0f, 80.f, 2200.f, 180.f, 0.12f, 31, 0.f));
		Bank.Add(TEXT("cannon_heavy"), Explosion(1.3f, 60.f, 1800.f, 120.f, 0.18f, 32, 0.f));
		Bank.Add(TEXT("artillery"), Explosion(2.2f, 38.f, 1300.f, 60.f, 0.42f, 33, 0.f));
		{
			FNoise Noise(34);
			FBandPass BP;
			FOnePole Pop;
			auto RocketFn = [=](float Duration) mutable
			{
				return Render(Duration, 0.8f, [=](float T) mutable
				{
					const float N = Noise.Next();
					const float Center = T < Duration * 0.3f ? ExpSweep(500.f, 3200.f, T, Duration * 0.3f) : ExpSweep(3200.f, 900.f, T - Duration * 0.3f, Duration * 0.7f);
					float Out = BP.Process(N, Center) * Env(T, 0.03f, Duration * 0.35f) * 3.f;
					Out += Pop.LowPass(N, 1200.f) * Env(T, 0.001f, 0.03f) * 2.f;
					return SoftClip(Out);
				});
			};
			Bank.Add(TEXT("rocket"), RocketFn(0.9f));
			Bank.Add(TEXT("mlrs"), RocketFn(0.6f));
		}
		{
			FNoise Noise(35);
			FOnePole LP;
			FOnePole HP;
			FNoise Flutter(36);
			float FlutterValue = 1.f;
			Bank.Add(TEXT("flame"), Render(0.8f, 0.8f, [=](float T) mutable
			{
				if (FMath::FloorToInt(T * SR) % 400 == 0)
				{
					FlutterValue = 0.6f + 0.4f * FMath::Abs(Flutter.Next());
				}
				const float N = Noise.Next();
				return HP.HighPass(LP.LowPass(N, 1100.f), 120.f) * Env(T, 0.04f, 0.28f) * FlutterValue * 3.f;
			}));
		}
		{
			auto ZapFn = [](float Duration, float Low, float High, uint32 Seed, float Boom)
			{
				FNoise Noise(Seed);
				FOnePole HP;
				float Phase = 0.f;
				float Freq = Low;
				float Sweep = 0.f;
				return Render(Duration, 0.85f, [=](float T) mutable
				{
					if (FMath::FloorToInt(T * SR) % 280 == 0)
					{
						Freq = FMath::Lerp(Low, High, FMath::Abs(Noise.Next()));
					}
					Phase = FMath::Fmod(Phase + Freq / SR, 1.f);
					const float Saw = Phase * 2.f - 1.f;
					float Out = HP.HighPass(Saw * 0.7f + Noise.Next() * 0.5f, 300.f) * Env(T, 0.002f, Duration * 0.3f);
					if (Boom > 0.f)
					{
						Sweep += ExpSweep(60.f, 900.f, T, Duration) / SR;
						Out += FMath::Sin(TwoPi * Sweep) * Env(T, 0.05f, Duration * 0.5f) * Boom;
					}
					return SoftClip(Out * 1.5f);
				});
			};
			Bank.Add(TEXT("zap"), ZapFn(0.45f, 220.f, 1700.f, 41, 0.f));
			Bank.Add(TEXT("aurora"), ZapFn(1.8f, 90.f, 900.f, 42, 0.9f));
		}
		{
			FNoise Noise(43);
			FBandPass BP;
			Bank.Add(TEXT("flak"), Render(0.4f, 0.75f, [=](float T) mutable
			{
				float Out = BP.Process(Noise.Next(), 1400.f) * Env(T, 0.001f, 0.035f) * 4.f;
				Out += FMath::Sin(TwoPi * 130.f * T) * Env(T, 0.001f, 0.05f);
				const float Second = T - 0.12f;
				Out += BP.Process(Noise.Next(), 1100.f) * Env(Second, 0.001f, 0.03f) * 2.f;
				return SoftClip(Out);
			}));
		}
		{
			FNoise Noise(44);
			FOnePole LP;
			Bank.Add(TEXT("death"), Render(0.45f, 0.6f, [=](float T) mutable
			{
				float Out = LP.LowPass(Noise.Next(), 700.f) * Env(T, 0.003f, 0.07f) * 3.f;
				Out += FMath::Sin(TwoPi * ExpSweep(220.f, 140.f, T, 0.3f) * T) * Env(T, 0.01f, 0.12f) * 0.3f;
				return SoftClip(Out);
			}));
		}
		{
			FNoise Noise(45);
			FOnePole LP;
			Bank.Add(TEXT("deploy"), Render(1.4f, 0.7f, [=](float T) mutable
			{
				float Out = LP.LowPass(Noise.Next(), 500.f) * Env(T, 0.05f, 0.5f) * 2.f;
				Out += FMath::Sin(TwoPi * ExpSweep(120.f, 360.f, T, 1.0f) * T) * Env(T, 0.1f, 0.6f) * 0.4f;
				Out += FMath::Sin(TwoPi * 70.f * (T - 1.0f)) * Env(T - 1.0f, 0.002f, 0.12f);
				return SoftClip(Out);
			}));
		}
		{
			FNoise Noise(46);
			FOnePole LP;
			Bank.Add(TEXT("place"), Render(0.5f, 0.7f, [=](float T) mutable
			{
				float Out = FMath::Sin(TwoPi * ExpSweep(140.f, 55.f, T, 0.15f) * T) * Env(T, 0.001f, 0.1f);
				Out += LP.LowPass(Noise.Next(), 2500.f) * Env(T, 0.0005f, 0.012f) * 1.5f;
				return SoftClip(Out);
			}));
		}

		// --- UI & announcer ------------------------------------------------------------
		Bank.Add(TEXT("click"), Tone(0.08f, { 1400.f }, 0.f, 0.012f, 0.f, 0.35f));
		Bank.Add(TEXT("deny"), Tone(0.3f, { 220.f, 196.f }, 0.11f, 0.05f, 0.8f, 0.45f));
		Bank.Add(TEXT("select"), Tone(0.12f, { 980.f, 1320.f }, 0.035f, 0.02f, 0.2f, 0.35f));
		Bank.Add(TEXT("select_vehicle"), Tone(0.16f, { 620.f, 830.f }, 0.05f, 0.03f, 0.5f, 0.4f));
		Bank.Add(TEXT("select_building"), Tone(0.14f, { 520.f }, 0.f, 0.04f, 0.3f, 0.35f));
		Bank.Add(TEXT("ack"), Tone(0.18f, { 1046.f, 1318.f }, 0.06f, 0.03f, 0.3f, 0.35f));
		Bank.Add(TEXT("ack_attack"), Tone(0.2f, { 740.f, 523.f }, 0.06f, 0.035f, 0.7f, 0.4f));
		Bank.Add(TEXT("message"), Tone(0.12f, { 880.f }, 0.f, 0.03f, 0.f, 0.3f));
		Bank.Add(TEXT("queue"), Tone(0.1f, { 1175.f, 1568.f }, 0.03f, 0.015f, 0.f, 0.3f));
		Bank.Add(TEXT("build_complete"), Bell(1.2f, { 523.f, 659.f, 784.f, 1047.f }, 0.09f, 0.35f));
		Bank.Add(TEXT("unit_ready"), Bell(0.8f, { 784.f, 1047.f }, 0.1f, 0.25f));
		Bank.Add(TEXT("credits"), Bell(0.5f, { 1318.f, 1760.f }, 0.06f, 0.12f));
		Bank.Add(TEXT("capture"), Bell(1.0f, { 587.f, 740.f, 880.f }, 0.1f, 0.3f));
		Bank.Add(TEXT("objective"), Bell(1.6f, { 523.f, 659.f, 784.f, 1047.f, 1319.f }, 0.07f, 0.6f));
		Bank.Add(TEXT("objective_failed"), Bell(1.6f, { 494.f, 415.f, 349.f }, 0.18f, 0.5f));
		Bank.Add(TEXT("reinforce"), Brass(1.3f, { 392.f, 523.f }, { 0.f, 0.22f }, 0.5f));
		Bank.Add(TEXT("victory"), Brass(3.4f, { 392.f, 523.f, 659.f, 784.f, 659.f, 1047.f }, { 0.f, 0.2f, 0.4f, 0.6f, 0.95f, 1.15f }, 0.9f));
		Bank.Add(TEXT("defeat"), Brass(3.6f, { 330.f, 311.f, 262.f, 196.f }, { 0.f, 0.5f, 1.0f, 1.6f }, 0.9f));
		{
			// Two-tone warble.
			Bank.Add(TEXT("low_power"), Render(0.9f, 0.45f, [](float T)
			{
				const float F = FMath::Fmod(T * 8.f, 1.f) < 0.5f ? 420.f : 315.f;
				const float S = FMath::Sin(TwoPi * F * T);
				return FMath::Sign(S) * 0.5f * Env(T, 0.01f, 0.5f) + S * 0.3f;
			}));
			// Rising and falling siren.
			float Phase = 0.f;
			Bank.Add(TEXT("alarm"), Render(1.5f, 0.45f, [=](float T) mutable
			{
				const float F = 520.f + 380.f * FMath::Sin(TwoPi * 0.67f * T - 1.57f) * 0.5f + 190.f;
				Phase += F / SR;
				const float S = FMath::Sin(TwoPi * Phase);
				return (S * 0.7f + FMath::Sign(S) * 0.15f) * FMath::Min(1.f, T * 20.f) * FMath::Min(1.f, (1.5f - T) * 6.f);
			}));
		}
		{
			// Radio squelch: static burst, then a short chirp.
			FNoise Noise(51);
			FOnePole HP;
			Bank.Add(TEXT("radio"), Render(0.3f, 0.4f, [=](float T) mutable
			{
				float Out = HP.HighPass(Noise.Next(), 1500.f) * (T < 0.14f ? 1.f : 0.f) * 0.8f;
				const float Local = T - 0.16f;
				if (Local >= 0.f)
				{
					Out += FMath::Sin(TwoPi * 1600.f * Local) * Env(Local, 0.002f, 0.03f);
				}
				return Out;
			}));
		}
	}
}
