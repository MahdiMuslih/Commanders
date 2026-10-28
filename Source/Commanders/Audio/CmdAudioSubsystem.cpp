#include "Audio/CmdAudioSubsystem.h"
#include "Audio/CmdAudioActor.h"
#include "Audio/CmdAudioMixerComponent.h"
#include "Data/CmdDataRegistry.h"
#include "Game/CmdGameInstance.h"
#include "Game/CmdSaveGame.h"
#include "Core/CmdJson.h"
#include "Commanders.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

namespace
{
	bool IsCombatSound(const FName& Sound)
	{
		const FString S = Sound.ToString();
		return S.StartsWith(TEXT("gun_")) || S.StartsWith(TEXT("cannon")) || S.StartsWith(TEXT("explode")) || S == TEXT("rocket")
			|| S == TEXT("mlrs") || S == TEXT("artillery") || S == TEXT("flame") || S == TEXT("zap") || S == TEXT("flak") || S == TEXT("aurora");
	}

	const TCHAR* MoodName(ECmdMusicMood Mood)
	{
		switch (Mood)
		{
		case ECmdMusicMood::Menu: return TEXT("menu");
		case ECmdMusicMood::Tension: return TEXT("tension");
		case ECmdMusicMood::Battle: return TEXT("battle");
		case ECmdMusicMood::Victory: return TEXT("victory");
		case ECmdMusicMood::Defeat: return TEXT("defeat");
		default: return TEXT("silence");
		}
	}
}

void UCmdAudioSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Collection.InitializeDependency<UCmdDataRegistry>();
	Super::Initialize(Collection);
	const double Start = FPlatformTime::Seconds();
	CmdSynth::BuildBank(Bank);
	LoadOverrides();
	UE_LOG(LogCommanders, Log, TEXT("Synthesised %d sounds in %.1f ms"), Bank.Num(), (FPlatformTime::Seconds() - Start) * 1000.0);
}

UCmdAudioSubsystem* UCmdAudioSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = (WorldContext && GEngine) ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UCmdAudioSubsystem>() : nullptr;
}

void UCmdAudioSubsystem::Play2D(const UObject* WorldContext, FName Sound, float Volume)
{
	if (UCmdAudioSubsystem* Audio = Get(WorldContext))
	{
		Audio->Play(WorldContext->GetWorld(), Sound, nullptr, Volume);
	}
}

void UCmdAudioSubsystem::PlayAt(const UObject* WorldContext, FName Sound, const FVector& Location, float Volume)
{
	if (UCmdAudioSubsystem* Audio = Get(WorldContext))
	{
		Audio->Play(WorldContext->GetWorld(), Sound, &Location, Volume);
	}
}

void UCmdAudioSubsystem::PlayWeapon(const UObject* WorldContext, const FCmdWeaponDef& Weapon, const FVector& Location)
{
	FName Sound = Weapon.Sound;
	if (Sound.IsNone())
	{
		// Sensible defaults for weapons that don't name a sound.
		switch (Weapon.Warhead)
		{
		case ECmdWarhead::SmallArms: Sound = Weapon.Cooldown < 0.6f ? TEXT("gun_mg") : TEXT("gun_rifle"); break;
		case ECmdWarhead::ArmorPiercing: Sound = Weapon.ProjectileSpeed < 3000.f ? TEXT("rocket") : TEXT("cannon"); break;
		case ECmdWarhead::HighExplosive: Sound = TEXT("artillery"); break;
		case ECmdWarhead::Fire: Sound = TEXT("flame"); break;
		case ECmdWarhead::Electric: Sound = TEXT("zap"); break;
		case ECmdWarhead::Flak: Sound = TEXT("flak"); break;
		default: Sound = TEXT("gun_rifle"); break;
		}
	}
	PlayAt(WorldContext, Sound, Location, 1.f);
}

