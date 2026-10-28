#pragma once

#include "CoreMinimal.h"
#include "CmdTypes.generated.h"

/** Armor classes. Damage dealt is Weapon.Damage * Multiplier[Warhead][Armor]. */
UENUM(BlueprintType)
enum class ECmdArmor : uint8
{
	None,		// infantry
	Light,		// scouts, harvesters, light vehicles
	Heavy,		// tanks
	Structure,	// buildings
	Aircraft,	// helicopters / gunships
	Count UMETA(Hidden)
};

UENUM(BlueprintType)
enum class ECmdWarhead : uint8
{
	SmallArms,
	ArmorPiercing,
	HighExplosive,
	Fire,
	Electric,
	Flak,
	Count UMETA(Hidden)
};

/** Production categories - also the four sidebar tabs. */
UENUM(BlueprintType)
enum class ECmdCategory : uint8
{
	Structure,
	Infantry,
	Vehicle,
	Aircraft,
	Count UMETA(Hidden)
};

UENUM(BlueprintType)
enum class ECmdOrder : uint8
{
	Idle,
	Move,
	AttackMove,
	Attack,
	Guard,
	Harvest,
	Capture,
	Dock
};

/** High level flow of a play session. */
UENUM(BlueprintType)
enum class ECmdFlowState : uint8
{
	None,
	MainMenu,
	Briefing,
	Cinematic,
	Playing,
	Victory,
	Defeat,
	Lobby		// multiplayer: players gathering before the match starts
};

UENUM(BlueprintType)
enum class ECmdSessionMode : uint8
{
	Menu,
	Campaign,
	Skirmish,
	MultiplayerHost,	// listen server: lobby, then a networked skirmish
	MultiplayerClient	// connected to someone else's game
};

/** Per-player outcome of a networked match. */
UENUM(BlueprintType)
enum class ECmdMatchResult : uint8
{
	None,
	Victory,
	Defeat
};

/** Every player action that changes the simulation travels to the server as one of these. */
UENUM(BlueprintType)
enum class ECmdCommandType : uint8
{
	Move,
	AttackMove,
	Target,			// smart right-click on an entity: attack / capture / dock / follow
	ForceTarget,	// ctrl + right-click: attack anything
	Stop,
	Guard,
	Deploy,
	Harvest,
	Queue,
	Cancel,
	Hold,
	Place,
	Sell,
	Repair,
	Rally,
	Primary
};

namespace CmdConst
{
	/** World units per grid cell. Everything in data files is expressed in cells. */
	constexpr float CellSize = 200.f;
	constexpr int32 MaxAlliances = 8;
	constexpr int32 CategoryCount = static_cast<int32>(ECmdCategory::Count);
	constexpr float OreValuePerBale = 25.f;
	constexpr int32 MaxPlayers = 4;
}

namespace Cmd
{
	inline FVector CellToWorld(const FIntPoint& Cell, float Z = 0.f)
	{
		return FVector((Cell.X + 0.5f) * CmdConst::CellSize, (Cell.Y + 0.5f) * CmdConst::CellSize, Z);
	}

	inline FVector CellToWorld(float X, float Y, float Z = 0.f)
	{
		return FVector(X * CmdConst::CellSize, Y * CmdConst::CellSize, Z);
	}

	inline FIntPoint WorldToCell(const FVector& Location)
	{
		return FIntPoint(FMath::FloorToInt(Location.X / CmdConst::CellSize), FMath::FloorToInt(Location.Y / CmdConst::CellSize));
	}

	COMMANDERS_API float DamageMultiplier(ECmdWarhead Warhead, ECmdArmor Armor);
	COMMANDERS_API ECmdArmor ParseArmor(const FString& Text);
	COMMANDERS_API ECmdWarhead ParseWarhead(const FString& Text);
	COMMANDERS_API ECmdCategory ParseCategory(const FString& Text);
	COMMANDERS_API FString CategoryLabel(ECmdCategory Category);
	COMMANDERS_API FLinearColor ParseNamedColor(const FString& Text, const FLinearColor& Fallback);
}
