// Copyright (c) Jared Taylor

#include "MobFoliagePivots.h"

#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "AssetRegistry/AssetData.h"
#include "Engine/StaticMesh.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/ScopedSlowTask.h"
#include "UObject/Package.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "MobFoliagePivots"

DEFINE_LOG_CATEGORY_STATIC(LogMobFoliagePivots, Log, All);

namespace
{
	/**
	 * A coarse grid over vertex positions, so a leaf can ask what is near it without walking the
	 * whole mesh. A tree is ten thousand vertices and forty leaves, and the brute force answer is
	 * the product of the two.
	 */
	struct FMobPointGrid
	{
		float CellSize = 8.f;
		TMap<FIntVector, TArray<int32>> Cells;

		FIntVector CellOf(const FVector3f& P) const
		{
			return FIntVector(
				FMath::FloorToInt(P.X / CellSize),
				FMath::FloorToInt(P.Y / CellSize),
				FMath::FloorToInt(P.Z / CellSize));
		}

		void Add(const FVector3f& P, int32 Index)
		{
			Cells.FindOrAdd(CellOf(P)).Add(Index);
		}

		/** Every index within Radius of P, gathered from the cells that radius can reach. */
		void Query(const FVector3f& P, float Radius, TArray<int32>& Out) const
		{
			Out.Reset();
			const int32 Reach = FMath::Max(1, FMath::CeilToInt(Radius / CellSize));
			const FIntVector Centre = CellOf(P);
			for (int32 X = -Reach; X <= Reach; ++X)
			{
				for (int32 Y = -Reach; Y <= Reach; ++Y)
				{
					for (int32 Z = -Reach; Z <= Reach; ++Z)
					{
						if (const TArray<int32>* Found = Cells.Find(Centre + FIntVector(X, Y, Z)))
						{
							Out.Append(*Found);
						}
					}
				}
			}
		}
	};

	/** Union find over vertex instances. Two triangles sharing one are the same leaf. */
	struct FMobShells
	{
		TMap<int32, int32> Parent;

		void Add(int32 A)
		{
			if (!Parent.Contains(A))
			{
				Parent.Add(A, A);
			}
		}

		int32 Find(int32 A)
		{
			int32 Root = A;
			while (Parent[Root] != Root)
			{
				Root = Parent[Root];
			}
			while (Parent[A] != Root)
			{
				const int32 Next = Parent[A];
				Parent[A] = Root;
				A = Next;
			}
			return Root;
		}

		void Union(int32 A, int32 B)
		{
			const int32 RA = Find(A);
			const int32 RB = Find(B);
			if (RA != RB)
			{
				Parent[RB] = RA;
			}
		}
	};

	/** A repeatable per-leaf random, so a rebake does not reshuffle every leaf's phase. */
	float ShellRandom(int32 ShellRoot)
	{
		const uint32 H = GetTypeHash(ShellRoot) * 2654435761u;
		return static_cast<float>((H >> 8) & 0xFFFF) / 65535.f;
	}

	bool SectionIsFoliage(const UStaticMesh* Mesh, const FName& SlotName)
	{
		const int32 Index = Mesh->GetMaterialIndexFromImportedMaterialSlotName(SlotName);
		const UMaterialInterface* Material = Mesh->GetMaterial(
			Index != INDEX_NONE ? Index : Mesh->GetMaterialIndex(SlotName));
		if (!Material)
		{
			return false;
		}

		// The foliage master is the only one that shades as two sided foliage, so this separates
		// leaves from bark without anything having to be named or tagged.
		return Material->GetShadingModels().HasShadingModel(MSM_TwoSidedFoliage);
	}
}