ACmdAudioActor* UCmdAudioSubsystem::EnsureForWorld(UWorld* World)
{
	if (!World || !World->IsGameWorld() || World->GetNetMode() == NM_DedicatedServer)
	{
		return nullptr;
	}
	if (ACmdAudioActor* Existing = AudioActor.Get())
	{
		if (Existing->GetWorld() == World && IsValid(Existing))
		{
			return Existing;
		}
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACmdAudioActor* Spawned = World->SpawnActor<ACmdAudioActor>(ACmdAudioActor::StaticClass(), FTransform::Identity, Params);
	AudioActor = Spawned;
	CombatIntensity = 0.f;
	return Spawned;
}

void UCmdAudioSubsystem::Play(UWorld* World, FName Sound, const FVector* Location, float Volume)
{
	if (!World || Sound.IsNone())
	{
		return;
	}
	ACmdAudioActor* Actor = EnsureForWorld(World);
	if (!Actor)
	{
		return;
	}

	// Rate-limit: forty machine guns firing in the same frame should not be forty sounds.
	const double Now = FPlatformTime::Seconds();
	double& Last = LastPlayed.FindOrAdd(Sound, -1000.0);
	if (Now - Last < MinIntervalFor(Sound))
	{
		return;
	}

	float Gain = Volume * GainFor(Sound);
	float Pan = 0.f;
	float Pitch = 1.f;
	if (Location)
	{
		// Listen from the point on the ground the camera is looking at (works in cinematics too).
		const APlayerController* PC = World->GetFirstPlayerController();
		if (PC && PC->PlayerCameraManager)
		{
			const FVector CamLocation = PC->PlayerCameraManager->GetCameraLocation();
			const FVector Forward = PC->PlayerCameraManager->GetCameraRotation().Vector();
			FVector Focus = CamLocation;
			if (Forward.Z < -0.05f)
			{
				Focus = CamLocation + Forward * (-CamLocation.Z / Forward.Z);
			}
			const float Cells = FVector::Dist2D(Focus, *Location) / CmdConst::CellSize;
			const float Attenuation = Cells < 9.f ? 1.f : FMath::Clamp(1.f - (Cells - 9.f) / 24.f, 0.f, 1.f);
			Gain *= Attenuation * Attenuation;

			int32 SizeX = 0;
			int32 SizeY = 0;
			PC->GetViewportSize(SizeX, SizeY);
			FVector2D Screen;
			if (SizeX > 0 && PC->ProjectWorldLocationToScreen(*Location, Screen))
			{
				Pan = FMath::Clamp((static_cast<float>(Screen.X) / SizeX) * 2.f - 1.f, -1.f, 1.f) * 0.75f;
			}
		}
		Pitch = FMath::FRandRange(0.94f, 1.06f);
	}
	if (Gain < 0.02f)
	{
		return;
	}
	Last = Now;

	if (Location && IsCombatSound(Sound))
	{
		CombatIntensity = FMath::Min(1.f, CombatIntensity + 0.05f * Gain);
	}

	if (const TObjectPtr<USoundBase>* Asset = AssetOverrides.Find(Sound))
	{
		UGameplayStatics::PlaySound2D(World, *Asset, Gain * GetSfxVolume(), Pitch);
		return;
	}
	const FCmdSoundBuffer* Buffer = Bank.Find(Sound);
	if (!Buffer)
	{
		UE_LOG(LogCommanders, Warning, TEXT("Unknown sound '%s'"), *Sound.ToString());
		return;
	}
	if (UCmdAudioMixerComponent* Mixer = Actor->GetMixer())
	{
		Mixer->PlayVoice(*Buffer, Gain, Pan, Pitch);
	}
}

void UCmdAudioSubsystem::DecayCombat(float DeltaSeconds)
{
	CombatIntensity = FMath::Max(0.f, CombatIntensity - DeltaSeconds * 0.06f);
}

float UCmdAudioSubsystem::GainFor(FName Sound) const
{
	const float* Gain = Gains.Find(Sound);
	return Gain ? *Gain : 1.f;
}

float UCmdAudioSubsystem::MinIntervalFor(FName Sound) const
{
	const FString S = Sound.ToString();
	if (S.StartsWith(TEXT("gun_"))) return 0.045f;
	if (S.StartsWith(TEXT("explode")) || S == TEXT("collapse")) return 0.07f;
	if (S == TEXT("message") || S == TEXT("queue")) return 0.12f;
	if (S == TEXT("alarm") || S == TEXT("low_power")) return 3.0f;
	if (S == TEXT("click") || S.StartsWith(TEXT("select")) || S.StartsWith(TEXT("ack"))) return 0.03f;
	return 0.05f;
}

float UCmdAudioSubsystem::GetMusicVolume() const
{
	const UCmdGameInstance* GI = Cast<UCmdGameInstance>(GetGameInstance());
	return (GI && GI->Save) ? GI->Save->MusicVolume : 0.6f;
}

float UCmdAudioSubsystem::GetSfxVolume() const
{
	const UCmdGameInstance* GI = Cast<UCmdGameInstance>(GetGameInstance());
	return (GI && GI->Save) ? GI->Save->SfxVolume : 0.85f;
}

void UCmdAudioSubsystem::SetMusicVolume(float Value)
{
	if (UCmdGameInstance* GI = Cast<UCmdGameInstance>(GetGameInstance()))
	{
		if (GI->Save)
		{
			GI->Save->MusicVolume = FMath::Clamp(Value, 0.f, 1.f);
			GI->SaveProgress();
		}
	}
}

void UCmdAudioSubsystem::SetSfxVolume(float Value)
{
	if (UCmdGameInstance* GI = Cast<UCmdGameInstance>(GetGameInstance()))
	{
		if (GI->Save)
		{
			GI->Save->SfxVolume = FMath::Clamp(Value, 0.f, 1.f);
			GI->SaveProgress();
		}
	}
}

USoundBase* UCmdAudioSubsystem::GetMusicAsset(ECmdMusicMood Mood) const
{
	const TObjectPtr<USoundBase>* Asset = MusicOverrides.Find(FName(MoodName(Mood)));
	return Asset ? Asset->Get() : nullptr;
}

void UCmdAudioSubsystem::LoadOverrides()
{
	const TSharedPtr<FJsonObject> Root = UCmdDataRegistry::LoadJsonFile(TEXT("Audio.json"));
	if (!Root.IsValid())
	{
		return;
	}
	if (const TSharedPtr<FJsonObject> GainObj = CmdJson::Obj(Root, TEXT("gain")))
	{
		for (const auto& Pair : GainObj->Values)
		{
			if (Pair.Value.IsValid() && Pair.Value->Type == EJson::Number)
			{
				Gains.Add(FName(*Pair.Key), static_cast<float>(Pair.Value->AsNumber()));
			}
		}
	}

	auto LoadAssets = [](const TSharedPtr<FJsonObject>& Obj, TMap<FName, TObjectPtr<USoundBase>>& Out)
	{
		if (!Obj.IsValid())
		{
			return;
		}
		for (const auto& Pair : Obj->Values)
		{
			if (!Pair.Value.IsValid() || Pair.Value->Type != EJson::String || Pair.Value->AsString().IsEmpty())
			{
				continue;
			}
			if (USoundBase* Sound = LoadObject<USoundBase>(nullptr, *Pair.Value->AsString()))
			{
				Out.Add(FName(*Pair.Key), Sound);
			}
			else
			{
				UE_LOG(LogCommanders, Warning, TEXT("Audio.json: could not load '%s'"), *Pair.Value->AsString());
			}
		}
	};
	LoadAssets(CmdJson::Obj(Root, TEXT("sfx_assets")), AssetOverrides);
	LoadAssets(CmdJson::Obj(Root, TEXT("music_assets")), MusicOverrides);
}
