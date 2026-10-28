#include "Data/CmdDataRegistry.h"
#include "Commanders.h"
#include "Core/CmdJson.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

void UCmdDataRegistry::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Reload();
}

UCmdDataRegistry* UCmdDataRegistry::Get(const UObject* WorldContext)
{
	if (!WorldContext || !GEngine)
	{
		return nullptr;
	}
	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull);
	const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UCmdDataRegistry>() : nullptr;
}

TSharedPtr<FJsonObject> UCmdDataRegistry::LoadJsonFile(const FString& RelativePath)
{
	const FString FullPath = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data"), RelativePath);
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *FullPath))
	{
		UE_LOG(LogCommanders, Error, TEXT("Could not read data file %s"), *FullPath);
		return nullptr;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogCommanders, Error, TEXT("Malformed JSON in %s: %s"), *FullPath, *Reader->GetErrorMessage());
		return nullptr;
	}
	return Root;
}

void UCmdDataRegistry::Reload()
{
	Entities.Reset();
	EntityOrder.Reset();
	Weapons.Reset();
	Factions.Reset();
	FactionOrder.Reset();
	Campaigns.Reset();
	Missions.Reset();

	LoadFactions();
	LoadWeapons();
	LoadEntities();
	LoadCampaigns();
	GlobalCinematics = LoadJsonFile(TEXT("Cinematics.json"));

	bLoaded = true;
	UE_LOG(LogCommanders, Log, TEXT("Data loaded: %d factions, %d weapons, %d entities, %d campaigns, %d missions"),
		Factions.Num(), Weapons.Num(), Entities.Num(), Campaigns.Num(), Missions.Num());
}

const FCmdCampaignDef* UCmdDataRegistry::GetCampaign(FName Id) const
{
	return Campaigns.FindByPredicate([Id](const FCmdCampaignDef& C) { return C.Id == Id; });
}

TSharedPtr<FJsonObject> UCmdDataRegistry::GetGlobalCinematic(FName Id) const
{
	return CmdJson::Obj(GlobalCinematics, *Id.ToString());
}

void UCmdDataRegistry::LoadFactions()
{
	const TSharedPtr<FJsonObject> Root = LoadJsonFile(TEXT("Factions.json"));
	for (const TSharedPtr<FJsonValue>& V : CmdJson::Arr(Root, TEXT("factions")))
	{
		const TSharedPtr<FJsonObject> J = V->AsObject();
		FCmdFactionDef F;
		F.Id = CmdJson::Name(J, TEXT("id"));
		F.Name = CmdJson::Str(J, TEXT("name"));
		F.Motto = CmdJson::Str(J, TEXT("motto"));
		F.Description = CmdJson::Str(J, TEXT("description"));
		F.Color = CmdJson::Color(J, TEXT("color"), FLinearColor::White);
		F.MCV = CmdJson::Name(J, TEXT("mcv"), TEXT("mcv"));
		F.Harvester = CmdJson::Name(J, TEXT("harvester"), TEXT("harvester"));
		F.AIDefenses = CmdJson::Names(J, TEXT("ai_defenses"));
		F.StarterUnits = CmdJson::Names(J, TEXT("starter_units"));
		if (!F.Id.IsNone())
		{
			FactionOrder.Add(F.Id);
			Factions.Add(F.Id, MoveTemp(F));
		}
	}
}

