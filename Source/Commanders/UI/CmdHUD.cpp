#include "UI/CmdHUD.h"
#include "Game/CmdGameState.h"
#include "Game/CmdGameInstance.h"
#include "Game/CmdGameMode.h"
#include "Game/CmdSaveGame.h"
#include "Player/CmdPlayerController.h"
#include "Player/CmdCameraPawn.h"
#include "Mission/CmdMissionDirector.h"
#include "Mission/CmdCinematicDirector.h"
#include "Gameplay/CmdEntity.h"
#include "Gameplay/CmdUnit.h"
#include "Gameplay/CmdBuilding.h"
#include "World/CmdGridSubsystem.h"
#include "World/CmdTerrainActor.h"
#include "Data/CmdDataRegistry.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/KismetSystemLibrary.h"
#include "TextureResource.h"

namespace
{
	const FLinearColor ColPanel(0.015f, 0.02f, 0.025f, 0.88f);
	const FLinearColor ColPanelLight(0.05f, 0.06f, 0.07f, 0.92f);
	const FLinearColor ColBorder(0.55f, 0.42f, 0.14f, 1.f);
	const FLinearColor ColAccent(0.95f, 0.72f, 0.2f, 1.f);
	const FLinearColor ColText(0.92f, 0.9f, 0.85f, 1.f);
	const FLinearColor ColDim(0.55f, 0.56f, 0.56f, 1.f);
	const FLinearColor ColGood(0.35f, 0.95f, 0.4f, 1.f);
	const FLinearColor ColBad(1.f, 0.3f, 0.25f, 1.f);

	UFont* LargeFont() { return GEngine ? GEngine->GetLargeFont() : nullptr; }
	UFont* MediumFont() { return GEngine ? GEngine->GetMediumFont() : nullptr; }
	UFont* SmallFont() { return GEngine ? GEngine->GetSmallFont() : nullptr; }

	FString FormatTime(float Seconds)
	{
		const int32 Total = FMath::Max(0, FMath::FloorToInt(Seconds));
		return FString::Printf(TEXT("%02d:%02d"), Total / 60, Total % 60);
	}

	FString FormatCredits(float Credits)
	{
		const int32 V = FMath::FloorToInt(Credits);
		FString Digits = FString::FromInt(V);
		FString Out;
		for (int32 i = 0; i < Digits.Len(); ++i)
		{
			if (i > 0 && (Digits.Len() - i) % 3 == 0)
			{
				Out.AppendChar(TEXT(','));
			}
			Out.AppendChar(Digits[i]);
		}
		return Out;
	}

	struct FKeyValue
	{
		FString Key;
		FString Value;
	};

	FString Initials(const FString& Name)
	{
		TArray<FString> Words;
		Name.ParseIntoArrayWS(Words);
		FString Out;
		for (const FString& W : Words)
		{
			if (W.Len() > 0 && FChar::IsAlpha(W[0]))
			{
				Out.AppendChar(FChar::ToUpper(W[0]));
			}
			if (Out.Len() >= 2)
			{
				break;
			}
		}
		return Out.IsEmpty() ? TEXT("?") : Out;
	}
}

ACmdHUD::ACmdHUD()
{
	PrimaryActorTick.bTickEvenWhenPaused = true;
}

void ACmdHUD::BeginPlay()
{
	Super::BeginPlay();
	if (UCmdGameInstance* GI = GetGI())
	{
		if (!GI->PendingMenuPage.IsNone())
		{
			MenuPage = GI->PendingMenuPage;
			GI->PendingMenuPage = NAME_None;
		}
		SkirmishDraft = GI->Skirmish;
		if (!GI->CurrentMission.IsNone())
		{
			if (const UCmdDataRegistry* Data = UCmdDataRegistry::Get(this))
			{
				if (const FCmdMissionInfo* M = Data->GetMission(GI->CurrentMission))
				{
					SelectedCampaign = M->Campaign;
				}
			}
		}
	}
}

ACmdGameState* ACmdHUD::GetGS() const
{
	return GetWorld() ? GetWorld()->GetGameState<ACmdGameState>() : nullptr;
}

ACmdPlayerController* ACmdHUD::GetPC() const
{
	return Cast<ACmdPlayerController>(PlayerOwner);
}

ACmdMissionDirector* ACmdHUD::GetDirector() const
{
	const ACmdGameState* GS = GetGS();
	return GS ? GS->MissionDirector.Get() : nullptr;
}

UCmdGameInstance* ACmdHUD::GetGI() const
{
	return Cast<UCmdGameInstance>(GetGameInstance());
}

// ===========================================================================
// Primitive helpers
// ===========================================================================

void ACmdHUD::Panel(float X, float Y, float W, float H, const FLinearColor& Fill, const FLinearColor& Border, float BorderWidth)
{
	DrawRect(Fill, X, Y, W, H);
	if (Border.A > 0.f && BorderWidth > 0.f)
	{
		DrawRect(Border, X, Y, W, BorderWidth);
		DrawRect(Border, X, Y + H - BorderWidth, W, BorderWidth);
		DrawRect(Border, X, Y, BorderWidth, H);
		DrawRect(Border, X + W - BorderWidth, Y, BorderWidth, H);
	}
}

void ACmdHUD::Text(const FString& Str, float X, float Y, const FLinearColor& Color, float Scale, UFont* Font, bool bCenter, bool bShadow)
{
	if (Str.IsEmpty())
	{
		return;
	}
	UFont* F = Font ? Font : MediumFont();
	const float Sc = Scale * S();
	float DrawX = X;
	if (bCenter)
	{
		float W = 0.f;
		float H = 0.f;
		GetTextSize(Str, W, H, F, Sc);
		DrawX = X - W * 0.5f;
	}
	if (bShadow)
	{
		DrawText(Str, FLinearColor(0.f, 0.f, 0.f, Color.A * 0.8f), DrawX + 1.5f * S(), Y + 1.5f * S(), F, Sc);
	}
	DrawText(Str, Color, DrawX, Y, F, Sc);
}

FVector2D ACmdHUD::Measure(const FString& Str, float Scale, UFont* Font)
{
	float W = 0.f;
	float H = 0.f;
	GetTextSize(Str, W, H, Font ? Font : MediumFont(), Scale * S());
	return FVector2D(W, H);
}

TArray<FString> ACmdHUD::Wrap(const FString& Str, float MaxWidth, float Scale, UFont* Font)
{
	TArray<FString> Lines;
	TArray<FString> Paragraphs;
	Str.ParseIntoArray(Paragraphs, TEXT("\n"), false);
	for (const FString& Para : Paragraphs)
	{
		TArray<FString> Words;
		Para.ParseIntoArrayWS(Words);
		FString Current;
		for (const FString& Word : Words)
		{
			const FString Candidate = Current.IsEmpty() ? Word : Current + TEXT(" ") + Word;
			if (Measure(Candidate, Scale, Font).X > MaxWidth && !Current.IsEmpty())
			{
				Lines.Add(Current);
				Current = Word;
			}
			else
			{
				Current = Candidate;
			}
		}
		Lines.Add(Current);
	}
	return Lines;
}

float ACmdHUD::Paragraph(const FString& Str, float X, float Y, float MaxWidth, const FLinearColor& Color, float Scale, UFont* Font, float LineSpacing)
{
	UFont* F = Font ? Font : MediumFont();
	const float LineH = Measure(TEXT("Ag"), Scale, F).Y * LineSpacing;
	for (const FString& L : Wrap(Str, MaxWidth, Scale, F))
	{
		Text(L, X, Y, Color, Scale, F, false, true);
		Y += LineH;
	}
	return Y;
}

bool ACmdHUD::IsMouseIn(float X, float Y, float W, float H) const
{
	return Mouse.X >= X && Mouse.X <= X + W && Mouse.Y >= Y && Mouse.Y <= Y + H;
}

void ACmdHUD::AddHitArea(float X, float Y, float W, float H, FName Action, FName Param, int32 Int)
{
	FButton B;
	B.Pos = FVector2D(X, Y);
	B.Size = FVector2D(W, H);
	B.Action = Action;
	B.Param = Param;
	B.Int = Int;
	PendingButtons.Add(B);
}

bool ACmdHUD::Button(const FString& Label, float X, float Y, float W, float H, FName Action, FName Param, int32 Int, bool bEnabled, bool bSelected, float TextScale)
{
	const bool bHover = bEnabled && IsMouseIn(X, Y, W, H);
	FLinearColor Fill = bEnabled ? FLinearColor(0.06f, 0.07f, 0.08f, 0.92f) : FLinearColor(0.03f, 0.03f, 0.03f, 0.8f);
	if (bSelected) Fill = FLinearColor(0.32f, 0.22f, 0.05f, 0.95f);
	if (bHover) Fill = FLinearColor(0.45f, 0.32f, 0.08f, 0.95f);
	const FLinearColor Border = bEnabled ? (bHover || bSelected ? ColAccent : ColBorder) : FLinearColor(0.2f, 0.2f, 0.2f, 1.f);
	Panel(X, Y, W, H, Fill, Border, FMath::Max(1.f, 1.5f * S()));
	const FVector2D Size = Measure(Label, TextScale, MediumFont());
	Text(Label, X + W * 0.5f, Y + (H - Size.Y) * 0.5f, bEnabled ? ColText : ColDim, TextScale, MediumFont(), true);

	FButton B;
	B.Pos = FVector2D(X, Y);
	B.Size = FVector2D(W, H);
	B.Action = Action;
	B.Param = Param;
	B.Int = Int;
	B.bEnabled = bEnabled;
	PendingButtons.Add(B);
	return bHover;
}

void ACmdHUD::Line(const FVector2D& A, const FVector2D& B, const FLinearColor& Color, float Thickness)
{
	DrawLine(A.X, A.Y, B.X, B.Y, Color, Thickness);
}

void ACmdHUD::Brackets(const FVector2D& C, float HalfW, float HalfH, const FLinearColor& Color)
{
	const float L = FMath::Max(4.f, FMath::Min(HalfW, HalfH) * 0.45f);
	const float T = FMath::Max(1.f, 1.5f * S());
	const FVector2D TL(C.X - HalfW, C.Y - HalfH);
	const FVector2D TR(C.X + HalfW, C.Y - HalfH);
	const FVector2D BL(C.X - HalfW, C.Y + HalfH);
	const FVector2D BR(C.X + HalfW, C.Y + HalfH);
	Line(TL, TL + FVector2D(L, 0), Color, T); Line(TL, TL + FVector2D(0, L), Color, T);
	Line(TR, TR - FVector2D(L, 0), Color, T); Line(TR, TR + FVector2D(0, L), Color, T);
	Line(BL, BL + FVector2D(L, 0), Color, T); Line(BL, BL - FVector2D(0, L), Color, T);
	Line(BR, BR - FVector2D(L, 0), Color, T); Line(BR, BR - FVector2D(0, L), Color, T);
}

void ACmdHUD::Bar(float X, float Y, float W, float H, float Pct, const FLinearColor& Color)
{
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.8f), X - 1.f, Y - 1.f, W + 2.f, H + 2.f);
	DrawRect(Color, X, Y, W * FMath::Clamp(Pct, 0.f, 1.f), H);
}

FLinearColor ACmdHUD::HealthColor(float Pct) const
{
	if (Pct > 0.6f) return FLinearColor(0.2f, 0.9f, 0.25f);
	if (Pct > 0.3f) return FLinearColor(0.95f, 0.8f, 0.1f);
	return FLinearColor(0.95f, 0.15f, 0.1f);
}

// ===========================================================================
// Frame
// ===========================================================================

void ACmdHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas)
	{
		return;
	}
	UIScale = FMath::Max(0.6f, Canvas->ClipY / 1080.f);
	Time = GetWorld()->GetRealTimeSeconds();
	if (const ACmdPlayerController* PC = GetPC())
	{
		Mouse = PC->GetMousePos();
	}
	PendingButtons.Reset();
	HoveredBuildDef = nullptr;

	const ACmdGameState* GS = GetGS();
	if (!GS)
	{
		return;
	}

	switch (GS->Flow)
	{
	case ECmdFlowState::MainMenu:
		DrawMainMenu();
		break;
	case ECmdFlowState::Briefing:
		DrawBriefing();
		break;
	case ECmdFlowState::Cinematic:
		DrawCinematic();
		break;
	case ECmdFlowState::Playing:
		DrawGameplay();
		if (bPauseMenu)
		{
			DrawPauseMenu();
		}
		break;
	case ECmdFlowState::Victory:
	case ECmdFlowState::Defeat:
		DrawResult();
		break;
	default:
		break;
	}

	// Fade back in after a cinematic ends.
	if (GS->Flow != ECmdFlowState::Cinematic)
	{
		if (const ACmdMissionDirector* Dir = GetDirector())
		{
			if (const ACmdCinematicDirector* Cine = Dir->GetCinematicDirector())
			{
				if (Cine->GetFade() > 0.01f)
				{
					DrawRect(FLinearColor(0.f, 0.f, 0.f, Cine->GetFade()), 0.f, 0.f, Canvas->ClipX, Canvas->ClipY);
				}
			}
		}
	}

	Buttons = PendingButtons;
}

// ===========================================================================
// Input from the player controller
// ===========================================================================

bool ACmdHUD::HandleClick(const FVector2D& Pos, bool bRight)
{
	Mouse = Pos;
	for (int32 i = Buttons.Num() - 1; i >= 0; --i)
	{
		const FButton& B = Buttons[i];
		if (Pos.X >= B.Pos.X && Pos.X <= B.Pos.X + B.Size.X && Pos.Y >= B.Pos.Y && Pos.Y <= B.Pos.Y + B.Size.Y)
		{
			if (B.bEnabled)
			{
				const FButton Copy = B;	// Execute may rebuild state
				Execute(Copy, bRight);
			}
			return true;
		}
	}

	const ACmdGameState* GS = GetGS();
	if (GS && GS->Flow == ECmdFlowState::Playing && !bPauseMenu)
	{
		// Minimap: left jumps the camera, right orders selected units there.
		if (MinimapSize > 0.f && Pos.X >= MinimapPos.X && Pos.Y >= MinimapPos.Y && Pos.X <= MinimapPos.X + MinimapSize && Pos.Y <= MinimapPos.Y + MinimapSize && GS->HasRadar(GS->LocalTeamId))
		{
			const FVector2D Cell = MinimapToCell(Pos);
			ACmdPlayerController* PC = GetPC();
			if (PC && Cell.X >= 0.f)
			{
				const FVector World(Cell.X * CmdConst::CellSize, Cell.Y * CmdConst::CellSize, 0.f);
				if (bRight)
				{
					TArray<ACmdEntity*> Sel = PC->GetSelection();
					TSet<FIntPoint> Used;
					const UCmdGridSubsystem* Grid = GetWorld()->GetSubsystem<UCmdGridSubsystem>();
					for (ACmdEntity* E : Sel)
					{
						ACmdUnit* U = Cast<ACmdUnit>(E);
						if (U && U->TeamId == GS->LocalTeamId && Grid)
						{
							const FIntPoint C = Grid->FindNearestPassable(Cmd::WorldToCell(World), 12, &Used);
							Used.Add(C);
							U->OrderMove(C, false);
						}
					}
				}
				else
				{
					PC->FocusCameraOn(World);
				}
			}
			return true;
		}
		// Anything else on the sidebar is swallowed.
		if (Pos.X >= SidebarX)
		{
			return true;
		}
	}
	return false;
}

void ACmdHUD::HandleEscape()
{
	const ACmdGameState* GS = GetGS();
	if (!GS)
	{
		return;
	}
	if (bPauseMenu)
	{
		ClosePauseMenu();
		return;
	}
	if (GS->Flow == ECmdFlowState::MainMenu)
	{
		if (MenuPage == TEXT("missions")) MenuPage = TEXT("campaign");
		else MenuPage = TEXT("main");
	}
}

void ACmdHUD::HandleConfirm()
{
	const ACmdGameState* GS = GetGS();
	if (GS && GS->Flow == ECmdFlowState::Briefing)
	{
		if (ACmdMissionDirector* Dir = GetDirector())
		{
			Dir->ConfirmBriefing();
		}
	}
}

void ACmdHUD::HandleScroll(int32 Delta)
{
}

void ACmdHUD::OpenPauseMenu()
{
	bPauseMenu = true;
	bConfirmAbandon = false;
	if (APlayerController* PC = GetPC())
	{
		PC->SetPause(true);
	}
}

void ACmdHUD::ClosePauseMenu()
{
	bPauseMenu = false;
	if (APlayerController* PC = GetPC())
	{
		PC->SetPause(false);
	}
}

void ACmdHUD::Execute(const FButton& B, bool bRight)
{
	UCmdGameInstance* GI = GetGI();
	ACmdGameState* GS = GetGS();
	ACmdPlayerController* PC = GetPC();
	const UCmdDataRegistry* Data = UCmdDataRegistry::Get(this);
	const FName A = B.Action;

	// --- Menus ------------------------------------------------------------
	if (A == TEXT("page"))
	{
		MenuPage = B.Param;
	}
	else if (A == TEXT("campaign"))
	{
		SelectedCampaign = B.Param;
		MenuPage = TEXT("missions");
	}
	else if (A == TEXT("mission") && GI)
	{
		GI->StartMission(B.Param);
	}
	else if (A == TEXT("unlock_all") && GI)
	{
		GI->UnlockAll();
	}
	else if (A == TEXT("reset_progress") && GI)
	{
		GI->ResetProgress();
	}
	else if (A == TEXT("sk_faction"))
	{
		SkirmishDraft.PlayerFaction = SkirmishDraft.PlayerFaction == TEXT("concord") ? FName(TEXT("pact")) : FName(TEXT("concord"));
	}
	else if (A == TEXT("sk_enemies"))
	{
		SkirmishDraft.NumEnemies = 1 + (SkirmishDraft.NumEnemies - 1 + B.Int + 3) % 3;
	}
	else if (A == TEXT("sk_diff"))
	{
		SkirmishDraft.Difficulty = (SkirmishDraft.Difficulty + B.Int + 3) % 3;
	}
	else if (A == TEXT("sk_size"))
	{
		static const int32 Sizes[3] = { 64, 80, 100 };
		int32 Index = SkirmishDraft.MapSize <= 64 ? 0 : (SkirmishDraft.MapSize <= 80 ? 1 : 2);
		Index = (Index + B.Int + 3) % 3;
		SkirmishDraft.MapSize = Sizes[Index];
	}
	else if (A == TEXT("sk_credits"))
	{
		static const int32 Credits[4] = { 2500, 5000, 10000, 20000 };
		int32 Index = 0;
		for (int32 i = 0; i < 4; ++i) { if (SkirmishDraft.StartingCredits == Credits[i]) Index = i; }
		Index = (Index + B.Int + 4) % 4;
		SkirmishDraft.StartingCredits = Credits[Index];
	}
	else if (A == TEXT("sk_theme"))
	{
		SkirmishDraft.Theme = (SkirmishDraft.Theme + B.Int + 4) % 4;
	}
	else if (A == TEXT("sk_start") && GI)
	{
		SkirmishDraft.Seed = 0;
		GI->StartSkirmish(SkirmishDraft);
	}
	else if (A == TEXT("quit"))
	{
		UKismetSystemLibrary::QuitGame(this, PC, EQuitPreference::Quit, false);
	}
	// --- Briefing / pause / results ---------------------------------------
	else if (A == TEXT("brief_start"))
	{
		if (ACmdMissionDirector* Dir = GetDirector())
		{
			Dir->ConfirmBriefing();
		}
	}
	else if (A == TEXT("to_menu") && GI)
	{
		if (PC) PC->SetPause(false);
		GI->ReturnToMenu(GI->Mode == ECmdSessionMode::Campaign ? FName(TEXT("missions")) : FName(TEXT("main")));
	}
	else if (A == TEXT("resume"))
	{
		ClosePauseMenu();
	}
	else if (A == TEXT("abandon"))
	{
		bConfirmAbandon = true;
	}
	else if (A == TEXT("restart") && GI)
	{
		if (PC) PC->SetPause(false);
		GI->RestartSession();
	}
	else if (A == TEXT("next_mission") && GI)
	{
		GI->StartMission(B.Param);
	}
	else if (A == TEXT("help"))
	{
		bShowHelp = !bShowHelp;
	}
	// --- Sidebar ------------------------------------------------------------
	else if (A == TEXT("tab"))
	{
		ActiveTab = B.Int;
	}
	else if (A == TEXT("build") && GS && Data && PC)
	{
		const int32 Team = GS->LocalTeamId;
		const FCmdEntityDef* Def = Data->GetEntity(B.Param);
		const FCmdTeam* T = GS->GetTeam(Team);
		if (!Def || !T)
		{
			return;
		}
		const FCmdProductionQueue& Q = T->Queues[static_cast<int32>(Def->Category)];
		if (bRight)
		{
			GS->CancelProduction(Team, Def->Id);
			return;
		}
		if (Q.Items.Num() > 0 && Q.Items[0] == Def->Id && Q.bOnHold)
		{
			GS->ToggleHold(Team, Def->Category);
			return;
		}
		if (Def->bIsBuilding)
		{
			if (GS->GetReadyStructure(Team) == Def->Id)
			{
				PC->BeginPlacement(Def->Id);
			}
			else
			{
				GS->QueueProduction(Team, Def->Id);
			}
		}
		else
		{
			const bool bShift = PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::RightShift);
			for (int32 i = 0; i < (bShift ? 5 : 1); ++i)
			{
				GS->QueueProduction(Team, Def->Id, i == 0);
			}
		}
	}
	else if (A == TEXT("sell") && PC)
	{
		PC->SetCursorMode(PC->GetCursorMode() == ECmdCursorMode::Sell ? ECmdCursorMode::Normal : ECmdCursorMode::Sell);
	}
	else if (A == TEXT("repair") && PC)
	{
		PC->SetCursorMode(PC->GetCursorMode() == ECmdCursorMode::Repair ? ECmdCursorMode::Normal : ECmdCursorMode::Repair);
	}
	else if (A == TEXT("deploy") && PC)
	{
		PC->CommandDeploy();
	}
	else if (A == TEXT("stop") && PC)
	{
		PC->CommandStop();
	}
	else if (A == TEXT("guard") && PC)
	{
		PC->CommandGuard();
	}
	else if (A == TEXT("attackmove") && PC)
	{
		PC->SetCursorMode(ECmdCursorMode::AttackMove);
	}
	else if (A == TEXT("objectives"))
	{
		ToggleObjectives();
	}
	else if (A == TEXT("pause"))
	{
		OpenPauseMenu();
	}
}

// ===========================================================================
// Main menu
// ===========================================================================

void ACmdHUD::DrawMenuBackdrop(const FString& Heading)
{
	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;
	// Vignette over the live battle.
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.35f), 0.f, 0.f, W, H);
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f), 0.f, 0.f, W, 120.f * S());
	DrawRect(ColBorder, 0.f, 120.f * S(), W, 2.f * S());
	Text(TEXT("COMMANDERS"), 60.f * S(), 28.f * S(), ColAccent, 2.0f, LargeFont());
	Text(Heading, W - 60.f * S() - Measure(Heading, 1.2f, LargeFont()).X, 44.f * S(), ColText, 1.2f, LargeFont());
}

