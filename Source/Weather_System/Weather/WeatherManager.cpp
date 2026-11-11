#include "Weather/WeatherManager.h"

#include "Weather/WeatherTimeOfDay.h"
#include "Weather/WeatherHUDWidget.h"

#include "Blueprint/UserWidget.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "UObject/ObjectSaveContext.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SceneComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/DataTable.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogWeatherManager, Log, All);

AWeatherManager::AWeatherManager()
{
	PrimaryActorTick.bCanEverTick = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	VolumetricCloud = CreateDefaultSubobject<UVolumetricCloudComponent>(TEXT("VolumetricCloud"));
	VolumetricCloud->SetupAttachment(SceneRoot);

	ExponentialHeightFog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("ExponentialHeightFog"));
	ExponentialHeightFog->SetupAttachment(SceneRoot);

	FX_Rain = CreateDefaultSubobject<UNiagaraComponent>(TEXT("FX_Rain"));
	FX_Rain->SetupAttachment(SceneRoot);
	FX_Rain->SetAutoActivate(false);

	FX_Snow = CreateDefaultSubobject<UNiagaraComponent>(TEXT("FX_Snow"));
	FX_Snow->SetupAttachment(SceneRoot);
	FX_Snow->SetAutoActivate(false);
}

// -------------------------------------------------------------------------------------------------
// Lifecycle
// -------------------------------------------------------------------------------------------------

void AWeatherManager::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyNiagaraSystems();
	// Runs on every property edit in the editor, so Custom-mode changes show immediately.
	// Deferred one tick: during construction the components are unregistered, and Niagara cannot activate until they are registered again.
	if (UWorld* World = GetWorld())
	{
		if (World->WorldType == EWorldType::Editor && !HasActorBegunPlay() && !IsTemplate())
		{
			TWeakObjectPtr<AWeatherManager> WeakThis(this);
			World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([WeakThis]()
			{
				if (WeakThis.IsValid())
				{
					WeakThis->EditorPreview();
				}
			}));
		}
	}
}

void AWeatherManager::EditorPreview()
{
	const UWorld* World = GetWorld();
	if (!World || World->WorldType != EWorldType::Editor || HasActorBegunPlay() || IsTemplate())
	{
		return;
	}

	ResolvedTime = TimeSource ? TimeSource.Get()
		: Cast<AWeatherTimeOfDay>(UGameplayStatics::GetActorOfClass(this, AWeatherTimeOfDay::StaticClass()));

	ActiveMode = Mode;
	bCustomClockDirty = true;
	bInitialised = false;
	float Alpha = 0.5f;

	if (Mode == EWeatherSystemMode::Custom)
	{
		bOverrideActive = false;
		Alpha = EvaluateCustom();
	}
	else
	{
		// Continuous: hand the clock back and show the starting state.
		if (ResolvedTime)
		{
			ResolvedTime->SetExternalFreeze(false);
			ResolvedTime->RerunConstructionScripts();
		}
		bNeedsReevaluation = true;
		const double Hours = ResolvedTime ? ResolvedTime->GetTotalHours() : 0.0;
		LastChangeHours = Hours;
		Alpha = EvaluateContinuous(Hours);
	}

	ComputeTarget(Alpha);
	SnapToTarget();

	// Niagara was possibly just activated by that call; write its parameters on the next tick.
	TWeakObjectPtr<AWeatherManager> WeakThis(this);
	GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([WeakThis]()
	{
		if (WeakThis.IsValid() && !WeakThis->HasActorBegunPlay())
		{
			WeakThis->ApplyOutputs(0.f);
		}
	}));
}

#if WITH_EDITOR
void AWeatherManager::PreSave(FObjectPreSaveContext ObjectSaveContext)
{
	// Never save the transient preview material into the level.
	if (VolumetricCloud && CloudMaterial && BaseCloudMaterial && VolumetricCloud->GetMaterial() == CloudMaterial)
	{
		VolumetricCloud->SetMaterial(BaseCloudMaterial);
	}
	Super::PreSave(ObjectSaveContext);
}
#endif

