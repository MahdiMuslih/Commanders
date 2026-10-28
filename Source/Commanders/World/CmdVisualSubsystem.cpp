#include "World/CmdVisualSubsystem.h"
#include "Commanders.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

UCmdVisualSubsystem* UCmdVisualSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UCmdVisualSubsystem>() : nullptr;
}

UStaticMesh* UCmdVisualSubsystem::GetMesh(const FString& ShapeOrPath)
{
	if (TObjectPtr<UStaticMesh>* Found = MeshCache.Find(ShapeOrPath))
	{
		return *Found;
	}

	FString Path = ShapeOrPath;
	if (ShapeOrPath == TEXT("cube")) Path = TEXT("/Engine/BasicShapes/Cube.Cube");
	else if (ShapeOrPath == TEXT("sphere")) Path = TEXT("/Engine/BasicShapes/Sphere.Sphere");
	else if (ShapeOrPath == TEXT("cylinder")) Path = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	else if (ShapeOrPath == TEXT("cone")) Path = TEXT("/Engine/BasicShapes/Cone.Cone");
	else if (ShapeOrPath == TEXT("plane")) Path = TEXT("/Engine/BasicShapes/Plane.Plane");

	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path);
	if (!Mesh)
	{
		UE_LOG(LogCommanders, Warning, TEXT("Mesh '%s' not found, using cube."), *ShapeOrPath);
		Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	}
	MeshCache.Add(ShapeOrPath, Mesh);
	return Mesh;
}

UMaterialInterface* UCmdVisualSubsystem::GetColorMaterial(const FLinearColor& Color)
{
	if (!BaseMaterial)
	{
		BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	}
	if (!BaseMaterial)
	{
		return nullptr;
	}

	// Quantise (HDR-safe) so near-identical colours share one material instance.
	auto Q = [](float V) { return static_cast<uint64>(FMath::Clamp(FMath::RoundToInt(V * 100.f), 0, 0xFFFF)); };
	const uint64 Key = (Q(Color.R) << 32) | (Q(Color.G) << 16) | Q(Color.B);
	if (TObjectPtr<UMaterialInstanceDynamic>* Found = MaterialCache.Find(Key))
	{
		return *Found;
	}

	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(BaseMaterial, this);
	MID->SetVectorParameterValue(TEXT("Color"), Color);
	MaterialCache.Add(Key, MID);
	return MID;
}

FLinearColor UCmdVisualSubsystem::ResolvePartColor(const FCmdVisualPart& Part, const FLinearColor& TeamColor) const
{
	switch (Part.ColorMode)
	{
	case FCmdVisualPart::EColorMode::Team: return TeamColor;
	case FCmdVisualPart::EColorMode::TeamDark: return TeamColor * 0.45f;
	case FCmdVisualPart::EColorMode::Metal: return FLinearColor(0.22f, 0.23f, 0.22f);
	case FCmdVisualPart::EColorMode::Dark: return FLinearColor(0.035f, 0.035f, 0.035f);
	case FCmdVisualPart::EColorMode::Concrete: return FLinearColor(0.32f, 0.31f, 0.29f);
	case FCmdVisualPart::EColorMode::Glass: return FLinearColor(0.15f, 0.45f, 0.6f);
	case FCmdVisualPart::EColorMode::Light: return FLinearColor(0.95f, 0.85f, 0.45f);
	default: return Part.CustomColor;
	}
}

UStaticMeshComponent* UCmdVisualSubsystem::AddMeshComponent(AActor* Owner, USceneComponent* Parent, const FString& Shape, const FTransform& Relative, const FLinearColor& Color, bool bCastShadows)
{
	if (!Owner || !Parent)
	{
		return nullptr;
	}
	UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(Owner);
	Comp->SetStaticMesh(GetMesh(Shape));
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Comp->SetGenerateOverlapEvents(false);
	Comp->SetCanEverAffectNavigation(false);
	Comp->SetCastShadow(bCastShadows);
	Comp->SetupAttachment(Parent);
	Comp->SetRelativeTransform(Relative);
	Comp->SetMaterial(0, GetColorMaterial(Color));
	Comp->RegisterComponent();
	Owner->AddInstanceComponent(Comp);
	return Comp;
}

void UCmdVisualSubsystem::BuildParts(AActor* Owner, USceneComponent* BodyRoot, USceneComponent* TurretRoot, const TArray<FCmdVisualPart>& Parts,
	const FLinearColor& TeamColor, TArray<UStaticMeshComponent*>& OutParts, TArray<UStaticMeshComponent*>* OutRotors,
	const FLinearColor* OverrideColor, bool bCastShadows)
{
	for (const FCmdVisualPart& Part : Parts)
	{
		USceneComponent* Parent = (Part.bTurret && TurretRoot) ? TurretRoot : BodyRoot;
		FVector Offset = Part.Offset;
		if (Part.bTurret && TurretRoot && TurretRoot != BodyRoot)
		{
			// Turret parts are authored in body space; convert to turret-root space.
			Offset -= TurretRoot->GetRelativeLocation();
		}
		const FTransform Relative(Part.Rotation, Offset, Part.Scale);
		const FLinearColor Color = OverrideColor ? *OverrideColor : ResolvePartColor(Part, TeamColor);
		UStaticMeshComponent* Comp = AddMeshComponent(Owner, Parent, Part.Mesh, Relative, Color, bCastShadows && !Part.bGlow);
		OutParts.Add(Comp);
		if (Part.bRotor && OutRotors)
		{
			OutRotors->Add(Comp);
		}
	}
}

void UCmdVisualSubsystem::RecolorParts(const TArray<UStaticMeshComponent*>& Components, const TArray<FCmdVisualPart>& Parts, const FLinearColor& TeamColor)
{
	for (int32 i = 0; i < Components.Num() && i < Parts.Num(); ++i)
	{
		if (Components[i])
		{
			Components[i]->SetMaterial(0, GetColorMaterial(ResolvePartColor(Parts[i], TeamColor)));
		}
	}
}
