#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Data/CmdDataRegistry.h"
#include "CmdVisualSubsystem.generated.h"

class UStaticMesh;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;
class USceneComponent;

/**
 * Builds the procedural "toy soldier" look used by every unit and structure.
 * Meshes are the engine's basic shapes (or any asset path given in the data files)
 * and materials are cached tinted instances of BasicShapeMaterial, so the game
 * runs with zero imported art. Swap in real meshes by putting asset paths in Entities.json.
 */
UCLASS()
class COMMANDERS_API UCmdVisualSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UCmdVisualSubsystem* Get(const UObject* WorldContext);

	UStaticMesh* GetMesh(const FString& ShapeOrPath);
	UMaterialInterface* GetColorMaterial(const FLinearColor& Color);

	FLinearColor ResolvePartColor(const FCmdVisualPart& Part, const FLinearColor& TeamColor) const;

	/**
	 * Spawns mesh components for each part. Turret parts attach to TurretRoot (if given),
	 * rotors are returned in OutRotors so the owner can spin them.
	 */
	void BuildParts(AActor* Owner, USceneComponent* BodyRoot, USceneComponent* TurretRoot, const TArray<FCmdVisualPart>& Parts,
		const FLinearColor& TeamColor, TArray<UStaticMeshComponent*>& OutParts, TArray<UStaticMeshComponent*>* OutRotors = nullptr,
		const FLinearColor* OverrideColor = nullptr, bool bCastShadows = true);

	/** Re-tints existing parts (after a capture changes team). */
	void RecolorParts(const TArray<UStaticMeshComponent*>& Components, const TArray<FCmdVisualPart>& Parts, const FLinearColor& TeamColor);

	UStaticMeshComponent* AddMeshComponent(AActor* Owner, USceneComponent* Parent, const FString& Shape, const FTransform& Relative, const FLinearColor& Color, bool bCastShadows = true);

private:
	UPROPERTY()
	TMap<FString, TObjectPtr<UStaticMesh>> MeshCache;

	UPROPERTY()
	TMap<uint64, TObjectPtr<UMaterialInstanceDynamic>> MaterialCache;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> BaseMaterial;
};
