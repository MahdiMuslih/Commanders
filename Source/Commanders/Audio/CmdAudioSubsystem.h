#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Audio/CmdSynth.h"
#include "Audio/CmdMusic.h"
#include "CmdAudioSubsystem.generated.h"

class ACmdAudioActor;
class USoundBase;
struct FCmdWeaponDef;

/**
 * Front door for all game audio.
 *  - Builds the procedural sound bank once per game instance.
 *  - Play2D / PlayAt: picks volume and stereo pan from the camera, rate-limits
 *    repeated sounds and feeds the mixer on the current level's audio actor.
 *  - Tracks combat "intensity" that drives the battle music.
 *  - Content/Data/Audio.json can override any sound or music mood with a real asset.
 */
UCLASS()
class COMMANDERS_API UCmdAudioSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	static UCmdAudioSubsystem* Get(const UObject* WorldContext);

	/** Convenience wrappers that tolerate a missing subsystem. */
	static void Play2D(const UObject* WorldContext, FName Sound, float Volume = 1.f);
	static void PlayAt(const UObject* WorldContext, FName Sound, const FVector& Location, float Volume = 1.f);
	static void PlayWeapon(const UObject* WorldContext, const FCmdWeaponDef& Weapon, const FVector& Location);

	void Play(UWorld* World, FName Sound, const FVector* Location, float Volume);

	/** Spawns the level's audio actor (music starts immediately). */
	ACmdAudioActor* EnsureForWorld(UWorld* World);

	// Settings (persisted in the save game) -------------------------------------
	float GetMusicVolume() const;
	float GetSfxVolume() const;
	void SetMusicVolume(float Value);
	void SetSfxVolume(float Value);
	bool IsMusicMuted() const { return bMusicMuted; }
	void ToggleMusicMute() { bMusicMuted = !bMusicMuted; }

	// Music ------------------------------------------------------------------------
	float GetCombatIntensity() const { return CombatIntensity; }
	void DecayCombat(float DeltaSeconds);
	USoundBase* GetMusicAsset(ECmdMusicMood Mood) const;

private:
	float GainFor(FName Sound) const;
	float MinIntervalFor(FName Sound) const;
	void LoadOverrides();

	TMap<FName, FCmdSoundBuffer> Bank;
	TMap<FName, float> Gains;
	TMap<FName, double> LastPlayed;

	UPROPERTY()
	TMap<FName, TObjectPtr<USoundBase>> AssetOverrides;

	UPROPERTY()
	TMap<FName, TObjectPtr<USoundBase>> MusicOverrides;

	TWeakObjectPtr<ACmdAudioActor> AudioActor;
	float CombatIntensity = 0.f;
	bool bMusicMuted = false;
};
