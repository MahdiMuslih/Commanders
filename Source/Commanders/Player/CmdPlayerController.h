#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Core/CmdTypes.h"
#include "Game/CmdNetTypes.h"
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

	/** Every order goes through here: executed directly with authority, else sent to the server. */
	void SendCommand(const FCmdCommand& Command);

	/** The team this controller is allowed to command (server: its seat; client/SP: the local team). */
	int32 GetCommandTeam() const;

	/** Networked match: our side has been eliminated and we are watching the rest. */
	bool IsSpectating() const;

	// Networking --------------------------------------------------------------------
	UFUNCTION(Server, Reliable)
	void ServerCommand(const FCmdCommand& Command);

	UFUNCTION(Client, Reliable)
	void ClientShowMessage(const FString& Text, FLinearColor Color, FName Sound);

	/** Lobby: faction, side and ready flag for this player. */
	UFUNCTION(Server, Reliable)
	void ServerSetLobbyChoice(FName Faction, int32 Alliance, bool bReady);

	UFUNCTION(Server, Reliable)
	void ServerChat(const FString& Text);

	// Text entry (player name, host address, chat) -----------------------------------
	void BeginTextEntry(FName Field, const FString& Initial, int32 MaxLength);
	void EndTextEntry(bool bCommit);
	bool IsTextEntryActive() const { return !TextField.IsNone(); }
	FName GetTextField() const { return TextField; }
	const FString& GetTextBuffer() const { return TextBuffer; }

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
	FCmdCommand MakeUnitCommand(ECmdCommandType Type) const;
	void HandleTextEntry();
	void IssueTargetCommand(ACmdEntity* Target, bool bForce);
	void CleanSelection();
	void PlaySelectionSound();
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

	FName TextField;
	FString TextBuffer;
	int32 TextMaxLength = 32;
};
