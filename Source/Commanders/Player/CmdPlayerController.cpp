#include "Player/CmdPlayerController.h"
#include "Player/CmdCameraPawn.h"
#include "Audio/CmdAudioSubsystem.h"
#include "UI/CmdHUD.h"
#include "Game/CmdGameState.h"
#include "Game/CmdGameMode.h"
#include "Game/CmdPlayerState.h"
#include "Gameplay/CmdCommands.h"
#include "Gameplay/CmdEntity.h"
#include "Gameplay/CmdUnit.h"
#include "Gameplay/CmdBuilding.h"
#include "Gameplay/CmdEffect.h"
#include "Mission/CmdMissionDirector.h"
#include "Mission/CmdCinematicDirector.h"
#include "World/CmdGridSubsystem.h"
#include "World/CmdVisualSubsystem.h"
#include "Data/CmdDataRegistry.h"
#include "Components/StaticMeshComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"

ACmdPlayerController::ACmdPlayerController()
{
	bShowMouseCursor = true;
	bEnableClickEvents = false;
	bEnableMouseOverEvents = false;
	bShouldPerformFullTickWhenPaused = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	DefaultMouseCursor = EMouseCursor::Default;
}

void ACmdPlayerController::BeginPlay()
{
	Super::BeginPlay();
	FInputModeGameAndUI Mode;
	Mode.SetHideCursorDuringCapture(false);
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::LockAlways);
	SetInputMode(Mode);
}

ACmdCameraPawn* ACmdPlayerController::GetCameraPawn() const
{
	return Cast<ACmdCameraPawn>(GetPawn());
}

ACmdGameState* ACmdPlayerController::GetCmdGameState() const
{
	return GetWorld() ? GetWorld()->GetGameState<ACmdGameState>() : nullptr;
}

ACmdHUD* ACmdPlayerController::GetCmdHUD() const
{
	return Cast<ACmdHUD>(GetHUD());
}

int32 ACmdPlayerController::GetLocalTeam() const
{
	const ACmdGameState* GS = GetCmdGameState();
	return GS ? GS->LocalTeamId : INDEX_NONE;
}

int32 ACmdPlayerController::GetCommandTeam() const
{
	if (const ACmdPlayerState* PS = GetPlayerState<ACmdPlayerState>())
	{
		if (PS->SlotTeam != INDEX_NONE)
		{
			return PS->SlotTeam;
		}
	}
	// Single player: the one local controller commands the local team.
	return IsLocalController() && GetNetMode() == NM_Standalone ? GetLocalTeam() : INDEX_NONE;
}

bool ACmdPlayerController::IsSpectating() const
{
	const ACmdGameState* GS = GetCmdGameState();
	const FCmdTeam* Team = GS ? GS->GetTeam(GS->LocalTeamId) : nullptr;
	return GS && GS->IsNetworked() && Team && Team->bDefeated;
}

// ---------------------------------------------------------------------------
// Networking
// ---------------------------------------------------------------------------

void ACmdPlayerController::SendCommand(const FCmdCommand& Command)
{
	if (HasAuthority())
	{
		CmdCommands::Execute(GetWorld(), GetCommandTeam(), Command);
	}
	else
	{
		ServerCommand(Command);
	}
}

void ACmdPlayerController::ServerCommand_Implementation(const FCmdCommand& Command)
{
	const int32 Team = GetCommandTeam();
	if (Team != INDEX_NONE)
	{
		CmdCommands::Execute(GetWorld(), Team, Command);
	}
}

void ACmdPlayerController::ClientShowMessage_Implementation(const FString& Text, FLinearColor Color, FName Sound)
{
	if (ACmdGameState* GS = GetCmdGameState())
	{
		GS->ShowMessageLocal(Text, Color, Sound);
	}
}

void ACmdPlayerController::ServerSetLobbyChoice_Implementation(FName Faction, int32 Alliance, bool bReady)
{
	ACmdPlayerState* PS = GetPlayerState<ACmdPlayerState>();
	const ACmdGameState* GS = GetCmdGameState();
	if (!PS || !GS || GS->Flow != ECmdFlowState::Lobby)
	{
		return;
	}
	PS->Faction = Faction == TEXT("pact") ? FName(TEXT("pact")) : FName(TEXT("concord"));
	PS->Alliance = FMath::Clamp(Alliance, 0, CmdConst::MaxPlayers - 1);
	PS->bReady = bReady;
}

void ACmdPlayerController::ServerChat_Implementation(const FString& Text)
{
	const FString Clean = Text.TrimStartAndEnd().Left(120);
	ACmdGameState* GS = GetCmdGameState();
	if (Clean.IsEmpty() || !GS || !PlayerState)
	{
		return;
	}
	GS->AddMessage(INDEX_NONE, FString::Printf(TEXT("%s: %s"), *PlayerState->GetPlayerName(), *Clean), FLinearColor(0.75f, 0.9f, 1.f), TEXT("message"));
}

// ---------------------------------------------------------------------------
// Text entry: polled keys, enough for names, addresses and chat
// ---------------------------------------------------------------------------

