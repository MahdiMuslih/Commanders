#include "World/CmdGridSubsystem.h"
#include "World/CmdTerrainActor.h"
#include "Gameplay/CmdBuilding.h"
#include "Gameplay/CmdUnit.h"
#include "Game/CmdGameState.h"
#include "Data/CmdDataRegistry.h"
#include "Engine/World.h"
#include "Algo/Reverse.h"

namespace
{
	constexpr int32 BuildRadius = 3;

	struct FOpenNode
	{
		int32 Index;
		float F;
	};

	struct FOpenNodePredicate
	{
		bool operator()(const FOpenNode& A, const FOpenNode& B) const { return A.F < B.F; }
	};
}

void UCmdGridSubsystem::InitGrid(int32 InWidth, int32 InHeight)
{
	Width = FMath::Max(8, InWidth);
	Height = FMath::Max(8, InHeight);
	Cells.Reset();
	Cells.SetNum(Width * Height);
	OreCapacity.Reset();
	OreCapacity.SetNumZeroed(Width * Height);
	DirtyOre.Reset();
	++Revision;
}

FIntPoint UCmdGridSubsystem::ClampCell(const FIntPoint& C) const
{
	return FIntPoint(FMath::Clamp(C.X, 0, Width - 1), FMath::Clamp(C.Y, 0, Height - 1));
}

FVector UCmdGridSubsystem::GetMapCenter() const
{
	return FVector(Width * CmdConst::CellSize * 0.5f, Height * CmdConst::CellSize * 0.5f, 0.f);
}

FVector UCmdGridSubsystem::GetMapSizeWorld() const
{
	return FVector(Width * CmdConst::CellSize, Height * CmdConst::CellSize, 0.f);
}

void UCmdGridSubsystem::SetTerrain(const FIntPoint& C, ECmdTerrain Terrain)
{
	if (FCmdCell* Cell = GetCell(C))
	{
		Cell->Terrain = Terrain;
		if (Terrain != ECmdTerrain::Clear && Terrain != ECmdTerrain::Road)
		{
			Cell->Ore = 0;
			OreCapacity[ToIndex(C)] = 0;
		}
		++Revision;
	}
}

void UCmdGridSubsystem::FillTerrain(const FIntRect& Rect, ECmdTerrain Terrain)
{
	for (int32 Y = Rect.Min.Y; Y < Rect.Max.Y; ++Y)
	{
		for (int32 X = Rect.Min.X; X < Rect.Max.X; ++X)
		{
			SetTerrain(FIntPoint(X, Y), Terrain);
		}
	}
}

void UCmdGridSubsystem::AddOreField(const FIntPoint& Center, float Radius, int32 Richness, bool bGems)
{
	const int32 R = FMath::CeilToInt(Radius);
	for (int32 Y = -R; Y <= R; ++Y)
	{
		for (int32 X = -R; X <= R; ++X)
		{
			const FIntPoint C = Center + FIntPoint(X, Y);
			FCmdCell* Cell = GetCell(C);
			if (!Cell || Cell->Terrain != ECmdTerrain::Clear)
			{
				continue;
			}
			const float D = FMath::Sqrt(static_cast<float>(X * X + Y * Y));
			if (D > Radius)
			{
				continue;
			}
			// Denser in the middle, ragged at the edge.
			const float Falloff = 1.f - (D / (Radius + 0.5f));
			const int32 Amount = FMath::Max(1, FMath::RoundToInt(Richness * (0.35f + 0.65f * Falloff)));
			// Deterministic raggedness (hash of the cell) so every networked machine generates the same field.
			const uint32 Hash = HashCombine(GetTypeHash(C.X * 7919), GetTypeHash(C.Y * 104729 + 17));
			if (D > Radius - 1.f && (Hash % 100u) < 35u)
			{
				continue;
			}
			Cell->Ore = FMath::Max(Cell->Ore, Amount);
			Cell->bGems = bGems;
			OreCapacity[ToIndex(C)] = FMath::Max(OreCapacity[ToIndex(C)], Amount);
		}
	}
	++Revision;
}

