#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Core/CmdTypes.h"
#include "CmdPlayerController.generated.h"

class ACmdEntity;
class ACmdUnit;
class ACmdBuilding;
class ACmdCameraPawn;
class ACmdHUD;
class ACmdGameState;
class UStaticMeshComponent;
struct FCmdEntityDef;

UENUM()
enum class ECmdCursorMode : uint8
{
	Normal,
	Place,
	Sell,
	Repair,
	AttackMove
};

/**
 * RTS input: polled directly (no input assets needed).
 * Left mouse selects / box-selects, right mouse commands, sidebar & minimap via the HUD.
 */
UCLASS()
class COMMANDERS_API ACmdPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ACmdPlayerController();

	virtual void BeginPlay() override;
	virtual void PlayerTick(float DeltaTime) override;

	void OnSessionBuilt(const FVector& CameraStart, bool bIsMenu);
	void OnMissionEnded();
	void FocusCameraOn(const FVector& Location);

	// Selection ------------------------------------------------------------------
	TArray<ACmdEntity*> GetSelection() const;
	ACmdEntity* GetPrimarySelection() const;
	void ClearSelection();
	void SelectEntities(const TArray<ACmdEntity*>& Entities, bool bAdd);
	bool HasOwnUnitsSelected() const;

	// Modes ------------------------------------------------------------------------
	ECmdCursorMode GetCursorMode() const { return CursorMode; }
	void SetCursorMode(ECmdCursorMode Mode);
	void BeginPlacement(FName StructureId);
	FName GetPlacementId() const { return PlacementId; }
	FIntPoint GetPlacementOrigin() const { return PlacementOrigin; }
	bool IsPlacementValid() const { return bPlacementValid; }

	// Commands (also used by HUD buttons) ------------------------------------------
	void CommandStop();
	void CommandGuard();
	void CommandDeploy();

	bool IsDragging() const { return bDragging && bDragActive; }
	FVector2D GetDragStart() const { return DragStart; }
	FVector2D GetMousePos() const { return MousePos; }
	ACmdEntity* GetHoveredEntity() const { return Hovered.Get(); }
	ACmdCameraPawn* GetCameraPawn() const;
	ACmdGameState* GetCmdGameState() const;
	int32 GetLocalTeam() const;

	/** Ground point under the mouse cursor (Z=0 plane). */
	bool GetGroundUnderCursor(FVector& OutLocation) const;
	bool ScreenToGround(const FVector2D& Screen, FVector& OutLocation) const;
	ACmdEntity* PickEntityAt(const FVector2D& Screen) const;

private:
	void HandleCamera(float DeltaTime);
	void HandleGameplayInput();
	void HandleHotkeys();
	void HandleLeftPress();
	void HandleLeftRelease();
	void HandleRightClick();
	void UpdateHover();
	void UpdatePlacementGhost();
	void DestroyGhost();
	void BoxSelect(const FVector2D& A, const FVector2D& B, bool bAdd);
	void SelectSameTypeOnScreen(ACmdEntity* Like);
	void IssueMove(const FIntPoint& Cell, bool bAttackMove);
	void IssueTargetCommand(ACmdEntity* Target, bool bForce);
	void CleanSelection();
	ACmdHUD* GetCmdHUD() const;
	bool IsKeyDown(const FKey& Key) const { return IsInputKeyDown(Key); }
	bool Pressed(const FKey& Key) const { return WasInputKeyJustPressed(Key); }

	TArray<TWeakObjectPtr<ACmdEntity>> Selected;
	TArray<TWeakObjectPtr<ACmdEntity>> Groups[10];
	float LastGroupTime[10] = {};
	TWeakObjectPtr<ACmdEntity> Hovered;

	ECmdCursorMode CursorMode = ECmdCursorMode::Normal;
	FName PlacementId;
	FIntPoint PlacementOrigin = FIntPoint::ZeroValue;
	bool bPlacementValid = false;

	UPROPERTY()
	TObjectPtr<AActor> Ghost;
	TArray<UStaticMeshComponent*> GhostParts;
	bool bGhostValidColor = true;

	FVector2D MousePos = FVector2D::ZeroVector;
	FVector2D DragStart = FVector2D::ZeroVector;
	bool bDragging = false;
	bool bDragActive = false;
	float LastClickTime = -10.f;
	TWeakObjectPtr<ACmdEntity> LastClicked;
	bool bSessionBuilt = false;
	bool bMenu = false;
};
