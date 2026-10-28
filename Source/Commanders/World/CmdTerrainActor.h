#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CmdTerrainActor.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMeshComponent;
class UCmdGridSubsystem;

/** Colour palette + lighting mood for a map. */
struct FCmdMapTheme
{
	FString Id = TEXT("temperate");
	FLinearColor Ground = FLinearColor(0.12f, 0.2f, 0.07f);
	FLinearColor GroundPatch = FLinearColor(0.16f, 0.22f, 0.08f);
	FLinearColor Outer = FLinearColor(0.06f, 0.09f, 0.04f);
	FLinearColor Rock = FLinearColor(0.25f, 0.23f, 0.21f);
	FLinearColor TreeTop = FLinearColor(0.04f, 0.16f, 0.04f);
	FLinearColor Trunk = FLinearColor(0.14f, 0.08f, 0.04f);
	FLinearColor Water = FLinearColor(0.03f, 0.1f, 0.2f);
	FLinearColor Road = FLinearColor(0.18f, 0.16f, 0.13f);
	FLinearColor Ore = FLinearColor(0.85f, 0.6f, 0.08f);
	FLinearColor Gems = FLinearColor(0.2f, 0.45f, 0.95f);
	FLinearColor SunColor = FLinearColor(1.f, 0.95f, 0.85f);
	float SunPitch = -48.f;
	float SunYaw = 35.f;
	float SunIntensity = 6.f;
	float SkyIntensity = 1.f;
	float FogDensity = 0.01f;

	static FCmdMapTheme FromName(const FString& Name);
};

/**
 * Renders the battlefield: ground, cliffs, forests, water, ore and the black shroud.
 * All decoration is instanced so even 128x128 maps are cheap.
 */
UCLASS()
class COMMANDERS_API ACmdTerrainActor : public AActor
{
	GENERATED_BODY()

public:
	ACmdTerrainActor();

	void Build(const FCmdMapTheme& Theme, bool bShroud);

	void OnOreChanged(const FIntPoint& Cell);
	void SetShroudVisible(bool bVisible);
	void OnExplored(const FIntPoint& Cell);

	const FCmdMapTheme& GetTheme() const { return Theme; }

private:
	UInstancedStaticMeshComponent* MakeISM(const FString& Shape, const FLinearColor& Color, bool bShadows);
	FTransform OreTransform(const FIntPoint& Cell, int32 Amount) const;

	UPROPERTY()
	TObjectPtr<USceneComponent> Root;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Ground;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> OuterGround;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> PatchISM;
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> RockISM;
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> TrunkISM;
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> TreeTopISM;
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> WaterISM;
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> RoadISM;
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> OreISM;
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> GemISM;
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> ShroudISM;

	TArray<int32> OreInstance;	// per cell, INDEX_NONE if none
	TArray<int32> ShroudInstance;	// per cell, INDEX_NONE if none
	TArray<bool> OreIsGem;
	TWeakObjectPtr<UCmdGridSubsystem> Grid;
	FCmdMapTheme Theme;
	bool bShroudDirty = false;
	bool bOreDirty = false;

	virtual void Tick(float DeltaSeconds) override;
};
