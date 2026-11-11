// Simulates seasons and weather and drives rain, snow, fog and clouds.
//
// Two modes:
//   Continuous - time runs on its own; weather evolves from layered noise, season by season.
//   Custom     - time of day, season and weather are set by hand in the "Custom Mode" categories.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UObject/ObjectSaveContext.h"
#include "Weather/WeatherTypes.h"
#include "WeatherManager.generated.h"

class AWeatherTimeOfDay;
class UDataTable;
class UExponentialHeightFogComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UMaterialParameterCollection;
class UNiagaraComponent;
class UNiagaraSystem;
class USceneComponent;
class UUserWidget;
class UVolumetricCloudComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnWeatherStateChanged, EWeatherState, OldState, EWeatherState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSeasonChanged, ESeason, OldSeason, ESeason, NewSeason);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnWeatherModeChanged, EWeatherSystemMode, OldMode, EWeatherSystemMode, NewMode);

UCLASS(Blueprintable)
class WEATHER_SYSTEM_API AWeatherManager : public AActor
{
	GENERATED_BODY()

public:
	AWeatherManager();

	virtual void Tick(float DeltaSeconds) override;
	virtual bool ShouldTickIfViewportsOnly() const override { return true; } // lets the FX follow the editor camera
	virtual void OnConstruction(const FTransform& Transform) override;
#if WITH_EDITOR
	virtual void PreSave(FObjectPreSaveContext ObjectSaveContext) override;
#endif

	// =============================================================================================
	// MODE
	// =============================================================================================

