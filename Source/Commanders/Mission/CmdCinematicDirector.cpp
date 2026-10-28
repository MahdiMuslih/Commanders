#include "Mission/CmdCinematicDirector.h"
#include "Mission/CmdMissionDirector.h"
#include "Game/CmdGameState.h"
#include "World/CmdTerrainActor.h"
#include "EngineUtils.h"
#include "Core/CmdJson.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

ACmdCinematicDirector::ACmdCinematicDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
}

FVector ACmdCinematicDirector::ParsePoint(const TSharedPtr<FJsonObject>& Json, const TCHAR* Key, float DefaultZ)
{
	// Points are authored as [cellX, cellY, heightUU].
	const TArray<double> N = CmdJson::Numbers(CmdJson::Field(Json, Key));
	if (N.Num() < 2)
	{
		return FVector(0.f, 0.f, DefaultZ);
	}
	return FVector(N[0] * CmdConst::CellSize, N[1] * CmdConst::CellSize, N.Num() >= 3 ? N[2] : DefaultZ);
}

void ACmdCinematicDirector::Play(const TSharedPtr<FJsonObject>& Json, TFunction<void()> OnFinished)
{
	if (bPlaying)
	{
		Finish();
	}
	Shots.Reset();
	Subtitles.Reset();
	Titles.Reset();
	Events.Reset();
	FinishedCallback = MoveTemp(OnFinished);
	Time = 0.f;
	bSkipping = false;

	float Cursor = 0.f;
	for (const TSharedPtr<FJsonValue>& V : CmdJson::Arr(Json, TEXT("shots")))
	{
		const TSharedPtr<FJsonObject> S = V->AsObject();
		FShot Shot;
		Shot.Start = Cursor;
		Shot.Duration = CmdJson::Num(S, TEXT("duration"), 4.0);
		Shot.bEase = CmdJson::Str(S, TEXT("ease"), TEXT("smooth")) != TEXT("linear");
		Shot.Shake = CmdJson::Num(S, TEXT("shake"), 0.0);
		Shot.FovFrom = CmdJson::Num(S, TEXT("fov"), 45.0);
		Shot.FovTo = CmdJson::Num(S, TEXT("fov_to"), Shot.FovFrom);

		const TSharedPtr<FJsonObject> Orbit = CmdJson::Obj(S, TEXT("orbit"));
		if (Orbit.IsValid())
		{
			Shot.bOrbit = true;
			Shot.OrbitCenter = ParsePoint(Orbit, TEXT("center"), 0.f);
			Shot.OrbitRadius = CmdJson::Num(Orbit, TEXT("radius"), 8.0) * CmdConst::CellSize;
			Shot.OrbitHeight = CmdJson::Num(Orbit, TEXT("height"), 900.0);
			Shot.AngleFrom = CmdJson::Num(Orbit, TEXT("from"), 0.0);
			Shot.AngleTo = CmdJson::Num(Orbit, TEXT("to"), 60.0);
		}
		else
		{
			Shot.From = ParsePoint(S, TEXT("from"), 800.f);
			Shot.To = CmdJson::Has(S, TEXT("to")) ? ParsePoint(S, TEXT("to"), Shot.From.Z) : Shot.From;
			Shot.LookFrom = ParsePoint(S, TEXT("look"), 0.f);
			Shot.LookTo = CmdJson::Has(S, TEXT("look_to")) ? ParsePoint(S, TEXT("look_to"), Shot.LookFrom.Z) : Shot.LookFrom;
		}
		Shots.Add(Shot);
		Cursor += Shot.Duration;

		// Shots may carry their own subtitle / title for convenience.
		if (CmdJson::Has(S, TEXT("text")))
		{
			FTimedText T;
			T.Start = Shot.Start + CmdJson::Num(S, TEXT("text_delay"), 0.3);
			T.Duration = Shot.Duration - 0.4f;
			T.A = CmdJson::Str(S, TEXT("speaker"));
			T.B = CmdJson::Str(S, TEXT("text"));
			Subtitles.Add(T);
		}
		if (CmdJson::Has(S, TEXT("title")))
		{
			FTimedText T;
			T.Start = Shot.Start + 0.4f;
			T.Duration = FMath::Min(Shot.Duration - 0.6f, 5.f);
			T.A = CmdJson::Str(S, TEXT("title"));
			T.B = CmdJson::Str(S, TEXT("title_sub"));
			Titles.Add(T);
		}
	}
	TotalDuration = FMath::Max(Cursor, 0.5f);

	for (const TSharedPtr<FJsonValue>& V : CmdJson::Arr(Json, TEXT("subtitles")))
	{
		const TSharedPtr<FJsonObject> S = V->AsObject();
		FTimedText T;
		T.Start = CmdJson::Num(S, TEXT("t"), 0.0);
		T.A = CmdJson::Str(S, TEXT("speaker"));
		T.B = CmdJson::Str(S, TEXT("text"));
		T.Duration = CmdJson::Num(S, TEXT("duration"), FMath::Clamp(T.B.Len() * 0.07f, 2.5f, 8.f));
		Subtitles.Add(T);
	}
	for (const TSharedPtr<FJsonValue>& V : CmdJson::Arr(Json, TEXT("titles")))
	{
		const TSharedPtr<FJsonObject> S = V->AsObject();
		FTimedText T;
		T.Start = CmdJson::Num(S, TEXT("t"), 0.0);
		T.Duration = CmdJson::Num(S, TEXT("duration"), 4.0);
		T.A = CmdJson::Str(S, TEXT("text"));
		T.B = CmdJson::Str(S, TEXT("sub"));
		Titles.Add(T);
	}
	for (const TSharedPtr<FJsonValue>& V : CmdJson::Arr(Json, TEXT("events")))
	{
		const TSharedPtr<FJsonObject> S = V->AsObject();
		FTimedEvent E;
		E.Time = CmdJson::Num(S, TEXT("t"), 0.0);
		E.Action = CmdJson::Obj(S, TEXT("action"));
		Events.Add(E);
	}
	Events.Sort([](const FTimedEvent& A, const FTimedEvent& B) { return A.Time < B.Time; });

	FadeIn = CmdJson::Num(Json, TEXT("fade_in"), 1.0);
	FadeOut = CmdJson::Num(Json, TEXT("fade_out"), 1.0);
	bUseLetterbox = CmdJson::Bool(Json, TEXT("letterbox"), true);
	Fade = FadeIn > 0.f ? 1.f : 0.f;

	if (!Camera)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Camera = GetWorld()->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity, Params);
		if (Camera)
		{
			Camera->GetCameraComponent()->bConstrainAspectRatio = false;
		}
	}
	// Cinematics may frame areas the player hasn't explored yet: lift the shroud while they play.
	if (CmdJson::Bool(Json, TEXT("reveal"), true))
	{
		if (ACmdGameState* GS = GetWorld()->GetGameState<ACmdGameState>())
		{
			if (!bLiftedShroud)
			{
				bPrevShroud = GS->bShroudEnabled;
			}
			GS->bShroudEnabled = false;
			bLiftedShroud = true;
			for (TActorIterator<ACmdTerrainActor> It(GetWorld()); It; ++It)
			{
				It->SetShroudVisible(false);
			}
		}
	}

	bPlaying = true;
	if (Shots.Num() > 0)
	{
		ApplyShot(Shots[0], 0.f);
	}
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		PC->SetViewTargetWithBlend(Camera, 0.f);
	}
}

