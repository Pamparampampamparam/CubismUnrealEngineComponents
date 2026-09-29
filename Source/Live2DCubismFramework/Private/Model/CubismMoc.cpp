/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */


#include "Model/CubismMoc3.h"

#include "CubismSourcePathUtils.h"
#include "Model/CubismModelComponent.h"
#include "CubismLog.h"
#include "Misc/Paths.h"

UCubismMoc3::UCubismMoc3()
	: Version(0)
	, RawMoc(nullptr)
{
}

void UCubismMoc3::SetupModel(UCubismModelComponent* InModel)
{
	if (!InModel)
	{
		return;
	}

	// The moc may not have been post-loaded yet when a model referencing it is set up during loading.
	if (!RawMoc)
	{
		if (HasAnyFlags(RF_NeedPostLoad))
		{
			ConditionalPostLoad();
		}

		if (!RawMoc)
		{
			Setup();
		}
	}

	if (!RawMoc)
	{
		UE_LOG(LogCubism, Error, TEXT("UCubismMoc3::SetupModel: moc '%s' has no valid data."), *GetName());

		return;
	}

	const uint32 Size = csmGetSizeofModel(RawMoc);

	if (Size == 0)
	{
		UE_LOG(LogCubism, Error, TEXT("UCubismMoc3::SetupModel: failed to query the model size of moc '%s'."), *GetName());

		return;
	}

	void* ModelAddress = FMemory::Malloc(Size, csmAlignofModel);

	csmModel* NewRawModel = csmInitializeModelInPlace(RawMoc, ModelAddress, Size);

	if (!NewRawModel)
	{
		FMemory::Free(ModelAddress);

		UE_LOG(LogCubism, Error, TEXT("UCubismMoc3::SetupModel: failed to initialize a model from moc '%s'."), *GetName());

		return;
	}

	UE_LOG(LogCubism, Log, TEXT("Cubism Model Has Created!: %p"), NewRawModel);

	if (InModel->RawModel)
	{
		DeleteModel(InModel);
	}

	InModel->Moc = this;
	InModel->RawModel = NewRawModel;
}

void UCubismMoc3::DeleteModel(UCubismModelComponent* InModel)
{
	if (!InModel || !InModel->RawModel)
	{
		return;
	}

	UE_LOG(LogCubism, Log, TEXT("Cubism Model Has Destroyed!: %p"), InModel->RawModel);

	FMemory::Free(InModel->RawModel);

	InModel->RawModel = nullptr;
}

void UCubismMoc3::Setup()
{
	if (RawMoc)
	{
		// Already revived. Models created from this moc reference its memory, so it is never reallocated.
		return;
	}

	const uint32 Size = Bytes.Num();

	if (Size == 0)
	{
		UE_LOG(LogCubism, Warning, TEXT("UCubismMoc3::Setup: moc '%s' has no data."), *GetName());

		return;
	}

	void* MocAddress = FMemory::Malloc(Size, csmAlignofMoc);

	FMemory::Memcpy(MocAddress, Bytes.GetData(), Size);

	// The consistency check expects the same alignment as the revive call.
	if (!HasMocConsistency(MocAddress, Size))
	{
		FMemory::Free(MocAddress);

		UE_LOG(LogCubism, Error, TEXT("UCubismMoc3::Setup: moc '%s' failed the consistency check."), *GetName());

		return;
	}

	RawMoc = csmReviveMocInPlace(MocAddress, Size);

	if (!RawMoc)
	{
		FMemory::Free(MocAddress);

		UE_LOG(LogCubism, Error, TEXT("UCubismMoc3::Setup: failed to revive moc '%s'."), *GetName());

		return;
	}

	Version = GetMocVersion(MocAddress, Size);
	if (Version < GetLatestMocVersion())
	{
		UE_LOG(LogCubism, Warning, TEXT("Moc Version is outdated. Moc Version: %d, Latest Moc Version: %d") , Version, GetLatestMocVersion());
	}
	else
	{
		UE_LOG(LogCubism, Log, TEXT("Moc Version: %d"), Version);
	}
}

int32 UCubismMoc3::GetVersion()
{
	return csmGetVersion();
}

int32 UCubismMoc3::GetLatestMocVersion()
{
	return csmGetLatestMocVersion();
}

int32 UCubismMoc3::GetMocVersion(const void* Address, const int32 Size)
{
	return csmGetMocVersion(Address, Size);
}

bool UCubismMoc3::HasMocConsistency(void* Address, const int32 Size)
{
	return csmHasMocConsistency(Address, Size) != 0;
}

////

CubismLogFunction UCubismMoc3::GetLogFunction() const
{
	return csmGetLogFunction();
}

////

void UCubismMoc3::SetLogFunction(CubismLogFunction LogFunction)
{
	csmSetLogFunction(LogFunction);
}

////

int32 UCubismMoc3::GetSizeOfModel() const
{
	return RawMoc ? csmGetSizeofModel(RawMoc) : 0;
}

void UCubismMoc3::PostLoad()
{
	Super::PostLoad();

	Setup();

#if WITH_EDITORONLY_DATA
	if (CubismRepairStoredSourcePath(AssetImportData, this, CubismStoredSourcePath))
	{
		MarkPackageDirty();
	}
#endif

	UE_LOG(LogCubism, Warning, TEXT("UCubismMoc3::PostLoad CubismStoredSourcePath=%s"), *CubismStoredSourcePath);
}

void UCubismMoc3::PostInitProperties()
{
#if WITH_EDITORONLY_DATA
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		AssetImportData = NewObject<UAssetImportData>(this, TEXT("AssetImportData"));
	}
#endif
	Super::PostInitProperties();
}

#if WITH_EDITORONLY_DATA
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4
void UCubismMoc3::GetAssetRegistryTags(FAssetRegistryTagsContext Context) const
{
	if (AssetImportData)
	{
		Context.AddTag( FAssetRegistryTag(SourceFileTagName(), AssetImportData->GetSourceData().ToJson(), FAssetRegistryTag::TT_Hidden) );
	}

	Super::GetAssetRegistryTags(Context);
}
#else
void UCubismMoc3::GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const
{
	if (AssetImportData)
	{
		OutTags.Add( FAssetRegistryTag(SourceFileTagName(), AssetImportData->GetSourceData().ToJson(), FAssetRegistryTag::TT_Hidden) );
	}

	Super::GetAssetRegistryTags(OutTags);
}
#endif
void UCubismMoc3::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	if (Ar.IsLoading() && Ar.UEVer() < VER_UE4_ASSET_IMPORT_DATA_AS_JSON && !AssetImportData)
	{
		// AssetImportData should always be valid
		AssetImportData = NewObject<UAssetImportData>(this, TEXT("AssetImportData"));
	}
}
#endif
