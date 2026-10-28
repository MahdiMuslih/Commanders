#include "World/CmdMapGenerator.h"
#include "Game/CmdGameInstance.h"
#include "Data/CmdDataRegistry.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include <initializer_list>

namespace
{
	TSharedPtr<FJsonValue> NumArray(std::initializer_list<double> Values)
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (double V : Values)
		{
			Arr.Add(MakeShared<FJsonValueNumber>(V));
		}
		return MakeShared<FJsonValueArray>(Arr);
	}

	TSharedPtr<FJsonValue> StrArray(const TArray<FName>& Values)
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FName& V : Values)
		{
			Arr.Add(MakeShared<FJsonValueString>(V.ToString()));
		}
		return MakeShared<FJsonValueArray>(Arr);
	}

	struct FBuilder
	{
		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		TSharedPtr<FJsonObject> Map = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Teams;
		TArray<TSharedPtr<FJsonValue>> Entities;
		TArray<TSharedPtr<FJsonValue>> Rocks;
		TArray<TSharedPtr<FJsonValue>> Trees;
		TArray<TSharedPtr<FJsonValue>> Water;
		TArray<TSharedPtr<FJsonValue>> Ore;
		TArray<TSharedPtr<FJsonValue>> Clear;
		TArray<FIntRect> ClearRects;
		int32 Size = 64;
		FRandomStream Rng;

		void AddClear(const FIntRect& R)
		{
			ClearRects.Add(R);
			Clear.Add(NumArray({ (double)R.Min.X, (double)R.Min.Y, (double)R.Width(), (double)R.Height() }));
		}

		bool IsClear(const FIntPoint& C) const
		{
			if (C.X < 1 || C.Y < 1 || C.X >= Size - 1 || C.Y >= Size - 1)
			{
				return false;
			}
			for (const FIntRect& R : ClearRects)
			{
				if (C.X >= R.Min.X && C.X < R.Max.X && C.Y >= R.Min.Y && C.Y < R.Max.Y)
				{
					return false;
				}
			}
			return true;
		}

		void AddEntity(const FString& Type, int32 Team, int32 X, int32 Y, const FString& Tag = FString())
		{
			TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
			E->SetStringField(TEXT("type"), Type);
			E->SetNumberField(TEXT("team"), Team);
			E->SetField(TEXT("at"), NumArray({ (double)X, (double)Y }));
			if (!Tag.IsEmpty())
			{
				E->SetStringField(TEXT("tag"), Tag);
			}
			Entities.Add(MakeShared<FJsonValueObject>(E));
		}

		void AddOre(int32 X, int32 Y, double Radius, int32 Richness, bool bGems)
		{
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetField(TEXT("at"), NumArray({ (double)X, (double)Y }));
			O->SetNumberField(TEXT("radius"), Radius);
			O->SetNumberField(TEXT("richness"), Richness);
			O->SetBoolField(TEXT("gems"), bGems);
			Ore.Add(MakeShared<FJsonValueObject>(O));
			AddClear(FIntRect(X - FMath::CeilToInt(Radius), Y - FMath::CeilToInt(Radius), X + FMath::CeilToInt(Radius) + 1, Y + FMath::CeilToInt(Radius) + 1));
		}

		void AddTeam(const FString& Name, const FString& Faction, bool bPlayer, bool bAI, int32 Alliance, int32 Credits, const FString& Color, int32 Difficulty, bool bNeutral = false)
		{
			TSharedPtr<FJsonObject> T = MakeShared<FJsonObject>();
			T->SetStringField(TEXT("name"), Name);
			T->SetStringField(TEXT("faction"), Faction);
			T->SetBoolField(TEXT("player"), bPlayer);
			T->SetBoolField(TEXT("ai"), bAI);
			T->SetBoolField(TEXT("neutral"), bNeutral);
			T->SetNumberField(TEXT("alliance"), Alliance);
			T->SetNumberField(TEXT("credits"), Credits);
			T->SetStringField(TEXT("color"), Color);
			T->SetStringField(TEXT("difficulty"), Difficulty == 0 ? TEXT("easy") : (Difficulty == 2 ? TEXT("hard") : TEXT("normal")));
			Teams.Add(MakeShared<FJsonValueObject>(T));
		}

		/** Random-walk blobs of a terrain type. */
		void Scatter(TArray<TSharedPtr<FJsonValue>>& Target, int32 Clusters, int32 MinLen, int32 MaxLen)
		{
			for (int32 c = 0; c < Clusters; ++c)
			{
				FIntPoint P(Rng.RandRange(2, Size - 3), Rng.RandRange(2, Size - 3));
				const int32 Len = Rng.RandRange(MinLen, MaxLen);
				for (int32 i = 0; i < Len; ++i)
				{
					if (IsClear(P))
					{
						Target.Add(NumArray({ (double)P.X, (double)P.Y, 1.0, 1.0 }));
					}
					P += FIntPoint(Rng.RandRange(-1, 1), Rng.RandRange(-1, 1));
					P.X = FMath::Clamp(P.X, 1, Size - 2);
					P.Y = FMath::Clamp(P.Y, 1, Size - 2);
				}
			}
		}

		void Lake(int32 X, int32 Y, int32 Radius)
		{
			for (int32 dy = -Radius; dy <= Radius; ++dy)
			{
				for (int32 dx = -Radius; dx <= Radius; ++dx)
				{
					const float D = FMath::Sqrt((float)(dx * dx + dy * dy)) + Rng.FRandRange(-0.6f, 0.6f);
					const FIntPoint P(X + dx, Y + dy);
					if (D <= Radius && IsClear(P))
					{
						Water.Add(NumArray({ (double)P.X, (double)P.Y, 1.0, 1.0 }));
					}
				}
			}
		}

		TSharedPtr<FJsonObject> Finish(const FString& Theme)
		{
			Map->SetNumberField(TEXT("width"), Size);
			Map->SetNumberField(TEXT("height"), Size);
			Map->SetStringField(TEXT("theme"), Theme);
			Map->SetArrayField(TEXT("rocks"), Rocks);
			Map->SetArrayField(TEXT("trees"), Trees);
			Map->SetArrayField(TEXT("water"), Water);
			Map->SetArrayField(TEXT("ore"), Ore);
			Root->SetObjectField(TEXT("map"), Map);
			Root->SetArrayField(TEXT("teams"), Teams);
			Root->SetArrayField(TEXT("entities"), Entities);
			return Root;
		}
	};

	const TCHAR* ThemeName(int32 Theme)
	{
		switch (Theme)
		{
		case 1: return TEXT("winter");
		case 2: return TEXT("desert");
		case 3: return TEXT("dusk");
		default: return TEXT("temperate");
		}
	}
}