void ACmdCinematicDirector::ApplyShot(const FShot& Shot, float LocalTime)
{
	if (!Camera)
	{
		return;
	}
	const float Raw = FMath::Clamp(LocalTime / FMath::Max(0.01f, Shot.Duration), 0.f, 1.f);
	const float Alpha = Shot.bEase ? FMath::InterpEaseInOut(0.f, 1.f, Raw, 2.f) : Raw;

	FVector Location;
	FVector Look;
	if (Shot.bOrbit)
	{
		const float Angle = FMath::DegreesToRadians(FMath::Lerp(Shot.AngleFrom, Shot.AngleTo, Alpha));
		Location = Shot.OrbitCenter + FVector(FMath::Cos(Angle) * Shot.OrbitRadius, FMath::Sin(Angle) * Shot.OrbitRadius, Shot.OrbitHeight);
		Look = Shot.OrbitCenter;
	}
	else
	{
		Location = FMath::Lerp(Shot.From, Shot.To, Alpha);
		Look = FMath::Lerp(Shot.LookFrom, Shot.LookTo, Alpha);
	}
	if (Shot.Shake > 0.f)
	{
		const float T = GetWorld()->GetTimeSeconds();
		Location += FVector(FMath::Sin(T * 37.f), FMath::Cos(T * 29.f), FMath::Sin(T * 41.f)) * Shot.Shake * 12.f;
	}
	const FVector Dir = Look - Location;
	Camera->SetActorLocationAndRotation(Location, Dir.IsNearlyZero() ? FRotator(-45.f, 0.f, 0.f) : Dir.Rotation());
	Camera->GetCameraComponent()->SetFieldOfView(FMath::Lerp(Shot.FovFrom, Shot.FovTo, Alpha));
}

void ACmdCinematicDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const float LetterTarget = (bPlaying && bUseLetterbox) ? 1.f : 0.f;
	Letterbox = FMath::FInterpConstantTo(Letterbox, LetterTarget, DeltaSeconds, 2.5f);

	if (!bPlaying)
	{
		Fade = FMath::FInterpConstantTo(Fade, 0.f, DeltaSeconds, 2.f);
		return;
	}

	Time += DeltaSeconds;

	// Timeline events (spawns, orders, explosions...).
	for (FTimedEvent& E : Events)
	{
		if (!E.bDone && Time >= E.Time)
		{
			E.bDone = true;
			if (Director)
			{
				Director->ExecuteAction(E.Action);
			}
		}
	}

	for (const FShot& Shot : Shots)
	{
		if (Time >= Shot.Start && Time < Shot.Start + Shot.Duration)
		{
			ApplyShot(Shot, Time - Shot.Start);
			break;
		}
	}

	// Fades.
	if (FadeIn > 0.f && Time < FadeIn)
	{
		Fade = 1.f - Time / FadeIn;
	}
	else if (FadeOut > 0.f && Time > TotalDuration - FadeOut)
	{
		Fade = FMath::Clamp((Time - (TotalDuration - FadeOut)) / FadeOut, 0.f, 1.f);
	}
	else
	{
		Fade = 0.f;
	}

	if (Time >= TotalDuration)
	{
		Finish();
	}
}

void ACmdCinematicDirector::Skip()
{
	if (!bPlaying)
	{
		return;
	}
	// Fire remaining events so the mission state matches the un-skipped outcome.
	for (FTimedEvent& E : Events)
	{
		if (!E.bDone)
		{
			E.bDone = true;
			if (Director)
			{
				Director->ExecuteAction(E.Action);
			}
		}
	}
	Finish();
}

void ACmdCinematicDirector::Finish()
{
	bPlaying = false;
	Fade = FadeOut > 0.f ? 1.f : 0.f;
	if (bLiftedShroud)
	{
		bLiftedShroud = false;
		if (ACmdGameState* GS = GetWorld()->GetGameState<ACmdGameState>())
		{
			GS->bShroudEnabled = bPrevShroud;
		}
		for (TActorIterator<ACmdTerrainActor> It(GetWorld()); It; ++It)
		{
			It->SetShroudVisible(bPrevShroud);
		}
	}
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (PC->GetPawn())
		{
			PC->SetViewTargetWithBlend(PC->GetPawn(), 0.f);
		}
	}
	TFunction<void()> Callback = MoveTemp(FinishedCallback);
	FinishedCallback = nullptr;
	if (Callback)
	{
		Callback();
	}
}

bool ACmdCinematicDirector::GetSubtitle(FString& OutSpeaker, FString& OutText, float& OutAlpha) const
{
	if (!bPlaying)
	{
		return false;
	}
	for (const FTimedText& T : Subtitles)
	{
		if (Time >= T.Start && Time < T.Start + T.Duration)
		{
			OutSpeaker = T.A;
			OutText = T.B;
			const float Local = Time - T.Start;
			OutAlpha = FMath::Min(FMath::Min(Local / 0.3f, (T.Duration - Local) / 0.3f), 1.f);
			return true;
		}
	}
	return false;
}

bool ACmdCinematicDirector::GetTitle(FString& OutTitle, FString& OutSub, float& OutAlpha) const
{
	if (!bPlaying)
	{
		return false;
	}
	for (const FTimedText& T : Titles)
	{
		if (Time >= T.Start && Time < T.Start + T.Duration)
		{
			OutTitle = T.A;
			OutSub = T.B;
			const float Local = Time - T.Start;
			OutAlpha = FMath::Min(FMath::Min(Local / 0.8f, (T.Duration - Local) / 0.8f), 1.f);
			return true;
		}
	}
	return false;
}