	/** Continuous: everything runs by itself. Custom: you set time of day, season and weather by hand. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mode")
	EWeatherSystemMode Mode = EWeatherSystemMode::Continuous;

	UFUNCTION(BlueprintCallable, Category = "Mode")
	void SetMode(EWeatherSystemMode NewMode);

	UFUNCTION(BlueprintPure, Category = "Mode")
	EWeatherSystemMode GetMode() const { return Mode; }

	UFUNCTION(BlueprintPure, Category = "Mode")
	bool IsCustomMode() const { return Mode == EWeatherSystemMode::Custom; }

	UFUNCTION(BlueprintPure, Category = "Mode")
	FText GetModeText() const { return FWeatherEnumUtils::ToText(Mode); }

	UPROPERTY(BlueprintAssignable, Category = "Mode")
	FOnWeatherModeChanged OnModeChanged;

	// =============================================================================================
	// SHARED (both modes)
	// =============================================================================================

	// ---- Sources & data ------------------------------------------------------------------------

	/** Clock to follow/drive. If empty, the first AWeatherTimeOfDay in the level is used; if none, an internal clock runs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shared|Sources")
	TObjectPtr<AWeatherTimeOfDay> TimeSource;

	/** Row type FWeatherConditionRow, one row per EWeatherState. Built-in defaults are used when empty. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shared|Sources")
	TObjectPtr<UDataTable> ConditionsTable;

	/** Row type FSeasonThresholdRow, one row per ESeason. Built-in defaults are used when empty. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shared|Sources")
	TObjectPtr<UDataTable> SeasonTable;

	/** Optional: receives Rain_Intensity, Fog_Intensity, Wetness and Clouds every frame for use in materials. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shared|Sources")
	TObjectPtr<UMaterialParameterCollection> MaterialCollection;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shared|Sources")
	TSoftObjectPtr<UNiagaraSystem> RainSystem;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shared|Sources")
	TSoftObjectPtr<UNiagaraSystem> SnowSystem;

	// ---- Seasons -------------------------------------------------------------------------------

	/** Season lengths in days; winter takes whatever remains of the year. Used by both modes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shared|Seasons", meta = (ClampMin = "1"))
	int32 SpringLengthDays = 91;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shared|Seasons", meta = (ClampMin = "1"))
	int32 SummerLengthDays = 91;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shared|Seasons", meta = (ClampMin = "1"))
	int32 AutumnLengthDays = 91;

	// ---- Blending ------------------------------------------------------------------------------

	/** How fast the visible values chase their targets (higher = snappier). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shared|Blending", meta = (ClampMin = "0.01"))
	float TransitionSpeed = 0.5f;

	/** When the clock runs faster than normal, blend weather changes (and wetness) proportionally faster too. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shared|Blending")
	bool bScaleTransitionsWithTime = true;

	/** Wetness gained per second at full rain intensity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shared|Blending", meta = (ClampMin = "0.0"))
	float WetnessRiseRate = 0.1f;

	/** Wetness lost per second when it is dry. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shared|Blending", meta = (ClampMin = "0.0"))
	float WetnessDryRate = 0.02f;

	/** Start with fully-developed weather instead of fading in from nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shared|Blending")
	bool bSnapOnBeginPlay = true;

	// ---- Output wiring -------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shared|Output")
	FName CloudCoverageParameter = TEXT("Cloud_GlobalCoverage");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shared|Output")
	FName RainNiagaraParameter = TEXT("RainIntensity");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shared|Output")
	FName SnowNiagaraParameter = TEXT("SnowIntensity");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shared|Output")
	FName RainCollectionParameter = TEXT("Rain_Intensity");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shared|Output")
	FName FogCollectionParameter = TEXT("Fog_Intensity");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shared|Output")
	FName WetnessCollectionParameter = TEXT("Wetness");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shared|Output")
	FName CloudsCollectionParameter = TEXT("Clouds");

	/**
	 * Keeps the rain/snow emitters on top of the viewer so precipitation is always visible:
	 * the local player's pawn while playing, the editor viewport camera in the editor.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shared|Output")
	bool bFollowPlayerCamera = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shared|Output", meta = (EditCondition = "bFollowPlayerCamera"))
	FVector FXFollowOffset = FVector(0.f, 0.f, 500.f);

	// ---- HUD -----------------------------------------------------------------------------------

	/** If set (and bShowHUD), this widget is created and added to the first player's screen on BeginPlay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shared|HUD")
	TSubclassOf<UUserWidget> HUDWidgetClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shared|HUD")
	bool bShowHUD = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shared|HUD")
	int32 HUDZOrder = 10;

	UFUNCTION(BlueprintCallable, Category = "Shared|HUD")
	void SetHUDVisible(bool bVisible);

	// =============================================================================================
	// CONTINUOUS MODE
	// =============================================================================================

	/** Offsets the noise so every seed produces a different (but repeatable) weather sequence. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Continuous Mode|Weather", meta = (EditCondition = "Mode == EWeatherSystemMode::Continuous", EditConditionHides))
	int32 WeatherSeed = 1337;

	/** In-game hours per unit of noise coordinate: bigger = slower, longer-lasting weather. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Continuous Mode|Weather", meta = (ClampMin = "0.1", EditCondition = "Mode == EWeatherSystemMode::Continuous", EditConditionHides))
	float HoursPerNoiseUnit = 6.f;

	/** Stretches the noise away from 0.5 so all weather bands are reached (Perlin noise clusters in the middle). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Continuous Mode|Weather", meta = (ClampMin = "1.0", EditCondition = "Mode == EWeatherSystemMode::Continuous", EditConditionHides))
	float NoiseContrast = 1.8f;

	/** The noise must move at least this far from the value used for the last decision before weather may change. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Continuous Mode|Weather", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "Mode == EWeatherSystemMode::Continuous", EditConditionHides))
	float WeatherSwitchThreshold = 0.1f;

	/** Minimum in-game hours a weather state must last before another change is allowed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Continuous Mode|Weather", meta = (ClampMin = "0.0", EditCondition = "Mode == EWeatherSystemMode::Continuous", EditConditionHides))
	float MinStateDurationHours = 1.f;

	/** Hours per real second used for the noise when no time source exists. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Continuous Mode|Weather", meta = (ClampMin = "0.0", EditCondition = "Mode == EWeatherSystemMode::Continuous", EditConditionHides))
	float FallbackHoursPerSecond = 0.5f;

	// (The Continuous-mode clock settings - day length, start time, speed multiplier - live on AWeatherTimeOfDay
	//  under "Continuous Mode|Clock".)

	// =============================================================================================
	// CUSTOM MODE
	// =============================================================================================

	// ---- Time ----------------------------------------------------------------------------------

	/** Hour of the day (0-24) the sun/moon are set to. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Mode|Time", meta = (ClampMin = "0.0", ClampMax = "24.0", EditCondition = "Mode == EWeatherSystemMode::Custom", EditConditionHides))
	float CustomTimeOfDay = 12.f;

	/** If on, the clock keeps running from the custom time (at the clock's speed multiplier). If off, time is frozen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Mode|Time", meta = (EditCondition = "Mode == EWeatherSystemMode::Custom", EditConditionHides))
	bool bCustomTimeFlows = false;

	// ---- Season --------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Mode|Season", meta = (EditCondition = "Mode == EWeatherSystemMode::Custom", EditConditionHides))
	ESeason CustomSeason = ESeason::Spring;

	/** Day inside the chosen season (0 = first day). Clamped to the season's length. Sets the day of the year. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Mode|Season", meta = (ClampMin = "0", EditCondition = "Mode == EWeatherSystemMode::Custom", EditConditionHides))
	int32 CustomDayInSeason = 0;

	// ---- Weather -------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Mode|Weather", meta = (EditCondition = "Mode == EWeatherSystemMode::Custom", EditConditionHides))
	EWeatherState CustomWeatherState = EWeatherState::Clear;

	/** Rain or snow for precipitation states. Auto rolls it from the season's snow chance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Mode|Weather", meta = (EditCondition = "Mode == EWeatherSystemMode::Custom", EditConditionHides))
	ECustomPrecipitation CustomPrecipitation = ECustomPrecipitation::Auto;

	/** Where inside the state's range the weather sits: 0 = the range minimum, 1 = the maximum. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Mode|Weather", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "Mode == EWeatherSystemMode::Custom", EditConditionHides))
	float CustomIntensity = 0.5f;

	/** Off = changes apply instantly. On = they blend over time (see Shared|Blending). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Mode|Weather", meta = (EditCondition = "Mode == EWeatherSystemMode::Custom", EditConditionHides))
	bool bCustomSmoothTransitions = true;

	// =============================================================================================
	// QUERIES & CONTROL
	// =============================================================================================

	UFUNCTION(BlueprintPure, Category = "Weather")
	EWeatherState GetWeatherState() const { return WeatherState; }

	UFUNCTION(BlueprintPure, Category = "Weather")
	ESeason GetSeason() const { return Season; }

	UFUNCTION(BlueprintPure, Category = "Weather")
	const FWeatherSnapshot& GetCurrentWeather() const { return Current; }

	/** The 0..1 noise value that currently selects the weather band (Continuous mode). */
	UFUNCTION(BlueprintPure, Category = "Weather")
	float GetWeatherNoise() const { return WeatherNoise; }

