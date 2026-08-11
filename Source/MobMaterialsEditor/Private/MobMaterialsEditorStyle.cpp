// Copyright (c) Jared Taylor

#include "MobMaterialsEditorStyle.h"

#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"

TSharedPtr<FSlateStyleSet> FMobMaterialsEditorStyle::StyleSet;

FName FMobMaterialsEditorStyle::GetStyleSetName()
{
	static const FName StyleName(TEXT("MobMaterialsEditorStyle"));
	return StyleName;
}

void FMobMaterialsEditorStyle::Register()
{
	if (StyleSet.IsValid())
	{
		return;
	}

	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("MobMaterials"));
	if (!Plugin.IsValid())
	{
		return;
	}

	StyleSet = MakeShared<FSlateStyleSet>(GetStyleSetName());
	StyleSet->SetContentRoot(Plugin->GetBaseDir() / TEXT("Resources"));

	// 16 square is what a toolbar entry draws at; anything larger is downsampled every frame.
	StyleSet->Set(GetMenuIconName(),
		new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("Icon64"), TEXT(".png")), FVector2D(16.f, 16.f)));

	FSlateStyleRegistry::RegisterSlateStyle(*StyleSet);
}

void FMobMaterialsEditorStyle::Unregister()
{
	if (StyleSet.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSet);
		StyleSet.Reset();
	}
}