void ACmdPlayerController::BeginTextEntry(FName Field, const FString& Initial, int32 MaxLength)
{
	TextField = Field;
	TextBuffer = Initial;
	TextMaxLength = FMath::Max(1, MaxLength);
}

void ACmdPlayerController::EndTextEntry(bool bCommit)
{
	if (TextField.IsNone())
	{
		return;
	}
	const FName Field = TextField;
	const FString Text = TextBuffer;
	TextField = NAME_None;
	TextBuffer.Reset();
	if (!bCommit)
	{
		return;
	}
	if (Field == TEXT("chat"))
	{
		if (!Text.TrimStartAndEnd().IsEmpty())
		{
			ServerChat(Text);
		}
		return;
	}
	if (ACmdHUD* HUD = GetCmdHUD())
	{
		HUD->OnTextCommitted(Field, Text);
	}
}

void ACmdPlayerController::HandleTextEntry()
{
	if (Pressed(EKeys::Enter))
	{
		EndTextEntry(true);
		return;
	}
	if (Pressed(EKeys::Escape))
	{
		EndTextEntry(false);
		return;
	}
	if (Pressed(EKeys::BackSpace) && TextBuffer.Len() > 0)
	{
		TextBuffer.LeftChopInline(1);
	}
	const bool bShift = IsKeyDown(EKeys::LeftShift) || IsKeyDown(EKeys::RightShift);
	auto AddChar = [this](TCHAR C)
	{
		if (TextBuffer.Len() < TextMaxLength)
		{
			TextBuffer.AppendChar(C);
		}
	};

	static const FKey Letters[26] = { EKeys::A, EKeys::B, EKeys::C, EKeys::D, EKeys::E, EKeys::F, EKeys::G, EKeys::H, EKeys::I, EKeys::J, EKeys::K, EKeys::L, EKeys::M,
		EKeys::N, EKeys::O, EKeys::P, EKeys::Q, EKeys::R, EKeys::S, EKeys::T, EKeys::U, EKeys::V, EKeys::W, EKeys::X, EKeys::Y, EKeys::Z };
	for (int32 i = 0; i < 26; ++i)
	{
		if (Pressed(Letters[i]))
		{
			AddChar(static_cast<TCHAR>((bShift ? 'A' : 'a') + i));
		}
	}
	static const FKey Digits[10] = { EKeys::Zero, EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine };
	static const FKey PadDigits[10] = { EKeys::NumPadZero, EKeys::NumPadOne, EKeys::NumPadTwo, EKeys::NumPadThree, EKeys::NumPadFour, EKeys::NumPadFive, EKeys::NumPadSix, EKeys::NumPadSeven, EKeys::NumPadEight, EKeys::NumPadNine };
	for (int32 i = 0; i < 10; ++i)
	{
		if (Pressed(Digits[i]))
		{
			if (!bShift)
			{
				AddChar(static_cast<TCHAR>('0' + i));
			}
			else if (i == 1)
			{
				AddChar(TEXT('!'));
			}
		}
		if (Pressed(PadDigits[i]))
		{
			AddChar(static_cast<TCHAR>('0' + i));
		}
	}
	if (Pressed(EKeys::SpaceBar)) AddChar(TEXT(' '));
	if (Pressed(EKeys::Period) || Pressed(EKeys::Decimal)) AddChar(TEXT('.'));
	if (Pressed(EKeys::Comma)) AddChar(TEXT(','));
	if (Pressed(EKeys::Hyphen) || Pressed(EKeys::Subtract)) AddChar(bShift ? TEXT('_') : TEXT('-'));
	if (Pressed(EKeys::Semicolon)) AddChar(bShift ? TEXT(':') : TEXT(';'));
	if (Pressed(EKeys::Slash)) AddChar(bShift ? TEXT('?') : TEXT('/'));
	if (Pressed(EKeys::Apostrophe)) AddChar(TEXT('\''));
}

void ACmdPlayerController::OnSessionBuilt(const FVector& CameraStart, bool bIsMenu)
{
	bSessionBuilt = true;
	bMenu = bIsMenu;
	if (ACmdCameraPawn* Cam = GetCameraPawn())
	{
		if (const UCmdGridSubsystem* Grid = GetWorld()->GetSubsystem<UCmdGridSubsystem>())
		{
			const FVector Size = Grid->GetMapSizeWorld();
			Cam->SetBounds(FVector(0.f, 0.f, 0.f), FVector(Size.X, Size.Y + 600.f, 0.f));
			if (bIsMenu)
			{
				Cam->SetAttractMode(true, Grid->GetMapCenter());
			}
		}
		Cam->FocusOn(CameraStart, true);
	}
}

void ACmdPlayerController::FocusCameraOn(const FVector& Location)
{
	if (ACmdCameraPawn* Cam = GetCameraPawn())
	{
		Cam->FocusOn(Location, false);
	}
}

void ACmdPlayerController::OnMissionEnded()
{
	ClearSelection();
	SetCursorMode(ECmdCursorMode::Normal);
}

