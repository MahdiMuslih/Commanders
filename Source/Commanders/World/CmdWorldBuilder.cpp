#include "World/CmdWorldBuilder.h"
#include "World/CmdGridSubsystem.h"
#include "World/CmdTerrainActor.h"
#include "World/CmdEnvironmentActor.h"
#include "Data/CmdDataRegistry.h"
#include "Core/CmdJson.h"
#include "Engine/World.h"
#include "Engine/DirectionalLight.h"
#include "EngineUtils.h"

namespace
{
	void PaintTerrain(UCmdGridSubsystem* Grid, const TSharedPtr<FJsonObject>& Map, const TArray<FIntRect>& KeepClear)
	{
		auto ForEachRect = [&Map](const TCHAR* Key, TFunctionRef<void(const FIntRect&)> Fn)
		{
			for (const TSharedPtr<FJsonValue>& V : CmdJson::Arr(Map, Key))
			{
				const TArray<double> N = CmdJson::Numbers(V);
				if (N.Num() >= 4)
				{
					const int32 X = FMath::FloorToInt(N[0]);
					const int32 Y = FMath::FloorToInt(N[1]);
					Fn(FIntRect(X, Y, X + FMath::Max(1, FMath::FloorToInt(N[2])), Y + FMath::Max(1, FMath::FloorToInt(N[3]))));
				}
				else if (N.Num() >= 2)
				{
					const int32 X = FMath::FloorToInt(N[0]);
					const int32 Y = FMath::FloorToInt(N[1]);
					Fn(FIntRect(X, Y, X + 1, Y + 1));
				}
			}
		};

		auto IsKeptClear = [&KeepClear](const FIntPoint& C)
		{
			for (const FIntRect& R : KeepClear)
			{
				if (C.X >= R.Min.X && C.X < R.Max.X && C.Y >= R.Min.Y && C.Y < R.Max.Y)
				{
					return true;
				}
			}
			return false;
		};

		auto Paint = [&](const FIntRect& R, ECmdTerrain Type)
		{
			for (int32 Y = R.Min.Y; Y < R.Max.Y; ++Y)
			{
				for (int32 X = R.Min.X; X < R.Max.X; ++X)
				{
					const FIntPoint C(X, Y);
					if (Type == ECmdTerrain::Road || !IsKeptClear(C))
					{
						Grid->SetTerrain(C, Type);
					}
				}
			}
		};

		// Roads go last so they can bridge rivers.
		ForEachRect(TEXT("water"), [&](const FIntRect& R) { Paint(R, ECmdTerrain::Water); });
		ForEachRect(TEXT("rocks"), [&](const FIntRect& R) { Paint(R, ECmdTerrain::Rock); });
		ForEachRect(TEXT("trees"), [&](const FIntRect& R) { Paint(R, ECmdTerrain::Tree); });
		ForEachRect(TEXT("roads"), [&](const FIntRect& R) { Paint(R, ECmdTerrain::Road); });

		// Optional random scatter for hand-made maps.
		FRandomStream Rng(CmdJson::Int(Map, TEXT("seed"), 1));
		const double TreeDensity = CmdJson::Num(Map, TEXT("random_trees"), 0.0);
		const double RockDensity = CmdJson::Num(Map, TEXT("random_rocks"), 0.0);
		if (TreeDensity > 0.0 || RockDensity > 0.0)
		{
			TArray<FIntRect> Clear;
			ForEachRect(TEXT("clear"), [&](const FIntRect& R) { Clear.Add(R); });
			for (int32 Y = 1; Y < Grid->GetHeight() - 1; ++Y)
			{
				for (int32 X = 1; X < Grid->GetWidth() - 1; ++X)
				{
					const FIntPoint C(X, Y);
					if (IsKeptClear(C) || Grid->GetCell(C)->Terrain != ECmdTerrain::Clear)
					{
						continue;
					}
					bool bInClear = false;
					for (const FIntRect& R : Clear)
					{
						bInClear |= C.X >= R.Min.X && C.X < R.Max.X && C.Y >= R.Min.Y && C.Y < R.Max.Y;
					}
					if (bInClear)
					{
						continue;
					}
					const float Roll = Rng.FRand();
					if (Roll < TreeDensity)
					{
						Grid->SetTerrain(C, ECmdTerrain::Tree);
					}
					else if (Roll < TreeDensity + RockDensity)
					{
						Grid->SetTerrain(C, ECmdTerrain::Rock);
					}
				}
			}
		}

		for (const TSharedPtr<FJsonValue>& V : CmdJson::Arr(Map, TEXT("ore")))
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			Grid->AddOreField(CmdJson::Cell(O, TEXT("at"), FIntPoint(0, 0)), CmdJson::Num(O, TEXT("radius"), 3.0), CmdJson::Int(O, TEXT("richness"), 8), CmdJson::Bool(O, TEXT("gems"), false));
		}

