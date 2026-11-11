// Owns the world clock (time of day, day of year) and drives the sun and moon lights.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WeatherTimeOfDay.generated.h"

class UDirectionalLightComponent;
class USceneComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHourChanged, int32, NewHour);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDayChanged, int32, NewDayOfYear);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDayNightChanged, bool, bIsDay);

UCLASS(Blueprintable)
class WEATHER_SYSTEM_API AWeatherTimeOfDay : public AActor
{
	GENERATED_BODY()

public:
	AWeatherTimeOfDay();

	virtual void Tick(float DeltaSeconds) override;
	virtual void OnConstruction(const FTransform& Transform) override;

	// ---- Clock ---------------------------------------------------------------------------------

	/** Real-time seconds one full 24h in-game day takes. (Original: SunSpeed 0.5 h/s == 48 s.) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Continuous Mode|Clock", meta = (ClampMin = "1.0"))
	float DayLengthSeconds = 48.f;

	/** In-game hour (0..24) the clock starts at. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Continuous Mode|Clock", meta = (ClampMin = "0.0", ClampMax = "24.0"))
	float StartTimeOfDay = 8.f;

	/** Day of the year (0-based) the clock starts at. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Continuous Mode|Clock", meta = (ClampMin = "0"))
	int32 StartDayOfYear = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Time", meta = (ClampMin = "1"))
	int32 DaysPerYear = 365;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Continuous Mode|Clock")
	bool bPaused = false;

	/** Multiplies how fast game time passes (1 = normal, 10 = ten times faster). Weather follows game time, so it speeds up too. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Continuous Mode|Clock", meta = (ClampMin = "0.0", ClampMax = "10000.0"))
	float TimeSpeedMultiplier = 1.f;

	/** Lets you change TimeSpeedMultiplier while playing with hotkeys: ] doubles, [ halves, \ resets to 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Continuous Mode|Clock")
	bool bEnableSpeedHotkeys = true;

	/** Hotkey limits. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Continuous Mode|Clock", meta = (ClampMin = "0.01"))
	float MinHotkeySpeed = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Continuous Mode|Clock", meta = (ClampMin = "1.0"))
	float MaxHotkeySpeed = 1024.f;

	UFUNCTION(BlueprintPure, Category = "Time")
	float GetTimeSpeedMultiplier() const { return TimeSpeedMultiplier; }

	UFUNCTION(BlueprintCallable, Category = "Time")
	void SetTimeSpeedMultiplier(float NewMultiplier);

	/**
	 * Freezes/unfreezes the clock from outside (used by AWeatherManager in Custom mode).
	 * Independent of bPaused, which is the Continuous-mode pause switch.
	 */
	UFUNCTION(BlueprintCallable, Category = "Time")
	void SetExternalFreeze(bool bFreeze) { bExternallyFrozen = bFreeze; }

	UFUNCTION(BlueprintPure, Category = "Time")
	bool IsClockRunning() const { return !bPaused && !bExternallyFrozen; }

	/** Hour at which the sun takes over from the moon. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun|Moon", meta = (ClampMin = "0.0", ClampMax = "24.0"))
	float SunriseHour = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun|Moon", meta = (ClampMin = "0.0", ClampMax = "24.0"))
	float SunsetHour = 18.f;

	/** Compass heading of the sun's arc (world yaw, degrees). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun|Moon")
	float SunYaw = 0.f;

	// ---- Queries -------------------------------------------------------------------------------

	/** Current hour of the day in [0, 24). */
	UFUNCTION(BlueprintPure, Category = "Time")
	float GetTimeOfDay() const { return TimeOfDay; }

	UFUNCTION(BlueprintPure, Category = "Time")
	int32 GetDayOfYear() const { return DayOfYear; }

	/** Hours elapsed since day 0, hour 0 (monotonic; does not wrap at the year boundary). */
	UFUNCTION(BlueprintPure, Category = "Time")
	double GetTotalHours() const { return TotalHours; }

	UFUNCTION(BlueprintPure, Category = "Time")
	bool IsDaytime() const { return bIsDay; }

	/** "HH:MM" style text for HUDs. */
	UFUNCTION(BlueprintPure, Category = "Time")
	FText GetTimeText() const;

	UFUNCTION(BlueprintCallable, Category = "Time")
	void SetTimeOfDay(float NewHour);

	UFUNCTION(BlueprintCallable, Category = "Time")
	void SetDayOfYear(int32 NewDay);

	// ---- Events --------------------------------------------------------------------------------

	UPROPERTY(BlueprintAssignable, Category = "Time")
	FOnHourChanged OnHourChanged;

	UPROPERTY(BlueprintAssignable, Category = "Time")
	FOnDayChanged OnDayChanged;

	UPROPERTY(BlueprintAssignable, Category = "Time")
	FOnDayNightChanged OnDayNightChanged;

	// ---- Components ----------------------------------------------------------------------------

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UDirectionalLightComponent> Sun;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UDirectionalLightComponent> Moon;

protected:
	virtual void BeginPlay() override;

private:
	/** Recomputes TimeOfDay/DayOfYear from TotalHours and fires events. */
	void ApplyTotalHours(bool bBroadcast);
	void UpdateLights(bool bForceDayNightRefresh);

	double TotalHours = 0.0;
	float TimeOfDay = 0.f;
	int32 DayOfYear = 0;
	int32 LastHour = -1;
	bool bIsDay = true;
	bool bDayNightInitialised = false;
	bool bExternallyFrozen = false;
};