FMobFoliagePivotResult UMobFoliagePivots::BakeFoliagePivots(UStaticMesh* Mesh,
	const FMobFoliagePivotSettings& Settings)
{
	FMobFoliagePivotResult Result;

	if (!Mesh)
	{
		Result.Message = TEXT("No mesh.");
		return Result;
	}

	FMeshDescription* MeshDesc = Mesh->GetMeshDescription(0);
	if (!MeshDesc)
	{
		Result.Message = TEXT("No mesh description on LOD 0.");
		return Result;
	}

	FStaticMeshAttributes Attributes(*MeshDesc);
	TVertexAttributesConstRef<FVector3f> Positions = Attributes.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();

	const int32 NeededChannels = Settings.PivotUVChannel + 2;
	if (UVs.GetNumChannels() < NeededChannels)
	{
		UVs.SetNumChannels(NeededChannels);
	}

	// A mesh that was never painted has no colour attribute at all, and writing through an invalid
	// ref is a silent no-op rather than an error.
	if (Settings.bWriteVertexColour && !Attributes.GetVertexInstanceColors().IsValid())
	{
		MeshDesc->VertexInstanceAttributes().RegisterAttribute<FVector4f>(
			MeshAttribute::VertexInstance::Color, 1, FVector4f(1.f, 1.f, 1.f, 1.f),
			EMeshAttributeFlags::Lerpable);
	}
	TVertexInstanceAttributesRef<FVector4f> Colours = Attributes.GetVertexInstanceColors();

	// Which sections are leaves. Everything else is left exactly as it was.
	const TVertexInstanceAttributesRef<FVector2f> UVsConst = UVs;
	TSet<FPolygonGroupID> Baked;
	const TPolygonGroupAttributesConstRef<FName> SlotNames =
		Attributes.GetPolygonGroupMaterialSlotNames();
	for (const FPolygonGroupID GroupID : MeshDesc->PolygonGroups().GetElementIDs())
	{
		if (!Settings.bFoliageSectionsOnly || SectionIsFoliage(Mesh, SlotNames[GroupID]))
		{
			Baked.Add(GroupID);
		}
	}
	Result.SectionsBaked = Baked.Num();

	if (Baked.Num() == 0)
	{
		Result.Message = TEXT("No foliage sections. Nothing on this mesh shades as two sided foliage.");
		return Result;
	}

	// Leaves are UV shells, and a shell is defined by position and texture coordinate together.
	// Position alone joins every leaf to the twig it is welded to. Coordinate alone joins every
	// leaf to every other leaf, because foliage cards all share one rectangle of the atlas. Two
	// corners are the same point of the same leaf only when both agree.
	FMobShells Shells;
	TArray<FTriangleID> BakedTriangles;
	TMap<TPair<int32, FIntPoint>, int32> CornerKeys;

	auto KeyOf = [MeshDesc, &UVs, &CornerKeys](const FVertexInstanceID Corner) -> int32
	{
		const FVector2f UV = UVs.Get(Corner, 0);
		const TPair<int32, FIntPoint> Key(
			MeshDesc->GetVertexInstanceVertex(Corner).GetValue(),
			FIntPoint(FMath::RoundToInt(UV.X * 4096.f), FMath::RoundToInt(UV.Y * 4096.f)));
		if (const int32* Found = CornerKeys.Find(Key))
		{
			return *Found;
		}
		const int32 Fresh = CornerKeys.Num();
		CornerKeys.Add(Key, Fresh);
		return Fresh;
	};

	TMap<FVertexInstanceID, int32> CornerToKey;
	for (const FTriangleID TriangleID : MeshDesc->Triangles().GetElementIDs())
	{
		if (!Baked.Contains(MeshDesc->GetTrianglePolygonGroup(TriangleID)))
		{
			continue;
		}
		BakedTriangles.Add(TriangleID);

		TArrayView<const FVertexInstanceID> Corners = MeshDesc->GetTriangleVertexInstances(TriangleID);
		TArray<int32, TInlineAllocator<3>> Keys;
		for (const FVertexInstanceID Corner : Corners)
		{
			const int32 Key = KeyOf(Corner);
			CornerToKey.Add(Corner, Key);
			Shells.Add(Key);
			Keys.Add(Key);
		}
		for (int32 i = 1; i < Keys.Num(); ++i)
		{
			Shells.Union(Keys[0], Keys[i]);
		}
	}

	// Every vertex instance in the baked sections, grouped by which leaf it belongs to.
	TMap<int32, TArray<FVertexInstanceID>> ByShell;
	for (const FTriangleID TriangleID : BakedTriangles)
	{
		for (const FVertexInstanceID Corner : MeshDesc->GetTriangleVertexInstances(TriangleID))
		{
			ByShell.FindOrAdd(Shells.Find(CornerToKey[Corner])).AddUnique(Corner);
		}
	}
	Result.Shells = ByShell.Num();

	if (Settings.bDryRun)
	{
		Result.bSucceeded = true;
		Result.Message = FString::Printf(TEXT("dry run: %d leaves across %d section(s)"),
			Result.Shells, Result.SectionsBaked);
		return Result;
	}

	// One grid over every vertex instance we might attach to, tagged with its own leaf so a leaf
	// never anchors to itself.
	FMobPointGrid Grid;
	Grid.CellSize = FMath::Max(1.f, Settings.AttachSearchRadius);
	TArray<FVector3f> GridPoints;
	TArray<int32> GridShell;
	for (const FVertexInstanceID Corner : MeshDesc->VertexInstances().GetElementIDs())
	{
		const FVector3f P = Positions[MeshDesc->GetVertexInstanceVertex(Corner)];
		const int32* Key = CornerToKey.Find(Corner);
		GridPoints.Add(P);
		GridShell.Add(Key ? Shells.Find(*Key) : INDEX_NONE);
		Grid.Add(P, GridPoints.Num() - 1);
	}

	TArray<int32> Nearby;
	for (const TPair<int32, TArray<FVertexInstanceID>>& Shell : ByShell)
	{
		const TArray<FVertexInstanceID>& Corners = Shell.Value;

		FVector3f Pivot = FVector3f::ZeroVector;
		bool bFound = false;

		if (Settings.PivotSource == EMobPivotSource::NearestOtherGeometry)
		{
			float BestSq = TNumericLimits<float>::Max();
			for (const FVertexInstanceID Corner : Corners)
			{
				const FVector3f P = Positions[MeshDesc->GetVertexInstanceVertex(Corner)];
				Grid.Query(P, Settings.AttachSearchRadius, Nearby);
				for (const int32 Index : Nearby)
				{
					if (GridShell[Index] == Shell.Key)
					{
						continue;
					}
					const float DistSq = FVector3f::DistSquared(P, GridPoints[Index]);
					if (DistSq < BestSq && DistSq <= FMath::Square(Settings.AttachSearchRadius))
					{
						BestSq = DistSq;
						Pivot = P;
						bFound = true;
					}
				}
			}
		}

		if (!bFound)
		{
			// Nothing to hang off, so the bottom of it is the root.
			float LowestZ = TNumericLimits<float>::Max();
			for (const FVertexInstanceID Corner : Corners)
			{
				const FVector3f P = Positions[MeshDesc->GetVertexInstanceVertex(Corner)];
				if (P.Z < LowestZ)
				{
					LowestZ = P.Z;
					Pivot = P;
				}
			}
			if (Settings.PivotSource == EMobPivotSource::NearestOtherGeometry)
			{
				++Result.FreeFloating;
			}
		}

		// Stiffness runs from nothing at the pivot to everything at the furthest point of the leaf.
		float Furthest = 0.f;
		for (const FVertexInstanceID Corner : Corners)
		{
			Furthest = FMath::Max(Furthest,
				FVector3f::Dist(Positions[MeshDesc->GetVertexInstanceVertex(Corner)], Pivot));
		}

		const float Random = ShellRandom(Shell.Key);

		for (const FVertexInstanceID Corner : Corners)
		{
			const FVector3f P = Positions[MeshDesc->GetVertexInstanceVertex(Corner)];
			const float Stiffness = Furthest > UE_KINDA_SMALL_NUMBER
				? FMath::Pow(FMath::Clamp(FVector3f::Dist(P, Pivot) / Furthest, 0.f, 1.f),
					FMath::Max(Settings.StiffnessPower, 0.1f))
				: 1.f;

			UVs.Set(Corner, Settings.PivotUVChannel, FVector2f(Pivot.X, Pivot.Y));
			UVs.Set(Corner, Settings.PivotUVChannel + 1, FVector2f(Pivot.Z, Stiffness));

			if (Settings.bWriteVertexColour && Colours.IsValid())
			{
				const FVector4f Was = Colours[Corner];
				Colours[Corner] = FVector4f(Stiffness, Random, Was.Z, Was.W);
			}
		}
	}

	Mesh->ModifyMeshDescription(0);
	Mesh->CommitMeshDescription(0);

	// Read back rather than assume. Writing through an unregistered attribute is silent, and a
	// stiffness that never arrived looks exactly like a stiffness of one.
	if (const FMeshDescription* Committed = Mesh->GetMeshDescription(0))
	{
		FStaticMeshAttributes Check(*const_cast<FMeshDescription*>(Committed));
		TVertexInstanceAttributesConstRef<FVector4f> Read = Check.GetVertexInstanceColors();
		if (Read.IsValid())
		{
			Result.bColoursWritten = true;
			Result.RedMin = TNumericLimits<float>::Max();
			Result.RedMax = -TNumericLimits<float>::Max();
			for (const TPair<int32, TArray<FVertexInstanceID>>& Shell : ByShell)
			{
				for (const FVertexInstanceID Corner : Shell.Value)
				{
					Result.RedMin = FMath::Min(Result.RedMin, Read[Corner].X);
					Result.RedMax = FMath::Max(Result.RedMax, Read[Corner].X);
				}
			}
		}
	}

	if (Mesh->GetNumSourceModels() > 0)
	{
		// The pivot rides in a UV channel, and half precision quantises a position in centimetres
		// to something a leaf can visibly swing about the wrong point.
		Mesh->GetSourceModel(0).BuildSettings.bUseFullPrecisionUVs = true;
	}

	Mesh->PostEditChange();
	Mesh->MarkPackageDirty();

	Result.bSucceeded = true;
	Result.Message = FString::Printf(
		TEXT("%d leaves across %d section(s)%s"),
		Result.Shells, Result.SectionsBaked,
		Result.FreeFloating > 0
			? *FString::Printf(TEXT(", %d free floating"), Result.FreeFloating)
			: TEXT(""));

	UE_LOG(LogMobFoliagePivots, Log, TEXT("%s: %s"), *Mesh->GetName(), *Result.Message);
	return Result;
}