bool UCmdGridSubsystem::IsPassable(const FIntPoint& C) const
{
	const FCmdCell* Cell = GetCell(C);
	return Cell && (Cell->Terrain == ECmdTerrain::Clear || Cell->Terrain == ECmdTerrain::Road) && Cell->Building == nullptr;
}

bool UCmdGridSubsystem::IsBuildableCell(const FIntPoint& C) const
{
	const FCmdCell* Cell = GetCell(C);
	return Cell && (Cell->Terrain == ECmdTerrain::Clear || Cell->Terrain == ECmdTerrain::Road) && Cell->Building == nullptr && Cell->Ore == 0;
}

void UCmdGridSubsystem::SetBuilding(const FIntPoint& Origin, const FIntPoint& Size, ACmdBuilding* Building)
{
	for (int32 Y = 0; Y < Size.Y; ++Y)
	{
		for (int32 X = 0; X < Size.X; ++X)
		{
			if (FCmdCell* Cell = GetCell(Origin + FIntPoint(X, Y)))
			{
				if (Building == nullptr || Cell->Building == nullptr)
				{
					Cell->Building = Building;
				}
			}
		}
	}
	++Revision;
}

ACmdBuilding* UCmdGridSubsystem::GetBuildingAt(const FIntPoint& C) const
{
	const FCmdCell* Cell = GetCell(C);
	return Cell ? Cell->Building : nullptr;
}

bool UCmdGridSubsystem::CanPlaceStructure(int32 TeamId, const FCmdEntityDef& Def, const FIntPoint& Origin, bool bRequireAdjacency, const AActor* IgnoreActor) const
{
	for (int32 Y = 0; Y < Def.Footprint.Y; ++Y)
	{
		for (int32 X = 0; X < Def.Footprint.X; ++X)
		{
			if (!IsBuildableCell(Origin + FIntPoint(X, Y)))
			{
				return false;
			}
		}
	}

	const ACmdGameState* GS = GetWorld() ? GetWorld()->GetGameState<ACmdGameState>() : nullptr;
	if (!GS)
	{
		return true;
	}

	// Ground units standing on the footprint block placement.
	const FIntRect Footprint(Origin, Origin + Def.Footprint);
	for (const ACmdEntity* E : GS->Entities)
	{
		if (!E || E == IgnoreActor || E->IsBuilding() || E->IsAircraft() || !E->IsAlive())
		{
			continue;
		}
		const FIntPoint C = E->GetCell();
		if (C.X >= Footprint.Min.X && C.X < Footprint.Max.X && C.Y >= Footprint.Min.Y && C.Y < Footprint.Max.Y)
		{
			return false;
		}
	}

	if (!bRequireAdjacency)
	{
		return true;
	}

	const FIntRect Expanded(Origin - FIntPoint(BuildRadius, BuildRadius), Origin + Def.Footprint + FIntPoint(BuildRadius, BuildRadius));
	for (const ACmdEntity* E : GS->Entities)
	{
		const ACmdBuilding* B = Cast<ACmdBuilding>(E);
		if (!B || B->TeamId != TeamId || !B->IsAlive() || !B->Def)
		{
			continue;
		}
		const FIntRect BR(B->Origin, B->Origin + B->Def->Footprint);
		const bool bOverlap = BR.Min.X < Expanded.Max.X && BR.Max.X > Expanded.Min.X && BR.Min.Y < Expanded.Max.Y && BR.Max.Y > Expanded.Min.Y;
		if (bOverlap)
		{
			return true;
		}
	}
	return false;
}

float UCmdGridSubsystem::Heuristic(const FIntPoint& A, const FIntPoint& B) const
{
	const int32 DX = FMath::Abs(A.X - B.X);
	const int32 DY = FMath::Abs(A.Y - B.Y);
	return static_cast<float>(FMath::Max(DX, DY)) + 0.41421356f * static_cast<float>(FMath::Min(DX, DY));
}

