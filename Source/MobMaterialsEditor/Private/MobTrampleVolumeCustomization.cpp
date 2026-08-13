// Copyright (c) Jared Taylor

#include "MobTrampleVolumeCustomization.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "MobTrampleVolume.h"
#include "PropertyHandle.h"
#include "SWarningOrErrorBox.h"
#include "Widgets/Layout/SBox.h"

#define LOCTEXT_NAMESPACE "MobTrampleVolumeCustomization"

TSharedRef<IDetailCustomization> FMobTrampleVolumeCustomization::MakeInstance()
{
	return MakeShared<FMobTrampleVolumeCustomization>();
}

void FMobTrampleVolumeCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	IDetailCategoryBuilder& Trample = DetailBuilder.EditCategory(TEXT("Trample"), FText::GetEmpty(),
		ECategoryPriority::Important);

	HoldSecondsHandle = DetailBuilder.GetProperty(
		GET_MEMBER_NAME_CHECKED(AMobTrampleVolume, HoldSeconds));
	MaxMarksHandle = DetailBuilder.GetProperty(
		GET_MEMBER_NAME_CHECKED(AMobTrampleVolume, MaxMarks));

	Trample.AddCustomRow(LOCTEXT("NeverFadesSearch", "hold seconds fade prints forever"), false)
		.Visibility(TAttribute<EVisibility>::CreateSP(this, &FMobTrampleVolumeCustomization::WarningVisibility))
		.WholeRowContent()
		[
			SNew(SBox)
			.Padding(0.f, 4.f)
			[
				SNew(SWarningOrErrorBox)
				.MessageStyle(EMessageStyle::Warning)
				.Message(TAttribute<FText>::CreateSP(this, &FMobTrampleVolumeCustomization::WarningText))
			]
		];
}

bool FMobTrampleVolumeCustomization::NeverFades() const
{
	float HoldSeconds = 0.f;
	return HoldSecondsHandle.IsValid()
		&& HoldSecondsHandle->GetValue(HoldSeconds) == FPropertyAccess::Success
		&& HoldSeconds <= 0.f;
}

EVisibility FMobTrampleVolumeCustomization::WarningVisibility() const
{
	return NeverFades() ? EVisibility::Visible : EVisibility::Collapsed;
}

FText FMobTrampleVolumeCustomization::WarningText() const
{
	int32 MaxMarks = 0;
	if (!MaxMarksHandle.IsValid() || MaxMarksHandle->GetValue(MaxMarks) != FPropertyAccess::Success)
	{
		MaxMarks = 0;
	}

	return FText::Format(LOCTEXT("NeverFadesWarning",
		"Prints never fade. Hold Seconds is 0, so nothing retires and every print is kept until "
		"Max Marks ({0}) drops the oldest.\n\n"
		"This will cause serious frame hitches. The whole list is cleared and redrawn each flush, "
		"on the render thread with the game thread waiting on it, so every footstep makes the "
		"stall longer until the cap is reached, and it stays there. Set Hold Seconds above 0."),
		FText::AsNumber(MaxMarks));
}

#undef LOCTEXT_NAMESPACE
