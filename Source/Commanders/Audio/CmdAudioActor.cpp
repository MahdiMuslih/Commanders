#include "Audio/CmdAudioActor.h"
#include "Audio/CmdAudioMixerComponent.h"
#include "Audio/CmdAudioSubsystem.h"
#include "Game/CmdGameState.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"

ACmdAudioActor::ACmdAudioActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;

	Mixer = CreateDefaultSubobject<UCmdAudioMixerComponent>(TEXT("Mixer"));
	RootComponent = Mixer;
}

void ACmdAudioActor::BeginPlay()
{
	Super::BeginPlay();
	if (UCmdAudioSubsystem* Audio = UCmdAudioSubsystem::Get(this))
	{
		Mixer->SetVolumes(Audio->GetMusicVolume(), Audio->GetSfxVolume());
	}
	Mixer->Start();
}

ECmdMusicMood ACmdAudioActor::MoodForFlow(ECmdFlowState Flow) const
{
	switch (Flow)
	{
	case ECmdFlowState::MainMenu: return ECmdMusicMood::Menu;
	case ECmdFlowState::Briefing:
	case ECmdFlowState::Cinematic: return ECmdMusicMood::Tension;
	case ECmdFlowState::Playing: return ECmdMusicMood::Battle;
	case ECmdFlowState::Victory: return ECmdMusicMood::Victory;
	case ECmdFlowState::Defeat: return ECmdMusicMood::Defeat;
	default: return CurrentMood;
	}
}

void ACmdAudioActor::ApplyMusicAsset(ECmdMusicMood Mood)
{
	UCmdAudioSubsystem* Audio = UCmdAudioSubsystem::Get(this);
	USoundBase* Track = Audio ? Audio->GetMusicAsset(Mood) : nullptr;
	if (!Track)
	{
		if (MusicAsset && bAssetMusicActive)
		{
			MusicAsset->FadeOut(1.5f, 0.f);
		}
		bAssetMusicActive = false;
		return;
	}
	if (!MusicAsset)
	{
		MusicAsset = UGameplayStatics::CreateSound2D(this, Track, 1.f, 1.f, 0.f, nullptr, false, false);
		if (!MusicAsset)
		{
			return;
		}
		MusicAsset->bIsUISound = true;
	}
	MusicAsset->SetSound(Track);
	MusicAsset->FadeIn(1.5f, 1.f);
	bAssetMusicActive = true;
}

void ACmdAudioActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UCmdAudioSubsystem* Audio = UCmdAudioSubsystem::Get(this);
	if (!Audio || !Mixer)
	{
		return;
	}
	const float RealDelta = static_cast<float>(FMath::Min(FApp::GetDeltaTime(), 0.1));
	Audio->DecayCombat(RealDelta);

	const ACmdGameState* GS = GetWorld()->GetGameState<ACmdGameState>();
	const ECmdFlowState Flow = GS ? GS->Flow : ECmdFlowState::None;
	if (Flow != LastFlow)
	{
		if (Flow == ECmdFlowState::Victory)
		{
			UCmdAudioSubsystem::Play2D(this, TEXT("victory"));
		}
		else if (Flow == ECmdFlowState::Defeat)
		{
			UCmdAudioSubsystem::Play2D(this, TEXT("defeat"));
		}
		LastFlow = Flow;
	}

	const ECmdMusicMood Mood = MoodForFlow(Flow);
	if (Mood != CurrentMood)
	{
		CurrentMood = Mood;
		ApplyMusicAsset(Mood);
	}
	Mixer->SetMood(Mood);
	Mixer->SetIntensity(Audio->GetCombatIntensity());

	const float MusicVolume = Audio->IsMusicMuted() ? 0.f : Audio->GetMusicVolume();
	if (bAssetMusicActive && MusicAsset)
	{
		MusicAsset->SetVolumeMultiplier(FMath::Max(0.001f, MusicVolume));
		Mixer->SetVolumes(0.f, Audio->GetSfxVolume());
	}
	else
	{
		Mixer->SetVolumes(MusicVolume, Audio->GetSfxVolume());
	}
}