void ACmdHUD::DrawMainMenu()
{
	if (MenuPage == TEXT("campaign")) { DrawCampaignSelect(); return; }
	if (MenuPage == TEXT("missions")) { DrawMissionSelect(); return; }
	if (MenuPage == TEXT("skirmish")) { DrawSkirmishSetup(); return; }
	if (MenuPage == TEXT("howto")) { DrawHowToPlay(); return; }
	if (MenuPage == TEXT("credits")) { DrawCredits(); return; }

	const float H = Canvas->ClipY;
	const float PanelW = 600.f * S();
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.72f), 0.f, 0.f, PanelW, H);
	DrawRect(ColBorder, PanelW, 0.f, 2.f * S(), H);

	const float X = 70.f * S();
	float Y = 120.f * S();
	Text(TEXT("COMMANDERS"), X, Y, ColAccent, 3.2f, LargeFont());
	Y += 120.f * S();
	Text(TEXT("THE IRON WINTER"), X + 4.f * S(), Y, ColText, 1.3f, MediumFont());
	Y += 34.f * S();
	Text(TEXT("A story of two armies and one terrible machine."), X + 4.f * S(), Y, ColDim, 0.9f, SmallFont());
	Y += 80.f * S();

	const float BW = 400.f * S();
	const float BH = 58.f * S();
	const float Gap = 16.f * S();
	Button(TEXT("CAMPAIGN"), X, Y, BW, BH, TEXT("page"), TEXT("campaign"), 0, true, false, 1.2f); Y += BH + Gap;
	Button(TEXT("SKIRMISH"), X, Y, BW, BH, TEXT("page"), TEXT("skirmish"), 0, true, false, 1.2f); Y += BH + Gap;
	Button(TEXT("HOW TO PLAY"), X, Y, BW, BH, TEXT("page"), TEXT("howto"), 0, true, false, 1.2f); Y += BH + Gap;
	Button(TEXT("CREDITS"), X, Y, BW, BH, TEXT("page"), TEXT("credits"), 0, true, false, 1.2f); Y += BH + Gap;
	Button(TEXT("QUIT"), X, Y, BW, BH, TEXT("quit"), NAME_None, 0, true, false, 1.2f);

	if (const UCmdGameInstance* GI = GetGI())
	{
		if (GI->Save)
		{
			const FString Record = FString::Printf(TEXT("Campaign missions completed: %d     Skirmish record: %d W / %d L"),
				GI->Save->CompletedMissions.Num(), GI->Save->SkirmishWins, GI->Save->SkirmishLosses);
			Text(Record, X, H - 80.f * S(), ColDim, 0.8f, SmallFont());
		}
	}
	Text(TEXT("v1.0  |  Built entirely in Unreal Engine C++ - no imported assets"), X, H - 50.f * S(), ColDim, 0.75f, SmallFont());
}

void ACmdHUD::DrawCampaignSelect()
{
	DrawMenuBackdrop(TEXT("SELECT CAMPAIGN"));
	const UCmdDataRegistry* Data = UCmdDataRegistry::Get(this);
	const UCmdGameInstance* GI = GetGI();
	if (!Data || !GI)
	{
		return;
	}
	const TArray<FCmdCampaignDef>& Campaigns = Data->GetCampaigns();
	const float CardW = 620.f * S();
	const float CardH = 600.f * S();
	const float Gap = 60.f * S();
	const float TotalW = Campaigns.Num() * CardW + FMath::Max(0, Campaigns.Num() - 1) * Gap;
	float X = (Canvas->ClipX - TotalW) * 0.5f;
	const float Y = 190.f * S();

	for (const FCmdCampaignDef& C : Campaigns)
	{
		const bool bHover = IsMouseIn(X, Y, CardW, CardH);
		Panel(X, Y, CardW, CardH, FLinearColor(0.01f, 0.012f, 0.015f, 0.9f), bHover ? ColAccent : ColBorder, 2.f * S());
		DrawRect(C.Color, X, Y, CardW, 10.f * S());

		float TY = Y + 40.f * S();
		Text(C.Name.ToUpper(), X + 40.f * S(), TY, ColText, 1.5f, LargeFont());
		TY += 64.f * S();
		Text(C.Tagline, X + 40.f * S(), TY, C.Color * 1.5f, 1.f, MediumFont());
		TY += 50.f * S();
		TY = Paragraph(C.Synopsis, X + 40.f * S(), TY, CardW - 80.f * S(), ColText, 0.95f, SmallFont(), 1.3f);

		int32 Done = 0;
		for (const FName& M : C.Missions)
		{
			Done += GI->IsMissionCompleted(M) ? 1 : 0;
		}
		Text(FString::Printf(TEXT("Missions completed: %d / %d"), Done, C.Missions.Num()), X + 40.f * S(), Y + CardH - 130.f * S(), ColDim, 0.9f, SmallFont());
		Bar(X + 40.f * S(), Y + CardH - 100.f * S(), CardW - 80.f * S(), 6.f * S(), C.Missions.Num() > 0 ? (float)Done / C.Missions.Num() : 0.f, C.Color);
		Button(TEXT("SELECT"), X + 40.f * S(), Y + CardH - 80.f * S(), CardW - 80.f * S(), 52.f * S(), TEXT("campaign"), C.Id, 0, true, false, 1.1f);
		X += CardW + Gap;
	}

	Button(TEXT("BACK"), 60.f * S(), Canvas->ClipY - 100.f * S(), 220.f * S(), 52.f * S(), TEXT("page"), TEXT("main"));
}

void ACmdHUD::DrawMissionSelect()
{
	const UCmdDataRegistry* Data = UCmdDataRegistry::Get(this);
	const UCmdGameInstance* GI = GetGI();
	const FCmdCampaignDef* C = Data ? Data->GetCampaign(SelectedCampaign) : nullptr;
	if (!C || !GI)
	{
		MenuPage = TEXT("campaign");
		return;
	}
	DrawMenuBackdrop(C->Name.ToUpper());

	const float X = 120.f * S();
	float Y = 170.f * S();
	const float RowW = Canvas->ClipX - 240.f * S();
	const float RowH = 96.f * S();

	Text(C->Tagline, X, Y, C->Color * 1.5f, 1.1f, MediumFont());
	Y += 50.f * S();

	int32 Index = 1;
	for (const FName& MissionId : C->Missions)
	{
		const FCmdMissionInfo* M = Data->GetMission(MissionId);
		if (!M)
		{
			continue;
		}
		const bool bUnlocked = GI->IsMissionUnlocked(MissionId);
		const bool bDone = GI->IsMissionCompleted(MissionId);
		const bool bHover = bUnlocked && IsMouseIn(X, Y, RowW, RowH - 10.f * S());
		Panel(X, Y, RowW, RowH - 10.f * S(), bHover ? FLinearColor(0.08f, 0.07f, 0.04f, 0.92f) : FLinearColor(0.01f, 0.012f, 0.015f, 0.88f), bHover ? ColAccent : ColBorder, 1.5f * S());
		DrawRect(bUnlocked ? C->Color : FLinearColor(0.2f, 0.2f, 0.2f), X, Y, 8.f * S(), RowH - 10.f * S());

		Text(FString::Printf(TEXT("%02d"), Index), X + 30.f * S(), Y + 18.f * S(), bUnlocked ? ColAccent : ColDim, 1.6f, LargeFont());
		Text(bUnlocked ? M->Title.ToUpper() : TEXT("CLASSIFIED"), X + 130.f * S(), Y + 14.f * S(), bUnlocked ? ColText : ColDim, 1.1f, LargeFont());
		Text(bUnlocked ? M->Subtitle : TEXT("Complete the previous mission to unlock."), X + 130.f * S(), Y + 52.f * S(), ColDim, 0.9f, SmallFont());

		const FString Status = bDone ? TEXT("COMPLETED") : (bUnlocked ? TEXT("AVAILABLE") : TEXT("LOCKED"));
		Text(Status, X + RowW - 420.f * S(), Y + 30.f * S(), bDone ? ColGood : (bUnlocked ? ColAccent : ColDim), 1.f, MediumFont());
		Button(bDone ? TEXT("REPLAY") : TEXT("PLAY"), X + RowW - 220.f * S(), Y + 18.f * S(), 190.f * S(), 50.f * S(), TEXT("mission"), MissionId, 0, bUnlocked);

		Y += RowH;
		++Index;
	}

	Button(TEXT("BACK"), 60.f * S(), Canvas->ClipY - 100.f * S(), 220.f * S(), 52.f * S(), TEXT("page"), TEXT("campaign"));
	Button(TEXT("Unlock all (testing)"), Canvas->ClipX - 560.f * S(), Canvas->ClipY - 90.f * S(), 250.f * S(), 40.f * S(), TEXT("unlock_all"), NAME_None, 0, true, false, 0.8f);
	Button(TEXT("Reset progress"), Canvas->ClipX - 290.f * S(), Canvas->ClipY - 90.f * S(), 230.f * S(), 40.f * S(), TEXT("reset_progress"), NAME_None, 0, true, false, 0.8f);
}

void ACmdHUD::DrawSkirmishSetup()
{
	DrawMenuBackdrop(TEXT("SKIRMISH"));
	const float PW = 900.f * S();
	const float PX = (Canvas->ClipX - PW) * 0.5f;
	float Y = 200.f * S();
	Panel(PX, Y, PW, 640.f * S(), ColPanel, ColBorder, 2.f * S());
	Y += 40.f * S();

	static const TCHAR* Difficulty[3] = { TEXT("Easy"), TEXT("Normal"), TEXT("Hard") };
	static const TCHAR* Themes[4] = { TEXT("Temperate"), TEXT("Winter"), TEXT("Desert"), TEXT("Dusk") };
	const UCmdDataRegistry* Data = UCmdDataRegistry::Get(this);
	const FCmdFactionDef* Faction = Data ? Data->GetFaction(SkirmishDraft.PlayerFaction) : nullptr;

	struct FRow { FString Label; FString Value; FName Action; };
	const TArray<FRow> Rows = {
		{ TEXT("Your faction"), Faction ? Faction->Name : SkirmishDraft.PlayerFaction.ToString(), TEXT("sk_faction") },
		{ TEXT("Opponents"), FString::FromInt(SkirmishDraft.NumEnemies), TEXT("sk_enemies") },
		{ TEXT("Difficulty"), Difficulty[FMath::Clamp(SkirmishDraft.Difficulty, 0, 2)], TEXT("sk_diff") },
		{ TEXT("Map size"), SkirmishDraft.MapSize <= 64 ? TEXT("Small") : (SkirmishDraft.MapSize <= 80 ? TEXT("Medium") : TEXT("Large")), TEXT("sk_size") },
		{ TEXT("Starting credits"), FString::Printf(TEXT("$%s"), *FormatCredits(SkirmishDraft.StartingCredits)), TEXT("sk_credits") },
		{ TEXT("Theatre"), Themes[FMath::Clamp(SkirmishDraft.Theme, 0, 3)], TEXT("sk_theme") },
	};
	for (const FRow& R : Rows)
	{
		Text(R.Label, PX + 60.f * S(), Y + 10.f * S(), ColText, 1.1f, MediumFont());
		Button(TEXT("<"), PX + 460.f * S(), Y, 56.f * S(), 50.f * S(), R.Action, NAME_None, -1);
		Text(R.Value, PX + 670.f * S(), Y + 10.f * S(), ColAccent, 1.1f, MediumFont(), true);
		Button(TEXT(">"), PX + 780.f * S(), Y, 56.f * S(), 50.f * S(), R.Action, NAME_None, 1);
		Y += 72.f * S();
	}
	if (Faction)
	{
		Paragraph(Faction->Description, PX + 60.f * S(), Y + 4.f * S(), PW - 120.f * S(), ColDim, 0.85f, SmallFont());
	}

	Button(TEXT("BACK"), 60.f * S(), Canvas->ClipY - 100.f * S(), 220.f * S(), 52.f * S(), TEXT("page"), TEXT("main"));
	Button(TEXT("START BATTLE"), Canvas->ClipX - 380.f * S(), Canvas->ClipY - 110.f * S(), 320.f * S(), 64.f * S(), TEXT("sk_start"), NAME_None, 0, true, true, 1.2f);
}

