// Shared enums, structs and built-in default data for the weather system.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "WeatherTypes.generated.h"

/** The nine weather states, ordered from calmest to most severe. */
UENUM(BlueprintType)
enum class EWeatherState : uint8
{
	Clear				UMETA(DisplayName = "Clear"),
	MostlyClear			UMETA(DisplayName = "Mostly Clear"),
	PartlyCloudy		UMETA(DisplayName = "Partly Cloudy"),
	MostlyCloudy		UMETA(DisplayName = "Mostly Cloudy"),
	Overcast			UMETA(DisplayName = "Overcast"),
	LightPrecipitation	UMETA(DisplayName = "Light Precipitation"),
	Precipitation		UMETA(DisplayName = "Precipitation"),
	HeavyPrecipitation	UMETA(DisplayName = "Heavy Precipitation"),
	Storm				UMETA(DisplayName = "Storm")
};

UENUM(BlueprintType)
enum class ESeason : uint8
{
	Spring	UMETA(DisplayName = "Spring"),
	Summer	UMETA(DisplayName = "Summer"),
	Autumn	UMETA(DisplayName = "Autumn"),
	Winter	UMETA(DisplayName = "Winter")
};

/** How the whole weather system is driven. */
UENUM(BlueprintType)
enum class EWeatherSystemMode : uint8
{
	/** Time runs on its own and the weather evolves from noise, season by season. */
	Continuous	UMETA(DisplayName = "Continuous"),
	/** Time of day, season and weather are set by hand. */
	Custom		UMETA(DisplayName = "Custom")
};

/** What kind of precipitation falls when a precipitation state is selected in Custom mode. */
UENUM(BlueprintType)
enum class ECustomPrecipitation : uint8
{
	/** Rolled from the season's snow chance (same as Continuous mode). */
	Auto	UMETA(DisplayName = "Auto (by season)"),
	Rain	UMETA(DisplayName = "Rain"),
	Snow	UMETA(DisplayName = "Snow")
};

/** A closed [Min, Max] range of values. */
USTRUCT(BlueprintType)
struct WEATHER_SYSTEM_API FWeatherRange
{
	GENERATED_BODY()

	FWeatherRange() = default;
	FWeatherRange(float InMin, float InMax) : Min(InMin), Max(InMax) {}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	float Min = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	float Max = 0.f;

	/** Linearly interpolates between Min and Max. */
	float Evaluate(float Alpha) const { return FMath::Lerp(Min, Max, Alpha); }
};

/** One DataTable row per EWeatherState: the value range of every visual channel. Row name == enum name. */
USTRUCT(BlueprintType)
struct WEATHER_SYSTEM_API FWeatherConditionRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	FWeatherRange Rain;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	FWeatherRange Fog;

	/** Cloud coverage. Written to the cloud material scalar parameter (negative = clearer sky). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	FWeatherRange Clouds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	FWeatherRange Snow;
};

/**
 * One DataTable row per ESeason. Each band is the noise interval [Min, Max] (0..1) that selects
 * the matching weather state. Row name == enum name.
 */
USTRUCT(BlueprintType)
struct WEATHER_SYSTEM_API FSeasonThresholdRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bands")
	FWeatherRange Clear;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bands")
	FWeatherRange MostlyClear;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bands")
	FWeatherRange PartlyCloudy;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bands")
	FWeatherRange MostlyCloudy;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bands")
	FWeatherRange Overcast;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bands")
	FWeatherRange LightPrecipitation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bands")
	FWeatherRange Precipitation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bands")
	FWeatherRange HeavyPrecipitation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bands")
	FWeatherRange Storm;

	/** Probability (0..1) that a precipitation event in this season falls as snow instead of rain. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Precipitation", meta = (ClampMin = "0", ClampMax = "1"))
	float SnowChance = 0.f;

	/** Returns the noise band belonging to a state. */
	const FWeatherRange& GetBand(EWeatherState State) const;

	/** Returns the first state whose band upper edge is >= Noise (Storm if none). */
	EWeatherState Categorize(float Noise) const;
};

/** Current, smoothed output values of the weather simulation. */
USTRUCT(BlueprintType)
struct WEATHER_SYSTEM_API FWeatherSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	float Rain = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	float Snow = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	float Fog = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	float Clouds = 0.f;

	/** 0..1 surface wetness: rises while it rains/snows, dries slowly afterwards. */
	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	float Wetness = 0.f;
};

/** Built-in fallback data, used whenever no DataTable is assigned (or a row is missing). */
struct WEATHER_SYSTEM_API FWeatherDefaults
{
	static FWeatherConditionRow GetCondition(EWeatherState State);
	static FSeasonThresholdRow GetSeasonThresholds(ESeason Season);
};

/** Helpers for enum <-> name/text conversion (also used as DataTable row names). */
struct WEATHER_SYSTEM_API FWeatherEnumUtils
{
	static FName ToRowName(EWeatherState State);
	static FName ToRowName(ESeason Season);
	static FText ToText(EWeatherState State);
	static FText ToText(ESeason Season);
	static FText ToText(EWeatherSystemMode Mode);
	static bool IsPrecipitation(EWeatherState State) { return State >= EWeatherState::LightPrecipitation; }
};