void AWeatherManager::BeginPlay()
{
	Super::BeginPlay();

	ResolvedTime = TimeSource ? TimeSource.Get()
		: Cast<AWeatherTimeOfDay>(UGameplayStatics::GetActorOfClass(this, AWeatherTimeOfDay::StaticClass()));
	if (ResolvedTime)
	{
		// Always evaluate after the clock has advanced this frame.
		AddTickPrerequisiteActor(ResolvedTime);
	}
	else
	{
		UE_LOG(LogWeatherManager, Warning, TEXT("%s: no AWeatherTimeOfDay found, using an internal clock."), *GetName());
	}

	ApplyNiagaraSystems();
	EnsureCloudMaterial();

	// Prime the simulation so the very first frame already shows the right weather.
	const double Hours = GetWorldHours(0.f);
	LastChangeHours = Hours;
	EnterMode(Mode, Hours);
	const float Alpha = EvaluateActiveMode(Hours, 0.f);
	ComputeTarget(Alpha);
	if (bSnapOnBeginPlay || (Mode == EWeatherSystemMode::Custom && !bCustomSmoothTransitions))
	{
		SnapToTarget();
	}
	bInitialised = true;

	// The clock actor resets itself in its own BeginPlay, which may run after ours: re-apply the custom time on the first tick.
	bCustomClockDirty = true;

	CreateHUD();
}

void AWeatherManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HUDWidget)
	{
		HUDWidget->RemoveFromParent();
		HUDWidget = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void AWeatherManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// In the editor (no play session) the simulation does not run; only keep the FX around the viewport camera.
	if (const UWorld* World = GetWorld(); World && !World->IsGameWorld())
	{
		FollowViewer();
		return;
	}

	const double Hours = GetWorldHours(DeltaSeconds);

	// The mode can be flipped at any time from the details panel or from Blueprints.
	if (Mode != ActiveMode)
	{
		const EWeatherSystemMode OldMode = ActiveMode;
		EnterMode(Mode, Hours);
		OnModeChanged.Broadcast(OldMode, Mode);
	}

	const float Alpha = EvaluateActiveMode(Hours, DeltaSeconds);
	ComputeTarget(Alpha);

	if (ActiveMode == EWeatherSystemMode::Custom && !bCustomSmoothTransitions)
	{
		CopyTargetToCurrent();
	}
	else
	{
		const float TimeFactor = (bScaleTransitionsWithTime && ResolvedTime) ? FMath::Max(1.f, ResolvedTime->GetTimeSpeedMultiplier()) : 1.f;
		AdvanceCurrent(DeltaSeconds * TimeFactor);
	}
	ApplyOutputs(DeltaSeconds);
}

// -------------------------------------------------------------------------------------------------
// Modes
// -------------------------------------------------------------------------------------------------

void AWeatherManager::SetMode(EWeatherSystemMode NewMode)
{
	Mode = NewMode; // picked up (and broadcast) on the next tick
}

void AWeatherManager::EnterMode(EWeatherSystemMode NewMode, double Hours)
{
	ActiveMode = NewMode;
	bModeEntered = true;

	if (NewMode == EWeatherSystemMode::Custom)
	{
		bOverrideActive = false;
		bCustomClockDirty = true;
	}
	else
	{
		// Hand the clock back and let the noise pick the weather again from where we are.
		if (ResolvedTime)
		{
			ResolvedTime->SetExternalFreeze(false);
		}
		bNeedsReevaluation = true;
		LastChangeHours = Hours;
	}
}

float AWeatherManager::EvaluateActiveMode(double Hours, float DeltaSeconds)
{
	return ActiveMode == EWeatherSystemMode::Custom ? EvaluateCustom() : EvaluateContinuous(Hours);
}

float AWeatherManager::EvaluateContinuous(double Hours)
{
	const double Coordinate = Hours / FMath::Max(HoursPerNoiseUnit, 0.1f);

	UpdateSeason(GetCurrentDayOfYear(Hours));
	UpdateWeatherState(Hours);

	// A second noise channel moves the intensity smoothly inside the current state's range.
	return SampleNoise(Coordinate * 1.7 + 113.7);
}

