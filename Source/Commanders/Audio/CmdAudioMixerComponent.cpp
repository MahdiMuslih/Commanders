#include "Audio/CmdAudioMixerComponent.h"

UCmdAudioMixerComponent::UCmdAudioMixerComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Plain stereo, keeps playing while the game is paused (menus, briefings).
	bAllowSpatialization = false;
	bIsUISound = true;
	bAutoActivate = false;
}

bool UCmdAudioMixerComponent::Init(int32& SampleRate)
{
	NumChannels = 2;
	DeviceRate = SampleRate > 0 ? static_cast<float>(SampleRate) : 48000.f;
	Music.SetSampleRate(DeviceRate);
	Voices.Reserve(MaxVoices);
	return true;
}

void UCmdAudioMixerComponent::PlayVoice(const FCmdSoundBuffer& Buffer, float Volume, float Pan, float Pitch)
{
	if (!Buffer.IsValid() || Volume <= 0.001f)
	{
		return;
	}
	// Constant-power panning.
	const float Angle = (FMath::Clamp(Pan, -1.f, 1.f) + 1.f) * 0.25f * PI;
	FVoiceCommand Command;
	Command.Buffer = Buffer;
	Command.GainL = Volume * FMath::Cos(Angle) * 1.4142f;
	Command.GainR = Volume * FMath::Sin(Angle) * 1.4142f;
	Command.Pitch = FMath::Clamp(Pitch, 0.25f, 4.f);
	Commands.Enqueue(MoveTemp(Command));
}

int32 UCmdAudioMixerComponent::OnGenerateAudio(float* OutAudio, int32 NumSamples)
{
	const int32 Channels = FMath::Max(1, NumChannels);
	const int32 Frames = NumSamples / Channels;
	if (Scratch.Num() < Frames * 2)
	{
		Scratch.SetNumZeroed(Frames * 2);
	}
	FMemory::Memzero(Scratch.GetData(), sizeof(float) * Frames * 2);

	// New voices from the game thread; steal the oldest if we run out.
	FVoiceCommand Command;
	while (Commands.Dequeue(Command))
	{
		if (Voices.Num() >= MaxVoices)
		{
			Voices.RemoveAt(0);
		}
		FVoice Voice;
		Voice.Buffer = MoveTemp(Command.Buffer);
		Voice.GainL = Command.GainL;
		Voice.GainR = Command.GainR;
		Voice.Step = Command.Pitch * static_cast<float>(CmdSynth::SampleRate) / DeviceRate;
		Voices.Add(MoveTemp(Voice));
	}

	// Soundtrack.
	Music.SetMood(static_cast<ECmdMusicMood>(PendingMood.load()));
	Music.SetIntensity(PendingIntensity.load());
	Music.Render(Scratch.GetData(), Frames, MusicVolume.load());

	// Sound effects (linear-interpolated resampling).
	const float Sfx = SfxVolume.load();
	for (int32 v = Voices.Num() - 1; v >= 0; --v)
	{
		FVoice& Voice = Voices[v];
		const TArray<float>& Data = *Voice.Buffer;
		const int32 Length = Data.Num();
		bool bFinished = false;
		for (int32 f = 0; f < Frames; ++f)
		{
			const int32 Index = static_cast<int32>(Voice.Position);
			if (Index + 1 >= Length)
			{
				bFinished = true;
				break;
			}
			const float Alpha = static_cast<float>(Voice.Position - Index);
			const float Sample = Data[Index] + (Data[Index + 1] - Data[Index]) * Alpha;
			Scratch[f * 2] += Sample * Voice.GainL * Sfx;
			Scratch[f * 2 + 1] += Sample * Voice.GainR * Sfx;
			Voice.Position += Voice.Step;
		}
		if (bFinished)
		{
			Voices.RemoveAtSwap(v);
		}
	}

	// Gentle peak limiter, then write out in the device's channel layout.
	for (int32 f = 0; f < Frames; ++f)
	{
		float L = Scratch[f * 2];
		float R = Scratch[f * 2 + 1];
		const float Peak = FMath::Max(FMath::Abs(L), FMath::Abs(R)) * LimiterGain;
		if (Peak > 0.9f)
		{
			LimiterGain *= 0.9f / Peak;
		}
		else
		{
			LimiterGain = FMath::Min(1.f, LimiterGain + 0.00002f);
		}
		L *= LimiterGain;
		R *= LimiterGain;

		if (Channels == 1)
		{
			OutAudio[f] = (L + R) * 0.5f;
		}
		else
		{
			OutAudio[f * Channels] = L;
			OutAudio[f * Channels + 1] = R;
			for (int32 c = 2; c < Channels; ++c)
			{
				OutAudio[f * Channels + c] = 0.f;
			}
		}
	}
	return NumSamples;
}
