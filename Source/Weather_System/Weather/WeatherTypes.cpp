#include "Weather/WeatherTypes.h"

const FWeatherRange& FSeasonThresholdRow::GetBand(EWeatherState State) const
{
	switch (State)
	{
	case EWeatherState::Clear:				return Clear;
	case EWeatherState::MostlyClear:		return MostlyClear;
	case EWeatherState::PartlyCloudy:		return PartlyCloudy;
	case EWeatherState::MostlyCloudy:		return MostlyCloudy;
	case EWeatherState::Overcast:			return Overcast;
	case EWeatherState::LightPrecipitation:	return LightPrecipitation;
	case EWeatherState::Precipitation:		return Precipitation;
	case EWeatherState::HeavyPrecipitation:	return HeavyPrecipitation;
	default:								return Storm;
	}
}

EWeatherState FSeasonThresholdRow::Categorize(float Noise) const
{
	// Walk the bands from calmest to most severe; the first band whose upper edge covers the noise wins.
	for (int32 i = 0; i < static_cast<int32>(EWeatherState::Storm); ++i)
	{
		const EWeatherState State = static_cast<EWeatherState>(i);
		if (Noise <= GetBand(State).Max)
		{
			return State;
		}
	}
	return EWeatherState::Storm;
}

FWeatherConditionRow FWeatherDefaults::GetCondition(EWeatherState State)
{
	FWeatherConditionRow Row;
	// Rain, Fog, Clouds, Snow ranges. Mirrors DT_WeatherConditions_Balanced: every channel rises steadily with severity,
	// and each state's Min equals the previous state's Max so there are no jumps when the weather changes state.
	switch (State)
	{
	case EWeatherState::Clear:
		Row.Fog = {0.0005f, 0.001f};  Row.Clouds = {-1.f, -0.8f}; break;
	case EWeatherState::MostlyClear:
		Row.Fog = {0.001f, 0.0015f};  Row.Clouds = {-0.8f, -0.6f}; break;
	case EWeatherState::PartlyCloudy:
		Row.Fog = {0.0015f, 0.0025f}; Row.Clouds = {-0.6f, -0.35f}; break;
	case EWeatherState::MostlyCloudy:
		Row.Fog = {0.0025f, 0.005f};  Row.Clouds = {-0.35f, -0.1f}; break;
	case EWeatherState::Overcast:
		Row.Fog = {0.005f, 0.01f};    Row.Clouds = {-0.1f, 0.1f}; break;
	case EWeatherState::LightPrecipitation:
		Row.Rain = {0.1f, 0.3f};  Row.Snow = {0.1f, 0.3f}; Row.Fog = {0.01f, 0.0175f};   Row.Clouds = {0.1f, 0.2f}; break;
	case EWeatherState::Precipitation:
		Row.Rain = {0.3f, 0.55f}; Row.Snow = {0.3f, 0.5f}; Row.Fog = {0.0175f, 0.0275f}; Row.Clouds = {0.2f, 0.3f}; break;
	case EWeatherState::HeavyPrecipitation:
		Row.Rain = {0.55f, 0.8f}; Row.Snow = {0.5f, 0.7f}; Row.Fog = {0.0275f, 0.04f};   Row.Clouds = {0.3f, 0.45f}; break;
	case EWeatherState::Storm:
		Row.Rain = {0.8f, 1.f};   Row.Snow = {0.7f, 0.9f}; Row.Fog = {0.04f, 0.06f};     Row.Clouds = {0.45f, 0.7f}; break;
	}
	return Row;
}

FSeasonThresholdRow FWeatherDefaults::GetSeasonThresholds(ESeason Season)
{
	// Upper edge of each band, calmest -> most severe. The lower edge is the previous band's upper edge.
	// Mirrors DT_SeasonThresholds_Balanced. Every season has its own climate; storms are rare in all of them.
	float Edges[9];
	float SnowChance = 0.f;
	switch (Season)
	{
	case ESeason::Spring: // changeable, showery
		{ const float E[9] = {0.12f, 0.26f, 0.42f, 0.58f, 0.72f, 0.84f, 0.93f, 0.98f, 1.f}; FMemory::Memcpy(Edges, E); SnowChance = 0.1f; }
		break;
	case ESeason::Summer: // clearest, rare but possible storms
		{ const float E[9] = {0.25f, 0.45f, 0.63f, 0.77f, 0.86f, 0.92f, 0.96f, 0.99f, 1.f}; FMemory::Memcpy(Edges, E); SnowChance = 0.f; }
		break;
	case ESeason::Autumn: // cloudier and wetter
		{ const float E[9] = {0.12f, 0.24f, 0.38f, 0.54f, 0.68f, 0.80f, 0.90f, 0.97f, 1.f}; FMemory::Memcpy(Edges, E); SnowChance = 0.1f; }
		break;
	default: // Winter: overcast and precipitation heavy, mostly snow
		{ const float E[9] = {0.08f, 0.16f, 0.28f, 0.42f, 0.62f, 0.76f, 0.88f, 0.96f, 1.f}; FMemory::Memcpy(Edges, E); SnowChance = 0.85f; }
		break;
	}

	FSeasonThresholdRow Row;
	FWeatherRange* Bands[9] = {&Row.Clear, &Row.MostlyClear, &Row.PartlyCloudy, &Row.MostlyCloudy, &Row.Overcast,
		&Row.LightPrecipitation, &Row.Precipitation, &Row.HeavyPrecipitation, &Row.Storm};
	float Previous = 0.f;
	for (int32 i = 0; i < 9; ++i)
	{
		*Bands[i] = FWeatherRange(Previous, Edges[i]);
		Previous = Edges[i];
	}
	Row.SnowChance = SnowChance;
	return Row;
}

FName FWeatherEnumUtils::ToRowName(EWeatherState State)
{
	return FName(*StaticEnum<EWeatherState>()->GetNameStringByValue(static_cast<int64>(State)));
}

FName FWeatherEnumUtils::ToRowName(ESeason Season)
{
	return FName(*StaticEnum<ESeason>()->GetNameStringByValue(static_cast<int64>(Season)));
}

FText FWeatherEnumUtils::ToText(EWeatherState State)
{
	return StaticEnum<EWeatherState>()->GetDisplayNameTextByValue(static_cast<int64>(State));
}

FText FWeatherEnumUtils::ToText(EWeatherSystemMode Mode)
{
	return StaticEnum<EWeatherSystemMode>()->GetDisplayNameTextByValue(static_cast<int64>(Mode));
}

FText FWeatherEnumUtils::ToText(ESeason Season)
{
	return StaticEnum<ESeason>()->GetDisplayNameTextByValue(static_cast<int64>(Season));
}