void UCmdDataRegistry::LoadWeapons()
{
	const TSharedPtr<FJsonObject> Root = LoadJsonFile(TEXT("Weapons.json"));
	for (const TSharedPtr<FJsonValue>& V : CmdJson::Arr(Root, TEXT("weapons")))
	{
		const TSharedPtr<FJsonObject> J = V->AsObject();
		FCmdWeaponDef W;
		W.Id = CmdJson::Name(J, TEXT("id"));
		W.Name = CmdJson::Str(J, TEXT("name"), W.Id.ToString());
		W.Damage = CmdJson::Num(J, TEXT("damage"), W.Damage);
		W.Range = CmdJson::Num(J, TEXT("range"), W.Range);
		W.MinRange = CmdJson::Num(J, TEXT("min_range"), W.MinRange);
		W.Cooldown = CmdJson::Num(J, TEXT("cooldown"), W.Cooldown);
		W.Warhead = Cmd::ParseWarhead(CmdJson::Str(J, TEXT("warhead"), TEXT("smallarms")));
		W.ProjectileSpeed = CmdJson::Num(J, TEXT("speed"), W.ProjectileSpeed);
		W.ArcHeight = CmdJson::Num(J, TEXT("arc"), W.ArcHeight);
		W.Splash = CmdJson::Num(J, TEXT("splash"), W.Splash);
		W.Burst = FMath::Max(1, CmdJson::Int(J, TEXT("burst"), W.Burst));
		W.bTargetGround = CmdJson::Bool(J, TEXT("ground"), true);
		W.bTargetAir = CmdJson::Bool(J, TEXT("air"), false);
		W.Color = CmdJson::Color(J, TEXT("color"), W.Color);
		W.ProjectileScale = CmdJson::Num(J, TEXT("size"), W.ProjectileScale);
		W.MuzzleFlash = CmdJson::Num(J, TEXT("flash"), W.MuzzleFlash);
		W.Sound = CmdJson::Name(J, TEXT("sound"));
		if (!W.Id.IsNone())
		{
			Weapons.Add(W.Id, MoveTemp(W));
		}
	}
}

void UCmdDataRegistry::ParsePart(const TSharedPtr<FJsonObject>& J, FCmdVisualPart& P)
{
	P.Mesh = CmdJson::Str(J, TEXT("shape"), TEXT("cube"));
	P.Offset = CmdJson::Vec(J, TEXT("at"), FVector::ZeroVector);
	P.Scale = CmdJson::Vec(J, TEXT("size"), FVector::OneVector);
	const FVector Rot = CmdJson::Vec(J, TEXT("rot"), FVector::ZeroVector);
	P.Rotation = FRotator(Rot.X, Rot.Y, Rot.Z);	// pitch, yaw, roll
	P.bTurret = CmdJson::Bool(J, TEXT("turret"));
	P.bRotor = CmdJson::Bool(J, TEXT("rotor"));
	P.bGlow = CmdJson::Bool(J, TEXT("glow"));
	P.Spin = CmdJson::Num(J, TEXT("spin"), P.bRotor ? 1440.0 : 0.0);

	const TSharedPtr<FJsonValue> ColorValue = CmdJson::Field(J, TEXT("color"));
	if (ColorValue.IsValid() && ColorValue->Type == EJson::String)
	{
		const FString C = ColorValue->AsString();
		if (C == TEXT("team")) P.ColorMode = FCmdVisualPart::EColorMode::Team;
		else if (C == TEXT("teamdark")) P.ColorMode = FCmdVisualPart::EColorMode::TeamDark;
		else if (C == TEXT("metal")) P.ColorMode = FCmdVisualPart::EColorMode::Metal;
		else if (C == TEXT("dark")) P.ColorMode = FCmdVisualPart::EColorMode::Dark;
		else if (C == TEXT("concrete")) P.ColorMode = FCmdVisualPart::EColorMode::Concrete;
		else if (C == TEXT("glass")) P.ColorMode = FCmdVisualPart::EColorMode::Glass;
		else if (C == TEXT("light")) P.ColorMode = FCmdVisualPart::EColorMode::Light;
		else
		{
			P.ColorMode = FCmdVisualPart::EColorMode::Custom;
			P.CustomColor = Cmd::ParseNamedColor(C, FLinearColor::White);
		}
	}
	else if (ColorValue.IsValid())
	{
		P.ColorMode = FCmdVisualPart::EColorMode::Custom;
		P.CustomColor = CmdJson::Color(J, TEXT("color"), FLinearColor::White);
	}
}