bool UCmdGridSubsystem::FindPath(const FIntPoint& InStart, const FIntPoint& InGoal, TArray<FIntPoint>& OutPath, int32 MaxExpanded) const
{
	OutPath.Reset();
	if (!IsInitialized())
	{
		return false;
	}

	const FIntPoint Start = ClampCell(InStart);
	FIntPoint Goal = ClampCell(InGoal);
	if (!IsPassable(Goal))
	{
		Goal = FindNearestPassable(Goal, 12);
	}
	if (Start == Goal)
	{
		OutPath.Add(Goal);
		return true;
	}

	const int32 N = Width * Height;
	TArray<float> G;
	G.Init(TNumericLimits<float>::Max(), N);
	TArray<int32> Parent;
	Parent.Init(INDEX_NONE, N);
	TBitArray<> Closed(false, N);

	TArray<FOpenNode> Open;
	const int32 StartIndex = ToIndex(Start);
	const int32 GoalIndex = ToIndex(Goal);
	G[StartIndex] = 0.f;
	Open.HeapPush({ StartIndex, Heuristic(Start, Goal) }, FOpenNodePredicate());

	int32 BestIndex = StartIndex;
	float BestH = Heuristic(Start, Goal);
	int32 Expanded = 0;

	static const FIntPoint Dirs[8] = {
		FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1),
		FIntPoint(1, 1), FIntPoint(1, -1), FIntPoint(-1, 1), FIntPoint(-1, -1)
	};

	bool bFound = false;
	while (Open.Num() > 0 && Expanded < MaxExpanded)
	{
		FOpenNode Node;
		Open.HeapPop(Node, FOpenNodePredicate());
		if (Closed[Node.Index])
		{
			continue;
		}
		Closed[Node.Index] = true;
		++Expanded;

		if (Node.Index == GoalIndex)
		{
			bFound = true;
			BestIndex = GoalIndex;
			break;
		}

		const FIntPoint Current = FromIndex(Node.Index);
		const float H = Heuristic(Current, Goal);
		if (H < BestH)
		{
			BestH = H;
			BestIndex = Node.Index;
		}

		for (int32 D = 0; D < 8; ++D)
		{
			const FIntPoint Next = Current + Dirs[D];
			if (!IsPassable(Next))
			{
				continue;
			}
			const bool bDiagonal = D >= 4;
			if (bDiagonal && (!IsPassable(FIntPoint(Current.X + Dirs[D].X, Current.Y)) || !IsPassable(FIntPoint(Current.X, Current.Y + Dirs[D].Y))))
			{
				continue;	// no corner cutting
			}
			const int32 NextIndex = ToIndex(Next);
			if (Closed[NextIndex])
			{
				continue;
			}
			float StepCost = bDiagonal ? 1.41421356f : 1.f;
			if (Cells[NextIndex].Terrain == ECmdTerrain::Road)
			{
				StepCost *= 0.8f;
			}
			const float NewG = G[Node.Index] + StepCost;
			if (NewG < G[NextIndex])
			{
				G[NextIndex] = NewG;
				Parent[NextIndex] = Node.Index;
				Open.HeapPush({ NextIndex, NewG + Heuristic(Next, Goal) }, FOpenNodePredicate());
			}
		}
	}

	// Walk back from the goal (or the closest point we reached).
	TArray<FIntPoint> Raw;
	for (int32 Index = BestIndex; Index != INDEX_NONE; Index = Parent[Index])
	{
		Raw.Add(FromIndex(Index));
		if (Index == StartIndex)
		{
			break;
		}
	}
	Algo::Reverse(Raw);

	// String-pull: drop waypoints that can be skipped by walking in a straight line.
	if (Raw.Num() > 2)
	{
		int32 Anchor = 0;
		for (int32 i = 1; i < Raw.Num(); ++i)
		{
			if (i == Raw.Num() - 1 || !IsLineClear(Raw[Anchor], Raw[i + 1]))
			{
				OutPath.Add(Raw[i]);
				Anchor = i;
			}
		}
	}
	else
	{
		for (int32 i = 1; i < Raw.Num(); ++i)
		{
			OutPath.Add(Raw[i]);
		}
	}

	if (OutPath.Num() == 0)
	{
		OutPath.Add(Goal);
	}
	return bFound;
}