float AWeatherManager::EvaluateCustom()
{
	// Time: re-apply only when one of the custom time/season values (or the flow flag) was edited.
	const int32 DayInSeason = FMath::Clamp(CustomDayInSeason, 0, GetSeasonLengthDays(CustomSeason) - 1);
	if (bCustomClockDirty
		|| !FMath::IsNearlyEqual(CustomTimeOfDay, AppliedCustomTime)
		|| CustomSeason != AppliedCustomSeason
		|| DayInSeason != AppliedCustomDay
		|| bCustomTimeFlows != bAppliedCustomFlows)
	{
		ApplyCustomClock();
	}

	// Season: fixed while time is frozen; follows the calendar once time is allowed to flow.
	if (bCustomTimeFlows && ResolvedTime)
	{
		UpdateSeason(ResolvedTime->GetDayOfYear());
	}
	else
	{
		SetSeasonDirect(CustomSeason);
	}

	// Weather
	if (CustomWeatherState != WeatherState)
	{
		ChangeWeatherState(CustomWeatherState);
	}
	if (CustomPrecipitation == ECustomPrecipitation::Rain)
	{
		bPrecipitationIsSnow = false;
	}
	else if (CustomPrecipitation == ECustomPrecipitation::Snow)
	{
		bPrecipitationIsSnow = true;
	}

	return FMath::Clamp(CustomIntensity, 0.f, 1.f);
}

void AWeatherManager::ApplyCustomClock()
{
	const int32 DayInSeason = FMath::Clamp(CustomDayInSeason, 0, GetSeasonLengthDays(CustomSeason) - 1);

	if (ResolvedTime)
	{
		ResolvedTime->SetDayOfYear(GetSeasonStartDay(CustomSeason) + DayInSeason);
		ResolvedTime->SetTimeOfDay(CustomTimeOfDay);
		ResolvedTime->SetExternalFreeze(!bCustomTimeFlows);
	}

	AppliedCustomTime = CustomTimeOfDay;
	AppliedCustomSeason = CustomSeason;
	AppliedCustomDay = DayInSeason;
	bAppliedCustomFlows = bCustomTimeFlows;
	bCustomClockDirty = false;
}

int32 AWeatherManager::GetSeasonLengthDays(ESeason InSeason) const
{
	const int32 YearLength = ResolvedTime ? ResolvedTime->DaysPerYear : 365;
	switch (InSeason)
	{
	case ESeason::Spring: return FMath::Max(1, SpringLengthDays);
	case ESeason::Summer: return FMath::Max(1, SummerLengthDays);
	case ESeason::Autumn: return FMath::Max(1, AutumnLengthDays);
	default:              return FMath::Max(1, YearLength - GetSeasonStartDay(ESeason::Winter));
	}
}

int32 AWeatherManager::GetSeasonStartDay(ESeason InSeason) const
{
	switch (InSeason)
	{
	case ESeason::Spring: return 0;
	case ESeason::Summer: return SpringLengthDays;
	case ESeason::Autumn: return SpringLengthDays + SummerLengthDays;
	default:              return SpringLengthDays + SummerLengthDays + AutumnLengthDays;
	}
}

int32 AWeatherManager::GetCurrentDayOfYear(double Hours) const
{
	return ResolvedTime ? ResolvedTime->GetDayOfYear()
		: static_cast<int32>(FMath::Fmod(FMath::FloorToDouble(Hours / 24.0), 365.0));
}

// -------------------------------------------------------------------------------------------------
// Simulation
// -------------------------------------------------------------------------------------------------

double AWeatherManager::GetWorldHours(float DeltaSeconds)
{
	if (ResolvedTime)
	{
		return ResolvedTime->GetTotalHours();
	}
	FallbackHours += static_cast<double>(DeltaSeconds) * FallbackHoursPerSecond;
	return FallbackHours;
}

float AWeatherManager::SampleNoise(double Coordinate) const
{
	// Fractal Perlin noise (3 octaves) with a per-seed offset, remapped to 0..1.
	const float Offset = FMath::Fmod(static_cast<float>(WeatherSeed) * 13.37f, 1024.f);
	const float X = static_cast<float>(Coordinate) + Offset;

	float Sum = 0.f, Amplitude = 0.5f, Frequency = 1.f, Norm = 0.f;
	for (int32 Octave = 0; Octave < 3; ++Octave)
	{
		Sum += Amplitude * FMath::PerlinNoise1D(X * Frequency + Octave * 37.1f);
		Norm += Amplitude;
		Amplitude *= 0.5f;
		Frequency *= 2.f;
	}

	const float Unit = 0.5f + 0.5f * (Sum / Norm);
	return FMath::Clamp(0.5f + (Unit - 0.5f) * NoiseContrast, 0.f, 1.f);
}

