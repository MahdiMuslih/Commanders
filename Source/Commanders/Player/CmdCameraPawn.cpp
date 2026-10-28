#include "Player/CmdCameraPawn.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Misc/App.h"

namespace
{
	constexpr float MinArm = 900.f;
	constexpr float MaxArm = 5200.f;
	constexpr float PanSpeed = 2600.f;
}

ACmdCameraPawn::ACmdCameraPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	Arm = CreateDefaultSubobject<USpringArmComponent>(TEXT("Arm"));
	Arm->SetupAttachment(Root);
	Arm->bDoCollisionTest = false;
	Arm->PrimaryComponentTick.bTickEvenWhenPaused = true;
	Arm->bEnableCameraLag = true;
	Arm->CameraLagSpeed = 12.f;
	Arm->bInheritPitch = false;
	Arm->bInheritYaw = false;
	Arm->bInheritRoll = false;
	Arm->TargetArmLength = ArmLength;
	Arm->SetRelativeRotation(FRotator(Pitch, Yaw, 0.f));

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Arm);
	Camera->SetFieldOfView(45.f);

	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	SetActorEnableCollision(false);
}

void ACmdCameraPawn::SetBounds(const FVector& Min, const FVector& Max)
{
	BoundsMin = Min;
	BoundsMax = Max;
}

void ACmdCameraPawn::ApplyArm()
{
	// Zooming out also tilts the camera a little further down, like most RTS cameras.
	const float ZoomAlpha = (ArmLength - MinArm) / (MaxArm - MinArm);
	Pitch = FMath::Lerp(-42.f, -64.f, ZoomAlpha);
	Arm->TargetArmLength = ArmLength;
	Arm->SetRelativeRotation(FRotator(Pitch, Yaw, 0.f));
}

void ACmdCameraPawn::Pan(const FVector2D& Direction, float DeltaSeconds)
{
	if (Direction.IsNearlyZero())
	{
		return;
	}
	bHasMoveTarget = false;
	bAttract = false;
	// Pan relative to the camera's yaw. Speed scales with zoom.
	const FRotator YawRot(0.f, Yaw, 0.f);
	const FVector Forward = YawRot.RotateVector(FVector::ForwardVector);
	const FVector Right = YawRot.RotateVector(FVector::RightVector);
	const float Speed = PanSpeed * (ArmLength / 2600.f);
	FVector L = GetActorLocation() + (Forward * Direction.Y + Right * Direction.X) * Speed * DeltaSeconds;
	L.X = FMath::Clamp(L.X, BoundsMin.X, BoundsMax.X);
	L.Y = FMath::Clamp(L.Y, BoundsMin.Y, BoundsMax.Y);
	L.Z = 0.f;
	SetActorLocation(L);
}

void ACmdCameraPawn::Zoom(float Amount)
{
	TargetArmLength = FMath::Clamp(TargetArmLength - Amount, MinArm, MaxArm);
}

void ACmdCameraPawn::Rotate(float Degrees)
{
	TargetYaw += Degrees;
}

void ACmdCameraPawn::FocusOn(const FVector& Location, bool bInstant)
{
	FVector L = Location;
	L.X = FMath::Clamp(L.X, BoundsMin.X, BoundsMax.X);
	L.Y = FMath::Clamp(L.Y, BoundsMin.Y, BoundsMax.Y);
	L.Z = 0.f;
	bAttract = false;
	if (bInstant)
	{
		SetActorLocation(L);
		bHasMoveTarget = false;
	}
	else
	{
		MoveTarget = L;
		bHasMoveTarget = true;
	}
}

void ACmdCameraPawn::SetAttractMode(bool bEnabled, const FVector& Center)
{
	bAttract = bEnabled;
	AttractCenter = Center;
	if (bEnabled)
	{
		TargetArmLength = 3800.f;
	}
}

void ACmdCameraPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// Use real time so the camera still moves while the game is paused (briefings).
	const float Dt = static_cast<float>(FMath::Min(FApp::GetDeltaTime(), 0.1));

	if (bAttract)
	{
		// Orbit the camera around the middle of the battle.
		AttractAngle += Dt * 3.f;
		SetActorLocation(FMath::VInterpTo(GetActorLocation(), AttractCenter, Dt, 0.5f));
		TargetYaw = AttractAngle;
		Yaw = TargetYaw;
	}
	else if (bHasMoveTarget)
	{
		const FVector L = FMath::VInterpTo(GetActorLocation(), MoveTarget, Dt, 6.f);
		SetActorLocation(L);
		if (FVector::Dist2D(L, MoveTarget) < 10.f)
		{
			bHasMoveTarget = false;
		}
	}

	ArmLength = FMath::FInterpTo(ArmLength, TargetArmLength, Dt, 8.f);
	Yaw = FMath::FInterpTo(Yaw, TargetYaw, Dt, 8.f);
	ApplyArm();
}