void UCmdDataRegistry::LoadEntities()
{
	const TSharedPtr<FJsonObject> Root = LoadJsonFile(TEXT("Entities.json"));

	// Reusable model templates ("model": "tank_body") keep the data file compact.
	TMap<FString, TArray<FCmdVisualPart>> Templates;
	const TSharedPtr<FJsonObject> TemplateObj = CmdJson::Obj(Root, TEXT("templates"));
	if (TemplateObj.IsValid())
	{
		for (const auto& Pair : TemplateObj->Values)
		{
			TArray<FCmdVisualPart>& Parts = Templates.Add(Pair.Key);
			if (Pair.Value.IsValid() && Pair.Value->Type == EJson::Array)
			{
				for (const TSharedPtr<FJsonValue>& PV : Pair.Value->AsArray())
				{
					FCmdVisualPart Part;
					ParsePart(PV->AsObject(), Part);
					Parts.Add(Part);
				}
			}
		}
	}

	int32 Order = 0;
	for (const TSharedPtr<FJsonValue>& V : CmdJson::Arr(Root, TEXT("entities")))
	{
		const TSharedPtr<FJsonObject> J = V->AsObject();
		FCmdEntityDef E;
		E.Id = CmdJson::Name(J, TEXT("id"));
		if (E.Id.IsNone())
		{
			continue;
		}
		E.Name = CmdJson::Str(J, TEXT("name"), E.Id.ToString());
		E.Description = CmdJson::Str(J, TEXT("desc"));
		E.Faction = CmdJson::Name(J, TEXT("faction"), TEXT("any"));
		E.Category = Cmd::ParseCategory(CmdJson::Str(J, TEXT("category"), TEXT("infantry")));
		E.bIsBuilding = E.Category == ECmdCategory::Structure;
		E.bBuildable = CmdJson::Bool(J, TEXT("buildable"), true);
		E.Cost = CmdJson::Int(J, TEXT("cost"), E.Cost);
		E.BuildTime = CmdJson::Num(J, TEXT("time"), FMath::Max(3.0, E.Cost / 60.0));
		E.MaxHealth = CmdJson::Num(J, TEXT("health"), E.MaxHealth);
		E.Armor = Cmd::ParseArmor(CmdJson::Str(J, TEXT("armor"), E.bIsBuilding ? TEXT("structure") : TEXT("none")));
		E.Speed = CmdJson::Num(J, TEXT("speed"), E.Speed);
		E.TurnRate = CmdJson::Num(J, TEXT("turn"), E.IsInfantry() ? 720.0 : 180.0);
		E.Sight = CmdJson::Num(J, TEXT("sight"), E.Sight);
		E.Weapon = CmdJson::Name(J, TEXT("weapon"));
		E.MuzzleOffset = CmdJson::Vec(J, TEXT("muzzle"), E.MuzzleOffset);
		E.Prereqs = CmdJson::Names(J, TEXT("prereqs"));
		E.Power = CmdJson::Int(J, TEXT("power"), 0);
		E.bRequiresPower = CmdJson::Bool(J, TEXT("requires_power"), false);
		E.Footprint = CmdJson::Cell(J, TEXT("footprint"), FIntPoint(1, 1));
		for (const FString& P : CmdJson::Strings(J, TEXT("produces")))
		{
			E.Produces.Add(Cmd::ParseCategory(P));
		}
		E.bConstructionYard = CmdJson::Bool(J, TEXT("conyard"));
		E.bRefinery = CmdJson::Bool(J, TEXT("refinery"));
		E.bRadar = CmdJson::Bool(J, TEXT("radar"));
		E.bHarvester = CmdJson::Bool(J, TEXT("harvester"));
		E.HarvestCapacity = CmdJson::Int(J, TEXT("capacity"), E.HarvestCapacity);
		E.bEngineer = CmdJson::Bool(J, TEXT("engineer"));
		E.bCapturable = CmdJson::Bool(J, TEXT("capturable"), E.bIsBuilding);
		E.bAircraft = E.Category == ECmdCategory::Aircraft;
		E.FlyHeight = CmdJson::Num(J, TEXT("fly_height"), E.bAircraft ? 450.0 : 0.0);
		E.DeploysInto = CmdJson::Name(J, TEXT("deploys_into"));
		E.FreeUnit = CmdJson::Name(J, TEXT("free_unit"));
		E.Income = CmdJson::Int(J, TEXT("income"), 0);
		E.bIgnoreForVictory = CmdJson::Bool(J, TEXT("ignore_for_victory"));
		E.SortOrder = CmdJson::Int(J, TEXT("order"), Order);

		const float DefaultRadius = E.bIsBuilding ? FMath::Max(E.Footprint.X, E.Footprint.Y) * CmdConst::CellSize * 0.5f : (E.IsInfantry() ? 25.f : 70.f);
		E.Radius = CmdJson::Num(J, TEXT("radius"), DefaultRadius);
		E.Height = CmdJson::Num(J, TEXT("height"), E.bIsBuilding ? 200.0 : (E.IsInfantry() ? 100.0 : 110.0));

		for (const FString& TemplateName : CmdJson::Strings(J, TEXT("model")))
		{
			if (const TArray<FCmdVisualPart>* T = Templates.Find(TemplateName))
			{
				E.Parts.Append(*T);
			}
			else
			{
				UE_LOG(LogCommanders, Warning, TEXT("Entity %s references unknown model template %s"), *E.Id.ToString(), *TemplateName);
			}
		}
		for (const TSharedPtr<FJsonValue>& PV : CmdJson::Arr(J, TEXT("parts")))
		{
			FCmdVisualPart Part;
			ParsePart(PV->AsObject(), Part);
			E.Parts.Add(Part);
		}

		++Order;
		EntityOrder.Add(E.Id);
		Entities.Add(E.Id, MoveTemp(E));
	}

	EntityOrder.Sort([this](const FName& A, const FName& B)
	{
		return Entities[A].SortOrder < Entities[B].SortOrder;
	});
}

