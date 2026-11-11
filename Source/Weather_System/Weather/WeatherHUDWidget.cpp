#include "Weather/WeatherHUDWidget.h"

#include "Weather/WeatherManager.h"
#include "Weather/WeatherTimeOfDay.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Kismet/GameplayStatics.h"
#include "Styling/CoreStyle.h"

TSharedRef<SWidget> UWeatherHUDWidget::RebuildWidget()
{
	// No designer layout? Build a semi-transparent info panel so the class is usable straight away.
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Canvas"));
		WidgetTree->RootWidget = Canvas;

		UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Panel"));
		Panel->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.55f));
		Panel->SetPadding(FMargin(16.f, 12.f));
		if (UCanvasPanelSlot* PanelSlot = Canvas->AddChildToCanvas(Panel))
		{
			PanelSlot->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
			PanelSlot->SetAlignment(FVector2D(0.f, 0.f));
			PanelSlot->SetPosition(ScreenPosition);
			PanelSlot->SetAutoSize(true);
		}

		UVerticalBox* Box = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Box"));
		Panel->SetContent(Box);

		const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", FontSize);
		const FSlateFontInfo SmallFont = FCoreStyle::GetDefaultFontStyle("Regular", FMath::Max(10, FontSize - 4));

		auto AddLine = [&](TObjectPtr<UTextBlock>& Out, const TCHAR* Name, const FSlateFontInfo& LineFont, const FLinearColor& Color)
		{
			Out = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
			Out->SetFont(LineFont);
			Out->SetColorAndOpacity(FSlateColor(Color));
			if (UVerticalBoxSlot* LineSlot = Box->AddChildToVerticalBox(Out))
			{
				LineSlot->SetPadding(FMargin(0.f, 2.f));
			}
		};

		const FLinearColor White(1.f, 1.f, 1.f, 1.f);
		const FLinearColor Dim(0.75f, 0.8f, 0.85f, 1.f);
		if (!ModeText)    { AddLine(ModeText, TEXT("ModeText"), Font, FLinearColor(1.f, 0.85f, 0.4f, 1.f)); }
		if (!DayText)     { AddLine(DayText, TEXT("DayText"), Font, White); }
		if (!SeasonText)  { AddLine(SeasonText, TEXT("SeasonText"), Font, White); }
		if (!WeatherText) { AddLine(WeatherText, TEXT("WeatherText"), Font, White); }
		if (!TimeText)    { AddLine(TimeText, TEXT("TimeText"), Font, White); }
		if (!DetailsText && bShowDetails) { AddLine(DetailsText, TEXT("DetailsText"), SmallFont, Dim); }
	}
	return Super::RebuildWidget();
}

void UWeatherHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (!WeatherManager)
	{
		WeatherManager = Cast<AWeatherManager>(UGameplayStatics::GetActorOfClass(this, AWeatherManager::StaticClass()));
	}
	if (!TimeOfDay)
	{
		TimeOfDay = Cast<AWeatherTimeOfDay>(UGameplayStatics::GetActorOfClass(this, AWeatherTimeOfDay::StaticClass()));
	}
	Refresh();
}

void UWeatherHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	Refresh();
}

void UWeatherHUDWidget::Refresh()
{
	if (ModeText)    { ModeText->SetText(GetModeText()); }
	if (DayText)     { DayText->SetText(GetDayText()); }
	if (SeasonText)  { SeasonText->SetText(GetSeasonText()); }
	if (WeatherText) { WeatherText->SetText(GetWeatherText()); }
	if (TimeText)    { TimeText->SetText(GetTimeText()); }
	if (DetailsText) { DetailsText->SetText(bShowDetails ? GetDetailsText() : FText::GetEmpty()); }
}

FText UWeatherHUDWidget::GetModeText() const
{
	return WeatherManager ? FText::FromString(TEXT("Mode: ") + WeatherManager->GetModeText().ToString()) : FText::GetEmpty();
}

FText UWeatherHUDWidget::GetDayText() const
{
	if (!TimeOfDay)
	{
		return FText::GetEmpty();
	}
	return FText::FromString(FString::Printf(TEXT("Day %d / %d"), TimeOfDay->GetDayOfYear() + 1, TimeOfDay->DaysPerYear));
}

FText UWeatherHUDWidget::GetSeasonText() const
{
	return WeatherManager ? FText::FromString(TEXT("Season: ") + WeatherManager->GetSeasonText().ToString()) : FText::GetEmpty();
}

FText UWeatherHUDWidget::GetWeatherText() const
{
	if (!WeatherManager)
	{
		return FText::GetEmpty();
	}
	FString Line = TEXT("Weather: ") + WeatherManager->GetWeatherText().ToString();
	if (WeatherManager->IsSnowing())
	{
		Line += TEXT(" (snow)");
	}
	if (WeatherManager->IsWeatherOverridden())
	{
		Line += TEXT(" [forced]");
	}
	return FText::FromString(Line);
}

FText UWeatherHUDWidget::GetTimeText() const
{
	if (!TimeOfDay)
	{
		return FText::GetEmpty();
	}
	FString Line = FString::Printf(TEXT("Time: %s (%s)"), *TimeOfDay->GetTimeText().ToString(), TimeOfDay->IsDaytime() ? TEXT("day") : TEXT("night"));
	if (TimeOfDay->bPaused)
	{
		Line += TEXT("  [paused]");
	}
	else if (!FMath::IsNearlyEqual(TimeOfDay->GetTimeSpeedMultiplier(), 1.f))
	{
		Line += FString::Printf(TEXT("  x%g"), TimeOfDay->GetTimeSpeedMultiplier());
	}
	return FText::FromString(Line);
}

FText UWeatherHUDWidget::GetDetailsText() const
{
	if (!WeatherManager)
	{
		return FText::GetEmpty();
	}
	const FWeatherSnapshot& W = WeatherManager->GetCurrentWeather();
	return FText::FromString(FString::Printf(
		TEXT("Rain %.2f   Snow %.2f\nFog %.4f   Clouds %.2f\nWetness %.2f   Noise %.2f"),
		W.Rain, W.Snow, W.Fog, W.Clouds, W.Wetness, WeatherManager->GetWeatherNoise()));
}
