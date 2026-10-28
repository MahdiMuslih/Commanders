#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "Core/CmdTypes.h"
#include "Game/CmdGameInstance.h"
#include "CmdHUD.generated.h"

class UFont;
class UTexture2D;
class ACmdGameState;
class ACmdPlayerController;
class ACmdMissionDirector;
class ACmdEntity;
struct FCmdEntityDef;

/**
 * The whole user interface, drawn on the canvas: menus, briefings, the classic
 * right-hand sidebar with radar and build queues, in-world selection brackets and
 * health bars, mission objectives, radio dialogue, cinematic letterbox and results.
 * Canvas drawing keeps the project asset-free; every element scales with resolution.
 */
UCLASS()
class COMMANDERS_API ACmdHUD : public AHUD
{
	GENERATED_BODY()

public:
	ACmdHUD();

	virtual void BeginPlay() override;
	virtual void DrawHUD() override;

	/** Returns true if the click hit the UI (and was handled). */
	bool HandleClick(const FVector2D& Pos, bool bRight);
	void HandleEscape();
	void HandleConfirm();
	void HandleScroll(int32 Delta);

	bool IsModalOpen() const { return bPauseMenu; }
	void OpenPauseMenu();
	void ClosePauseMenu();
	void ToggleObjectives() { bShowObjectives = !bShowObjectives; }
	void ToggleHelp() { bShowHelp = !bShowHelp; }

private:
	struct FButton
	{
		FVector2D Pos;
		FVector2D Size;
		FName Action;
		FName Param;
		int32 Int = 0;
		bool bEnabled = true;
	};

	// Primitive helpers -----------------------------------------------------------
	float S() const { return UIScale; }
	void Panel(float X, float Y, float W, float H, const FLinearColor& Fill, const FLinearColor& Border, float BorderWidth = 1.f);
	void Text(const FString& Str, float X, float Y, const FLinearColor& Color, float Scale = 1.f, UFont* Font = nullptr, bool bCenter = false, bool bShadow = true);
	FVector2D Measure(const FString& Str, float Scale = 1.f, UFont* Font = nullptr);
	TArray<FString> Wrap(const FString& Str, float MaxWidth, float Scale, UFont* Font);
	float Paragraph(const FString& Str, float X, float Y, float MaxWidth, const FLinearColor& Color, float Scale, UFont* Font, float LineSpacing = 1.15f);
	bool Button(const FString& Label, float X, float Y, float W, float H, FName Action, FName Param = NAME_None, int32 Int = 0, bool bEnabled = true, bool bSelected = false, float TextScale = 1.f);
	void AddHitArea(float X, float Y, float W, float H, FName Action, FName Param = NAME_None, int32 Int = 0);
	bool IsMouseIn(float X, float Y, float W, float H) const;
	void Line(const FVector2D& A, const FVector2D& B, const FLinearColor& Color, float Thickness = 1.f);
	void Brackets(const FVector2D& Center, float HalfW, float HalfH, const FLinearColor& Color);
	void Bar(float X, float Y, float W, float H, float Pct, const FLinearColor& Color);
	FLinearColor HealthColor(float Pct) const;

	// Screens -------------------------------------------------------------------
	void DrawMainMenu();
	void DrawMenuBackdrop(const FString& Heading);
	void DrawCampaignSelect();
	void DrawMissionSelect();
	void DrawSkirmishSetup();
	void DrawHowToPlay();
	void DrawCredits();
	void DrawBriefing();
	void DrawResult();
	void DrawPauseMenu();
	void DrawAudioSettings(float X, float Y, float W);
	void DrawCinematic();

	// Gameplay -------------------------------------------------------------------
	void DrawGameplay();
	void DrawWorldOverlays();
	void DrawPlacementGrid();
	void DrawSidebar();
	void DrawMinimap(float X, float Y, float Size, bool bShowUnits, bool bShroud);
	void DrawBuildTooltip(const FCmdEntityDef& Def, float RightEdge, float Y);
	void DrawSelectionPanel(float X, float Y, float W);
	void DrawMessages();
	void DrawObjectives();
	void DrawTimer();
	void DrawDialogue();
	void DrawHelpOverlay();
	void UpdateMinimapTexture(bool bShroud);
	FVector2D MinimapToCell(const FVector2D& Pos) const;

	void Execute(const FButton& Button, bool bRight);

	ACmdGameState* GetGS() const;
	ACmdPlayerController* GetPC() const;
	ACmdMissionDirector* GetDirector() const;
	UCmdGameInstance* GetGI() const;

	TArray<FButton> Buttons;
	TArray<FButton> PendingButtons;
	float UIScale = 1.f;
	float Time = 0.f;
	FVector2D Mouse = FVector2D::ZeroVector;

	FName MenuPage = TEXT("main");
	FName SelectedCampaign;
	FCmdSkirmishSettings SkirmishDraft;
	int32 ActiveTab = 0;
	bool bPauseMenu = false;
	bool bShowObjectives = true;
	bool bShowHelp = false;
	bool bConfirmAbandon = false;
	float SidebarX = 0.f;
	float SidebarW = 0.f;
	FVector2D MinimapPos = FVector2D::ZeroVector;
	float MinimapSize = 0.f;
	const FCmdEntityDef* HoveredBuildDef = nullptr;
	float HoveredBuildY = 0.f;

	UPROPERTY()
	TObjectPtr<UTexture2D> MinimapTexture;
	TArray<FColor> MinimapPixels;
	uint32 MinimapRevision = 0;
	float MinimapTimer = 0.f;
	bool bMinimapShroud = true;
};
