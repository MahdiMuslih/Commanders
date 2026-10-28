#include "Core/CmdTypes.h"

namespace Cmd
{
	float DamageMultiplier(ECmdWarhead Warhead, ECmdArmor Armor)
	{
		//                               None   Light  Heavy  Struct Air
		static const float Table[6][5] = {
			/* SmallArms     */ { 1.00f, 0.55f, 0.20f, 0.25f, 0.45f },
			/* ArmorPiercing */ { 0.30f, 0.85f, 1.00f, 0.70f, 0.80f },
			/* HighExplosive */ { 0.90f, 0.75f, 0.60f, 1.00f, 0.25f },
			/* Fire          */ { 1.40f, 0.60f, 0.30f, 0.85f, 0.00f },
			/* Electric      */ { 1.25f, 1.00f, 1.00f, 0.80f, 0.60f },
			/* Flak          */ { 0.50f, 0.30f, 0.10f, 0.10f, 1.25f },
		};
		const int32 W = FMath::Clamp(static_cast<int32>(Warhead), 0, 5);
		const int32 A = FMath::Clamp(static_cast<int32>(Armor), 0, 4);
		return Table[W][A];
	}

	ECmdArmor ParseArmor(const FString& Text)
	{
		if (Text.Equals(TEXT("light"), ESearchCase::IgnoreCase)) return ECmdArmor::Light;
		if (Text.Equals(TEXT("heavy"), ESearchCase::IgnoreCase)) return ECmdArmor::Heavy;
		if (Text.Equals(TEXT("structure"), ESearchCase::IgnoreCase)) return ECmdArmor::Structure;
		if (Text.Equals(TEXT("aircraft"), ESearchCase::IgnoreCase)) return ECmdArmor::Aircraft;
		return ECmdArmor::None;
	}

	ECmdWarhead ParseWarhead(const FString& Text)
	{
		if (Text.Equals(TEXT("ap"), ESearchCase::IgnoreCase) || Text.Equals(TEXT("armorpiercing"), ESearchCase::IgnoreCase)) return ECmdWarhead::ArmorPiercing;
		if (Text.Equals(TEXT("he"), ESearchCase::IgnoreCase) || Text.Equals(TEXT("highexplosive"), ESearchCase::IgnoreCase)) return ECmdWarhead::HighExplosive;
		if (Text.Equals(TEXT("fire"), ESearchCase::IgnoreCase)) return ECmdWarhead::Fire;
		if (Text.Equals(TEXT("electric"), ESearchCase::IgnoreCase)) return ECmdWarhead::Electric;
		if (Text.Equals(TEXT("flak"), ESearchCase::IgnoreCase)) return ECmdWarhead::Flak;
		return ECmdWarhead::SmallArms;
	}

	ECmdCategory ParseCategory(const FString& Text)
	{
		if (Text.Equals(TEXT("infantry"), ESearchCase::IgnoreCase)) return ECmdCategory::Infantry;
		if (Text.Equals(TEXT("vehicle"), ESearchCase::IgnoreCase)) return ECmdCategory::Vehicle;
		if (Text.Equals(TEXT("aircraft"), ESearchCase::IgnoreCase)) return ECmdCategory::Aircraft;
		return ECmdCategory::Structure;
	}

	FString CategoryLabel(ECmdCategory Category)
	{
		switch (Category)
		{
		case ECmdCategory::Structure: return TEXT("BUILD");
		case ECmdCategory::Infantry: return TEXT("INFANTRY");
		case ECmdCategory::Vehicle: return TEXT("VEHICLES");
		case ECmdCategory::Aircraft: return TEXT("AIR");
		default: return TEXT("?");
		}
	}

	FLinearColor ParseNamedColor(const FString& Text, const FLinearColor& Fallback)
	{
		if (Text.StartsWith(TEXT("#")))
		{
			return FLinearColor(FColor::FromHex(Text));
		}
		if (Text == TEXT("white")) return FLinearColor(0.9f, 0.9f, 0.9f);
		if (Text == TEXT("black")) return FLinearColor(0.02f, 0.02f, 0.02f);
		if (Text == TEXT("red")) return FLinearColor(0.75f, 0.06f, 0.05f);
		if (Text == TEXT("blue")) return FLinearColor(0.08f, 0.25f, 0.8f);
		if (Text == TEXT("green")) return FLinearColor(0.1f, 0.55f, 0.12f);
		if (Text == TEXT("yellow")) return FLinearColor(0.9f, 0.75f, 0.1f);
		if (Text == TEXT("orange")) return FLinearColor(0.95f, 0.4f, 0.05f);
		if (Text == TEXT("grey") || Text == TEXT("gray")) return FLinearColor(0.35f, 0.35f, 0.35f);
		return Fallback;
	}
}