bool UCmdGridSubsystem::IsLineClear(const FIntPoint& A, const FIntPoint& B) const
{
	// Sample the segment densely and test each touched cell (plus its thickness).
	const FVector2D PA(A.X + 0.5f, A.Y + 0.5f);
	const FVector2D PB(B.X + 0.5f, B.Y + 0.5f);
	const float Length = FVector2D::Distance(PA, PB);
	const int32 Steps = FMath::Max(1, FMath::CeilToInt(Length * 3.f));
	for (int32 i = 0; i <= Steps; ++i)
	{
		const FVector2D P = FMath::Lerp(PA, PB, static_cast<float>(i) / Steps);
		static const FVector2D Offsets[4] = { FVector2D(0.3f, 0.3f), FVector2D(-0.3f, 0.3f), FVector2D(0.3f, -0.3f), FVector2D(-0.3f, -0.3f) };
		for (const FVector2D& Off : Offsets)
		{
			const FIntPoint C(FMath::FloorToInt(P.X + Off.X), FMath::FloorToInt(P.Y + Off.Y));
			if (!IsPassable(C))
			{
				return false;
			}
		}
	}
	return true;
}

FIntPoint UCmdGridSubsystem::FindNearestPassable(const FIntPoint& Near, int32 MaxRadius, const TSet<FIntPoint>* Exclude) const
{
	const FIntPoint Origin = ClampCell(Near);
	if (IsPassable(Origin) && (!Exclude || !Exclude->Contains(Origin)))
	{
		return Origin;
	}
	for (int32 R = 1; R <= MaxRadius; ++R)
	{
		FIntPoint Best(-1, -1);
		float BestDist = TNumericLimits<float>::Max();
		for (int32 Y = -R; Y <= R; ++Y)
		{
			for (int32 X = -R; X <= R; ++X)
			{
				if (FMath::Abs(X) != R && FMath::Abs(Y) != R)
				{
					continue;	// ring only
				}
				const FIntPoint C = Origin + FIntPoint(X, Y);
				if (!IsPassable(C) || (Exclude && Exclude->Contains(C)))
				{
					continue;
				}
				const float D = static_cast<float>(X * X + Y * Y);
				if (D < BestDist)
				{
					BestDist = D;
					Best = C;
				}
			}
		}
		if (Best.X >= 0)
		{
			return Best;
		}
	}
	return Origin;
}

bool UCmdGridSubsystem::FindNearestOre(const FIntPoint& From, FIntPoint& OutCell, int32 MaxRadius) const
{
	float BestScore = TNumericLimits<float>::Max();
	bool bFound = false;
	for (int32 Y = -MaxRadius; Y <= MaxRadius; ++Y)
	{
		for (int32 X = -MaxRadius; X <= MaxRadius; ++X)
		{
			const FIntPoint C = From + FIntPoint(X, Y);
			const FCmdCell* Cell = GetCell(C);
			if (!Cell || Cell->Ore <= 0 || Cell->Building)
			{
				continue;
			}
			// Prefer rich and gem cells slightly.
			const float Score = FMath::Sqrt(static_cast<float>(X * X + Y * Y)) - (Cell->bGems ? 2.f : 0.f) - Cell->Ore * 0.05f;
			if (Score < BestScore)
			{
				BestScore = Score;
				OutCell = C;
				bFound = true;
			}
		}
	}
	return bFound;
}

int32 UCmdGridSubsystem::HarvestOre(const FIntPoint& C, int32 Bales)
{
	FCmdCell* Cell = GetCell(C);
	if (!Cell || Cell->Ore <= 0)
	{
		return 0;
	}
	const int32 Taken = FMath::Min(Cell->Ore, Bales);
	Cell->Ore -= Taken;
	MarkOreDirty(ToIndex(C));
	++Revision;
	if (ACmdTerrainActor* T = TerrainActor.Get())
	{
		T->OnOreChanged(C);
	}
	// Gems are worth double.
	return Cell->bGems ? Taken * 2 : Taken;
}

int32 UCmdGridSubsystem::GetOre(const FIntPoint& C) const
{
	const FCmdCell* Cell = GetCell(C);
	return Cell ? Cell->Ore : 0;
}

