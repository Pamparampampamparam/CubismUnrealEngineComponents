/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */


#include "Model/CubismModel3Json.h"

#include "CubismSourcePathUtils.h"
#include "CubismLog.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

void UCubismModel3Json::PostInitProperties()
{
#if WITH_EDITORONLY_DATA
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		AssetImportData = NewObject<UAssetImportData>(this, TEXT("AssetImportData"));
	}
#endif
	Super::PostInitProperties();
}

FString UCubismModel3Json::ResolveAssetPath(const FString& RelativePath) const
{
	if (RelativePath.IsEmpty())
	{
		return FString();
	}

	const FString LongPackagePath = FPackageName::GetLongPackagePath(GetOutermost()->GetPathName());

	FString DirectoryPath, FileNameWithoutExt, Ext;
	FPaths::Split(LongPackagePath / RelativePath, DirectoryPath, FileNameWithoutExt, Ext);

	DirectoryPath = FPaths::ConvertRelativePathToFull(DirectoryPath);
	DirectoryPath = FPackageName::FilenameToLongPackageName(DirectoryPath);

	FileNameWithoutExt = FileNameWithoutExt.Replace(TEXT(" "), TEXT("_")).Replace(TEXT("."), TEXT("_"));

	return FString::Printf(TEXT("%s/%s.%s"), *DirectoryPath, *FileNameWithoutExt, *FileNameWithoutExt);
}

void UCubismModel3Json::CollectReferencedAssets()
{
	TArray<FString> RelativePaths;

	RelativePaths.Add(MocPath);
	RelativePaths.Append(TexturePaths);
	RelativePaths.Add(PhysicsPath);
	RelativePaths.Add(PosePath);
	RelativePaths.Add(DisplayInfoPath);
	RelativePaths.Add(UserDataPath);

	for (const FExpressionEntry& Entry : Expressions)
	{
		RelativePaths.Add(Entry.Path);
	}

	for (const FMotionGroupEntry& Group : Motions)
	{
		RelativePaths.Append(Group.Paths);
	}

	TArray<TObjectPtr<UObject>> NewReferences;

	for (const FString& RelativePath : RelativePaths)
	{
		if (RelativePath.IsEmpty())
		{
			continue;
		}

		const FString AssetPath = ResolveAssetPath(RelativePath);

		UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);

		if (!Asset)
		{
			UE_LOG(LogCubism, Warning, TEXT("UCubismModel3Json::CollectReferencedAssets: '%s' referenced by '%s' was not found."), *AssetPath, *GetName());

			continue;
		}

		NewReferences.AddUnique(Asset);
	}

	if (NewReferences != ReferencedAssets)
	{
		ReferencedAssets = MoveTemp(NewReferences);

#if WITH_EDITOR
		MarkPackageDirty();
#endif
	}
}

void UCubismModel3Json::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITORONLY_DATA
	if (CubismRepairStoredSourcePath(AssetImportData, this, CubismStoredSourcePath))
	{
		MarkPackageDirty();
	}
#endif
}

#if WITH_EDITORONLY_DATA
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4
void UCubismModel3Json::GetAssetRegistryTags(FAssetRegistryTagsContext Context) const
{
	if (AssetImportData)
	{
		Context.AddTag( FAssetRegistryTag(SourceFileTagName(), AssetImportData->GetSourceData().ToJson(), FAssetRegistryTag::TT_Hidden) );
	}

	Super::GetAssetRegistryTags(Context);
}
#else
void UCubismModel3Json::GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const
{
	if (AssetImportData)
	{
		OutTags.Add( FAssetRegistryTag(SourceFileTagName(), AssetImportData->GetSourceData().ToJson(), FAssetRegistryTag::TT_Hidden) );
	}

	Super::GetAssetRegistryTags(OutTags);
}
#endif
void UCubismModel3Json::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	if (Ar.IsLoading() && Ar.UEVer() < VER_UE4_ASSET_IMPORT_DATA_AS_JSON && !AssetImportData)
	{
		// AssetImportData should always be valid
		AssetImportData = NewObject<UAssetImportData>(this, TEXT("AssetImportData"));
	}
}
#endif