		// Border cliffs keep units on the map.
		if (CmdJson::Bool(Map, TEXT("border"), false))
		{
			Grid->FillTerrain(FIntRect(0, 0, Grid->GetWidth(), 1), ECmdTerrain::Rock);
			Grid->FillTerrain(FIntRect(0, Grid->GetHeight() - 1, Grid->GetWidth(), Grid->GetHeight()), ECmdTerrain::Rock);
			Grid->FillTerrain(FIntRect(0, 0, 1, Grid->GetHeight()), ECmdTerrain::Rock);
			Grid->FillTerrain(FIntRect(Grid->GetWidth() - 1, 0, Grid->GetWidth(), Grid->GetHeight()), ECmdTerrain::Rock);
		}
}
}

namespace CmdWorldBuilder
{
	void BuildGrid(UWorld* World, const TSharedPtr<FJsonObject>& Scenario)
	{
		UCmdGridSubsystem* Grid = World ? World->GetSubsystem<UCmdGridSubsystem>() : nullptr;
		const UCmdDataRegistry* Data = World ? UCmdDataRegistry::Get(World) : nullptr;
		if (!Grid || !Data)
		{
			return;
		}
		const TSharedPtr<FJsonObject> Map = CmdJson::Obj(Scenario, TEXT("map"));
		Grid->InitGrid(CmdJson::Int(Map, TEXT("width"), 64), CmdJson::Int(Map, TEXT("height"), 64));

		// Keep decoration away from where entities start.
		TArray<FIntRect> KeepClear;
		for (const TSharedPtr<FJsonValue>& V : CmdJson::Arr(Scenario, TEXT("entities")))
		{
			const TSharedPtr<FJsonObject> E = V->AsObject();
			const FIntPoint At = CmdJson::Cell(E, TEXT("at"), FIntPoint(0, 0));
			const FCmdEntityDef* Def = Data->GetEntity(CmdJson::Name(E, TEXT("type")));
			const FIntPoint Footprint = Def ? Def->Footprint : FIntPoint(1, 1);
			KeepClear.Add(FIntRect(At - FIntPoint(1, 1), At + Footprint + FIntPoint(1, 2)));
		}
		PaintTerrain(Grid, Map, KeepClear);
	}

	FIntPoint FindTeamStart(const TSharedPtr<FJsonObject>& Scenario, int32 TeamId)
	{
		for (const TSharedPtr<FJsonValue>& V : CmdJson::Arr(Scenario, TEXT("entities")))
		{
			const TSharedPtr<FJsonObject> E = V->AsObject();
			if (CmdJson::Int(E, TEXT("team"), 0) == TeamId)
			{
				return CmdJson::Cell(E, TEXT("at"), FIntPoint(0, 0));
			}
		}
		return FIntPoint(-1, -1);
	}

	FCmdBuiltWorld BuildEnvironment(UWorld* World, const TSharedPtr<FJsonObject>& Scenario, bool bShroud, int32 ViewerAlliance, int32 LocalTeam)
	{
		FCmdBuiltWorld Out;
		UCmdGridSubsystem* Grid = World ? World->GetSubsystem<UCmdGridSubsystem>() : nullptr;
		if (!Grid)
		{
			return Out;
		}
		BuildGrid(World, Scenario);
		Grid->SetShroudViewer(bShroud ? ViewerAlliance : -1);

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const TSharedPtr<FJsonObject> Map = CmdJson::Obj(Scenario, TEXT("map"));
		const FCmdMapTheme Theme = FCmdMapTheme::FromName(CmdJson::Str(Map, TEXT("theme"), TEXT("temperate")));
		Out.Terrain = World->SpawnActor<ACmdTerrainActor>(ACmdTerrainActor::StaticClass(), FTransform::Identity, Params);
		Out.Terrain->Build(Theme, bShroud);

		bool bHasSun = false;
		for (TActorIterator<ADirectionalLight> It(World); It; ++It)
		{
			bHasSun = true;
		}
		if (!bHasSun)
		{
			Out.Environment = World->SpawnActor<ACmdEnvironmentActor>(ACmdEnvironmentActor::StaticClass(), FTransform::Identity, Params);
			Out.Environment->ApplyTheme(Theme);
		}

		// Initial camera: explicit, else the local team's first entity, else the map centre.
		const FIntPoint CamCell = CmdJson::Cell(Scenario, TEXT("camera"));
		const FIntPoint Start = FindTeamStart(Scenario, LocalTeam);
		if (CamCell.X >= 0)
		{
			Out.CameraStart = Cmd::CellToWorld(CamCell);
		}
		else if (LocalTeam != INDEX_NONE && Start.X >= 0)
		{
			Out.CameraStart = Cmd::CellToWorld(Start);
		}
		else
		{
			Out.CameraStart = Grid->GetMapCenter();
		}
		return Out;
	}
}