// ---------------------------------------------------------------------------
// Selection
// ---------------------------------------------------------------------------

void ACmdPlayerController::CleanSelection()
{
	Selected.RemoveAll([](const TWeakObjectPtr<ACmdEntity>& E) { return !E.IsValid() || !E->IsAlive(); });
}

TArray<ACmdEntity*> ACmdPlayerController::GetSelection() const
{
	TArray<ACmdEntity*> Out;
	for (const TWeakObjectPtr<ACmdEntity>& E : Selected)
	{
		if (E.IsValid() && E->IsAlive())
		{
			Out.Add(E.Get());
		}
	}
	return Out;
}

ACmdEntity* ACmdPlayerController::GetPrimarySelection() const
{
	for (const TWeakObjectPtr<ACmdEntity>& E : Selected)
	{
		if (E.IsValid() && E->IsAlive())
		{
			return E.Get();
		}
	}
	return nullptr;
}

void ACmdPlayerController::ClearSelection()
{
	for (const TWeakObjectPtr<ACmdEntity>& E : Selected)
	{
		if (E.IsValid())
		{
			E->bSelected = false;
		}
	}
	Selected.Reset();
}

void ACmdPlayerController::SelectEntities(const TArray<ACmdEntity*>& Entities, bool bAdd)
{
	if (!bAdd)
	{
		ClearSelection();
	}
	for (ACmdEntity* E : Entities)
	{
		if (E && E->IsAlive() && !Selected.Contains(E))
		{
			E->bSelected = true;
			Selected.Add(E);
		}
	}
}

void ACmdPlayerController::PlaySelectionSound()
{
	const ACmdEntity* First = GetPrimarySelection();
	if (!First || First->TeamId != GetLocalTeam())
	{
		return;
	}
	FName Sound = TEXT("select");
	if (First->IsBuilding())
	{
		Sound = TEXT("select_building");
	}
	else if (!First->IsInfantry())
	{
		Sound = TEXT("select_vehicle");
	}
	UCmdAudioSubsystem::Play2D(this, Sound);
}

bool ACmdPlayerController::HasOwnUnitsSelected() const
{
	for (const ACmdEntity* E : GetSelection())
	{
		if (E->TeamId == GetLocalTeam() && !E->IsBuilding())
		{
			return true;
		}
	}
	return false;
}

// ---------------------------------------------------------------------------
// Picking
// ---------------------------------------------------------------------------

bool ACmdPlayerController::ScreenToGround(const FVector2D& Screen, FVector& OutLocation) const
{
	FVector Origin;
	FVector Direction;
	if (!DeprojectScreenPositionToWorld(Screen.X, Screen.Y, Origin, Direction) || FMath::Abs(Direction.Z) < KINDA_SMALL_NUMBER)
	{
		return false;
	}
	const float T = -Origin.Z / Direction.Z;
	if (T < 0.f)
	{
		return false;
	}
	OutLocation = Origin + Direction * T;
	return true;
}

bool ACmdPlayerController::GetGroundUnderCursor(FVector& OutLocation) const
{
	return ScreenToGround(MousePos, OutLocation);
}

ACmdEntity* ACmdPlayerController::PickEntityAt(const FVector2D& Screen) const
{
	const ACmdGameState* GS = GetCmdGameState();
	if (!GS)
	{
		return nullptr;
	}
	const FVector CamRight = PlayerCameraManager ? PlayerCameraManager->GetCameraRotation().RotateVector(FVector::RightVector) : FVector::RightVector;

	// Units and aircraft first: closest projected centre within its projected radius.
	ACmdEntity* Best = nullptr;
	float BestDist = TNumericLimits<float>::Max();
	for (ACmdEntity* E : GS->Entities)
	{
		if (!E || !E->IsAlive() || E->IsHidden() || E->IsBuilding())
		{
			continue;
		}
		const FVector Center = E->GetActorLocation() + FVector(0.f, 0.f, E->GetBarHeight() * 0.4f);
		FVector2D P;
		FVector2D Edge;
		if (!ProjectWorldLocationToScreen(Center, P) || !ProjectWorldLocationToScreen(Center + CamRight * E->GetRadius(), Edge))
		{
			continue;
		}
		const float Radius = FMath::Max(12.f, FVector2D::Distance(P, Edge) * 1.4f);
		const float D = FVector2D::Distance(P, Screen);
		if (D <= Radius && D < BestDist)
		{
			BestDist = D;
			Best = E;
		}
	}
	if (Best)
	{
		return Best;
	}

	// Structures: whatever footprint the cursor's ground point falls into.
	FVector Ground;
	if (ScreenToGround(Screen, Ground))
	{
		if (const UCmdGridSubsystem* Grid = GetWorld()->GetSubsystem<UCmdGridSubsystem>())
		{
			ACmdBuilding* B = Grid->GetBuildingAt(Cmd::WorldToCell(Ground));
			if (B && B->IsAlive() && !B->IsHidden())
			{
				return B;
			}
		}
	}
	return nullptr;
}

// ---------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------

void ACmdPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	float MX = 0.f;
	float MY = 0.f;
	if (GetMousePosition(MX, MY))
	{
		MousePos = FVector2D(MX, MY);
	}

	ACmdGameState* GS = GetCmdGameState();
	ACmdHUD* HUD = GetCmdHUD();
	if (!GS || !HUD)
	{
		return;
	}
	CleanSelection();

	// Typing swallows the keyboard; a click elsewhere commits the field.
	if (IsTextEntryActive())
	{
		HandleTextEntry();
		if (Pressed(EKeys::LeftMouseButton) && TextField != TEXT("chat"))
		{
			EndTextEntry(true);
		}
		else
		{
			if (GS->Flow == ECmdFlowState::Playing && !HUD->IsModalOpen())
			{
				HandleCamera(DeltaTime);
			}
			return;
		}
	}

	// Cinematics: only allow skipping.
	if (GS->Flow == ECmdFlowState::Cinematic)
	{
		if (Pressed(EKeys::Escape) || Pressed(EKeys::SpaceBar) || Pressed(EKeys::Enter))
		{
			if (GS->MissionDirector)
			{
				GS->MissionDirector->SkipCinematic();
			}
		}
		return;
	}

	// Menu-like screens are entirely HUD driven.
	if (GS->Flow != ECmdFlowState::Playing || HUD->IsModalOpen())
	{
		DestroyGhost();
		if (Pressed(EKeys::LeftMouseButton))
		{
			HUD->HandleClick(MousePos, false);
		}
		if (Pressed(EKeys::Escape))
		{
			HUD->HandleEscape();
		}
		if (Pressed(EKeys::Enter) || Pressed(EKeys::SpaceBar))
		{
			HUD->HandleConfirm();
		}
		if (Pressed(EKeys::MouseScrollUp)) HUD->HandleScroll(-1);
		if (Pressed(EKeys::MouseScrollDown)) HUD->HandleScroll(1);
		return;
	}

	HandleCamera(DeltaTime);
	if (GS->IsNetworked() && Pressed(EKeys::Enter))
	{
		BeginTextEntry(TEXT("chat"), FString(), 120);
		return;
	}
	if (IsSpectating())
	{
		// Eliminated: free camera over the whole map, no orders.
		DestroyGhost();
		if (Pressed(EKeys::Escape))
		{
			HUD->OpenPauseMenu();
		}
		if (Pressed(EKeys::LeftMouseButton))
		{
			HUD->HandleClick(MousePos, false);
		}
		return;
	}
	HandleHotkeys();
	HandleGameplayInput();
	UpdateHover();
	UpdatePlacementGhost();
}

void ACmdPlayerController::HandleCamera(float DeltaTime)
{
	ACmdCameraPawn* Cam = GetCameraPawn();
	if (!Cam)
	{
		return;
	}
	FVector2D Pan = FVector2D::ZeroVector;
	if (IsKeyDown(EKeys::Up)) Pan.Y += 1.f;
	if (IsKeyDown(EKeys::Down)) Pan.Y -= 1.f;
	if (IsKeyDown(EKeys::Left)) Pan.X -= 1.f;
	if (IsKeyDown(EKeys::Right)) Pan.X += 1.f;

	int32 SizeX = 0;
	int32 SizeY = 0;
	GetViewportSize(SizeX, SizeY);
	const float Edge = 6.f;
	if (SizeX > 0 && SizeY > 0 && !bDragging)
	{
		if (MousePos.X <= Edge) Pan.X -= 1.f;
		if (MousePos.X >= SizeX - Edge) Pan.X += 1.f;
		if (MousePos.Y <= Edge) Pan.Y += 1.f;
		if (MousePos.Y >= SizeY - Edge) Pan.Y -= 1.f;
	}
	const float Dt = FMath::Min(DeltaTime, 0.05f);
	Cam->Pan(Pan.GetClampedToMaxSize(1.f), Dt);

	if (Pressed(EKeys::MouseScrollUp)) Cam->Zoom(350.f);
	if (Pressed(EKeys::MouseScrollDown)) Cam->Zoom(-350.f);
	if (IsKeyDown(EKeys::Q)) Cam->Rotate(-90.f * Dt);
	if (IsKeyDown(EKeys::E)) Cam->Rotate(90.f * Dt);

	// Middle mouse drag pans the view.
	if (IsKeyDown(EKeys::MiddleMouseButton))
	{
		float DX = 0.f;
		float DY = 0.f;
		GetInputMouseDelta(DX, DY);
		Cam->Pan(FVector2D(-DX, -DY) * 0.6f, Dt);
	}
}