bool UMobFoliagePivots::CanBakeSelection()
{
	TArray<FAssetData> Selected;
	FContentBrowserModule& Browser =
		FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
	Browser.Get().GetSelectedAssets(Selected);

	for (const FAssetData& Asset : Selected)
	{
		if (Asset.IsInstanceOf(UStaticMesh::StaticClass()))
		{
			return true;
		}
	}
	return false;
}

void UMobFoliagePivots::BakeSelection()
{
	TArray<FAssetData> Selected;
	FContentBrowserModule& Browser =
		FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
	Browser.Get().GetSelectedAssets(Selected);

	TArray<UStaticMesh*> Meshes;
	for (const FAssetData& Asset : Selected)
	{
		if (UStaticMesh* Mesh = Cast<UStaticMesh>(Asset.GetAsset()))
		{
			Meshes.Add(Mesh);
		}
	}

	if (Meshes.Num() == 0)
	{
		return;
	}

	const FMobFoliagePivotSettings Settings;
	FScopedSlowTask Task(Meshes.Num(), LOCTEXT("Baking", "Baking foliage pivots..."));
	Task.MakeDialog();

	int32 Shells = 0;
	int32 Failed = 0;
	for (UStaticMesh* Mesh : Meshes)
	{
		Task.EnterProgressFrame(1.f, FText::FromString(Mesh->GetName()));
		const FMobFoliagePivotResult Result = BakeFoliagePivots(Mesh, Settings);
		if (Result.bSucceeded)
		{
			Shells += Result.Shells;
		}
		else
		{
			++Failed;
			UE_LOG(LogMobFoliagePivots, Warning, TEXT("%s: %s"), *Mesh->GetName(), *Result.Message);
		}
	}

	FNotificationInfo Info(FText::Format(
		LOCTEXT("BakedFmt", "Mat: baked {0} leaves across {1} mesh(es){2}."),
		FText::AsNumber(Shells), FText::AsNumber(Meshes.Num() - Failed),
		Failed > 0 ? FText::Format(LOCTEXT("FailedFmt", ", {0} skipped"), FText::AsNumber(Failed))
				   : FText::GetEmpty()));
	Info.ExpireDuration = 6.f;
	if (const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
	{
		Item->SetCompletionState(Failed == 0 ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
	}
}

#undef LOCTEXT_NAMESPACE