void UCmdGridSubsystem::GrowOre()
{
	// Slow regrowth of depleted fields keeps long games going.
	const int32 Samples = FMath::Max(16, (Width * Height) / 40);
	for (int32 i = 0; i < Samples; ++i)
	{
		const int32 Index = FMath::RandRange(0, Cells.Num() - 1);
		FCmdCell& Cell = Cells[Index];
		if (OreCapacity[Index] > 0 && Cell.Ore < OreCapacity[Index] && Cell.Building == nullptr)
		{
			Cell.Ore = FMath::Min(OreCapacity[Index], Cell.Ore + 1);
			MarkOreDirty(Index);
			if (ACmdTerrainActor* T = TerrainActor.Get())
			{
				T->OnOreChanged(FromIndex(Index));
			}
		}
	}
	++Revision;
}

void UCmdGridSubsystem::MarkOreDirty(int32 Index)
{
	if (bTrackOreChanges)
	{
		DirtyOre.Add(Index);
	}
}

void UCmdGridSubsystem::ConsumeOreChanges(TArray<FCmdOreDelta>& OutChanges)
{
	OutChanges.Reset(DirtyOre.Num());
	for (const int32 Index : DirtyOre)
	{
		if (Cells.IsValidIndex(Index))
		{
			FCmdOreDelta Delta;
			Delta.Index = Index;
			Delta.Amount = static_cast<uint8>(FMath::Clamp(Cells[Index].Ore, 0, 255));
			OutChanges.Add(Delta);
		}
	}
	DirtyOre.Reset();
}

void UCmdGridSubsystem::ApplyOreDelta(int32 Index, int32 Amount)
{
	if (!Cells.IsValidIndex(Index) || Cells[Index].Ore == Amount)
	{
		return;
	}
	Cells[Index].Ore = Amount;
	++Revision;
	if (ACmdTerrainActor* T = TerrainActor.Get())
	{
		T->OnOreChanged(FromIndex(Index));
	}
}

void UCmdGridSubsystem::RevealCircle(int32 Alliance, const FVector& WorldCenter, float RadiusCells)
{
	if (Alliance < 0 || Alliance >= CmdConst::MaxAlliances || !IsInitialized())
	{
		return;
	}
	const uint8 Bit = static_cast<uint8>(1u << Alliance);
	const FVector2D Center(WorldCenter.X / CmdConst::CellSize, WorldCenter.Y / CmdConst::CellSize);
	const int32 R = FMath::CeilToInt(RadiusCells);
	const FIntPoint CC(FMath::FloorToInt(Center.X), FMath::FloorToInt(Center.Y));
	const float R2 = RadiusCells * RadiusCells;
	for (int32 Y = CC.Y - R; Y <= CC.Y + R; ++Y)
	{
		for (int32 X = CC.X - R; X <= CC.X + R; ++X)
		{
			const FIntPoint C(X, Y);
			if (!IsInside(C))
			{
				continue;
			}
			const float DX = X + 0.5f - Center.X;
			const float DY = Y + 0.5f - Center.Y;
			if (DX * DX + DY * DY > R2)
			{
				continue;
			}
			FCmdCell& Cell = Cells[ToIndex(C)];
			if ((Cell.ExploredMask & Bit) == 0)
			{
				Cell.ExploredMask |= Bit;
				++Revision;
				if (Alliance == ViewerAlliance)
				{
					if (ACmdTerrainActor* T = TerrainActor.Get())
					{
						T->OnExplored(C);
					}
				}
			}
		}
	}
}

void UCmdGridSubsystem::RevealAll(int32 Alliance)
{
	if (Alliance < 0 || Alliance >= CmdConst::MaxAlliances)
	{
		return;
	}
	const uint8 Bit = static_cast<uint8>(1u << Alliance);
	for (int32 i = 0; i < Cells.Num(); ++i)
	{
		if ((Cells[i].ExploredMask & Bit) == 0)
		{
			Cells[i].ExploredMask |= Bit;
			if (Alliance == ViewerAlliance)
			{
				if (ACmdTerrainActor* T = TerrainActor.Get())
				{
					T->OnExplored(FromIndex(i));
				}
			}
		}
	}
	++Revision;
}

bool UCmdGridSubsystem::IsExplored(int32 Alliance, const FIntPoint& C) const
{
	if (Alliance < 0 || Alliance >= CmdConst::MaxAlliances)
	{
		return true;	// spectators see everything
	}
	const FCmdCell* Cell = GetCell(C);
	return Cell && (Cell->ExploredMask & (1u << Alliance)) != 0;
}