void ACmdHUD::DrawHowToPlay()
{
	DrawMenuBackdrop(TEXT("HOW TO PLAY"));
	const float PW = Canvas->ClipX - 240.f * S();
	const float PX = 120.f * S();
	const float PY = 170.f * S();
	Panel(PX, PY, PW, Canvas->ClipY - 320.f * S(), ColPanel, ColBorder, 2.f * S());

	const TArray<FKeyValue> Left = {
		{ TEXT("Left click"), TEXT("Select a unit or structure") },
		{ TEXT("Left drag"), TEXT("Box-select units") },
		{ TEXT("Double click"), TEXT("Select all of that type on screen") },
		{ TEXT("Right click"), TEXT("Move / attack / capture / harvest") },
		{ TEXT("Ctrl + right click"), TEXT("Force-fire on anything") },
		{ TEXT("A then click"), TEXT("Attack-move") },
		{ TEXT("S / G / D"), TEXT("Stop / Guard / Deploy (MCV)") },
		{ TEXT("Ctrl+1..9, 1..9"), TEXT("Assign / recall control groups") },
		{ TEXT("Ctrl + A"), TEXT("Select all combat units") },
		{ TEXT("Arrows, screen edge"), TEXT("Scroll the battlefield") },
		{ TEXT("Mouse wheel / Q E"), TEXT("Zoom / rotate camera") },
		{ TEXT("H"), TEXT("Jump to your base") },
		{ TEXT("X / R"), TEXT("Sell / Repair mode") },
		{ TEXT("O / F1 / Esc"), TEXT("Objectives / Help / Pause") },
	};
	const TArray<FString> Tips = {
		TEXT("Deploy your MCV to create a Construction Yard, then build Power Plants before anything else."),
		TEXT("Click a structure in the sidebar to build it; when it says READY, click again and place it near your base."),
		TEXT("Ore Refineries come with a free Harvester. More refineries and harvesters mean more money."),
		TEXT("If power demand exceeds supply, production slows, radar goes dark and powered defences shut down."),
		TEXT("Right-click a production structure's surroundings to set its rally point; right-click it to make it primary."),
		TEXT("Engineers capture enemy and neutral structures, or fully repair your own. Oil Derricks pay out regularly."),
		TEXT("Rockets beat armour, machine guns beat infantry, flame beats everything soft, flak beats aircraft."),
	};

	float Y = PY + 40.f * S();
	for (const FKeyValue& P : Left)
	{
		Text(P.Key, PX + 50.f * S(), Y, ColAccent, 0.95f, MediumFont());
		Text(P.Value, PX + 360.f * S(), Y, ColText, 0.95f, MediumFont());
		Y += 40.f * S();
	}
	float TY = PY + 40.f * S();
	const float TX = PX + PW * 0.52f;
	Text(TEXT("COMMANDER'S NOTES"), TX, TY, ColAccent, 1.1f, LargeFont());
	TY += 56.f * S();
	for (const FString& T : Tips)
	{
		TY = Paragraph(FString(TEXT("- ")) + T, TX, TY, PW * 0.44f, ColText, 0.9f, SmallFont(), 1.25f) + 14.f * S();
	}
	Button(TEXT("BACK"), 60.f * S(), Canvas->ClipY - 100.f * S(), 220.f * S(), 52.f * S(), TEXT("page"), TEXT("main"));
}

void ACmdHUD::DrawCredits()
{
	DrawMenuBackdrop(TEXT("CREDITS"));
	const float CX = Canvas->ClipX * 0.5f;
	float Y = 220.f * S();
	const TArray<FKeyValue> Lines = {
		{ TEXT("COMMANDERS: THE IRON WINTER"), TEXT("") },
		{ TEXT("Game design, programming & story"), TEXT("The Commanders Team") },
		{ TEXT("Engine"), TEXT("Unreal Engine 5") },
		{ TEXT("Inspired by"), TEXT("the classic base-building RTS games of the 1990s") },
		{ TEXT("Special thanks"), TEXT("Everyone who ever built a wall of power plants") },
	};
	for (const FKeyValue& L : Lines)
	{
		Text(L.Key, CX, Y, ColAccent, 1.1f, LargeFont(), true);
		Y += 48.f * S();
		if (!L.Value.IsEmpty())
		{
			Text(L.Value, CX, Y, ColText, 1.f, MediumFont(), true);
			Y += 60.f * S();
		}
	}
	Button(TEXT("BACK"), 60.f * S(), Canvas->ClipY - 100.f * S(), 220.f * S(), 52.f * S(), TEXT("page"), TEXT("main"));
}

// ===========================================================================
// Briefing
// ===========================================================================

void ACmdHUD::DrawBriefing()
{
	ACmdMissionDirector* Dir = GetDirector();
	if (!Dir)
	{
		return;
	}
	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;
	DrawRect(FLinearColor(0.008f, 0.01f, 0.012f, 0.97f), 0.f, 0.f, W, H);
	// Subtle scanlines for that command-console feel.
	for (float Y = 0.f; Y < H; Y += 4.f * S())
	{
		DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.012f), 0.f, Y, W, 1.f);
	}

	const float X = 90.f * S();
	float Y = 70.f * S();
	Text(TEXT("// TOP SECRET - MISSION BRIEFING"), X, Y, ColAccent, 0.9f, SmallFont());
	Y += 34.f * S();
	Text(Dir->Title.ToUpper(), X, Y, ColText, 2.0f, LargeFont());
	Y += 78.f * S();
	if (!Dir->Subtitle.IsEmpty())
	{
		Text(Dir->Subtitle, X, Y, ColAccent, 1.1f, MediumFont());
		Y += 40.f * S();
	}
	const FString Meta = FString::Printf(TEXT("%s%s%s"), *Dir->Location, (!Dir->Location.IsEmpty() && !Dir->Date.IsEmpty()) ? TEXT("   |   ") : TEXT(""), *Dir->Date);
	Text(Meta, X, Y, ColDim, 0.9f, SmallFont());
	Y += 50.f * S();

	const float TextW = FMath::Min(980.f * S(), W - 760.f * S());
	for (const FString& Para : Dir->BriefingParagraphs)
	{
		Y = Paragraph(Para, X, Y, TextW, ColText, 0.95f, SmallFont(), 1.3f) + 16.f * S();
	}

	Y += 10.f * S();
	Text(TEXT("OBJECTIVES"), X, Y, ColAccent, 1.0f, MediumFont());
	Y += 38.f * S();
	for (const FCmdObjective& O : Dir->GetObjectives())
	{
		if (!O.bVisible)
		{
			continue;
		}
		Text(FString::Printf(TEXT("%s  %s"), O.bPrimary ? TEXT("[PRIMARY]") : TEXT("[BONUS]  "), *O.Text), X, Y, O.bPrimary ? ColText : ColDim, 0.95f, SmallFont());
		Y += 30.f * S();
	}

	// Recon map on the right.
	const float MapSize = FMath::Min(560.f * S(), H - 320.f * S());
	const float MX = W - MapSize - 90.f * S();
	const float MY = 150.f * S();
	Text(TEXT("SATELLITE RECONNAISSANCE"), MX, MY - 36.f * S(), ColAccent, 0.85f, SmallFont());
	Panel(MX - 6.f * S(), MY - 6.f * S(), MapSize + 12.f * S(), MapSize + 12.f * S(), FLinearColor(0.f, 0.f, 0.f, 1.f), ColBorder, 2.f * S());
	DrawMinimap(MX, MY, MapSize, false, false);
	// Sweep line.
	const float Sweep = FMath::Fmod(Time * 0.25f, 1.f);
	DrawRect(FLinearColor(0.3f, 1.f, 0.4f, 0.15f), MX, MY + Sweep * MapSize, MapSize, 3.f * S());

	Button(TEXT("BEGIN MISSION"), W - 470.f * S(), H - 120.f * S(), 380.f * S(), 66.f * S(), TEXT("brief_start"), NAME_None, 0, true, true, 1.25f);
	Button(TEXT("BACK"), X, H - 110.f * S(), 220.f * S(), 52.f * S(), TEXT("to_menu"));
}

// ===========================================================================
// Cinematic overlay
// ===========================================================================

void ACmdHUD::DrawCinematic()
{
	const ACmdMissionDirector* Dir = GetDirector();
	const ACmdCinematicDirector* Cine = Dir ? Dir->GetCinematicDirector() : nullptr;
	if (!Cine)
	{
		return;
	}
	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;
	const float BarH = H * 0.12f * Cine->GetLetterbox();
	DrawRect(FLinearColor::Black, 0.f, 0.f, W, BarH);
	DrawRect(FLinearColor::Black, 0.f, H - BarH, W, BarH);

	FString Title;
	FString Sub;
	float Alpha = 0.f;
	if (Cine->GetTitle(Title, Sub, Alpha))
	{
		Text(Title, W * 0.5f, H * 0.40f, FLinearColor(ColAccent.R, ColAccent.G, ColAccent.B, Alpha), 2.4f, LargeFont(), true);
		if (!Sub.IsEmpty())
		{
			Text(Sub, W * 0.5f, H * 0.40f + 96.f * S(), FLinearColor(ColText.R, ColText.G, ColText.B, Alpha), 1.3f, MediumFont(), true);
		}
	}

	FString Speaker;
	FString SubLine;
	if (Cine->GetSubtitle(Speaker, SubLine, Alpha))
	{
		const float MaxW = FMath::Min(1300.f * S(), W - 200.f * S());
		const TArray<FString> Lines = Wrap(SubLine, MaxW, 1.05f, MediumFont());
		const float LineH = Measure(TEXT("Ag"), 1.05f, MediumFont()).Y * 1.15f;
		float Y = H - BarH + 18.f * S();
		if (BarH < 80.f * S())
		{
			Y = H - 60.f * S() - LineH * Lines.Num();
		}
		if (!Speaker.IsEmpty())
		{
			Text(Speaker.ToUpper(), W * 0.5f, Y - 32.f * S(), FLinearColor(ColAccent.R, ColAccent.G, ColAccent.B, Alpha), 0.85f, SmallFont(), true);
		}
		for (const FString& L : Lines)
		{
			Text(L, W * 0.5f, Y, FLinearColor(1.f, 1.f, 1.f, Alpha), 1.05f, MediumFont(), true);
			Y += LineH;
		}
	}

	if (Cine->GetFade() > 0.01f)
	{
		DrawRect(FLinearColor(0.f, 0.f, 0.f, Cine->GetFade()), 0.f, 0.f, W, H);
	}
	if (Cine->CanSkip())
	{
		Text(TEXT("SPACE to skip"), W - 40.f * S() - Measure(TEXT("SPACE to skip"), 0.75f, SmallFont()).X, 30.f * S(), FLinearColor(0.6f, 0.6f, 0.6f, 0.7f), 0.75f, SmallFont());
	}
}

// ===========================================================================
// Results
// ===========================================================================

