#include "World/CmdTerrainActor.h"
#include "World/CmdGridSubsystem.h"
#include "World/CmdVisualSubsystem.h"
#include "Core/CmdTypes.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"

FCmdMapTheme FCmdMapTheme::FromName(const FString& Name)
{
	FCmdMapTheme T;
	T.Id = Name;
	if (Name == TEXT("winter"))
	{
		T.Ground = FLinearColor(0.62f, 0.65f, 0.7f);
		T.GroundPatch = FLinearColor(0.5f, 0.53f, 0.56f);
		T.Outer = FLinearColor(0.35f, 0.37f, 0.4f);
		T.Rock = FLinearColor(0.2f, 0.21f, 0.23f);
		T.TreeTop = FLinearColor(0.05f, 0.12f, 0.08f);
		T.Water = FLinearColor(0.12f, 0.2f, 0.28f);
		T.Road = FLinearColor(0.3f, 0.29f, 0.28f);
		T.SunColor = FLinearColor(0.85f, 0.9f, 1.f);
		T.SunPitch = -35.f;
		T.SunIntensity = 4.5f;
		T.FogDensity = 0.03f;
	}
	else if (Name == TEXT("desert"))
	{
		T.Ground = FLinearColor(0.5f, 0.38f, 0.2f);
		T.GroundPatch = FLinearColor(0.45f, 0.33f, 0.17f);
		T.Outer = FLinearColor(0.3f, 0.22f, 0.12f);
		T.Rock = FLinearColor(0.35f, 0.22f, 0.13f);
		T.TreeTop = FLinearColor(0.15f, 0.2f, 0.05f);
		T.Water = FLinearColor(0.04f, 0.18f, 0.22f);
		T.Road = FLinearColor(0.32f, 0.26f, 0.18f);
		T.SunColor = FLinearColor(1.f, 0.9f, 0.75f);
		T.SunPitch = -60.f;
		T.SunIntensity = 8.f;
		T.FogDensity = 0.008f;
	}
	else if (Name == TEXT("dusk"))
	{
		T.Ground = FLinearColor(0.09f, 0.11f, 0.06f);
		T.GroundPatch = FLinearColor(0.12f, 0.12f, 0.06f);
		T.Outer = FLinearColor(0.04f, 0.04f, 0.03f);
		T.Rock = FLinearColor(0.18f, 0.15f, 0.14f);
		T.TreeTop = FLinearColor(0.03f, 0.08f, 0.03f);
		T.Water = FLinearColor(0.05f, 0.06f, 0.12f);
		T.SunColor = FLinearColor(1.f, 0.45f, 0.2f);
		T.SunPitch = -12.f;
		T.SunYaw = 70.f;
		T.SunIntensity = 5.f;
		T.SkyIntensity = 0.6f;
		T.FogDensity = 0.04f;
	}
	return T;
}

ACmdTerrainActor::ACmdTerrainActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
}

UInstancedStaticMeshComponent* ACmdTerrainActor::MakeISM(const FString& Shape, const FLinearColor& Color, bool bShadows)
{
	UCmdVisualSubsystem* Visuals = UCmdVisualSubsystem::Get(this);
	UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(this);
	ISM->SetStaticMesh(Visuals->GetMesh(Shape));
	ISM->SetMaterial(0, Visuals->GetColorMaterial(Color));
	ISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ISM->SetCanEverAffectNavigation(false);
	ISM->SetCastShadow(bShadows);
	ISM->SetupAttachment(Root);
	ISM->RegisterComponent();
	AddInstanceComponent(ISM);
	return ISM;
}

FTransform ACmdTerrainActor::OreTransform(const FIntPoint& Cell, int32 Amount) const
{
	const float S = Amount <= 0 ? 0.f : FMath::Clamp(0.35f + Amount * 0.06f, 0.35f, 1.2f);
	const uint32 Hash = GetTypeHash(Cell);
	const FRotator Rot(0.f, static_cast<float>(Hash % 360), 0.f);
	const FVector Jitter(static_cast<float>((Hash >> 3) % 60) - 30.f, static_cast<float>((Hash >> 9) % 60) - 30.f, 0.f);
	return FTransform(Rot, Cmd::CellToWorld(Cell, 10.f * S) + Jitter, FVector(0.9f * S, 0.9f * S, 0.35f * S));
}