void AWeatherManager::UpdateSeason(int32 DayOfYear)
{
	ESeason NewSeason;
	if (DayOfYear < SpringLengthDays)
	{
		NewSeason = ESeason::Spring;
	}
	else if (DayOfYear < SpringLengthDays + SummerLengthDays)
	{
		NewSeason = ESeason::Summer;
	}
	else if (DayOfYear < SpringLengthDays + SummerLengthDays + AutumnLengthDays)
	{
		NewSeason = ESeason::Autumn;
	}
	else
	{
		NewSeason = ESeason::Winter;
	}
	SetSeasonDirect(NewSeason);
}

void AWeatherManager::SetSeasonDirect(ESeason NewSeason)
{
	if (!bSeasonInitialised)
	{
		bSeasonInitialised = true;
		Season = NewSeason;
		return;
	}
	if (NewSeason != Season)
	{
		const ESeason Old = Season;
		Season = NewSeason;
		bNeedsReevaluation = true; // new season = new thresholds
		OnSeasonChanged.Broadcast(Old, NewSeason);
	}
}

void AWeatherManager::UpdateWeatherState(double Hours)
{
	const double Coordinate = Hours / FMath::Max(HoursPerNoiseUnit, 0.1f);
	WeatherNoise = SampleNoise(Coordinate);

	if (Hours < LastChangeHours)
	{
		LastChangeHours = Hours; // clock was rewound
	}

	if (bOverrideActive)
	{
		if (OverrideState != WeatherState)
		{
			LastChangeHours = Hours;
			ChangeWeatherState(OverrideState);
		}
		return;
	}

	const bool bFarEnough = FMath::Abs(WeatherNoise - LastDecisionNoise) >= WeatherSwitchThreshold;
	const bool bLongEnough = (Hours - LastChangeHours) >= MinStateDurationHours;
	if (bNeedsReevaluation || (bFarEnough && bLongEnough))
	{
		bNeedsReevaluation = false;
		LastDecisionNoise = WeatherNoise;

		const EWeatherState Desired = GetSeasonRow(Season).Categorize(WeatherNoise);
		if (Desired != WeatherState || !bInitialised)
		{
			LastChangeHours = Hours;
			ChangeWeatherState(Desired);
		}
	}
}

void AWeatherManager::ChangeWeatherState(EWeatherState NewState)
{
	const EWeatherState Old = WeatherState;

	// Decide rain vs snow once, when a precipitation event begins, and keep it for the whole event.
	if (!FWeatherEnumUtils::IsPrecipitation(Old) && FWeatherEnumUtils::IsPrecipitation(NewState))
	{
		FRandomStream Stream(WeatherSeed ^ static_cast<int32>(LastChangeHours * 100.0));
		bPrecipitationIsSnow = Stream.FRand() < GetSeasonRow(Season).SnowChance;
	}

	WeatherState = NewState;
	if (bInitialised && Old != NewState)
	{
		OnWeatherStateChanged.Broadcast(Old, NewState);
	}
}

void AWeatherManager::ComputeTarget(float IntensityAlpha)
{
	const FWeatherConditionRow Condition = GetCondition(WeatherState);
	const bool bSnow = IsSnowing();

	Target.Rain = bSnow ? 0.f : Condition.Rain.Evaluate(IntensityAlpha);
	Target.Snow = bSnow ? Condition.Snow.Evaluate(IntensityAlpha) : 0.f;
	Target.Fog = Condition.Fog.Evaluate(IntensityAlpha);
	Target.Clouds = Condition.Clouds.Evaluate(IntensityAlpha);
}