void ACmdHUD::DrawResult()
{
	const ACmdGameState* GS = GetGS();
	const ACmdMissionDirector* Dir = GetDirector();
	UCmdGameInstance* GI = GetGI();
	if (!GS || !Dir || !GI)
	{
		return;
	}
	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;
	const bool bWin = GS->Flow == ECmdFlowState::Victory;
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.78f), 0.f, 0.f, W, H);

	const FString Heading = Dir->IsSkirmish() ? (bWin ? TEXT("VICTORY") : TEXT("DEFEAT")) : (bWin ? TEXT("MISSION ACCOMPLISHED") : TEXT("MISSION FAILED"));
	float Y = 120.f * S();
	Text(Heading, W * 0.5f, Y, bWin ? ColAccent : ColBad, 2.6f, LargeFont(), true);
	Y += 110.f * S();
	Y = FMath::Max(Y, Paragraph(Dir->ResultReason, W * 0.5f - 500.f * S(), Y, 1000.f * S(), ColText, 1.f, MediumFont()));
	Y += 30.f * S();

	// Stats table.
	const FCmdTeam* Me = GS->GetTeam(GS->LocalTeamId);
	if (Me)
	{
		const float PW = 760.f * S();
		const float PX = (W - PW) * 0.5f;
		Panel(PX, Y, PW, 250.f * S(), ColPanel, ColBorder, 1.5f * S());
		const TArray<FKeyValue> Stats = {
			{ TEXT("Mission time"), FormatTime(GS->MatchTime) },
			{ TEXT("Units built"), FString::FromInt(Me->Stats.UnitsBuilt) },
			{ TEXT("Units lost"), FString::FromInt(Me->Stats.UnitsLost) },
			{ TEXT("Enemies destroyed"), FString::FromInt(Me->Stats.Kills) },
			{ TEXT("Structures built / lost"), FString::Printf(TEXT("%d / %d"), Me->Stats.BuildingsBuilt, Me->Stats.BuildingsLost) },
			{ TEXT("Ore harvested"), FString::Printf(TEXT("$%s"), *FormatCredits(Me->Stats.CreditsHarvested)) },
		};
		float SY = Y + 22.f * S();
		for (const FKeyValue& P : Stats)
		{
			Text(P.Key, PX + 40.f * S(), SY, ColDim, 0.95f, SmallFont());
			Text(P.Value, PX + PW - 40.f * S() - Measure(P.Value, 0.95f, SmallFont()).X, SY, ColText, 0.95f, SmallFont());
			SY += 36.f * S();
		}
		Y += 280.f * S();
	}

	// Campaign epilogue after the final mission.
	const FName Next = GI->GetNextMission(GI->CurrentMission);
	if (bWin && GI->Mode == ECmdSessionMode::Campaign && Next.IsNone())
	{
		if (const UCmdDataRegistry* Data = UCmdDataRegistry::Get(this))
		{
			if (const FCmdMissionInfo* M = Data->GetMission(GI->CurrentMission))
			{
				if (const FCmdCampaignDef* C = Data->GetCampaign(M->Campaign))
				{
					Text(TEXT("EPILOGUE"), W * 0.5f, Y, ColAccent, 1.1f, MediumFont(), true);
					Y += 40.f * S();
					Paragraph(C->Epilogue, W * 0.5f - 600.f * S(), Y, 1200.f * S(), ColText, 0.9f, SmallFont(), 1.3f);
				}
			}
		}
	}

	const float BY = H - 120.f * S();
	const float BW = 300.f * S();
	const float BH = 60.f * S();
	if (bWin && GI->Mode == ECmdSessionMode::Campaign && !Next.IsNone())
	{
		Button(TEXT("NEXT MISSION"), W * 0.5f + 170.f * S(), BY, BW, BH, TEXT("next_mission"), Next, 0, true, true, 1.15f);
		Button(TEXT("MAIN MENU"), W * 0.5f - 170.f * S() - BW, BY, BW, BH, TEXT("to_menu"));
	}
	else
	{
		Button(bWin ? TEXT("PLAY AGAIN") : TEXT("RETRY"), W * 0.5f + 170.f * S(), BY, BW, BH, TEXT("restart"), NAME_None, 0, true, !bWin, 1.15f);
		Button(TEXT("MAIN MENU"), W * 0.5f - 170.f * S() - BW, BY, BW, BH, TEXT("to_menu"), NAME_None, 0, true, bWin, 1.15f);
	}
}

void ACmdHUD::DrawPauseMenu()
{
	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.65f), 0.f, 0.f, W, H);
	const float PW = 460.f * S();
	const float PH = 470.f * S();
	const float PX = (W - PW) * 0.5f;
	const float PY = (H - PH) * 0.5f;
	Panel(PX, PY, PW, PH, ColPanel, ColBorder, 2.f * S());
	Text(TEXT("PAUSED"), W * 0.5f, PY + 30.f * S(), ColAccent, 1.6f, LargeFont(), true);

	float Y = PY + 120.f * S();
	const float BX = PX + 50.f * S();
	const float BW = PW - 100.f * S();
	const float BH = 56.f * S();
	if (bConfirmAbandon)
	{
		Text(TEXT("Abandon this mission?"), W * 0.5f, Y, ColText, 1.f, MediumFont(), true);
		Y += 60.f * S();
		Button(TEXT("YES, ABANDON"), BX, Y, BW, BH, TEXT("to_menu"));
		Y += BH + 16.f * S();
		Button(TEXT("NO"), BX, Y, BW, BH, TEXT("resume"));
		return;
	}
	Button(TEXT("RESUME"), BX, Y, BW, BH, TEXT("resume"), NAME_None, 0, true, true); Y += BH + 16.f * S();
	Button(TEXT("RESTART"), BX, Y, BW, BH, TEXT("restart")); Y += BH + 16.f * S();
	Button(bShowHelp ? TEXT("HIDE CONTROLS") : TEXT("SHOW CONTROLS"), BX, Y, BW, BH, TEXT("help")); Y += BH + 16.f * S();
	Button(TEXT("ABANDON MISSION"), BX, Y, BW, BH, TEXT("abandon"));
}

// ===========================================================================
// Gameplay HUD
// ===========================================================================

void ACmdHUD::DrawGameplay()
{
	DrawWorldOverlays();
	DrawPlacementGrid();
	DrawSidebar();
	DrawObjectives();
	DrawTimer();
	DrawMessages();
	DrawDialogue();
	if (bShowHelp)
	{
		DrawHelpOverlay();
	}

	// Drag box.
	const ACmdPlayerController* PC = GetPC();
	if (PC && PC->IsDragging())
	{
		const FVector2D A = PC->GetDragStart();
		const FVector2D B = PC->GetMousePos();
		const FVector2D Min(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y));
		const FVector2D Size(FMath::Abs(B.X - A.X), FMath::Abs(B.Y - A.Y));
		Panel(Min.X, Min.Y, Size.X, Size.Y, FLinearColor(0.2f, 1.f, 0.3f, 0.07f), FLinearColor(0.3f, 1.f, 0.4f, 0.9f), 1.f);
	}

	// Cursor mode hint.
	if (PC && PC->GetCursorMode() != ECmdCursorMode::Normal)
	{
		FString Hint;
		switch (PC->GetCursorMode())
		{
		case ECmdCursorMode::Place: Hint = TEXT("Place structure  (right click to cancel)"); break;
		case ECmdCursorMode::Sell: Hint = TEXT("SELL: click a structure"); break;
		case ECmdCursorMode::Repair: Hint = TEXT("REPAIR: click a structure"); break;
		case ECmdCursorMode::AttackMove: Hint = TEXT("ATTACK-MOVE: click a destination"); break;
		default: break;
		}
		Text(Hint, Mouse.X + 22.f * S(), Mouse.Y + 18.f * S(), ColAccent, 0.8f, SmallFont());
	}

	// Pause button top-right of the play area.
	Button(TEXT("MENU"), SidebarX - 120.f * S(), 12.f * S(), 100.f * S(), 36.f * S(), TEXT("pause"), NAME_None, 0, true, false, 0.8f);
}

void ACmdHUD::DrawWorldOverlays()
{
	const ACmdGameState* GS = GetGS();
	const ACmdPlayerController* PC = GetPC();
	if (!GS || !PC || !PC->PlayerCameraManager)
	{
		return;
	}
	const FVector CamRight = PC->PlayerCameraManager->GetCameraRotation().RotateVector(FVector::RightVector);
	const FVector CamUp = PC->PlayerCameraManager->GetCameraRotation().RotateVector(FVector::UpVector);
	const ACmdEntity* Hovered = PC->GetHoveredEntity();
	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;

	for (const ACmdEntity* E : GS->Entities)
	{
		if (!E || !E->IsAlive() || E->IsHidden() || !E->Def)
		{
			continue;
		}
		const bool bOwn = E->TeamId == GS->LocalTeamId;
		const bool bSelected = E->bSelected;
		const bool bHovered = E == Hovered;
		const bool bDamaged = E->GetHealthPct() < 0.999f;
		const ACmdBuilding* B = Cast<ACmdBuilding>(E);
		const bool bShowBar = bSelected || bHovered || (bDamaged && bOwn) || (B && bOwn && B->bRepairing);
		if (!bShowBar)
		{
			continue;
		}

		const FVector Base = E->GetActorLocation();
		const FVector Center3 = Base + FVector(0.f, 0.f, E->GetBarHeight() * 0.45f);
		const FVector C = Project(Center3);
		if (C.Z <= 0.f || C.X < -100.f || C.Y < -100.f || C.X > W + 100.f || C.Y > H + 100.f)
		{
			continue;
		}
		const FVector R = Project(Center3 + CamRight * E->GetRadius());
		const FVector U = Project(Center3 + CamUp * E->GetBarHeight() * 0.55f);
		const float HalfW = FMath::Max(10.f * S(), FMath::Abs(R.X - C.X) * 1.1f);
		const float HalfH = FMath::Max(10.f * S(), FMath::Abs(U.Y - C.Y) * 1.1f);

		const FLinearColor TeamTint = bOwn ? FLinearColor(0.85f, 0.95f, 0.85f) : (GS->AreAllied(GS->LocalTeamId, E->TeamId) ? FLinearColor(0.5f, 0.8f, 1.f) : FLinearColor(1.f, 0.45f, 0.4f));
		if (bSelected)
		{
			Brackets(FVector2D(C.X, C.Y), HalfW, HalfH, TeamTint);
		}
		else if (bHovered)
		{
			Brackets(FVector2D(C.X, C.Y), HalfW, HalfH, TeamTint * 0.6f);
		}

		const float BarW = FMath::Clamp(HalfW * 1.6f, 26.f * S(), 160.f * S());
		const float BarH = FMath::Max(3.f, 5.f * S());
		const float BX = C.X - BarW * 0.5f;
		const float BY = C.Y - HalfH - 10.f * S();
		Bar(BX, BY, BarW, BarH, E->GetHealthPct(), HealthColor(E->GetHealthPct()));

		if (const ACmdUnit* Unit = Cast<ACmdUnit>(E))
		{
			if (Unit->IsHarvester() && bOwn)
			{
				const float Pct = Unit->Def->HarvestCapacity > 0 ? (float)Unit->GetCargo() / Unit->Def->HarvestCapacity : 0.f;
				Bar(BX, BY + BarH + 3.f, BarW, BarH * 0.7f, Pct, FLinearColor(0.95f, 0.7f, 0.1f));
			}
			if (bHovered && !bOwn && E->Def)
			{
				Text(E->Def->Name, C.X, BY - 22.f * S(), TeamTint, 0.75f, SmallFont(), true);
			}
		}
		if (B)
		{
			if (bHovered || bSelected)
			{
				Text(E->Def->Name, C.X, BY - 22.f * S(), TeamTint, 0.75f, SmallFont(), true);
			}
			if (B->bPrimary && bOwn)
			{
				Text(TEXT("PRIMARY"), C.X, BY + 8.f * S(), ColAccent, 0.65f, SmallFont(), true);
			}
			if (B->bRepairing && FMath::Fmod(Time, 0.8f) < 0.5f)
			{
				Text(TEXT("+ REPAIRING"), C.X, C.Y, ColGood, 0.75f, SmallFont(), true);
			}
			if (!B->IsPowered() && E->Def->bRequiresPower && FMath::Fmod(Time, 1.f) < 0.6f)
			{
				Text(TEXT("NO POWER"), C.X, C.Y + 18.f * S(), ColBad, 0.75f, SmallFont(), true);
			}
			// Rally point line.
			if (bSelected && bOwn && B->Def->Produces.Num() > 0 && B->RallyCell.X >= 0)
			{
				const FVector RP = Project(Cmd::CellToWorld(B->RallyCell, 10.f));
				const FVector BP = Project(B->GetActorLocation());
				if (RP.Z > 0.f && BP.Z > 0.f)
				{
					Line(FVector2D(BP.X, BP.Y), FVector2D(RP.X, RP.Y), FLinearColor(0.3f, 0.6f, 1.f, 0.8f), 1.5f);
					DrawRect(FLinearColor(0.3f, 0.6f, 1.f), RP.X - 4.f, RP.Y - 4.f, 8.f, 8.f);
				}
			}
		}
	}
}

