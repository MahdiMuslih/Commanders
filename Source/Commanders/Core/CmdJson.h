#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Core/CmdTypes.h"

/** Small, forgiving accessors for the JSON data files. Missing keys return the default. */
namespace CmdJson
{
	inline TSharedPtr<FJsonValue> Field(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key)
	{
		return Obj.IsValid() ? Obj->TryGetField(Key) : TSharedPtr<FJsonValue>();
	}

	inline bool Has(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key)
	{
		const TSharedPtr<FJsonValue> V = Field(Obj, Key);
		return V.IsValid() && V->Type != EJson::Null;
	}

	inline FString Str(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, const FString& Default = FString())
	{
		const TSharedPtr<FJsonValue> V = Field(Obj, Key);
		if (!V.IsValid()) return Default;
		if (V->Type == EJson::String) return V->AsString();
		if (V->Type == EJson::Number) return FString::SanitizeFloat(V->AsNumber());
		return Default;
	}

	inline FName Name(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, FName Default = NAME_None)
	{
		const FString S = Str(Obj, Key);
		return S.IsEmpty() ? Default : FName(*S);
	}

	inline double Num(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, double Default = 0.0)
	{
		const TSharedPtr<FJsonValue> V = Field(Obj, Key);
		return (V.IsValid() && V->Type == EJson::Number) ? V->AsNumber() : Default;
	}

	inline int32 Int(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, int32 Default = 0)
	{
		return FMath::RoundToInt(Num(Obj, Key, static_cast<double>(Default)));
	}

	inline bool Bool(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, bool Default = false)
	{
		const TSharedPtr<FJsonValue> V = Field(Obj, Key);
		return (V.IsValid() && V->Type == EJson::Boolean) ? V->AsBool() : Default;
	}

	inline const TArray<TSharedPtr<FJsonValue>>& Arr(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key)
	{
		static const TArray<TSharedPtr<FJsonValue>> Empty;
		const TSharedPtr<FJsonValue> V = Field(Obj, Key);
		return (V.IsValid() && V->Type == EJson::Array) ? V->AsArray() : Empty;
	}

	inline TSharedPtr<FJsonObject> Obj(const TSharedPtr<FJsonObject>& Parent, const TCHAR* Key)
	{
		const TSharedPtr<FJsonValue> V = Field(Parent, Key);
		return (V.IsValid() && V->Type == EJson::Object) ? V->AsObject() : TSharedPtr<FJsonObject>();
	}

	inline TArray<double> Numbers(const TSharedPtr<FJsonValue>& Value)
	{
		TArray<double> Out;
		if (Value.IsValid() && Value->Type == EJson::Array)
		{
			for (const TSharedPtr<FJsonValue>& E : Value->AsArray())
			{
				Out.Add((E.IsValid() && E->Type == EJson::Number) ? E->AsNumber() : 0.0);
			}
		}
		return Out;
	}

	inline FVector Vec(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, const FVector& Default = FVector::ZeroVector)
	{
		const TArray<double> N = Numbers(Field(Obj, Key));
		if (N.Num() == 1) return FVector(N[0]);
		if (N.Num() == 2) return FVector(N[0], N[1], Default.Z);
		if (N.Num() >= 3) return FVector(N[0], N[1], N[2]);
		return Default;
	}

	inline FVector2D Vec2(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, const FVector2D& Default = FVector2D::ZeroVector)
	{
		const TArray<double> N = Numbers(Field(Obj, Key));
		return N.Num() >= 2 ? FVector2D(N[0], N[1]) : Default;
	}

	inline FIntPoint Cell(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, const FIntPoint& Default = FIntPoint(-1, -1))
	{
		const TArray<double> N = Numbers(Field(Obj, Key));
		return N.Num() >= 2 ? FIntPoint(FMath::FloorToInt(N[0]), FMath::FloorToInt(N[1])) : Default;
	}

	/** Rect as [x, y, w, h] in cells. */
	inline FIntRect Rect(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key)
	{
		const TArray<double> N = Numbers(Field(Obj, Key));
		if (N.Num() >= 4)
		{
			const int32 X = FMath::FloorToInt(N[0]);
			const int32 Y = FMath::FloorToInt(N[1]);
			return FIntRect(X, Y, X + FMath::Max(1, FMath::FloorToInt(N[2])), Y + FMath::Max(1, FMath::FloorToInt(N[3])));
		}
		return FIntRect(0, 0, 0, 0);
	}

	inline FLinearColor Color(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, const FLinearColor& Default)
	{
		const TSharedPtr<FJsonValue> V = Field(Obj, Key);
		if (!V.IsValid()) return Default;
		if (V->Type == EJson::String) return Cmd::ParseNamedColor(V->AsString(), Default);
		const TArray<double> N = Numbers(V);
		if (N.Num() >= 3) return FLinearColor(N[0], N[1], N[2], N.Num() >= 4 ? N[3] : 1.0);
		return Default;
	}

	inline TArray<FString> Strings(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key)
	{
		TArray<FString> Out;
		const TSharedPtr<FJsonValue> V = Field(Obj, Key);
		if (V.IsValid() && V->Type == EJson::String)
		{
			Out.Add(V->AsString());
		}
		else
		{
			for (const TSharedPtr<FJsonValue>& E : Arr(Obj, Key))
			{
				if (E.IsValid() && E->Type == EJson::String) Out.Add(E->AsString());
			}
		}
		return Out;
	}

	inline TArray<FName> Names(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key)
	{
		TArray<FName> Out;
		for (const FString& S : Strings(Obj, Key)) Out.Add(FName(*S));
		return Out;
	}
}