void ACmdPlayerController::HandleHotkeys()
{
	ACmdGameState* GS = GetCmdGameState();
	ACmdHUD* HUD = GetCmdHUD();
	const bool bCtrl = IsKeyDown(EKeys::LeftControl) || IsKeyDown(EKeys::RightControl);

	if (Pressed(EKeys::Escape))
	{
		if (CursorMode != ECmdCursorMode::Normal)
		{
			SetCursorMode(ECmdCursorMode::Normal);
		}
		else
		{
			HUD->OpenPauseMenu();
		}
		return;
	}
	if (Pressed(EKeys::S)) CommandStop();
	if (Pressed(EKeys::G)) CommandGuard();
	if (Pressed(EKeys::D)) CommandDeploy();
	if (Pressed(EKeys::A) && !bCtrl && HasOwnUnitsSelected()) SetCursorMode(ECmdCursorMode::AttackMove);
	if (Pressed(EKeys::X)) SetCursorMode(CursorMode == ECmdCursorMode::Sell ? ECmdCursorMode::Normal : ECmdCursorMode::Sell);
	if (Pressed(EKeys::R)) SetCursorMode(CursorMode == ECmdCursorMode::Repair ? ECmdCursorMode::Normal : ECmdCursorMode::Repair);
	if (Pressed(EKeys::O)) HUD->ToggleObjectives();
	if (Pressed(EKeys::F1)) HUD->ToggleHelp();
	if (Pressed(EKeys::M))
	{
		if (UCmdAudioSubsystem* Audio = UCmdAudioSubsystem::Get(this))
		{
			Audio->ToggleMusicMute();
			GS->ShowMessageLocal(Audio->IsMusicMuted() ? TEXT("Music off.") : TEXT("Music on."), FLinearColor(0.9f, 0.9f, 0.6f), TEXT("click"));
		}
	}
	if (Pressed(EKeys::H) || Pressed(EKeys::Home))
	{
		FocusCameraOn(GS->GetTeamBaseLocation(GS->LocalTeamId));
	}
	if (bCtrl && Pressed(EKeys::A))
	{
		TArray<ACmdEntity*> All;
		for (ACmdEntity* E : GS->Entities)
		{
			ACmdUnit* U = Cast<ACmdUnit>(E);
			if (U && U->TeamId == GS->LocalTeamId && U->IsAlive() && !U->IsHarvester())
			{
				All.Add(U);
			}
		}
		SelectEntities(All, false);
	}

	// Control groups.
	static const FKey Digits[10] = { EKeys::Zero, EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine };
	for (int32 i = 0; i < 10; ++i)
	{
		if (!Pressed(Digits[i]))
		{
			continue;
		}
		if (bCtrl)
		{
			Groups[i].Reset();
			for (ACmdEntity* E : GetSelection())
			{
				if (E->TeamId == GS->LocalTeamId)
				{
					Groups[i].Add(E);
				}
			}
			GS->ShowMessageLocal(FString::Printf(TEXT("Group %d assigned."), i), FLinearColor(0.9f, 0.9f, 0.6f), TEXT("message"));
		}
		else
		{
			TArray<ACmdEntity*> Members;
			for (const TWeakObjectPtr<ACmdEntity>& E : Groups[i])
			{
				if (E.IsValid() && E->IsAlive() && E->TeamId == GS->LocalTeamId)
				{
					Members.Add(E.Get());
				}
			}
			if (Members.Num() == 0)
			{
				continue;
			}
			const double Now = FPlatformTime::Seconds();
			if (Now - LastGroupTime[i] < 0.4)
			{
				FocusCameraOn(Members[0]->GetActorLocation());
			}
			LastGroupTime[i] = static_cast<float>(Now);
			SelectEntities(Members, IsKeyDown(EKeys::LeftShift));
			PlaySelectionSound();
		}
	}
}

void ACmdPlayerController::HandleGameplayInput()
{
	ACmdHUD* HUD = GetCmdHUD();
	if (Pressed(EKeys::LeftMouseButton))
	{
		if (HUD->HandleClick(MousePos, false))
		{
			return;
		}
		HandleLeftPress();
	}
	if (bDragging && IsKeyDown(EKeys::LeftMouseButton))
	{
		if (FVector2D::Distance(DragStart, MousePos) > 6.f)
		{
			bDragActive = true;
		}
	}
	if (WasInputKeyJustReleased(EKeys::LeftMouseButton) && bDragging)
	{
		HandleLeftRelease();
	}
	if (Pressed(EKeys::RightMouseButton))
	{
		if (HUD->HandleClick(MousePos, true))
		{
			return;
		}
		HandleRightClick();
	}
}

