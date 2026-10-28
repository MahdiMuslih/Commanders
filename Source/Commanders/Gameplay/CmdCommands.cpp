#include "Gameplay/CmdCommands.h"
#include "Gameplay/CmdEntity.h"
#include "Gameplay/CmdUnit.h"
#include "Gameplay/CmdBuilding.h"
#include "Game/CmdGameState.h"
#include "Data/CmdDataRegistry.h"
#include "World/CmdGridSubsystem.h"
#include "Engine/World.h"

namespace
{
	TArray<ACmdUnit*> OwnedUnits(const FCmdCommand& Command, int32 TeamId)
	{
		TArray<ACmdUnit*> Out;
		for (ACmdEntity* E : Command.Units)
		{
			ACmdUnit* U = Cast<ACmdUnit>(E);
			if (IsValid(U) && U->IsAlive() && U->TeamId == TeamId)
			{
				Out.Add(U);
			}
		}
		return Out;
	}

	ACmdBuilding* OwnedBuilding(ACmdEntity* Entity, int32 TeamId)
	{
		ACmdBuilding* B = Cast<ACmdBuilding>(Entity);
		return (IsValid(B) && B->IsAlive() && B->TeamId == TeamId) ? B : nullptr;
	}

	bool MoveUnits(UWorld* World, TArray<ACmdUnit*> Units, const FIntPoint& Cell, bool bAttackMove)
	{
		const UCmdGridSubsystem* Grid = World->GetSubsystem<UCmdGridSubsystem>();
		if (Units.Num() == 0 || !Grid || !Grid->IsInside(Cell))
		{
			return false;
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
		return true;
	}

	bool TargetEntity(ACmdGameState* GS, const TArray<ACmdUnit*>& Units, ACmdEntity* Target, bool bForce, int32 TeamId)
	{
		if (!IsValid(Target) || !Target->IsAlive())
		{
			return false;
		}
		const bool bHostile = GS->AreHostile(TeamId, Target->TeamId);
		ACmdBuilding* TargetBuilding = Cast<ACmdBuilding>(Target);
		bool bAny = false;
		for (ACmdUnit* U : Units)
		{
			if (U->IsEngineer() && TargetBuilding && TargetBuilding->Def && (TargetBuilding->Def->bCapturable || TargetBuilding->TeamId == U->TeamId))
			{
				U->OrderCapture(TargetBuilding);
			}
			else if (U->IsHarvester() && TargetBuilding && TargetBuilding->Def && TargetBuilding->TeamId == U->TeamId && TargetBuilding->Def->bRefinery)
			{
				U->OrderDock(TargetBuilding);
			}
			else if ((bHostile || bForce) && U->HasWeapon() && U->CanTarget(Target))
			{
				U->OrderAttack(Target);
			}
			else
			{
				const FIntPoint Cell = TargetBuilding ? TargetBuilding->GetNearestAdjacentCell(U->GetCell()) : Target->GetCell();
				U->OrderMove(Cell, false);
			}
			bAny = true;
		}
		return bAny;
	}
}

namespace CmdCommands
{
	bool Execute(UWorld* World, int32 TeamId, const FCmdCommand& Command)
	{
		ACmdGameState* GS = World ? World->GetGameState<ACmdGameState>() : nullptr;
		const UCmdDataRegistry* Data = World ? UCmdDataRegistry::Get(World) : nullptr;
		const FCmdTeam* Team = GS ? GS->GetTeam(TeamId) : nullptr;
		if (!Team || !Data || Team->bDefeated || GS->Flow != ECmdFlowState::Playing)
		{
			return false;
		}

		switch (Command.Type)
		{
		case ECmdCommandType::Move:
		case ECmdCommandType::AttackMove:
			return MoveUnits(World, OwnedUnits(Command, TeamId), Command.Cell, Command.Type == ECmdCommandType::AttackMove);

		case ECmdCommandType::Target:
		case ECmdCommandType::ForceTarget:
			return TargetEntity(GS, OwnedUnits(Command, TeamId), Command.Target, Command.Type == ECmdCommandType::ForceTarget, TeamId);

		case ECmdCommandType::Harvest:
		{
			bool bAny = false;
			for (ACmdUnit* U : OwnedUnits(Command, TeamId))
			{
				if (U->IsHarvester())
				{
					U->OrderHarvest(&Command.Cell);
					bAny = true;
				}
			}
			return bAny;
		}

		case ECmdCommandType::Stop:
		case ECmdCommandType::Guard:
		{
			const TArray<ACmdUnit*> Units = OwnedUnits(Command, TeamId);
			for (ACmdUnit* U : Units)
			{
				Command.Type == ECmdCommandType::Stop ? U->OrderStop() : U->OrderGuard();
			}
			return Units.Num() > 0;
		}

		case ECmdCommandType::Deploy:
		{
			bool bAny = false;
			for (ACmdUnit* U : OwnedUnits(Command, TeamId))
			{
				if (U->Def && !U->Def->DeploysInto.IsNone())
				{
					bAny |= U->TryDeploy();
				}
			}
			return bAny;
		}

		case ECmdCommandType::Queue:
		{
			// Param = how many (shift-click queues five); only the first one announces.
			bool bAny = false;
			for (int32 i = 0; i < FMath::Clamp(Command.Param, 1, 5); ++i)
			{
				bAny |= GS->QueueProduction(TeamId, Command.Id, i == 0);
			}
			return bAny;
		}

		case ECmdCommandType::Cancel:
			GS->CancelProduction(TeamId, Command.Id);
			return true;

		case ECmdCommandType::Hold:
			if (Command.Param >= 0 && Command.Param < CmdConst::CategoryCount)
			{
				GS->ToggleHold(TeamId, static_cast<ECmdCategory>(Command.Param));
				return true;
			}
			return false;

		case ECmdCommandType::Place:
			if (GS->PlaceReadyStructure(TeamId, Command.Cell))
			{
				return true;
			}
			GS->AddMessage(TeamId, TEXT("Cannot build there."), FLinearColor(1.f, 0.4f, 0.3f), TEXT("deny"));
			return false;

		case ECmdCommandType::Sell:
			if (ACmdBuilding* B = OwnedBuilding(Command.Target, TeamId))
			{
				B->Sell();
				return true;
			}
			return false;

		case ECmdCommandType::Repair:
			if (ACmdBuilding* B = OwnedBuilding(Command.Target, TeamId))
			{
				B->ToggleRepair();
				return true;
			}
			return false;

		case ECmdCommandType::Rally:
			if (ACmdBuilding* B = OwnedBuilding(Command.Target, TeamId))
			{
				const UCmdGridSubsystem* Grid = World->GetSubsystem<UCmdGridSubsystem>();
				if (Grid && Grid->IsInside(Command.Cell))
				{
					B->RallyCell = Command.Cell;
					return true;
				}
			}
			return false;

		case ECmdCommandType::Primary:
			if (ACmdBuilding* B = OwnedBuilding(Command.Target, TeamId))
			{
				GS->SetPrimary(B);
				return true;
			}
			return false;

		default:
			return false;
		}
	}
}