void ACmdHUD::DrawPlacementGrid()
{
	const ACmdPlayerController* PC = GetPC();
	if (!PC || PC->GetCursorMode() != ECmdCursorMode::Place)
	{
		return;
	}
	const UCmdDataRegistry* Data = UCmdDataRegistry::Get(this);
	const FCmdEntityDef* Def = Data ? Data->GetEntity(PC->GetPlacementId()) : nullptr;
	const UCmdGridSubsystem* Grid = GetWorld()->GetSubsystem<UCmdGridSubsystem>();
	if (!Def || !Grid)
	{
		return;
	}
	const FIntPoint O = PC->GetPlacementOrigin();
	for (int32 Y = 0; Y < Def->Footprint.Y; ++Y)
	{
		for (int32 X = 0; X < Def->Footprint.X; ++X)
		{
			const FIntPoint C = O + FIntPoint(X, Y);
			const bool bOk = Grid->IsBuildableCell(C) && PC->IsPlacementValid();
			const FLinearColor Col = bOk ? FLinearColor(0.2f, 1.f, 0.3f, 0.9f) : FLinearColor(1.f, 0.2f, 0.2f, 0.9f);
			const float CS = CmdConst::CellSize;
			const FVector P0 = Project(FVector(C.X * CS + 6.f, C.Y * CS + 6.f, 3.f));
			const FVector P1 = Project(FVector((C.X + 1) * CS - 6.f, C.Y * CS + 6.f, 3.f));
			const FVector P2 = Project(FVector((C.X + 1) * CS - 6.f, (C.Y + 1) * CS - 6.f, 3.f));
			const FVector P3 = Project(FVector(C.X * CS + 6.f, (C.Y + 1) * CS - 6.f, 3.f));
			Line(FVector2D(P0.X, P0.Y), FVector2D(P1.X, P1.Y), Col, 2.f);
			Line(FVector2D(P1.X, P1.Y), FVector2D(P2.X, P2.Y), Col, 2.f);
			Line(FVector2D(P2.X, P2.Y), FVector2D(P3.X, P3.Y), Col, 2.f);
			Line(FVector2D(P3.X, P3.Y), FVector2D(P0.X, P0.Y), Col, 2.f);
		}
	}
}

void ACmdHUD::DrawSidebar()
{
	ACmdGameState* GS = GetGS();
	const UCmdDataRegistry* Data = UCmdDataRegistry::Get(this);
	const FCmdTeam* Team = GS ? GS->GetLocalTeam() : nullptr;
	if (!Team || !Data)
	{
		return;
	}
	SidebarW = 320.f * S();
	SidebarX = Canvas->ClipX - SidebarW;
	const float X = SidebarX;
	const float W = SidebarW;
	const float H = Canvas->ClipY;
	const float Pad = 10.f * S();

	DrawRect(FLinearColor(0.012f, 0.014f, 0.016f, 0.96f), X, 0.f, W, H);
	DrawRect(ColBorder, X, 0.f, 2.f * S(), H);

	// Credits & power -----------------------------------------------------------
	float Y = Pad;
	Text(FString::Printf(TEXT("$ %s"), *FormatCredits(Team->Credits)), X + Pad + 4.f * S(), Y, ColAccent, 1.25f, LargeFont());
	const FString PowerText = FString::Printf(TEXT("POWER %d / %d"), Team->PowerProduced, Team->PowerConsumed);
	const FLinearColor PowerColor = Team->HasLowPower() ? ColBad : (Team->PowerProduced - Team->PowerConsumed < 30 ? FLinearColor(1.f, 0.8f, 0.2f) : ColGood);
	Text(PowerText, X + W - Pad - Measure(PowerText, 0.75f, SmallFont()).X, Y + 6.f * S(), PowerColor, 0.75f, SmallFont());
	const float PowerPct = Team->PowerProduced > 0 ? FMath::Clamp((float)Team->PowerConsumed / Team->PowerProduced, 0.f, 1.f) : 1.f;
	Bar(X + W - Pad - 130.f * S(), Y + 32.f * S(), 130.f * S(), 6.f * S(), PowerPct, PowerColor);
	Y += 52.f * S();

	// Radar ------------------------------------------------------------------------
	MinimapSize = W - Pad * 2.f;
	MinimapPos = FVector2D(X + Pad, Y);
	Panel(MinimapPos.X - 2.f, MinimapPos.Y - 2.f, MinimapSize + 4.f, MinimapSize + 4.f, FLinearColor::Black, ColBorder, 1.5f * S());
	if (GS->HasRadar(GS->LocalTeamId))
	{
		DrawMinimap(MinimapPos.X, MinimapPos.Y, MinimapSize, true, GS->bShroudEnabled);
	}
	else
	{
		// Static noise while the radar is offline.
		for (int32 i = 0; i < 70; ++i)
		{
			const float NX = FMath::FRand() * (MinimapSize - 4.f);
			const float NY = FMath::FRand() * (MinimapSize - 2.f);
			DrawRect(FLinearColor(0.2f, 0.25f, 0.2f, 0.4f), MinimapPos.X + NX, MinimapPos.Y + NY, 4.f, 2.f);
		}
		const bool bHasDome = Team->CountOwned(TEXT("radar")) > 0;
		Text(bHasDome ? TEXT("RADAR OFFLINE - LOW POWER") : TEXT("NO RADAR"), MinimapPos.X + MinimapSize * 0.5f, MinimapPos.Y + MinimapSize * 0.45f, ColDim, 0.8f, SmallFont(), true);
		if (!bHasDome)
		{
			Text(TEXT("Build a Radar Dome"), MinimapPos.X + MinimapSize * 0.5f, MinimapPos.Y + MinimapSize * 0.45f + 26.f * S(), ColDim, 0.7f, SmallFont(), true);
		}
	}
	Y += MinimapSize + Pad;

	// Tabs ----------------------------------------------------------------------------
	const float TabW = (W - Pad * 2.f) / 4.f;
	const float TabH = 34.f * S();
	static const TCHAR* TabNames[4] = { TEXT("BUILD"), TEXT("INF"), TEXT("VEH"), TEXT("AIR") };
	for (int32 i = 0; i < 4; ++i)
	{
		const bool bAvailable = GS->GetBuildOptions(GS->LocalTeamId, static_cast<ECmdCategory>(i)).Num() > 0 && GS->HasProducer(GS->LocalTeamId, static_cast<ECmdCategory>(i));
		const FCmdProductionQueue& Q = Team->Queues[i];
		const bool bBusy = Q.Items.Num() > 0;
		FString Label = TabNames[i];
		if (bBusy && Q.bReady && FMath::Fmod(Time, 0.8f) < 0.4f)
		{
			Label = TEXT("READY");
		}
		Button(Label, X + Pad + i * TabW, Y, TabW - 4.f * S(), TabH, TEXT("tab"), NAME_None, i, bAvailable, ActiveTab == i, 0.75f);
		if (bBusy)
		{
			DrawRect(ColGood, X + Pad + i * TabW + 4.f * S(), Y + TabH - 5.f * S(), (TabW - 12.f * S()) * Q.Progress, 3.f * S());
		}
	}
	Y += TabH + Pad;

	// Build grid ----------------------------------------------------------------------
	const ECmdCategory Category = static_cast<ECmdCategory>(ActiveTab);
	const TArray<const FCmdEntityDef*> Options = GS->GetBuildOptions(GS->LocalTeamId, Category);
	const bool bHasProducer = GS->HasProducer(GS->LocalTeamId, Category);
	const FCmdProductionQueue& Queue = Team->Queues[ActiveTab];
	const int32 Cols = 3;
	const float Gap = 5.f * S();
	const float IconW = (W - Pad * 2.f - Gap * (Cols - 1)) / Cols;
	const float IconH = 70.f * S();
	const float GridBottom = H - 220.f * S();

	if (Options.Num() == 0 || !bHasProducer)
	{
		static const TCHAR* Needs[4] = { TEXT("Deploy an MCV to build structures."), TEXT("Build a Barracks to train infantry."), TEXT("Build a War Factory for vehicles."), TEXT("Build a Helipad for aircraft.") };
		Paragraph(Team->bCanBuild || ActiveTab != 0 ? Needs[ActiveTab] : TEXT("Base construction unavailable on this mission."), X + Pad, Y + 10.f * S(), W - Pad * 2.f, ColDim, 0.75f, SmallFont());
	}
	else
	{
		for (int32 i = 0; i < Options.Num(); ++i)
		{
			const FCmdEntityDef* Def = Options[i];
			const float IX = X + Pad + (i % Cols) * (IconW + Gap);
			const float IY = Y + (i / Cols) * (IconH + Gap);
			if (IY + IconH > GridBottom)
			{
				break;
			}
			const bool bHover = IsMouseIn(IX, IY, IconW, IconH);
			const bool bFront = Queue.Items.Num() > 0 && Queue.Items[0] == Def->Id;
			const int32 Queued = GS->CountQueued(GS->LocalTeamId, Def->Id);
			const bool bAffordable = Team->Credits >= 1.f;
			const bool bBlocked = Def->bIsBuilding && Queue.Items.Num() > 0 && !bFront;

			// Icon: tinted plate with a big glyph and the unit's name.
			const FLinearColor Plate = Def->bIsBuilding ? FLinearColor(0.12f, 0.11f, 0.08f) : Team->Color * 0.35f;
			Panel(IX, IY, IconW, IconH, bHover ? Plate * 1.8f : Plate, bHover ? ColAccent : FLinearColor(0.3f, 0.28f, 0.22f), 1.f);
			const FString Glyph = Initials(Def->Name);
			Text(Glyph, IX + IconW * 0.5f, IY + 6.f * S(), FLinearColor(1.f, 1.f, 1.f, 0.22f), 1.4f, LargeFont(), true, false);
			const TArray<FString> NameLines = Wrap(Def->Name, IconW - 8.f * S(), 0.62f, SmallFont());
			float NY = IY + IconH - 38.f * S();
			for (int32 L = 0; L < FMath::Min(2, NameLines.Num()); ++L)
			{
				Text(NameLines[L], IX + 5.f * S(), NY, ColText, 0.62f, SmallFont());
				NY += 14.f * S();
			}
			Text(FString::Printf(TEXT("$%d"), Def->Cost), IX + IconW - 5.f * S() - Measure(FString::Printf(TEXT("$%d"), Def->Cost), 0.62f, SmallFont()).X, IY + 4.f * S(), ColAccent, 0.62f, SmallFont());

			// Production state overlays.
			if (bFront)
			{
				if (Queue.bReady)
				{
					DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.4f), IX, IY, IconW, IconH);
					if (FMath::Fmod(Time, 0.8f) < 0.55f)
					{
						Text(TEXT("READY"), IX + IconW * 0.5f, IY + IconH * 0.3f, ColGood, 0.95f, MediumFont(), true);
					}
				}
				else
				{
					DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.6f), IX, IY, IconW, IconH * (1.f - Queue.Progress));
					Text(FString::Printf(TEXT("%d%%"), FMath::FloorToInt(Queue.Progress * 100.f)), IX + IconW * 0.5f, IY + IconH * 0.3f, ColText, 0.8f, SmallFont(), true);
					if (Queue.bOnHold)
					{
						Text(TEXT("ON HOLD"), IX + IconW * 0.5f, IY + IconH * 0.55f, ColBad, 0.7f, SmallFont(), true);
					}
				}
			}
			if (Queued > 1 || (Queued == 1 && !bFront))
			{
				Text(FString::Printf(TEXT("x%d"), Queued), IX + 5.f * S(), IY + 4.f * S(), ColGood, 0.7f, SmallFont());
			}
			if (bBlocked)
			{
				DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.5f), IX, IY, IconW, IconH);
			}
			if (!bAffordable && !bFront)
			{
				DrawRect(FLinearColor(0.3f, 0.f, 0.f, 0.25f), IX, IY, IconW, IconH);
			}

			FButton Btn;
			Btn.Pos = FVector2D(IX, IY);
			Btn.Size = FVector2D(IconW, IconH);
			Btn.Action = TEXT("build");
			Btn.Param = Def->Id;
			PendingButtons.Add(Btn);

			if (bHover)
			{
				HoveredBuildDef = Def;
				HoveredBuildY = IY;
			}
		}
	}

	// Command buttons -----------------------------------------------------------------
	const ACmdPlayerController* PC = GetPC();
	float BY = H - 210.f * S();
	const float BW = (W - Pad * 2.f - Gap * 2.f) / 3.f;
	const float BH = 34.f * S();
	Button(TEXT("SELL"), X + Pad, BY, BW, BH, TEXT("sell"), NAME_None, 0, true, PC && PC->GetCursorMode() == ECmdCursorMode::Sell, 0.75f);
	Button(TEXT("REPAIR"), X + Pad + BW + Gap, BY, BW, BH, TEXT("repair"), NAME_None, 0, true, PC && PC->GetCursorMode() == ECmdCursorMode::Repair, 0.75f);
	Button(TEXT("OBJECTIVES"), X + Pad + (BW + Gap) * 2.f, BY, BW, BH, TEXT("objectives"), NAME_None, 0, true, bShowObjectives, 0.7f);
	BY += BH + Gap;

	DrawSelectionPanel(X + Pad, BY, W - Pad * 2.f);

	if (HoveredBuildDef)
	{
		DrawBuildTooltip(*HoveredBuildDef, X - 10.f * S(), HoveredBuildY);
	}
}