	UFUNCTION(BlueprintPure, Category = "Weather")
	bool IsSnowing() const { return bPrecipitationIsSnow && FWeatherEnumUtils::IsPrecipitation(WeatherState); }

	UFUNCTION(BlueprintPure, Category = "Weather")
	FText GetWeatherText() const { return FWeatherEnumUtils::ToText(WeatherState); }

	UFUNCTION(BlueprintPure, Category = "Weather")
	FText GetSeasonText() const { return FWeatherEnumUtils::ToText(Season); }

	/** Continuous mode only: pins the weather to a state until ClearWeatherOverride is called. Blends smoothly. */
	UFUNCTION(BlueprintCallable, Category = "Weather")
	void SetWeatherOverride(EWeatherState NewState);

	UFUNCTION(BlueprintCallable, Category = "Weather")
	void ClearWeatherOverride();

	UFUNCTION(BlueprintPure, Category = "Weather")
	bool IsWeatherOverridden() const { return bOverrideActive; }

	/** Jumps all output values to their targets instantly (no blending). */
	UFUNCTION(BlueprintCallable, Category = "Weather")
	void SnapToTarget();

	/** Day of the year (0-based) at which a season starts, using the season lengths above. */
	UFUNCTION(BlueprintPure, Category = "Weather")
	int32 GetSeasonStartDay(ESeason InSeason) const;