void ACmdPlayerController::HandleLeftPress()
{
	ACmdGameState* GS = GetCmdGameState();
	const int32 Team = GS->LocalTeamId;

	switch (CursorMode)
	{
	case ECmdCursorMode::Place:
		if (bPlacementValid)
		{
			FCmdCommand Command;
			Command.Type = ECmdCommandType::Place;
			Command.Cell = PlacementOrigin;
			SendCommand(Command);
			UCmdAudioSubsystem::Play2D(this, TEXT("place"));
			SetCursorMode(ECmdCursorMode::Normal);
		}
		else
		{
			GS->ShowMessageLocal(TEXT("Cannot build there."), FLinearColor(1.f, 0.4f, 0.3f), TEXT("deny"));
		}
		return;

	case ECmdCursorMode::Sell:
	{
		ACmdBuilding* B = Cast<ACmdBuilding>(PickEntityAt(MousePos));
		if (B && B->TeamId == Team)
		{
			FCmdCommand Command;
			Command.Type = ECmdCommandType::Sell;
			Command.Target = B;
			SendCommand(Command);
		}
		if (!IsKeyDown(EKeys::LeftShift))
		{
			SetCursorMode(ECmdCursorMode::Normal);
		}
		return;
	}

	case ECmdCursorMode::Repair:
	{
		ACmdBuilding* B = Cast<ACmdBuilding>(PickEntityAt(MousePos));
		if (B && B->TeamId == Team)
		{
			FCmdCommand Command;
			Command.Type = ECmdCommandType::Repair;
			Command.Target = B;
			SendCommand(Command);
			UCmdAudioSubsystem::Play2D(this, TEXT("click"));
		}
		if (!IsKeyDown(EKeys::LeftShift))
		{
			SetCursorMode(ECmdCursorMode::Normal);
		}
		return;
	}

	case ECmdCursorMode::AttackMove:
	{
		FVector Ground;
		if (GetGroundUnderCursor(Ground))
		{
			IssueMove(Cmd::WorldToCell(Ground), true);
		}
		SetCursorMode(ECmdCursorMode::Normal);
		return;
	}

	default:
		break;
	}

	bDragging = true;
	bDragActive = false;
	DragStart = MousePos;
}

void ACmdPlayerController::HandleLeftRelease()
{
	bDragging = false;
	const bool bShift = IsKeyDown(EKeys::LeftShift) || IsKeyDown(EKeys::RightShift);
	if (bDragActive)
	{
		bDragActive = false;
		BoxSelect(DragStart, MousePos, bShift);
		return;
	}

	ACmdEntity* Picked = PickEntityAt(MousePos);
	const double Now = FPlatformTime::Seconds();
	if (Picked)
	{
		const bool bDouble = LastClicked.Get() == Picked && Now - LastClickTime < 0.35;
		LastClicked = Picked;
		LastClickTime = static_cast<float>(Now);
		if (bDouble && Picked->TeamId == GetLocalTeam())
		{
			SelectSameTypeOnScreen(Picked);
			PlaySelectionSound();
			return;
		}
		if (bShift && Selected.Contains(Picked))
		{
			Picked->bSelected = false;
			Selected.Remove(Picked);
			return;
		}
		SelectEntities({ Picked }, bShift && Picked->TeamId == GetLocalTeam());
		PlaySelectionSound();
	}
	else if (!bShift)
	{
		ClearSelection();
	}
}

void ACmdPlayerController::BoxSelect(const FVector2D& A, const FVector2D& B, bool bAdd)
{
	const ACmdGameState* GS = GetCmdGameState();
	const FVector2D Min(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y));
	const FVector2D Max(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y));
	TArray<ACmdEntity*> Units;
	TArray<ACmdEntity*> Buildings;
	for (ACmdEntity* E : GS->Entities)
	{
		if (!E || !E->IsAlive() || E->TeamId != GS->LocalTeamId)
		{
			continue;
		}
		FVector2D P;
		if (!ProjectWorldLocationToScreen(E->GetActorLocation(), P))
		{
			continue;
		}
		if (P.X >= Min.X && P.X <= Max.X && P.Y >= Min.Y && P.Y <= Max.Y)
		{
			(E->IsBuilding() ? Buildings : Units).Add(E);
		}
	}
	// Units take priority over structures in a drag box.
	if (Units.Num() > 0)
	{
		SelectEntities(Units, bAdd);
		PlaySelectionSound();
	}
	else if (Buildings.Num() > 0)
	{
		SelectEntities({ Buildings[0] }, bAdd);
		PlaySelectionSound();
	}
	else if (!bAdd)
	{
		ClearSelection();
	}
}

void ACmdPlayerController::SelectSameTypeOnScreen(ACmdEntity* Like)
{
	const ACmdGameState* GS = GetCmdGameState();
	int32 SizeX = 0;
	int32 SizeY = 0;
	GetViewportSize(SizeX, SizeY);
	TArray<ACmdEntity*> Same;
	for (ACmdEntity* E : GS->Entities)
	{
		if (!E || !E->IsAlive() || E->TeamId != Like->TeamId || E->DefId != Like->DefId)
		{
			continue;
		}
		FVector2D P;
		if (ProjectWorldLocationToScreen(E->GetActorLocation(), P) && P.X >= 0 && P.Y >= 0 && P.X <= SizeX && P.Y <= SizeY)
		{
			Same.Add(E);
		}
	}
	SelectEntities(Same, false);
}

