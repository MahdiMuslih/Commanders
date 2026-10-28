#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Core/CmdTypes.h"
#include "CmdGridSubsystem.generated.h"

class ACmdBuilding;
class ACmdTerrainActor;
struct FCmdEntityDef;

enum class ECmdTerrain : uint8
{
	Clear,
	Rock,
	Water,
	Tree,
	Road
};

struct FCmdCell
{
	ECmdTerrain Terrain = ECmdTerrain::Clear;
	int32 Ore = 0;					// bales of ore remaining
	bool bGems = false;				// gem fields are worth more
	ACmdBuilding* Building = nullptr;
	uint8 ExploredMask = 0;			// one bit per alliance
};

/**
 * The tactical grid. Handles terrain passability, structure occupancy,
 * A* pathfinding, the ore economy and the per-alliance shroud.
 */
UCLASS()
class COMMANDERS_API UCmdGridSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	void InitGrid(int32 InWidth, int32 InHeight);

	int32 GetWidth() const { return Width; }
	int32 GetHeight() const { return Height; }
	bool IsInitialized() const { return Width > 0 && Height > 0; }

	bool IsInside(const FIntPoint& C) const { return C.X >= 0 && C.Y >= 0 && C.X < Width && C.Y < Height; }
	int32 ToIndex(const FIntPoint& C) const { return C.Y * Width + C.X; }
	FIntPoint FromIndex(int32 Index) const { return FIntPoint(Index % Width, Index / Width); }
	FCmdCell* GetCell(const FIntPoint& C) { return IsInside(C) ? &Cells[ToIndex(C)] : nullptr; }
	const FCmdCell* GetCell(const FIntPoint& C) const { return IsInside(C) ? &Cells[ToIndex(C)] : nullptr; }
	FIntPoint ClampCell(const FIntPoint& C) const;

	FVector GetMapCenter() const;
	FVector GetMapSizeWorld() const;

	// Terrain ---------------------------------------------------------------
	void SetTerrain(const FIntPoint& C, ECmdTerrain Terrain);
	void FillTerrain(const FIntRect& Rect, ECmdTerrain Terrain);
	void AddOreField(const FIntPoint& Center, float Radius, int32 Richness, bool bGems);

	bool IsPassable(const FIntPoint& C) const;
	bool IsBuildableCell(const FIntPoint& C) const;

	// Structures ------------------------------------------------------------
	void SetBuilding(const FIntPoint& Origin, const FIntPoint& Size, ACmdBuilding* Building);
	ACmdBuilding* GetBuildingAt(const FIntPoint& C) const;

	/**
	 * Placement test. When bRequireAdjacency is set the footprint must be within
	 * BuildRadius cells of one of the team's existing structures (classic rule).
	 */
	bool CanPlaceStructure(int32 TeamId, const FCmdEntityDef& Def, const FIntPoint& Origin, bool bRequireAdjacency, const AActor* IgnoreActor = nullptr) const;

	// Pathfinding -----------------------------------------------------------
	bool FindPath(const FIntPoint& Start, const FIntPoint& Goal, TArray<FIntPoint>& OutPath, int32 MaxExpanded = 8000) const;
	FIntPoint FindNearestPassable(const FIntPoint& Near, int32 MaxRadius = 16, const TSet<FIntPoint>* Exclude = nullptr) const;
	bool IsLineClear(const FIntPoint& A, const FIntPoint& B) const;

	// Ore --------------------------------------------------------------------
	bool FindNearestOre(const FIntPoint& From, FIntPoint& OutCell, int32 MaxRadius = 40) const;
	int32 HarvestOre(const FIntPoint& C, int32 Bales);
	int32 GetOre(const FIntPoint& C) const;
	void GrowOre();

	// Shroud -----------------------------------------------------------------
	void RevealCircle(int32 Alliance, const FVector& WorldCenter, float RadiusCells);
	void RevealAll(int32 Alliance);
	bool IsExplored(int32 Alliance, const FIntPoint& C) const;
	void SetShroudViewer(int32 Alliance) { ViewerAlliance = Alliance; }
	int32 GetShroudViewer() const { return ViewerAlliance; }

	/** Incremented whenever anything that the minimap draws changes. */
	uint32 GetRevision() const { return Revision; }

	void SetTerrainActor(ACmdTerrainActor* Actor) { TerrainActor = Actor; }

private:
	float Heuristic(const FIntPoint& A, const FIntPoint& B) const;

	int32 Width = 0;
	int32 Height = 0;
	TArray<FCmdCell> Cells;
	TArray<int32> OreCapacity;	// per cell max ore, used for regrowth
	int32 ViewerAlliance = 0;
	uint32 Revision = 1;

	TWeakObjectPtr<ACmdTerrainActor> TerrainActor;
};