void ACmdHUD::DrawBuildTooltip(const FCmdEntityDef& Def, float RightEdge, float Y)
{
	const ACmdGameState* GS = GetGS();
	const UCmdDataRegistry* Data = UCmdDataRegistry::Get(this);
	const float TW = 340.f * S();
	const float TX = RightEdge - TW;

	FString Missing;
	GS->HasPrerequisites(GS->LocalTeamId, Def, &Missing);
	const FCmdWeaponDef* Weapon = Data ? Data->GetWeapon(Def.Weapon) : nullptr;

	TArray<FString> Lines = Wrap(Def.Description, TW - 30.f * S(), 0.72f, SmallFont());
	const float LineH = Measure(TEXT("Ag"), 0.72f, SmallFont()).Y * 1.2f;
	const float TH = 110.f * S() + Lines.Num() * LineH + (Weapon ? 24.f * S() : 0.f);
	const float TY = FMath::Clamp(Y, 10.f * S(), Canvas->ClipY - TH - 10.f * S());
	Panel(TX, TY, TW, TH, FLinearColor(0.01f, 0.012f, 0.015f, 0.95f), ColBorder, 1.5f * S());

	float LY = TY + 12.f * S();
	Text(Def.Name, TX + 15.f * S(), LY, ColAccent, 0.95f, MediumFont());
	LY += 34.f * S();
	FString Info = FString::Printf(TEXT("Cost $%d   Build %.0fs"), Def.Cost, Def.BuildTime);
	if (Def.Power != 0)
	{
		Info += FString::Printf(TEXT("   Power %+d"), Def.Power);
	}
	Text(Info, TX + 15.f * S(), LY, ColText, 0.7f, SmallFont());
	LY += 24.f * S();
	if (Weapon)
	{
		Text(FString::Printf(TEXT("%s  dmg %.0f  rng %.1f%s"), *Weapon->Name, Weapon->Damage, Weapon->Range, Weapon->bTargetAir ? (Weapon->bTargetGround ? TEXT("  ground+air") : TEXT("  anti-air")) : TEXT("")), TX + 15.f * S(), LY, ColDim, 0.68f, SmallFont());
		LY += 24.f * S();
	}
	for (const FString& L : Lines)
	{
		Text(L, TX + 15.f * S(), LY, ColDim, 0.72f, SmallFont());
		LY += LineH;
	}
	if (!Missing.IsEmpty())
	{
		Text(FString::Printf(TEXT("Requires: %s"), *Missing), TX + 15.f * S(), LY + 4.f * S(), ColBad, 0.7f, SmallFont());
	}
}

void ACmdHUD::DrawSelectionPanel(float X, float Y, float W)
{
	const ACmdPlayerController* PC = GetPC();
	const ACmdGameState* GS = GetGS();
	if (!PC || !GS)
	{
		return;
	}
	const float H = Canvas->ClipY - Y - 10.f * S();
	Panel(X, Y, W, H, ColPanelLight, FLinearColor(0.25f, 0.22f, 0.16f), 1.f);

	const TArray<ACmdEntity*> Sel = PC->GetSelection();
	if (Sel.Num() == 0)
	{
		Text(TEXT("No selection"), X + W * 0.5f, Y + H * 0.4f, ColDim, 0.75f, SmallFont(), true);
		return;
	}
	const ACmdEntity* First = Sel[0];
	const bool bOwn = First->TeamId == GS->LocalTeamId;
	float LY = Y + 10.f * S();
	if (Sel.Num() == 1)
	{
		Text(First->Def ? First->Def->Name : TEXT("?"), X + 12.f * S(), LY, bOwn ? ColText : ColBad, 0.9f, MediumFont());
		LY += 30.f * S();
		Bar(X + 12.f * S(), LY, W - 24.f * S(), 8.f * S(), First->GetHealthPct(), HealthColor(First->GetHealthPct()));
		LY += 14.f * S();
		Text(FString::Printf(TEXT("%d / %d"), FMath::CeilToInt(First->Health), FMath::RoundToInt(First->GetMaxHealth())), X + 12.f * S(), LY, ColDim, 0.65f, SmallFont());
		if (const FCmdTeam* T = GS->GetTeam(First->TeamId))
		{
			Text(T->Name, X + W - 12.f * S() - Measure(T->Name, 0.65f, SmallFont()).X, LY, T->Color * 1.4f, 0.65f, SmallFont());
		}
		LY += 22.f * S();
		if (const ACmdUnit* U = Cast<ACmdUnit>(First))
		{
			if (U->IsHarvester())
			{
				Text(FString::Printf(TEXT("Cargo: %d / %d"), U->GetCargo(), U->Def->HarvestCapacity), X + 12.f * S(), LY, ColAccent, 0.65f, SmallFont());
				LY += 20.f * S();
			}
			else if (First->Kills > 0)
			{
				Text(FString::Printf(TEXT("Kills: %d"), First->Kills), X + 12.f * S(), LY, ColAccent, 0.65f, SmallFont());
				LY += 20.f * S();
			}
		}
	}
	else
	{
		// Group summary: counts per type.
		TMap<FString, int32> Counts;
		for (const ACmdEntity* E : Sel)
		{
			Counts.FindOrAdd(E->Def ? E->Def->Name : TEXT("?"))++;
		}
		Text(FString::Printf(TEXT("%d selected"), Sel.Num()), X + 12.f * S(), LY, ColText, 0.9f, MediumFont());
		LY += 30.f * S();
		int32 Shown = 0;
		for (const TPair<FString, int32>& P : Counts)
		{
			if (++Shown > 3)
			{
				break;
			}
			Text(FString::Printf(TEXT("%d x %s"), P.Value, *P.Key), X + 12.f * S(), LY, ColDim, 0.65f, SmallFont());
			LY += 18.f * S();
		}
	}

	if (!bOwn || First->IsBuilding())
	{
		return;
	}
	// Unit commands.
	const float BY = Y + H - 44.f * S();
	const float Gap = 5.f * S();
	const float BW = (W - 24.f * S() - Gap * 3.f) / 4.f;
	const bool bCanDeploy = Sel.Num() == 1 && Cast<ACmdUnit>(First) && First->Def && !First->Def->DeploysInto.IsNone();
	Button(TEXT("STOP"), X + 12.f * S(), BY, BW, 34.f * S(), TEXT("stop"), NAME_None, 0, true, false, 0.65f);
	Button(TEXT("GUARD"), X + 12.f * S() + (BW + Gap), BY, BW, 34.f * S(), TEXT("guard"), NAME_None, 0, true, false, 0.65f);
	Button(TEXT("A-MOVE"), X + 12.f * S() + (BW + Gap) * 2.f, BY, BW, 34.f * S(), TEXT("attackmove"), NAME_None, 0, true, false, 0.65f);
	Button(TEXT("DEPLOY"), X + 12.f * S() + (BW + Gap) * 3.f, BY, BW, 34.f * S(), TEXT("deploy"), NAME_None, 0, bCanDeploy, false, 0.65f);
}

// ===========================================================================
// Minimap
// ===========================================================================

void ACmdHUD::UpdateMinimapTexture(bool bShroud)
{
	const UCmdGridSubsystem* Grid = GetWorld()->GetSubsystem<UCmdGridSubsystem>();
	const ACmdGameState* GS = GetGS();
	if (!Grid || !Grid->IsInitialized() || !GS)
	{
		return;
	}
	const int32 W = Grid->GetWidth();
	const int32 H = Grid->GetHeight();
	if (!MinimapTexture || MinimapTexture->GetSizeX() != W || MinimapTexture->GetSizeY() != H)
	{
		MinimapTexture = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8);
		if (!MinimapTexture)
		{
			return;
		}
		MinimapTexture->Filter = TF_Nearest;
		MinimapTexture->SRGB = true;
		MinimapTexture->UpdateResource();
		MinimapRevision = 0;
	}
	if (MinimapRevision == Grid->GetRevision() && bMinimapShroud == bShroud)
	{
		return;
	}
	MinimapRevision = Grid->GetRevision();
	bMinimapShroud = bShroud;

	FCmdMapTheme Theme;
	for (TActorIterator<ACmdTerrainActor> It(GetWorld()); It; ++It)
	{
		Theme = It->GetTheme();
	}
	const int32 Viewer = GS->GetLocalAlliance();
	MinimapPixels.SetNumUninitialized(W * H);
	for (int32 Y = 0; Y < H; ++Y)
	{
		for (int32 X = 0; X < W; ++X)
		{
			const FIntPoint C(X, Y);
			const FCmdCell* Cell = Grid->GetCell(C);
			FLinearColor Col = Theme.Ground * 1.4f;
			switch (Cell->Terrain)
			{
			case ECmdTerrain::Rock: Col = Theme.Rock * 1.3f; break;
			case ECmdTerrain::Water: Col = Theme.Water * 2.f; break;
			case ECmdTerrain::Tree: Col = Theme.TreeTop * 1.6f; break;
			case ECmdTerrain::Road: Col = Theme.Road * 1.4f; break;
			default: break;
			}
			if (Cell->Ore > 0)
			{
				Col = Cell->bGems ? Theme.Gems : Theme.Ore;
			}
			if (Cell->Building && bShroud)
			{
				Col = GS->GetTeamColor(Cell->Building->TeamId) * 1.2f;
			}
			if (bShroud && !Grid->IsExplored(Viewer, C))
			{
				Col = FLinearColor::Black;
			}
			Col.A = 1.f;
			MinimapPixels[Y * W + X] = Col.ToFColor(true);
		}
	}

	FTexturePlatformData* PlatformData = MinimapTexture->GetPlatformData();
	if (PlatformData && PlatformData->Mips.Num() > 0)
	{
		FTexture2DMipMap& Mip = PlatformData->Mips[0];
		void* Dest = Mip.BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(Dest, MinimapPixels.GetData(), MinimapPixels.Num() * sizeof(FColor));
		Mip.BulkData.Unlock();
		MinimapTexture->UpdateResource();
	}
}

