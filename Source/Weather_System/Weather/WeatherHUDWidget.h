// Debug/status HUD: day of year, season, weather, time and live weather values.
// Works with no designer layout (it builds a styled panel itself), or bind TextBlocks with the names
// below in a Widget Blueprint child to use your own layout.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "WeatherHUDWidget.generated.h"

class AWeatherManager;
class AWeatherTimeOfDay;
class UTextBlock;

UCLASS(Blueprintable)
class WEATHER_SYSTEM_API UWeatherHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, Category = "Weather")
	TObjectPtr<AWeatherManager> WeatherManager;

	UPROPERTY(BlueprintReadWrite, Category = "Weather")
	TObjectPtr<AWeatherTimeOfDay> TimeOfDay;

	/** Font size of the auto-built layout. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	int32 FontSize = 18;

	/** Screen position (pixels from the top-left) of the auto-built layout. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	FVector2D ScreenPosition = FVector2D(24.0, 24.0);

	/** Shows the rain/snow/fog/cloud/wetness/noise lines. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	bool bShowDetails = true;

	/** "Mode: Continuous" / "Mode: Custom" */
	UFUNCTION(BlueprintPure, Category = "Weather")
	FText GetModeText() const;

	/** "Day 42 / 365" */
	UFUNCTION(BlueprintPure, Category = "Weather")
	FText GetDayText() const;

	UFUNCTION(BlueprintPure, Category = "Weather")
	FText GetSeasonText() const;

	/** Weather state name, with "(forced)" appended while overridden and "(snow)" for snowfall. */
	UFUNCTION(BlueprintPure, Category = "Weather")
	FText GetWeatherText() const;

	/** "14:30 (Day)" / "(Night)" */
	UFUNCTION(BlueprintPure, Category = "Weather")
	FText GetTimeText() const;

	/** Multi-line: rain, snow, fog, clouds, wetness, noise. */
	UFUNCTION(BlueprintPure, Category = "Weather")
	FText GetDetailsText() const;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ModeText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DayText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SeasonText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> WeatherText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TimeText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DetailsText;

private:
	void Refresh();
};