void ACmdPlayerController::HandleRightClick()
{
	if (CursorMode != ECmdCursorMode::Normal)
	{
		SetCursorMode(ECmdCursorMode::Normal);
		return;
	}
	ACmdGameState* GS = GetCmdGameState();
	TArray<ACmdEntity*> Sel = GetSelection();
	if (Sel.Num() == 0 || Sel[0]->TeamId != GS->LocalTeamId)
	{
		ClearSelection();
		return;
	}

	ACmdEntity* Target = PickEntityAt(MousePos);
	const bool bForce = IsKeyDown(EKeys::LeftControl) || IsKeyDown(EKeys::RightControl);

	// A selected production structure + right click = set rally point.
	if (!HasOwnUnitsSelected())
	{
		ACmdBuilding* B = Cast<ACmdBuilding>(Sel[0]);
		FVector Ground;
		if (B && B->Def && B->Def->Produces.Num() > 0 && GetGroundUnderCursor(Ground))
		{
			FCmdCommand Command;
			Command.Type = Target == B ? ECmdCommandType::Primary : ECmdCommandType::Rally;
			Command.Target = B;
			Command.Cell = Cmd::WorldToCell(Ground);
			SendCommand(Command);
			UCmdAudioSubsystem::Play2D(this, TEXT("click"));
			ACmdEffect::SpawnFlashLocal(GetWorld(), Ground + FVector(0.f, 0.f, 20.f), FLinearColor(0.3f, 0.6f, 1.f), 0.3f);
		}
		return;
	}

	if (Target && (bForce || Target->TeamId != GS->LocalTeamId))
	{
		IssueTargetCommand(Target, bForce);
		return;
	}
	if (Target && Target->TeamId == GS->LocalTeamId && Target->IsBuilding())
	{
		IssueTargetCommand(Target, false);
		return;
	}

	FVector Ground;
	if (!GetGroundUnderCursor(Ground))
	{
		return;
	}
	const FIntPoint Cell = Cmd::WorldToCell(Ground);
	const UCmdGridSubsystem* Grid = GetWorld()->GetSubsystem<UCmdGridSubsystem>();

	// Harvesters clicked onto ore go mine it.
	if (Grid && Grid->GetOre(Cell) > 0)
	{
		bool bAnyHarvester = false;
		for (ACmdEntity* E : Sel)
		{
			const ACmdUnit* U = Cast<ACmdUnit>(E);
			bAnyHarvester |= U && U->IsHarvester();
		}
		if (bAnyHarvester)
		{
			FCmdCommand Command = MakeUnitCommand(ECmdCommandType::Harvest);
			Command.Cell = Cell;
			SendCommand(Command);
			UCmdAudioSubsystem::Play2D(this, TEXT("ack"));
			ACmdEffect::SpawnFlashLocal(GetWorld(), Ground + FVector(0.f, 0.f, 20.f), FLinearColor(1.f, 0.8f, 0.2f), 0.3f);
			return;
		}
	}
	IssueMove(Cell, false);
}

FCmdCommand ACmdPlayerController::MakeUnitCommand(ECmdCommandType Type) const
{
	FCmdCommand Command;
	Command.Type = Type;
	const int32 Team = GetLocalTeam();
	for (ACmdEntity* E : GetSelection())
	{
		if (E->TeamId == Team && !E->IsBuilding())
		{
			Command.Units.Add(E);
		}
	}
	return Command;
}

void ACmdPlayerController::IssueTargetCommand(ACmdEntity* Target, bool bForce)
{
	ACmdGameState* GS = GetCmdGameState();
	FCmdCommand Command = MakeUnitCommand(bForce ? ECmdCommandType::ForceTarget : ECmdCommandType::Target);
	Command.Target = Target;
	if (Command.Units.Num() == 0)
	{
		return;
	}
	SendCommand(Command);

	// Feedback mirrors what the server will decide.
	const bool bHostile = GS->AreHostile(GS->LocalTeamId, Target->TeamId);
	bool bAttack = false;
	for (ACmdEntity* E : Command.Units)
	{
		bAttack |= (bHostile || bForce) && E->HasWeapon() && E->CanTarget(Target);
	}
	UCmdAudioSubsystem::Play2D(this, bAttack ? TEXT("ack_attack") : TEXT("ack"));
	ACmdEffect::SpawnFlashLocal(GetWorld(), Target->GetActorLocation() + FVector(0.f, 0.f, 30.f), bAttack ? FLinearColor(1.f, 0.15f, 0.1f) : FLinearColor(0.2f, 1.f, 0.3f), 0.35f);
}

void ACmdPlayerController::IssueMove(const FIntPoint& Cell, bool bAttackMove)
{
	FCmdCommand Command = MakeUnitCommand(bAttackMove ? ECmdCommandType::AttackMove : ECmdCommandType::Move);
	Command.Cell = Cell;
	if (Command.Units.Num() == 0)
	{
		return;
	}
	SendCommand(Command);
	UCmdAudioSubsystem::Play2D(this, bAttackMove ? TEXT("ack_attack") : TEXT("ack"));
	ACmdEffect::SpawnFlashLocal(GetWorld(), Cmd::CellToWorld(Cell) + FVector(0.f, 0.f, 20.f), bAttackMove ? FLinearColor(1.f, 0.5f, 0.1f) : FLinearColor(0.2f, 1.f, 0.3f), 0.3f);
}

void ACmdPlayerController::CommandStop()
{
	const FCmdCommand Command = MakeUnitCommand(ECmdCommandType::Stop);
	if (Command.Units.Num() > 0)
	{
		SendCommand(Command);
	}
}