namespace CmdMapGenerator
{
	TSharedPtr<FJsonObject> GenerateSkirmish(const FCmdSkirmishSettings& S, const UCmdDataRegistry* Data)
	{
		FBuilder B;
		B.Size = FMath::Clamp(S.MapSize, 48, 128);
		B.Rng.Initialize(S.Seed);
		const int32 N = B.Size;
		const int32 Players = FMath::Clamp(S.NumEnemies + 1, 2, 4);

		TArray<FIntPoint> Starts = { FIntPoint(9, 9), FIntPoint(N - 12, N - 12), FIntPoint(N - 12, 9), FIntPoint(9, N - 12) };
		// Shuffle so the player doesn't always get the same corner.
		for (int32 i = Starts.Num() - 1; i > 0; --i)
		{
			Starts.Swap(i, B.Rng.RandRange(0, i));
		}
		const FIntPoint Center(N / 2, N / 2);

		// Teams ------------------------------------------------------------------
		static const TCHAR* EnemyColors[3] = { TEXT("red"), TEXT("yellow"), TEXT("green") };
		const FString PlayerFaction = S.PlayerFaction.ToString();
		const FString RivalFaction = PlayerFaction == TEXT("pact") ? TEXT("concord") : TEXT("pact");
		auto FactionFor = [&](int32 Index) -> FString { return (Index % 2 == 1) ? RivalFaction : PlayerFaction; };
		B.AddTeam(TEXT("You"), PlayerFaction, true, false, 0, S.StartingCredits, PlayerFaction == TEXT("pact") ? TEXT("red") : TEXT("blue"), S.Difficulty);
		for (int32 i = 1; i < Players; ++i)
		{
			const FString Faction = FactionFor(i);
			FString Color = EnemyColors[(i - 1) % 3];
			if (PlayerFaction == TEXT("pact") && Color == TEXT("red"))
			{
				Color = TEXT("blue");
			}
			B.AddTeam(FString::Printf(TEXT("Commander %d"), i), Faction, false, true, i, S.StartingCredits, Color, S.Difficulty);
		}
		const int32 NeutralTeam = Players;
		B.AddTeam(TEXT("Civilians"), TEXT("neutral"), false, false, 7, 0, TEXT("grey"), 1, true);

		// Bases & resources ------------------------------------------------------
		for (int32 i = 0; i < Players; ++i)
		{
			const FIntPoint P = Starts[i];
			B.AddClear(FIntRect(P.X - 7, P.Y - 7, P.X + 10, P.Y + 10));
			const FVector2D ToCenter = FVector2D(Center - P).GetSafeNormal();
			const FIntPoint OreAt = P + FIntPoint(FMath::RoundToInt(ToCenter.X * 9.f), FMath::RoundToInt(ToCenter.Y * 9.f));
			B.AddOre(OreAt.X, OreAt.Y, 3.5, 9, false);

			const FCmdFactionDef* Faction = Data ? Data->GetFaction(FName(*FactionFor(i))) : nullptr;
			B.AddEntity(Faction ? Faction->MCV.ToString() : TEXT("mcv"), i, P.X + 1, P.Y + 1);
			if (Faction)
			{
				int32 k = 0;
				for (const FName& U : Faction->StarterUnits)
				{
					B.AddEntity(U.ToString(), i, P.X - 1 + (k % 4), P.Y + 4 + k / 4);
					++k;
				}
			}
		}
		B.AddOre(Center.X, Center.Y, 3.0, 12, true);
		B.AddOre(N / 2, 6, 3.0, 8, false);
		B.AddOre(N / 2, N - 7, 3.0, 8, false);
		B.AddOre(6, N / 2, 3.0, 8, false);
		B.AddOre(N - 7, N / 2, 3.0, 8, false);

		// Tech oil derricks for players who expand.
		B.AddEntity(TEXT("derrick"), NeutralTeam, Center.X + 5, Center.Y - 1);
		B.AddEntity(TEXT("derrick"), NeutralTeam, Center.X - 6, Center.Y);
		B.AddClear(FIntRect(Center.X - 7, Center.Y - 2, Center.X + 8, Center.Y + 3));

		// Terrain ------------------------------------------------------------------
		const int32 Area = N * N;
		B.Lake(B.Rng.RandRange(N / 4, 3 * N / 4), B.Rng.RandRange(N / 4, N / 2 - 6), B.Rng.RandRange(2, 4));
		B.Lake(B.Rng.RandRange(N / 4, 3 * N / 4), B.Rng.RandRange(N / 2 + 6, 3 * N / 4), B.Rng.RandRange(2, 4));
		B.Scatter(B.Rocks, Area / 500, 4, 14);
		B.Scatter(B.Trees, Area / 220, 5, 18);

		return B.Finish(ThemeName(S.Theme));
	}

