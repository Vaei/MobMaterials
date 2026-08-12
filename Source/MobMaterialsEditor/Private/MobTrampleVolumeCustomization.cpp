// Copyright (c) Jared Taylor

#include "MobTrampleVolumeCustomization.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"

TSharedRef<IDetailCustomization> FMobTrampleVolumeCustomization::MakeInstance()
{
	return MakeShared<FMobTrampleVolumeCustomization>();
}

void FMobTrampleVolumeCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	DetailBuilder.EditCategory(TEXT("Trample"), FText::GetEmpty(), ECategoryPriority::Important);
}
