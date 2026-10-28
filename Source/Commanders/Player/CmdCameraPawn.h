#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "CmdCameraPawn.generated.h"

class USpringArmComponent;
class UCameraComponent;

/** Classic top-down RTS camera: pan, zoom, rotate, clamped to the battlefield. */
UCLASS()
class COMMANDERS_API ACmdCameraPawn : public APawn
{
	GENERATED_BODY()

public:
	ACmdCameraPawn();

	virtual void Tick(float DeltaSeconds) override;

	void Pan(const FVector2D& Direction, float DeltaSeconds);
	void Zoom(float Amount);
	void Rotate(float Degrees);
	void FocusOn(const FVector& Location, bool bInstant = false);
	void SetBounds(const FVector& Min, const FVector& Max);

	/** Slowly circles the battlefield (main menu). */
	void SetAttractMode(bool bEnabled, const FVector& Center);

	float GetYaw() const { return Yaw; }
	FVector GetFocus() const { return GetActorLocation(); }

private:
	void ApplyArm();

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USpringArmComponent> Arm;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCameraComponent> Camera;

	float ArmLength = 2600.f;
	float TargetArmLength = 2600.f;
	// Yaw -90 puts cell (0,0) at the top-left of the screen: +X right, +Y down,
	// so mission maps read like images.
	float Yaw = -90.f;
	float TargetYaw = -90.f;
	float Pitch = -58.f;
	FVector BoundsMin = FVector(-1e6f);
	FVector BoundsMax = FVector(1e6f);
	FVector MoveTarget = FVector::ZeroVector;
	bool bHasMoveTarget = false;
	bool bAttract = false;
	FVector AttractCenter = FVector::ZeroVector;
	float AttractAngle = 0.f;
};