void ACmdPlayerController::CommandGuard()
{
	const FCmdCommand Command = MakeUnitCommand(ECmdCommandType::Guard);
	if (Command.Units.Num() > 0)
	{
		SendCommand(Command);
	}
}

void ACmdPlayerController::CommandDeploy()
{
	FCmdCommand Command = MakeUnitCommand(ECmdCommandType::Deploy);
	Command.Units.RemoveAll([](const TObjectPtr<ACmdEntity>& E) { return !E->Def || E->Def->DeploysInto.IsNone(); });
	if (Command.Units.Num() > 0)
	{
		SendCommand(Command);
	}
}
// ---------------------------------------------------------------------------
// Modes & placement ghost
// ---------------------------------------------------------------------------

void ACmdPlayerController::SetCursorMode(ECmdCursorMode Mode)
{
	CursorMode = Mode;
	if (Mode != ECmdCursorMode::Place)
	{
		PlacementId = NAME_None;
		DestroyGhost();
	}
	switch (Mode)
	{
	case ECmdCursorMode::AttackMove: CurrentMouseCursor = EMouseCursor::Crosshairs; break;
	case ECmdCursorMode::Sell: CurrentMouseCursor = EMouseCursor::SlashedCircle; break;
	case ECmdCursorMode::Repair: CurrentMouseCursor = EMouseCursor::Hand; break;
	case ECmdCursorMode::Place: CurrentMouseCursor = EMouseCursor::GrabHand; break;
	default: CurrentMouseCursor = EMouseCursor::Default; break;
	}
}

void ACmdPlayerController::BeginPlacement(FName StructureId)
{
	SetCursorMode(ECmdCursorMode::Place);
	PlacementId = StructureId;
}

void ACmdPlayerController::DestroyGhost()
{
	if (Ghost)
	{
		Ghost->Destroy();
		Ghost = nullptr;
	}
	GhostParts.Reset();
}

void ACmdPlayerController::UpdatePlacementGhost()
{
	if (CursorMode != ECmdCursorMode::Place)
	{
		return;
	}
	const ACmdGameState* GS = GetCmdGameState();
	if (GS->GetReadyStructure(GS->LocalTeamId) != PlacementId)
	{
		SetCursorMode(ECmdCursorMode::Normal);
		return;
	}
	const UCmdDataRegistry* Data = UCmdDataRegistry::Get(this);
	const FCmdEntityDef* Def = Data ? Data->GetEntity(PlacementId) : nullptr;
	const UCmdGridSubsystem* Grid = GetWorld()->GetSubsystem<UCmdGridSubsystem>();
	UCmdVisualSubsystem* Visuals = UCmdVisualSubsystem::Get(this);
	FVector Ground;
	if (!Def || !Grid || !Visuals || !GetGroundUnderCursor(Ground))
	{
		return;
	}

	PlacementOrigin = Cmd::WorldToCell(Ground) - FIntPoint(Def->Footprint.X / 2, Def->Footprint.Y / 2);
	bPlacementValid = Grid->CanPlaceStructure(GS->LocalTeamId, *Def, PlacementOrigin, true);

	const FLinearColor Color = bPlacementValid ? FLinearColor(0.1f, 0.8f, 0.2f) : FLinearColor(0.9f, 0.1f, 0.1f);
	if (!Ghost)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Ghost = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
		USceneComponent* Root = NewObject<USceneComponent>(Ghost, TEXT("GhostRoot"));
		Ghost->SetRootComponent(Root);
		Root->RegisterComponent();
		Visuals->BuildParts(Ghost, Root, Root, Def->Parts, Color, GhostParts, nullptr, &Color, false);
		bGhostValidColor = bPlacementValid;
	}
	else if (bGhostValidColor != bPlacementValid)
	{
		bGhostValidColor = bPlacementValid;
		for (UStaticMeshComponent* C : GhostParts)
		{
			if (C)
			{
				C->SetMaterial(0, Visuals->GetColorMaterial(Color));
			}
		}
	}
	const FVector Center((PlacementOrigin.X + Def->Footprint.X * 0.5f) * CmdConst::CellSize, (PlacementOrigin.Y + Def->Footprint.Y * 0.5f) * CmdConst::CellSize, 0.f);
	Ghost->SetActorLocation(Center);
}

void ACmdPlayerController::UpdateHover()
{
	Hovered = PickEntityAt(MousePos);
	if (CursorMode != ECmdCursorMode::Normal)
	{
		return;
	}
	const ACmdGameState* GS = GetCmdGameState();
	ACmdEntity* H = Hovered.Get();
	EMouseCursor::Type Cursor = EMouseCursor::Default;
	if (H && HasOwnUnitsSelected() && GS->AreHostile(GS->LocalTeamId, H->TeamId))
	{
		Cursor = EMouseCursor::Crosshairs;
	}
	else if (H && H->TeamId == GS->LocalTeamId)
	{
		Cursor = EMouseCursor::Hand;
	}
	CurrentMouseCursor = Cursor;
}