void UCmdDataRegistry::LoadCampaigns()
{
	const TSharedPtr<FJsonObject> Root = LoadJsonFile(TEXT("Campaigns.json"));
	for (const TSharedPtr<FJsonValue>& V : CmdJson::Arr(Root, TEXT("campaigns")))
	{
		const TSharedPtr<FJsonObject> J = V->AsObject();
		FCmdCampaignDef C;
		C.Id = CmdJson::Name(J, TEXT("id"));
		C.Name = CmdJson::Str(J, TEXT("name"));
		C.Faction = CmdJson::Name(J, TEXT("faction"));
		C.Tagline = CmdJson::Str(J, TEXT("tagline"));
		C.Synopsis = CmdJson::Str(J, TEXT("synopsis"));
		C.Epilogue = CmdJson::Str(J, TEXT("epilogue"));
		C.Color = CmdJson::Color(J, TEXT("color"), FLinearColor::White);

		int32 Index = 0;
		for (const TSharedPtr<FJsonValue>& MV : CmdJson::Arr(J, TEXT("missions")))
		{
			const TSharedPtr<FJsonObject> MJ = MV->AsObject();
			FCmdMissionInfo M;
			M.Id = CmdJson::Name(MJ, TEXT("id"));
			M.Campaign = C.Id;
			M.File = CmdJson::Str(MJ, TEXT("file"));
			M.Index = Index++;
			M.Json = LoadJsonFile(M.File);
			M.Title = CmdJson::Str(M.Json, TEXT("title"), M.Id.ToString());
			M.Subtitle = CmdJson::Str(M.Json, TEXT("subtitle"));
			if (!M.Id.IsNone() && M.Json.IsValid())
			{
				C.Missions.Add(M.Id);
				Missions.Add(M.Id, MoveTemp(M));
			}
		}
		Campaigns.Add(MoveTemp(C));
	}
}