void AWeatherManager::AdvanceCurrent(float DeltaSeconds)
{
	Current.Rain = FMath::FInterpTo(Current.Rain, Target.Rain, DeltaSeconds, TransitionSpeed);
	Current.Snow = FMath::FInterpTo(Current.Snow, Target.Snow, DeltaSeconds, TransitionSpeed);
	Current.Fog = FMath::FInterpTo(Current.Fog, Target.Fog, DeltaSeconds, TransitionSpeed);
	Current.Clouds = FMath::FInterpTo(Current.Clouds, Target.Clouds, DeltaSeconds, TransitionSpeed);

	const float WetTarget = FMath::Clamp(Current.Rain * 1.5f + Current.Snow * 0.5f, 0.f, 1.f);
	if (WetTarget > Current.Wetness)
	{
		Current.Wetness = FMath::Min(WetTarget, Current.Wetness + DeltaSeconds * WetnessRiseRate * FMath::Max(WetTarget, 0.1f));
	}
	else
	{
		Current.Wetness = FMath::Max(WetTarget, Current.Wetness - DeltaSeconds * WetnessDryRate);
	}
}

void AWeatherManager::CopyTargetToCurrent()
{
	Current.Rain = Target.Rain;
	Current.Snow = Target.Snow;
	Current.Fog = Target.Fog;
	Current.Clouds = Target.Clouds;
	Current.Wetness = FMath::Clamp(Current.Rain * 1.5f + Current.Snow * 0.5f, 0.f, 1.f);
}

void AWeatherManager::SnapToTarget()
{
	CopyTargetToCurrent();
	ApplyOutputs(0.f);
}

void AWeatherManager::SetWeatherOverride(EWeatherState NewState)
{
	bOverrideActive = true;
	OverrideState = NewState;
}

void AWeatherManager::ClearWeatherOverride()
{
	bOverrideActive = false;
	bNeedsReevaluation = true;
}

// -------------------------------------------------------------------------------------------------
// Data
// -------------------------------------------------------------------------------------------------

FWeatherConditionRow AWeatherManager::GetCondition(EWeatherState State) const
{
	if (ConditionsTable)
	{
		if (const FWeatherConditionRow* Row = ConditionsTable->FindRow<FWeatherConditionRow>(FWeatherEnumUtils::ToRowName(State), TEXT("AWeatherManager::GetCondition"), false))
		{
			return *Row;
		}
	}
	return FWeatherDefaults::GetCondition(State);
}

FSeasonThresholdRow AWeatherManager::GetSeasonRow(ESeason InSeason) const
{
	if (SeasonTable)
	{
		if (const FSeasonThresholdRow* Row = SeasonTable->FindRow<FSeasonThresholdRow>(FWeatherEnumUtils::ToRowName(InSeason), TEXT("AWeatherManager::GetSeasonRow"), false))
		{
			return *Row;
		}
	}
	return FWeatherDefaults::GetSeasonThresholds(InSeason);
}

// -------------------------------------------------------------------------------------------------
// Output
// -------------------------------------------------------------------------------------------------

void AWeatherManager::EnsureCloudMaterial()
{
	if (!VolumetricCloud)
	{
		return;
	}
	if (CloudMaterial)
	{
		if (VolumetricCloud->GetMaterial() != CloudMaterial)
		{
			VolumetricCloud->SetMaterial(CloudMaterial); // re-attach after a save restored the base material
		}
		return;
	}

	UMaterialInterface* Base = VolumetricCloud->GetMaterial();
	if (!Base)
	{
		Base = VolumetricCloud->Material.LoadSynchronous();
	}
	if (Base)
	{
		BaseCloudMaterial = Base;
		CloudMaterial = UMaterialInstanceDynamic::Create(Base, this);
		VolumetricCloud->SetMaterial(CloudMaterial);
	}
	else
	{
		static bool bWarned = false; // this runs every frame; say it once
		if (!bWarned)
		{
			bWarned = true;
			UE_LOG(LogWeatherManager, Warning, TEXT("%s: VolumetricCloud has no material, cloud coverage will not be driven."), *GetName());
		}
	}
}

void AWeatherManager::ApplyNiagaraSystems()
{
	if (FX_Rain && !RainSystem.IsNull() && !FX_Rain->GetAsset())
	{
		FX_Rain->SetAsset(RainSystem.LoadSynchronous());
	}
	if (FX_Snow && !SnowSystem.IsNull() && !FX_Snow->GetAsset())
	{
		FX_Snow->SetAsset(SnowSystem.LoadSynchronous());
	}
}