	TSharedPtr<FJsonObject> GenerateMenuBattle(int32 Seed)
	{
		FBuilder B;
		B.Size = 60;
		B.Rng.Initialize(Seed);
		const int32 N = B.Size;

		B.AddTeam(TEXT("Concord"), TEXT("concord"), false, true, 0, 6000, TEXT("blue"), 1);
		B.AddTeam(TEXT("Iron Pact"), TEXT("pact"), false, true, 1, 6000, TEXT("red"), 1);

		const FIntPoint A(6, 8);
		const FIntPoint Z(N - 18, N - 17);
		B.AddClear(FIntRect(A.X - 3, A.Y - 3, A.X + 15, A.Y + 13));
		B.AddClear(FIntRect(Z.X - 3, Z.Y - 3, Z.X + 15, Z.Y + 13));
		B.AddOre(A.X + 14, A.Y + 12, 3.5, 10, false);
		B.AddOre(Z.X - 2, Z.Y - 3, 3.5, 10, false);
		B.AddOre(N / 2, N / 2, 3.0, 12, true);

		const TCHAR* Layout[] = { TEXT("conyard"), TEXT("power"), TEXT("refinery"), TEXT("barracks"), TEXT("factory"), TEXT("power") };
		const FIntPoint Offsets[] = { FIntPoint(0, 0), FIntPoint(4, 0), FIntPoint(0, 4), FIntPoint(4, 4), FIntPoint(7, 0), FIntPoint(7, 4) };
		for (int32 t = 0; t < 2; ++t)
		{
			const FIntPoint Base = t == 0 ? A : Z;
			for (int32 i = 0; i < 6; ++i)
			{
				B.AddEntity(Layout[i], t, Base.X + Offsets[i].X, Base.Y + Offsets[i].Y);
			}
			B.AddEntity(TEXT("harvester"), t, Base.X + 1, Base.Y + 8);
			const TCHAR* Army[] = { TEXT("rifle"), TEXT("rifle"), TEXT("rocket"), t == 0 ? TEXT("lancer") : TEXT("bear"), t == 0 ? TEXT("warden") : TEXT("bear") };
			for (int32 i = 0; i < 5; ++i)
			{
				B.AddEntity(Army[i], t, Base.X + 3 + i, Base.Y + 10);
			}
		}

		B.Lake(N / 2 - 8, N / 2 + 6, 3);
		B.Lake(N / 2 + 8, N / 2 - 7, 3);
		B.Scatter(B.Rocks, 8, 4, 10);
		B.Scatter(B.Trees, 18, 6, 16);

		TSharedPtr<FJsonObject> Root = B.Finish(Seed % 3 == 0 ? TEXT("winter") : (Seed % 3 == 1 ? TEXT("temperate") : TEXT("dusk")));
		Root->SetBoolField(TEXT("shroud"), false);
		return Root;
	}
}