FVector2D ACmdHUD::MinimapToCell(const FVector2D& Pos) const
{
	const UCmdGridSubsystem* Grid = GetWorld()->GetSubsystem<UCmdGridSubsystem>();
	if (!Grid || MinimapSize <= 0.f)
	{
		return FVector2D(-1.f, -1.f);
	}
	const float Scale = MinimapSize / FMath::Max(Grid->GetWidth(), Grid->GetHeight());
	const float OX = MinimapPos.X + (MinimapSize - Grid->GetWidth() * Scale) * 0.5f;
	const float OY = MinimapPos.Y + (MinimapSize - Grid->GetHeight() * Scale) * 0.5f;
	return FVector2D((Pos.X - OX) / Scale, (Pos.Y - OY) / Scale);
}

void ACmdHUD::DrawMinimap(float X, float Y, float Size, bool bShowUnits, bool bShroud)
{
	const UCmdGridSubsystem* Grid = GetWorld()->GetSubsystem<UCmdGridSubsystem>();
	const ACmdGameState* GS = GetGS();
	if (!Grid || !GS || !Grid->IsInitialized())
	{
		return;
	}
	MinimapTimer -= GetWorld()->GetDeltaSeconds();
	if (MinimapTimer <= 0.f || !MinimapTexture || bMinimapShroud != bShroud)
	{
		MinimapTimer = 0.4f;
		UpdateMinimapTexture(bShroud);
	}
	if (!MinimapTexture)
	{
		return;
	}
	const int32 GW = Grid->GetWidth();
	const int32 GH = Grid->GetHeight();
	const float Scale = Size / FMath::Max(GW, GH);
	const float OX = X + (Size - GW * Scale) * 0.5f;
	const float OY = Y + (Size - GH * Scale) * 0.5f;
	DrawTexture(MinimapTexture, OX, OY, GW * Scale, GH * Scale, 0.f, 0.f, 1.f, 1.f, FLinearColor::White, BLEND_Opaque);

	if (!bShowUnits)
	{
		return;
	}
	const float Dot = FMath::Max(2.f, Scale * 1.2f);
	for (const ACmdEntity* E : GS->Entities)
	{
		if (!E || !E->IsAlive() || E->IsHidden() || E->IsBuilding())
		{
			continue;
		}
		const FVector L = E->GetActorLocation();
		const float PX = OX + (L.X / CmdConst::CellSize) * Scale;
		const float PY = OY + (L.Y / CmdConst::CellSize) * Scale;
		const FLinearColor C = E->bSelected ? FLinearColor::White : GS->GetTeamColor(E->TeamId) * 1.6f;
		DrawRect(C, PX - Dot * 0.5f, PY - Dot * 0.5f, Dot, Dot);
	}

	// Camera footprint.
	const ACmdPlayerController* PC = GetPC();
	if (PC)
	{
		const FVector2D Corners[4] = { FVector2D(0, 0), FVector2D(SidebarX > 0.f ? SidebarX : Canvas->ClipX, 0), FVector2D(SidebarX > 0.f ? SidebarX : Canvas->ClipX, Canvas->ClipY), FVector2D(0, Canvas->ClipY) };
		FVector2D Pts[4];
		bool bOk = true;
		for (int32 i = 0; i < 4; ++i)
		{
			FVector G;
			if (!PC->ScreenToGround(Corners[i], G))
			{
				bOk = false;
				break;
			}
			Pts[i] = FVector2D(FMath::Clamp(OX + (G.X / CmdConst::CellSize) * Scale, X, X + Size), FMath::Clamp(OY + (G.Y / CmdConst::CellSize) * Scale, Y, Y + Size));
		}
		if (bOk)
		{
			for (int32 i = 0; i < 4; ++i)
			{
				Line(Pts[i], Pts[(i + 1) % 4], FLinearColor(1.f, 1.f, 1.f, 0.8f), 1.f);
			}
		}
	}
}

// ===========================================================================
// Messages, objectives, timer, dialogue
// ===========================================================================

void ACmdHUD::DrawMessages()
{
	const ACmdGameState* GS = GetGS();
	if (!GS)
	{
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	float Y = Canvas->ClipY * 0.55f;
	for (const FCmdMessage& M : GS->Messages)
	{
		const float Age = Now - M.Time;
		const float Alpha = FMath::Clamp(7.f - Age, 0.f, 1.f);
		Text(M.Text, 24.f * S(), Y, FLinearColor(M.Color.R, M.Color.G, M.Color.B, Alpha), 0.85f, MediumFont());
		Y += 30.f * S();
	}
}

void ACmdHUD::DrawObjectives()
{
	const ACmdMissionDirector* Dir = GetDirector();
	if (!Dir || !bShowObjectives)
	{
		return;
	}
	TArray<const FCmdObjective*> Visible;
	for (const FCmdObjective& O : Dir->GetObjectives())
	{
		if (O.bVisible)
		{
			Visible.Add(&O);
		}
	}
	if (Visible.Num() == 0)
	{
		return;
	}
	const float X = 16.f * S();
	const float Y = 16.f * S();
	const float W = 520.f * S();
	const float LineH = 26.f * S();
	Panel(X, Y, W, 44.f * S() + Visible.Num() * LineH, FLinearColor(0.f, 0.f, 0.f, 0.55f), FLinearColor(0.f, 0.f, 0.f, 0.f), 0.f);
	Text(Dir->IsSkirmish() ? TEXT("OBJECTIVE") : Dir->Title.ToUpper(), X + 12.f * S(), Y + 8.f * S(), ColAccent, 0.75f, SmallFont());
	float LY = Y + 34.f * S();
	for (const FCmdObjective* O : Visible)
	{
		FString Mark = TEXT("[  ]");
		FLinearColor Col = O->bPrimary ? ColText : ColDim;
		if (O->State == ECmdObjectiveState::Completed) { Mark = TEXT("[OK]"); Col = ColGood; }
		if (O->State == ECmdObjectiveState::Failed) { Mark = TEXT("[XX]"); Col = ColBad; }
		Text(FString::Printf(TEXT("%s %s%s"), *Mark, *O->Text, O->bPrimary ? TEXT("") : TEXT(" (bonus)")), X + 12.f * S(), LY, Col, 0.72f, SmallFont());
		LY += LineH;
	}
}

void ACmdHUD::DrawTimer()
{
	const ACmdMissionDirector* Dir = GetDirector();
	FString Label;
	float Remaining = 0.f;
	if (!Dir || !Dir->GetVisibleTimer(Label, Remaining))
	{
		return;
	}
	const float CX = (SidebarX > 0.f ? SidebarX : Canvas->ClipX) * 0.5f;
	const bool bUrgent = Remaining < 30.f;
	const FLinearColor Col = bUrgent && FMath::Fmod(Time, 1.f) < 0.5f ? ColBad : ColText;
	Panel(CX - 140.f * S(), 10.f * S(), 280.f * S(), 76.f * S(), FLinearColor(0.f, 0.f, 0.f, 0.6f), ColBorder, 1.f);
	Text(Label.ToUpper(), CX, 16.f * S(), ColAccent, 0.7f, SmallFont(), true);
	Text(FormatTime(Remaining), CX, 36.f * S(), Col, 1.3f, LargeFont(), true);
}

void ACmdHUD::DrawDialogue()
{
	const ACmdMissionDirector* Dir = GetDirector();
	const FCmdDialogueLine* Current = Dir ? Dir->GetCurrentDialogue() : nullptr;
	if (!Current)
	{
		return;
	}
	const float Age = Dir->GetDialogueAge();
	const float Alpha = FMath::Clamp(FMath::Min(Age / 0.25f, (Current->Duration - Age) / 0.4f), 0.f, 1.f);
	const float W = FMath::Min(860.f * S(), (SidebarX > 0.f ? SidebarX : Canvas->ClipX) - 60.f * S());
	const float H = 132.f * S();
	const float X = 24.f * S();
	const float Y = Canvas->ClipY - H - 24.f * S();

	Panel(X, Y, W, H, FLinearColor(0.f, 0.01f, 0.015f, 0.85f * Alpha), FLinearColor(ColBorder.R, ColBorder.G, ColBorder.B, Alpha), 1.5f * S());

	// Portrait: a coloured badge with the speaker's initials and a scanline flicker.
	const float P = H - 20.f * S();
	const FLinearColor PortraitColor(Current->Color.R * 0.35f, Current->Color.G * 0.35f, Current->Color.B * 0.35f, Alpha);
	DrawRect(PortraitColor, X + 10.f * S(), Y + 10.f * S(), P, P);
	for (float SY = 0.f; SY < P; SY += 4.f * S())
	{
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.25f * Alpha), X + 10.f * S(), Y + 10.f * S() + SY, P, 1.f);
	}
	Text(Initials(Current->Speaker), X + 10.f * S() + P * 0.5f, Y + 10.f * S() + P * 0.28f, FLinearColor(Current->Color.R, Current->Color.G, Current->Color.B, Alpha), 1.8f, LargeFont(), true);

	const float TX = X + P + 26.f * S();
	Text(Current->Speaker.ToUpper(), TX, Y + 12.f * S(), FLinearColor(Current->Color.R, Current->Color.G, Current->Color.B, Alpha), 0.8f, SmallFont());
	// Typewriter.
	const int32 Chars = FMath::Clamp(FMath::FloorToInt(Age * 55.f), 0, Current->Text.Len());
	const FString Visible = Current->Text.Left(Chars);
	float LY = Y + 40.f * S();
	for (const FString& L : Wrap(Visible, W - P - 50.f * S(), 0.85f, MediumFont()))
	{
		Text(L, TX, LY, FLinearColor(ColText.R, ColText.G, ColText.B, Alpha), 0.85f, MediumFont());
		LY += 27.f * S();
	}
}

void ACmdHUD::DrawHelpOverlay()
{
	const float W = 460.f * S();
	const float X = (SidebarX > 0.f ? SidebarX : Canvas->ClipX) - W - 20.f * S();
	const float Y = 60.f * S();
	const TArray<FString> Lines = {
		TEXT("LMB select / drag box   RMB command"),
		TEXT("A attack-move   S stop   G guard   D deploy"),
		TEXT("Ctrl+RMB force fire   Ctrl+A all units"),
		TEXT("Ctrl+# set group   # recall (x2 = focus)"),
		TEXT("X sell   R repair   H home   O objectives"),
		TEXT("Arrows / edge scroll   Wheel zoom   Q/E rotate"),
		TEXT("Sidebar: LMB build, RMB hold/cancel, Shift = x5"),
	};
	Panel(X, Y, W, 30.f * S() + Lines.Num() * 24.f * S(), FLinearColor(0.f, 0.f, 0.f, 0.7f), ColBorder, 1.f);
	float LY = Y + 14.f * S();
	for (const FString& L : Lines)
	{
		Text(L, X + 14.f * S(), LY, ColText, 0.68f, SmallFont());
		LY += 24.f * S();
	}
}
