#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Core/CmdTypes.h"
#include "CmdDataRegistry.generated.h"

class FJsonObject;

/** One primitive in an entity's procedural model. */
struct FCmdVisualPart
{
	enum class EColorMode : uint8 { Team, TeamDark, Metal, Dark, Concrete, Glass, Light, Custom };

	FString Mesh = TEXT("cube");
	FVector Offset = FVector::ZeroVector;
	FVector Scale = FVector::OneVector;
	FRotator Rotation = FRotator::ZeroRotator;
	EColorMode ColorMode = EColorMode::Team;
	FLinearColor CustomColor = FLinearColor::White;
	bool bTurret = false;
	bool bRotor = false;
	bool bGlow = false;
	float Spin = 0.f;		// degrees / second around the part's own Z axis
};

struct FCmdWeaponDef
{
	FName Id;
	FString Name;
	float Damage = 10.f;
	float Range = 5.f;			// cells
	float MinRange = 0.f;		// cells
	float Cooldown = 1.f;		// seconds
	ECmdWarhead Warhead = ECmdWarhead::SmallArms;
	float ProjectileSpeed = 4000.f;	// uu/s, <= 0 means instant beam
	float ArcHeight = 0.f;		// uu, > 0 lobs the projectile
	float Splash = 0.f;			// cells
	int32 Burst = 1;
	bool bTargetGround = true;
	bool bTargetAir = false;
	FLinearColor Color = FLinearColor(1.f, 0.8f, 0.3f);
	float ProjectileScale = 0.08f;
	float MuzzleFlash = 1.f;
	FName Sound;			// sound bank id (see Audio/CmdSynth.cpp); derived from the warhead if empty
};

struct FCmdEntityDef
{
	FName Id;
	FString Name;
	FString Description;
	FName Faction = TEXT("any");
	ECmdCategory Category = ECmdCategory::Infantry;
	bool bIsBuilding = false;
	bool bBuildable = true;
	int32 Cost = 100;
	float BuildTime = 10.f;
	float MaxHealth = 100.f;
	ECmdArmor Armor = ECmdArmor::None;
	float Speed = 2.f;			// cells / second
	float TurnRate = 360.f;		// degrees / second
	float Sight = 6.f;			// cells
	float Radius = 40.f;		// uu, used for picking/separation
	float Height = 80.f;		// uu, health bar anchor
	FName Weapon;
	FVector MuzzleOffset = FVector(60.f, 0.f, 60.f);
	TArray<FName> Prereqs;
	int32 Power = 0;			// + produces, - consumes
	bool bRequiresPower = false;
	FIntPoint Footprint = FIntPoint(1, 1);
	TArray<ECmdCategory> Produces;
	bool bConstructionYard = false;
	bool bRefinery = false;
	bool bRadar = false;
	bool bHarvester = false;
	int32 HarvestCapacity = 28;
	bool bEngineer = false;
	bool bCapturable = true;
	bool bAircraft = false;
	float FlyHeight = 0.f;
	FName DeploysInto;
	FName FreeUnit;
	int32 Income = 0;			// credits granted every 10 seconds (oil derricks etc.)
	bool bIgnoreForVictory = false;
	int32 SortOrder = 0;
	TArray<FCmdVisualPart> Parts;

	bool IsInfantry() const { return !bIsBuilding && Category == ECmdCategory::Infantry; }
	bool HasTurret() const
	{
		for (const FCmdVisualPart& P : Parts) { if (P.bTurret) return true; }
		return false;
	}
};

struct FCmdFactionDef
{
	FName Id;
	FString Name;
	FString Motto;
	FString Description;
	FLinearColor Color = FLinearColor::White;
	FName MCV;
	FName Harvester;
	TArray<FName> AIDefenses;
	TArray<FName> StarterUnits;
};

struct FCmdMissionInfo
{
	FName Id;
	FName Campaign;
	FString Title;
	FString Subtitle;
	FString File;
	int32 Index = 0;
	TSharedPtr<FJsonObject> Json;
};

struct FCmdCampaignDef
{
	FName Id;
	FString Name;
	FName Faction;
	FString Tagline;
	FString Synopsis;
	FString Epilogue;
	FLinearColor Color = FLinearColor::White;
	TArray<FName> Missions;
};

/**
 * Loads all game definitions from Content/Data/*.json once per game instance.
 * Everything a designer touches (unit stats, models, missions, story, cinematics) lives in data.
 */
UCLASS()
class COMMANDERS_API UCmdDataRegistry : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	static UCmdDataRegistry* Get(const UObject* WorldContext);

	/** Reads and parses a JSON file relative to Content/Data. */
	static TSharedPtr<FJsonObject> LoadJsonFile(const FString& RelativePath);

	void Reload();

	const FCmdEntityDef* GetEntity(FName Id) const { return Entities.Find(Id); }
	const FCmdWeaponDef* GetWeapon(FName Id) const { return Weapons.Find(Id); }
	const FCmdFactionDef* GetFaction(FName Id) const { return Factions.Find(Id); }
	const FCmdMissionInfo* GetMission(FName Id) const { return Missions.Find(Id); }
	const FCmdCampaignDef* GetCampaign(FName Id) const;

	const TArray<FName>& GetEntityOrder() const { return EntityOrder; }
	const TArray<FCmdCampaignDef>& GetCampaigns() const { return Campaigns; }
	const TArray<FName>& GetFactionOrder() const { return FactionOrder; }

	/** Shared cinematics that are not tied to a single mission (menu intro etc). */
	TSharedPtr<FJsonObject> GetGlobalCinematic(FName Id) const;

	bool IsLoaded() const { return bLoaded; }

private:
	void LoadFactions();
	void LoadWeapons();
	void LoadEntities();
	void LoadCampaigns();

	static void ParsePart(const TSharedPtr<FJsonObject>& Json, FCmdVisualPart& Out);

	TMap<FName, FCmdEntityDef> Entities;
	TArray<FName> EntityOrder;
	TMap<FName, FCmdWeaponDef> Weapons;
	TMap<FName, FCmdFactionDef> Factions;
	TArray<FName> FactionOrder;
	TArray<FCmdCampaignDef> Campaigns;
	TMap<FName, FCmdMissionInfo> Missions;
	TSharedPtr<FJsonObject> GlobalCinematics;
	bool bLoaded = false;
};