void ACmdTerrainActor::Build(const FCmdMapTheme& InTheme, bool bShroud)
{
	Theme = InTheme;
	UCmdGridSubsystem* G = GetWorld()->GetSubsystem<UCmdGridSubsystem>();
	UCmdVisualSubsystem* Visuals = UCmdVisualSubsystem::Get(this);
	if (!G || !Visuals)
	{
		return;
	}
	Grid = G;
	G->SetTerrainActor(this);

	const int32 W = G->GetWidth();
	const int32 H = G->GetHeight();
	const FVector Size = G->GetMapSizeWorld();
	const FVector Center = G->GetMapCenter();

	// Plane mesh is 100x100 uu.
	Ground = Visuals->AddMeshComponent(this, Root, TEXT("plane"), FTransform(FRotator::ZeroRotator, FVector(Center.X, Center.Y, 0.f), FVector(Size.X / 100.f, Size.Y / 100.f, 1.f)), Theme.Ground, false);
	OuterGround = Visuals->AddMeshComponent(this, Root, TEXT("plane"), FTransform(FRotator::ZeroRotator, FVector(Center.X, Center.Y, -4.f), FVector(Size.X / 25.f, Size.Y / 25.f, 1.f)), Theme.Outer, false);

	PatchISM = MakeISM(TEXT("plane"), Theme.GroundPatch, false);
	RockISM = MakeISM(TEXT("cube"), Theme.Rock, true);
	TrunkISM = MakeISM(TEXT("cylinder"), Theme.Trunk, true);
	TreeTopISM = MakeISM(TEXT("cone"), Theme.TreeTop, true);
	WaterISM = MakeISM(TEXT("plane"), Theme.Water, false);
	RoadISM = MakeISM(TEXT("plane"), Theme.Road, false);
	OreISM = MakeISM(TEXT("sphere"), Theme.Ore, true);
	GemISM = MakeISM(TEXT("sphere"), Theme.Gems, true);
	ShroudISM = MakeISM(TEXT("cube"), FLinearColor(0.f, 0.f, 0.f), false);

	FRandomStream Rng(W * 7919 + H * 131);

	// Ground variation patches.
	const int32 PatchCount = (W * H) / 18;
	for (int32 i = 0; i < PatchCount; ++i)
	{
		const FVector P(Rng.FRandRange(0.f, Size.X), Rng.FRandRange(0.f, Size.Y), 1.f);
		const float S = Rng.FRandRange(2.f, 7.f);
		PatchISM->AddInstance(FTransform(FRotator(0.f, Rng.FRandRange(0.f, 360.f), 0.f), P, FVector(S, S * Rng.FRandRange(0.5f, 1.f), 1.f)));
	}

	OreInstance.Init(INDEX_NONE, W * H);
	ShroudInstance.Init(INDEX_NONE, W * H);
	OreIsGem.Init(false, W * H);

	for (int32 Y = 0; Y < H; ++Y)
	{
		for (int32 X = 0; X < W; ++X)
		{
			const FIntPoint C(X, Y);
			const FCmdCell* Cell = G->GetCell(C);
			const FVector Base = Cmd::CellToWorld(C);
			switch (Cell->Terrain)
			{
			case ECmdTerrain::Rock:
			{
				const float S = Rng.FRandRange(1.6f, 2.3f);
				RockISM->AddInstance(FTransform(FRotator(Rng.FRandRange(-8.f, 8.f), Rng.FRandRange(0.f, 90.f), Rng.FRandRange(-8.f, 8.f)),
					Base + FVector(0.f, 0.f, Rng.FRandRange(30.f, 90.f)), FVector(S, S, Rng.FRandRange(1.2f, 3.2f))));
				break;
			}
			case ECmdTerrain::Tree:
			{
				const int32 Count = Rng.RandRange(1, 3);
				for (int32 t = 0; t < Count; ++t)
				{
					const FVector P = Base + FVector(Rng.FRandRange(-60.f, 60.f), Rng.FRandRange(-60.f, 60.f), 0.f);
					const float S = Rng.FRandRange(0.8f, 1.4f);
					TrunkISM->AddInstance(FTransform(FRotator::ZeroRotator, P + FVector(0.f, 0.f, 40.f * S), FVector(0.15f * S, 0.15f * S, 0.8f * S)));
					TreeTopISM->AddInstance(FTransform(FRotator::ZeroRotator, P + FVector(0.f, 0.f, 150.f * S), FVector(1.f * S, 1.f * S, 2.f * S)));
				}
				break;
			}
			case ECmdTerrain::Water:
				WaterISM->AddInstance(FTransform(FRotator::ZeroRotator, Base + FVector(0.f, 0.f, 3.f), FVector(2.02f, 2.02f, 1.f)));
				break;
			case ECmdTerrain::Road:
				RoadISM->AddInstance(FTransform(FRotator::ZeroRotator, Base + FVector(0.f, 0.f, 2.f), FVector(2.02f, 2.02f, 1.f)));
				break;
			default:
				break;
			}

			if (Cell->Ore > 0)
			{
				const int32 Index = G->ToIndex(C);
				OreIsGem[Index] = Cell->bGems;
				OreInstance[Index] = (Cell->bGems ? GemISM : OreISM)->AddInstance(OreTransform(C, Cell->Ore));
			}

			if (bShroud && !G->IsExplored(G->GetShroudViewer(), C))
			{
				ShroudInstance[G->ToIndex(C)] = ShroudISM->AddInstance(FTransform(FRotator::ZeroRotator, Base + FVector(0.f, 0.f, 250.f), FVector(2.02f, 2.02f, 6.f)));
			}
		}
	}
}

