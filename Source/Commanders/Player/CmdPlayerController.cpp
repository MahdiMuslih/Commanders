#include "Player/CmdPlayerController.h"
#include "Player/CmdCameraPawn.h"
#include "UI/CmdHUD.h"
#include "Game/CmdGameState.h"
#include "Game/CmdGameMode.h"
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
			GS->AddMessage(GS->LocalTeamId, FString::Printf(TEXT("Group %d assigned."), i));
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
		if (bPlacementValid && GS->PlaceReadyStructure(Team, PlacementOrigin))
		{
			SetCursorMode(ECmdCursorMode::Normal);
		}
		else
		{
			GS->AddMessage(Team, TEXT("Cannot build there."), FLinearColor(1.f, 0.4f, 0.3f));
		}
		return;

	case ECmdCursorMode::Sell:
	{
		ACmdBuilding* B = Cast<ACmdBuilding>(PickEntityAt(MousePos));
		if (B && B->TeamId == Team)
		{
			B->Sell();
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
			B->ToggleRepair();
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
			return;
		}
		if (bShift && Selected.Contains(Picked))
		{
			Picked->bSelected = false;
			Selected.Remove(Picked);
			return;
		}
		SelectEntities({ Picked }, bShift && Picked->TeamId == GetLocalTeam());
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
	}
	else if (Buildings.Num() > 0)
	{
		SelectEntities({ Buildings[0] }, bAdd);
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
			B->RallyCell = Cmd::WorldToCell(Ground);
			ACmdEffect::SpawnFlash(GetWorld(), Ground + FVector(0.f, 0.f, 20.f), FLinearColor(0.3f, 0.6f, 1.f), 0.3f);
			if (Target == B)
			{
				GS->SetPrimary(B);
			}
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
			ACmdUnit* U = Cast<ACmdUnit>(E);
			if (U && U->IsHarvester())
			{
				U->OrderHarvest(&Cell);
				bAnyHarvester = true;
			}
		}
		if (bAnyHarvester)
		{
			ACmdEffect::SpawnFlash(GetWorld(), Ground + FVector(0.f, 0.f, 20.f), FLinearColor(1.f, 0.8f, 0.2f), 0.3f);
			return;
		}
	}
	IssueMove(Cell, false);
}

void ACmdPlayerController::IssueTargetCommand(ACmdEntity* Target, bool bForce)
{
	ACmdGameState* GS = GetCmdGameState();
	const bool bHostile = GS->AreHostile(GS->LocalTeamId, Target->TeamId);
	ACmdBuilding* TargetBuilding = Cast<ACmdBuilding>(Target);
	bool bAny = false;
	bool bAttack = false;

	for (ACmdEntity* E : GetSelection())
	{
		ACmdUnit* U = Cast<ACmdUnit>(E);
		if (!U || U->TeamId != GS->LocalTeamId)
		{
			continue;
		}
		if (U->IsEngineer() && TargetBuilding && (TargetBuilding->Def->bCapturable || TargetBuilding->TeamId == U->TeamId))
		{
			U->OrderCapture(TargetBuilding);
			bAny = true;
		}
		else if (U->IsHarvester() && TargetBuilding && TargetBuilding->TeamId == U->TeamId && TargetBuilding->Def->bRefinery)
		{
			U->OrderDock(TargetBuilding);
			bAny = true;
		}
		else if ((bHostile || bForce) && U->HasWeapon() && U->CanTarget(Target))
		{
			U->OrderAttack(Target);
			bAny = true;
			bAttack = true;
		}
		else
		{
			const FIntPoint Cell = TargetBuilding ? TargetBuilding->GetNearestAdjacentCell(U->GetCell()) : Target->GetCell();
			U->OrderMove(Cell, false);
			bAny = true;
		}
	}
	if (bAny)
	{
		ACmdEffect::SpawnFlash(GetWorld(), Target->GetActorLocation() + FVector(0.f, 0.f, 30.f), bAttack ? FLinearColor(1.f, 0.15f, 0.1f) : FLinearColor(0.2f, 1.f, 0.3f), 0.35f);
	}
}

void ACmdPlayerController::IssueMove(const FIntPoint& Cell, bool bAttackMove)
{
	const ACmdGameState* GS = GetCmdGameState();
	const UCmdGridSubsystem* Grid = GetWorld()->GetSubsystem<UCmdGridSubsystem>();
	TArray<ACmdUnit*> Units;
	for (ACmdEntity* E : GetSelection())
	{
		ACmdUnit* U = Cast<ACmdUnit>(E);
		if (U && U->TeamId == GS->LocalTeamId)
		{
			Units.Add(U);
		}
	}
	if (Units.Num() == 0 || !Grid)
	{
		return;
	}

	// Closest units claim the cells nearest the click: a tidy blob formation.
	const FVector Dest = Cmd::CellToWorld(Cell);
	Units.Sort([&Dest](const ACmdUnit& A, const ACmdUnit& B)
	{
		return FVector::DistSquared2D(A.GetActorLocation(), Dest) < FVector::DistSquared2D(B.GetActorLocation(), Dest);
	});
	TSet<FIntPoint> Used;
	for (ACmdUnit* U : Units)
	{
		FIntPoint Target = Cell;
		if (!U->IsAircraft())
		{
			Target = Grid->FindNearestPassable(Cell, 12, &Used);
		}
		else
		{
			Target = Cell + FIntPoint(Used.Num() % 3 - 1, Used.Num() / 3);
		}
		Used.Add(Target);
		U->OrderMove(Target, bAttackMove);
	}
	ACmdEffect::SpawnFlash(GetWorld(), Dest + FVector(0.f, 0.f, 20.f), bAttackMove ? FLinearColor(1.f, 0.5f, 0.1f) : FLinearColor(0.2f, 1.f, 0.3f), 0.3f);
}

void ACmdPlayerController::CommandStop()
{
	for (ACmdEntity* E : GetSelection())
	{
		if (ACmdUnit* U = Cast<ACmdUnit>(E))
		{
			if (U->TeamId == GetLocalTeam())
			{
				U->OrderStop();
			}
		}
	}
}

void ACmdPlayerController::CommandGuard()
{
	for (ACmdEntity* E : GetSelection())
	{
		if (ACmdUnit* U = Cast<ACmdUnit>(E))
		{
			if (U->TeamId == GetLocalTeam())
			{
				U->OrderGuard();
			}
		}
	}
}

void ACmdPlayerController::CommandDeploy()
{
	for (ACmdEntity* E : GetSelection())
	{
		if (ACmdUnit* U = Cast<ACmdUnit>(E))
		{
			if (U->TeamId == GetLocalTeam() && U->Def && !U->Def->DeploysInto.IsNone())
			{
				U->TryDeploy();
			}
		}
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