bool AWeatherManager::GetFXFollowLocation(FVector& OutLocation) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	if (World->IsGameWorld())
	{
		if (const APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
		{
			if (const APawn* Pawn = PC->GetPawn())
			{
				OutLocation = Pawn->GetActorLocation();
				return true;
			}
			if (PC->PlayerCameraManager)
			{
				OutLocation = PC->PlayerCameraManager->GetCameraLocation();
				return true;
			}
		}
		return false;
	}

	// Editor: the camera of whichever viewport rendered last.
	if (World->ViewLocationsRenderedLastFrame.Num() > 0)
	{
		OutLocation = World->ViewLocationsRenderedLastFrame[0];
		return true;
	}
	return false;
}

void AWeatherManager::FollowViewer()
{
	FVector Location;
	if (bFollowPlayerCamera && FX_Rain && FX_Snow && GetFXFollowLocation(Location))
	{
		Location += FXFollowOffset;
		FX_Rain->SetWorldLocation(Location);
		FX_Snow->SetWorldLocation(Location);
	}
}

bool AWeatherManager::DriveParticles(UNiagaraComponent* Component, FName Parameter, float Intensity)
{
	if (!Component || !Component->GetAsset() || !Component->IsRegistered())
	{
		return false;
	}

	if (Intensity > 0.002f)
	{
		if (!Component->IsActive())
		{
			// Parameters can only be written once the system instance exists (writing in the same frame
			// trips a Niagara ensure), so activate now and write them on the next call.
			Component->Activate();
			return false;
		}
		Component->SetVariableFloat(Parameter, Intensity);
		return true;
	}
	else if (Component->IsActive())
	{
		Component->SetVariableFloat(Parameter, 0.f);
		Component->Deactivate(); // lets live particles finish instead of popping out
	}
	return false;
}

void AWeatherManager::ApplyOutputs(float DeltaSeconds)
{
	// Fog
	if (ExponentialHeightFog)
	{
		if (!ExponentialHeightFog->bEnableVolumetricFog)
		{
			ExponentialHeightFog->SetVolumetricFog(true);
		}
		ExponentialHeightFog->SetFogDensity(Current.Fog);
	}

	// Clouds
	EnsureCloudMaterial();
	if (CloudMaterial)
	{
		CloudMaterial->SetScalarParameterValue(CloudCoverageParameter, Current.Clouds);
	}

	// Particles
	FollowViewer();
	DriveParticles(FX_Rain, RainNiagaraParameter, Current.Rain);
	DriveParticles(FX_Snow, SnowNiagaraParameter, Current.Snow);

	// Material parameter collection (global scalars for landscape/foliage/post-process materials)
	if (MaterialCollection)
	{
		UKismetMaterialLibrary::SetScalarParameterValue(this, MaterialCollection, RainCollectionParameter, Current.Rain);
		UKismetMaterialLibrary::SetScalarParameterValue(this, MaterialCollection, FogCollectionParameter, Current.Fog);
		UKismetMaterialLibrary::SetScalarParameterValue(this, MaterialCollection, WetnessCollectionParameter, Current.Wetness);
		UKismetMaterialLibrary::SetScalarParameterValue(this, MaterialCollection, CloudsCollectionParameter, Current.Clouds);
	}
}

// -------------------------------------------------------------------------------------------------
// HUD
// -------------------------------------------------------------------------------------------------

void AWeatherManager::CreateHUD()
{
	if (!bShowHUD || !HUDWidgetClass || HUDWidget)
	{
		return;
	}

	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (!PC || !PC->IsLocalController())
	{
		return; // dedicated server / no local player
	}

	HUDWidget = CreateWidget<UUserWidget>(PC, HUDWidgetClass);
	if (HUDWidget)
	{
		if (UWeatherHUDWidget* WeatherHUD = Cast<UWeatherHUDWidget>(HUDWidget))
		{
			WeatherHUD->WeatherManager = this;
			WeatherHUD->TimeOfDay = ResolvedTime;
		}
		HUDWidget->AddToViewport(HUDZOrder);
	}
}

void AWeatherManager::SetHUDVisible(bool bVisible)
{
	bShowHUD = bVisible;
	if (!HUDWidget)
	{
		if (bVisible)
		{
			CreateHUD();
		}
		return;
	}
	HUDWidget->SetVisibility(bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}
