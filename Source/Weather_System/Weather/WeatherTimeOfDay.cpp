#include "Weather/WeatherTimeOfDay.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"

AWeatherTimeOfDay::AWeatherTimeOfDay()
{
	PrimaryActorTick.bCanEverTick = true;
	// Tick before the weather manager so it always sees this frame's clock.
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	Sun = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Sun"));
	Sun->SetupAttachment(SceneRoot);
	Sun->SetMobility(EComponentMobility::Movable);
	Sun->SetIntensity(10.f);
	Sun->SetAtmosphereSunLight(true);
	Sun->SetAtmosphereSunLightIndex(0);

	Moon = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Moon"));
	Moon->SetupAttachment(SceneRoot);
	Moon->SetMobility(EComponentMobility::Movable);
	Moon->SetIntensity(0.3f);
	Moon->SetLightColor(FLinearColor(0.55f, 0.65f, 1.f));
	Moon->SetAtmosphereSunLight(true);
	Moon->SetAtmosphereSunLightIndex(1);
}

void AWeatherTimeOfDay::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// Preview the starting sun position in the editor viewport.
	if (!HasActorBegunPlay())
	{
		TotalHours = static_cast<double>(StartDayOfYear) * 24.0 + StartTimeOfDay;
		ApplyTotalHours(false);
		UpdateLights(true);
	}
}

void AWeatherTimeOfDay::BeginPlay()
{
	Super::BeginPlay();

	TotalHours = static_cast<double>(StartDayOfYear) * 24.0 + StartTimeOfDay;
	LastHour = -1;
	bDayNightInitialised = false;
	ApplyTotalHours(false);
	UpdateLights(true);
}

void AWeatherTimeOfDay::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bEnableSpeedHotkeys && !bExternallyFrozen)
	{
		if (const APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
		{
			if (PC->WasInputKeyJustPressed(EKeys::RightBracket))
			{
				SetTimeSpeedMultiplier(FMath::Clamp(TimeSpeedMultiplier * 2.f, MinHotkeySpeed, MaxHotkeySpeed));
			}
			if (PC->WasInputKeyJustPressed(EKeys::LeftBracket))
			{
				SetTimeSpeedMultiplier(FMath::Clamp(TimeSpeedMultiplier * 0.5f, MinHotkeySpeed, MaxHotkeySpeed));
			}
			if (PC->WasInputKeyJustPressed(EKeys::Backslash))
			{
				SetTimeSpeedMultiplier(1.f);
			}
		}
	}

	if (!bPaused && !bExternallyFrozen)
	{
		TotalHours += static_cast<double>(DeltaSeconds) * TimeSpeedMultiplier * (24.0 / FMath::Max(DayLengthSeconds, 0.01f));
		ApplyTotalHours(true);
	}
	UpdateLights(false);
}

void AWeatherTimeOfDay::ApplyTotalHours(bool bBroadcast)
{
	const double DayCount = FMath::FloorToDouble(TotalHours / 24.0);
	const int32 PreviousDay = DayOfYear;

	TimeOfDay = static_cast<float>(TotalHours - DayCount * 24.0);
	DayOfYear = static_cast<int32>(FMath::Fmod(DayCount, static_cast<double>(DaysPerYear)));

	const int32 Hour = FMath::FloorToInt(TimeOfDay);
	if (bBroadcast)
	{
		if (Hour != LastHour)
		{
			OnHourChanged.Broadcast(Hour);
		}
		if (DayOfYear != PreviousDay)
		{
			OnDayChanged.Broadcast(DayOfYear);
		}
	}
	LastHour = Hour;
}

void AWeatherTimeOfDay::UpdateLights(bool bForceDayNightRefresh)
{
	if (!Sun || !Moon)
	{
		return;
	}

	// Same arc as the Blueprint version: pitch +90 at midnight, -90 (straight down) at noon.
	const float SunPitch = 90.f - (TimeOfDay / 24.f) * 360.f;
	Sun->SetWorldRotation(FRotator(SunPitch, SunYaw, 0.f));
	Moon->SetWorldRotation(FRotator(SunPitch + 180.f, SunYaw, 0.f));

	// Only touch the (relatively expensive) light settings when day/night actually flips.
	const bool bNewIsDay = TimeOfDay >= SunriseHour && TimeOfDay <= SunsetHour;
	if (bForceDayNightRefresh || !bDayNightInitialised || bNewIsDay != bIsDay)
	{
		const bool bWasInitialised = bDayNightInitialised;
		bIsDay = bNewIsDay;
		bDayNightInitialised = true;

		UDirectionalLightComponent* Active = bIsDay ? Sun.Get() : Moon.Get();
		UDirectionalLightComponent* Inactive = bIsDay ? Moon.Get() : Sun.Get();

		Active->SetActive(true);
		Active->SetForwardShadingPriority(1);
		Active->SetAtmosphereSunLightIndex(0);

		Inactive->SetForwardShadingPriority(0);
		Inactive->SetAtmosphereSunLightIndex(1);
		Inactive->SetActive(false);

		if (bWasInitialised && HasActorBegunPlay())
		{
			OnDayNightChanged.Broadcast(bIsDay);
		}
	}
}

FText AWeatherTimeOfDay::GetTimeText() const
{
	const int32 Hours = FMath::FloorToInt(TimeOfDay);
	const int32 Minutes = FMath::FloorToInt((TimeOfDay - Hours) * 60.f);
	return FText::FromString(FString::Printf(TEXT("%02d:%02d"), Hours, Minutes));
}

void AWeatherTimeOfDay::SetTimeSpeedMultiplier(float NewMultiplier)
{
	TimeSpeedMultiplier = FMath::Max(NewMultiplier, 0.f);
}

void AWeatherTimeOfDay::SetTimeOfDay(float NewHour)
{
	const double DayCount = FMath::FloorToDouble(TotalHours / 24.0);
	TotalHours = DayCount * 24.0 + FMath::Clamp(NewHour, 0.f, 23.999f);
	ApplyTotalHours(true);
	UpdateLights(false);
}

void AWeatherTimeOfDay::SetDayOfYear(int32 NewDay)
{
	const int32 Day = FMath::Clamp(NewDay, 0, DaysPerYear - 1);
	TotalHours = static_cast<double>(Day) * 24.0 + TimeOfDay;
	ApplyTotalHours(true);
}
