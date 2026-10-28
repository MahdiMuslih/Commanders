#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CmdCinematicDirector.generated.h"

class FJsonObject;
class FJsonValue;
class ACameraActor;
class ACmdMissionDirector;

/**
 * Real-time, in-engine cinematics described in JSON.
 *
 * A cinematic is a list of camera shots (dolly / orbit / static) plus timelines of
 * subtitles, title cards and gameplay events (spawn units, order attacks, explosions...).
 * Because events run through the mission action system, cutscenes are staged with
 * the same live units the player then fights with.
 */
UCLASS()
class COMMANDERS_API ACmdCinematicDirector : public AActor
{
	GENERATED_BODY()

public:
	ACmdCinematicDirector();

	virtual void Tick(float DeltaSeconds) override;

	void Play(const TSharedPtr<FJsonObject>& Cinematic, TFunction<void()> OnFinished);
	void Skip();
	bool IsPlaying() const { return bPlaying; }

	// HUD queries
	float GetLetterbox() const { return Letterbox; }
	float GetFade() const { return Fade; }
	bool GetSubtitle(FString& OutSpeaker, FString& OutText, float& OutAlpha) const;
	bool GetTitle(FString& OutTitle, FString& OutSub, float& OutAlpha) const;
	float GetTime() const { return Time; }
	bool CanSkip() const { return bPlaying && Time > 0.5f; }

	UPROPERTY()
	TObjectPtr<ACmdMissionDirector> Director;

private:
	struct FShot
	{
		float Start = 0.f;
		float Duration = 4.f;
		FVector From = FVector::ZeroVector;
		FVector To = FVector::ZeroVector;
		FVector LookFrom = FVector::ZeroVector;
		FVector LookTo = FVector::ZeroVector;
		float FovFrom = 50.f;
		float FovTo = 50.f;
		bool bOrbit = false;
		FVector OrbitCenter = FVector::ZeroVector;
		float OrbitRadius = 1000.f;
		float OrbitHeight = 800.f;
		float AngleFrom = 0.f;
		float AngleTo = 90.f;
		bool bEase = true;
		float Shake = 0.f;
	};

	struct FTimedText
	{
		float Start = 0.f;
		float Duration = 3.f;
		FString A;
		FString B;
	};

	struct FTimedEvent
	{
		float Time = 0.f;
		TSharedPtr<FJsonObject> Action;
		bool bDone = false;
	};

	void Finish();
	void ApplyShot(const FShot& Shot, float LocalTime);
	static FVector ParsePoint(const TSharedPtr<FJsonObject>& Json, const TCHAR* Key, float DefaultZ);

	UPROPERTY()
	TObjectPtr<ACameraActor> Camera;

	TArray<FShot> Shots;
	TArray<FTimedText> Subtitles;
	TArray<FTimedText> Titles;
	TArray<FTimedEvent> Events;
	TFunction<void()> FinishedCallback;
	float Time = 0.f;
	float TotalDuration = 0.f;
	float FadeIn = 0.f;
	float FadeOut = 0.f;
	float Letterbox = 0.f;
	float Fade = 0.f;
	bool bUseLetterbox = true;
	bool bLiftedShroud = false;
	bool bPrevShroud = true;
	bool bPlaying = false;
	bool bSkipping = false;
};