	UFUNCTION(BlueprintPure, Category = "Weather")
	int32 GetSeasonLengthDays(ESeason InSeason) const;

	UPROPERTY(BlueprintAssignable, Category = "Weather")
	FOnWeatherStateChanged OnWeatherStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Weather")
	FOnSeasonChanged OnSeasonChanged;

	// ---- Components ----------------------------------------------------------------------------

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UVolumetricCloudComponent> VolumetricCloud;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UExponentialHeightFogComponent> ExponentialHeightFog;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UNiagaraComponent> FX_Rain;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UNiagaraComponent> FX_Snow;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** Where the rain/snow should be: the player pawn in a game world, the editor viewport camera otherwise. */
	bool GetFXFollowLocation(FVector& OutLocation) const;
	void FollowViewer();

	/** Editor-only: applies the current Mode's settings once so changes show in the viewport without pressing Play. */
	void EditorPreview();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> BaseCloudMaterial;

	void CreateHUD();

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> HUDWidget;

	// Simulation steps
	double GetWorldHours(float DeltaSeconds);
	float SampleNoise(double Coordinate) const;
	void UpdateSeason(int32 DayOfYear);
	void UpdateWeatherState(double Hours);
	void ComputeTarget(float IntensityAlpha);
	void AdvanceCurrent(float DeltaSeconds);
	void ApplyOutputs(float DeltaSeconds);
	void ChangeWeatherState(EWeatherState NewState);
	void SetSeasonDirect(ESeason NewSeason);

	/** Runs one frame of the active mode and returns the 0..1 intensity position inside the state's ranges. */
	float EvaluateActiveMode(double Hours, float DeltaSeconds);
	float EvaluateContinuous(double Hours);
	float EvaluateCustom();
	void EnterMode(EWeatherSystemMode NewMode, double Hours);
	void ApplyCustomClock();
	void CopyTargetToCurrent();
	int32 GetCurrentDayOfYear(double Hours) const;

	// Data lookup (DataTable first, built-in defaults as fallback)
	FWeatherConditionRow GetCondition(EWeatherState State) const;
	FSeasonThresholdRow GetSeasonRow(ESeason InSeason) const;

	// Output helpers
	void EnsureCloudMaterial();
	void ApplyNiagaraSystems();
	/** Returns true when the parameter was written (the component was already running). */
	static bool DriveParticles(UNiagaraComponent* Component, FName Parameter, float Intensity);

	ESeason Season = ESeason::Spring;
	EWeatherState WeatherState = EWeatherState::Clear;
	FWeatherSnapshot Current;
	FWeatherSnapshot Target;

	float WeatherNoise = 0.5f;
	float LastDecisionNoise = 0.f;
	double LastChangeHours = 0.0;
	double FallbackHours = 0.0;
	bool bPrecipitationIsSnow = false;
	bool bOverrideActive = false;
	EWeatherState OverrideState = EWeatherState::Clear;
	bool bNeedsReevaluation = true;
	bool bInitialised = false;
	bool bSeasonInitialised = false;

	// Mode bookkeeping
	EWeatherSystemMode ActiveMode = EWeatherSystemMode::Continuous;
	bool bModeEntered = false;
	float AppliedCustomTime = -1.f;
	ESeason AppliedCustomSeason = ESeason::Spring;
	int32 AppliedCustomDay = -1;
	bool bAppliedCustomFlows = false;
	bool bCustomClockDirty = true;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CloudMaterial;

	UPROPERTY(Transient)
	TObjectPtr<AWeatherTimeOfDay> ResolvedTime;
};