void ACmdTerrainActor::OnOreChanged(const FIntPoint& Cell)
{
	UCmdGridSubsystem* G = Grid.Get();
	if (!G || !OreISM)
	{
		return;
	}
	const int32 Index = G->ToIndex(Cell);
	if (!OreInstance.IsValidIndex(Index))
	{
		return;
	}
	const FCmdCell* C = G->GetCell(Cell);
	UInstancedStaticMeshComponent* ISM = OreIsGem[Index] ? GemISM : OreISM;
	if (OreInstance[Index] == INDEX_NONE)
	{
		if (C->Ore > 0)
		{
			OreIsGem[Index] = C->bGems;
			ISM = C->bGems ? GemISM : OreISM;
			OreInstance[Index] = ISM->AddInstance(OreTransform(Cell, C->Ore));
		}
		return;
	}
	ISM->UpdateInstanceTransform(OreInstance[Index], OreTransform(Cell, C->Ore), false, false, true);
	bOreDirty = true;
}

void ACmdTerrainActor::SetShroudVisible(bool bVisible)
{
	if (ShroudISM)
	{
		ShroudISM->SetVisibility(bVisible);
	}
}

void ACmdTerrainActor::OnExplored(const FIntPoint& Cell)
{
	UCmdGridSubsystem* G = Grid.Get();
	if (!G || !ShroudISM)
	{
		return;
	}
	const int32 Index = G->ToIndex(Cell);
	if (!ShroudInstance.IsValidIndex(Index) || ShroudInstance[Index] == INDEX_NONE)
	{
		return;
	}
	// Collapse the block instead of removing it so instance indices stay stable.
	ShroudISM->UpdateInstanceTransform(ShroudInstance[Index], FTransform(FRotator::ZeroRotator, Cmd::CellToWorld(Cell, -500.f), FVector(0.001f)), false, false, true);
	ShroudInstance[Index] = INDEX_NONE;
	bShroudDirty = true;
}

void ACmdTerrainActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bShroudDirty && ShroudISM)
	{
		ShroudISM->MarkRenderStateDirty();
		bShroudDirty = false;
	}
	if (bOreDirty)
	{
		if (OreISM) OreISM->MarkRenderStateDirty();
		if (GemISM) GemISM->MarkRenderStateDirty();
		bOreDirty = false;
	}
}
