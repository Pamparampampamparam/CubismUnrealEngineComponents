/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */


#include "CubismModelActorFactory.h"

#include "Effects/EyeBlink/CubismEyeBlinkComponent.h"
#include "Effects/LipSync/CubismLipSyncComponent.h"
#include "Effects/Raycast/CubismRaycastComponent.h"
#include "Effects/Raycast/CubismRaycastParameter.h"
#include "Physics/CubismPhysicsComponent.h"
#include "Pose/CubismPoseComponent.h"
#include "Expression/CubismExpressionComponent.h"
#include "Motion/CubismMotionComponent.h"
#include "Model/CubismModelComponent.h"
#include "Model/CubismParameterStoreComponent.h"
#include "Rendering/CubismRendererComponent.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ObjectTools.h"
#include "PackageTools.h"
#include "UObject/SavePackage.h"
#include "AssetToolsModule.h"
#include "CubismLog.h"

UCubismModelActorFactory::UCubismModelActorFactory()
{
	DisplayName = NSLOCTEXT("Live2D Cubism Framework", "CubismModelFactoryDisplayName", "Add Cubism Model");
	NewActorClass = ACubismModel::StaticClass();
}

void UCubismModelActorFactory::PostSpawnActor(UObject* Asset, AActor* NewActor)
{
	Super::PostSpawnActor(Asset, NewActor);

	if (const TObjectPtr<UCubismModel3Json>& Model3Json = Cast<UCubismModel3Json>(Asset))
	{
		ACubismModel* ModelActor = CastChecked<ACubismModel>(NewActor);
		CreateModel(ModelActor, Model3Json);
	}

	// spawn the actor at the click location
	NewActor->SetActorLocation(GEditor->ClickLocation);
}

void UCubismModelActorFactory::CreateModel(const TObjectPtr<ACubismModel>& ModelActor, const TObjectPtr<UCubismModel3Json>& Model3Json)
{
	UCubismModelComponent* Model = NewObject<UCubismModelComponent>(ModelActor, TEXT("CubismModel"), RF_Transactional);
	ModelActor->Model = Model;
	ModelActor->SetRootComponent(Model);

	// load .moc3
	Model->Moc = LoadMoc(Model3Json);

	if (!Model->Moc)
	{
		UE_LOG(LogCubism, Error, TEXT("UCubismModelActorFactory: the moc asset referenced by '%s' was not found. Import the model folder first."), *Model3Json->GetName());
	}

	// load textures
	const TArray<TObjectPtr<UTexture2D>>& Textures = LoadTextures(Model3Json);
	Model->Textures.Empty();
	Model->Textures.Append(Textures);

	// load displayinfo3.json
	const TObjectPtr<UCubismDisplayInfo3Json>& DisplayInfo3Json = LoadDisplayInfo3Json(Model3Json);
	if (DisplayInfo3Json != nullptr)
	{
		Model->DisplayInfoJson = DisplayInfo3Json;
	}

	// load userdata3.json
	const TObjectPtr<UCubismUserData3Json>& UserData3Json = LoadUserData3Json(Model3Json);
	if (UserData3Json != nullptr)
	{
		Model->UserDataJson = UserData3Json;
	}

	// The model component creates the parameter store, motion, pose, expression, physics and renderer
	// components itself from these assets when it is registered.
	{
		// load motion3.json
		const TArray<FMotion3JsonGroup>& Motion3JsonGroups = LoadMotion3Jsons(Model3Json);
		Model->MotionJsons.Empty();
		for (const FMotion3JsonGroup& Group : Motion3JsonGroups)
		{
			for (const TObjectPtr<UCubismMotion3Json>& Motion3Json : Group.Motion3Jsons)
			{
				if (Motion3Json)
				{
					Model->MotionJsons.Add(Motion3Json);
				}
			}
		}

		// load pose3.json
		Model->PoseJson = LoadPose3Json(Model3Json);

		// load exp3.json
		const TArray<TObjectPtr<UCubismExp3Json>>& Exp3Jsons = LoadExp3Jsons(Model3Json);
		Model->ExpressionJsons.Empty();
		for (const TObjectPtr<UCubismExp3Json>& Exp3Json : Exp3Jsons)
		{
			if (Exp3Json)
			{
				Model->ExpressionJsons.Add(Exp3Json);
			}
		}

		// load physics3.json
		Model->PhysicsJson = LoadPhysics3Json(Model3Json);
	}

	Model->RegisterComponent();
	ModelActor->AddInstanceComponent(Model);

	// setup eye blink if exists
	if (Model3Json->EyeBlinks.Num() > 0)
	{
		UCubismEyeBlinkComponent* EyeBlink = NewObject<UCubismEyeBlinkComponent>(Model, TEXT("CubismEyeBlink"), RF_Transactional);

		EyeBlink->Json = Model3Json;

		EyeBlink->RegisterComponent();
		ModelActor->AddInstanceComponent(EyeBlink);
	}

	// setup lip sync if exists
	if (Model3Json->LipSyncs.Num() > 0)
	{
		UCubismLipSyncComponent* LipSync = NewObject<UCubismLipSyncComponent>(Model, TEXT("CubismLipSync"), RF_Transactional);

		LipSync->Json = Model3Json;

		LipSync->RegisterComponent();
		ModelActor->AddInstanceComponent(LipSync);
	}

	// setup raycast if exists
	if (Model3Json->HitAreas.Num() > 0)
	{
		UCubismRaycastComponent* Raycast = NewObject<UCubismRaycastComponent>(Model, TEXT("CubismRaycast"), RF_Transactional);

		Raycast->Json = Model3Json;

		Raycast->RegisterComponent();
		ModelActor->AddInstanceComponent(Raycast);
	}
}

const FString GetAssetPath(const FString& SourcePath)
{
	FString DirectoryPath, FileNameWithoutExt, Ext;
	FPaths::Split(SourcePath, DirectoryPath, FileNameWithoutExt, Ext);

	DirectoryPath = FPackageName::FilenameToLongPackageName(ObjectTools::SanitizeObjectPath(DirectoryPath));
	FileNameWithoutExt = ObjectTools::SanitizeObjectName(FileNameWithoutExt);

	const FString& AssetPath = FString::Printf(TEXT("%s/%s.%s"), *DirectoryPath, *FileNameWithoutExt, *FileNameWithoutExt);

	return AssetPath;
}

TObjectPtr<UCubismMoc3> UCubismModelActorFactory::LoadMoc(const TObjectPtr<UCubismModel3Json>& Model3Json)
{
	if (Model3Json->MocPath.IsEmpty())
	{
		return nullptr;
	}

	const FString& LongPackagePath = FPackageName::GetLongPackagePath(Model3Json->GetOutermost()->GetPathName());

	const FString& AssetPath = GetAssetPath(LongPackagePath / Model3Json->MocPath);

	const TObjectPtr<UCubismMoc3>& Moc = LoadObject<UCubismMoc3>(nullptr, *AssetPath);

	return Moc;
}

TArray<TObjectPtr<UTexture2D>> UCubismModelActorFactory::LoadTextures(const TObjectPtr<UCubismModel3Json>& Model3Json)
{
	TArray<TObjectPtr<UTexture2D>> Textures;

	const FString& LongPackagePath = FPackageName::GetLongPackagePath(Model3Json->GetOutermost()->GetPathName());

	for (const FString& TexturePath : Model3Json->TexturePaths)
	{
		const FString& AssetPath = GetAssetPath(LongPackagePath / TexturePath);

		TObjectPtr<UTexture2D> Texture = LoadObject<UTexture2D>(nullptr, *AssetPath);

		if (!Texture)
		{
			UE_LOG(LogCubism, Warning, TEXT("UCubismModelActorFactory: texture '%s' was not found."), *AssetPath);

			// Keep the slot so that the texture indices of the drawables stay valid.
			Textures.Add(nullptr);

			continue;
		}

		// Workaround for when the texture is loaded as a normal map
		if (!Texture->SRGB || Texture->CompressionSettings != TC_Default || Texture->LODGroup != TEXTUREGROUP_World)
		{
			Texture->SRGB = true;
			Texture->CompressionSettings = TC_Default;
			Texture->LODGroup = TEXTUREGROUP_World;

			Texture->UpdateResource();

			Texture->MarkPackageDirty();
		}

		Textures.Add(Texture);
	}

	return Textures;
}

TObjectPtr<UCubismPhysics3Json> UCubismModelActorFactory::LoadPhysics3Json(const TObjectPtr<UCubismModel3Json>& Model3Json)
{
	if (Model3Json->PhysicsPath.IsEmpty())
	{
		return nullptr;
	}

	const FString& LongPackagePath = FPackageName::GetLongPackagePath(Model3Json->GetOutermost()->GetPathName());

	const FString& AssetPath = GetAssetPath(LongPackagePath / Model3Json->PhysicsPath);

	const TObjectPtr<UCubismPhysics3Json>& Json = LoadObject<UCubismPhysics3Json>(nullptr, *AssetPath);
 
	return Json;
}

TObjectPtr<UCubismPose3Json> UCubismModelActorFactory::LoadPose3Json(const TObjectPtr<UCubismModel3Json>& Model3Json)
{
	if (Model3Json->PosePath.IsEmpty())
	{
		return nullptr;
	}

	const FString& LongPackagePath = FPackageName::GetLongPackagePath(Model3Json->GetOutermost()->GetPathName());

	const FString& AssetPath = GetAssetPath(LongPackagePath / Model3Json->PosePath);

	const TObjectPtr<UCubismPose3Json>& Json = LoadObject<UCubismPose3Json>(nullptr, *AssetPath);

	return Json;
}

TArray<TObjectPtr<UCubismExp3Json>> UCubismModelActorFactory::LoadExp3Jsons(const TObjectPtr<UCubismModel3Json>& Model3Json)
{
	TArray<TObjectPtr<UCubismExp3Json>> Jsons;

	const FString& LongPackagePath = FPackageName::GetLongPackagePath(Model3Json->GetOutermost()->GetPathName());

	for (const FExpressionEntry& Entry : Model3Json->Expressions)
	{
		const FString& AssetPath = GetAssetPath(LongPackagePath / Entry.Path);

		TObjectPtr<UCubismExp3Json> Json = LoadObject<UCubismExp3Json>(nullptr, *AssetPath);

		Jsons.Add(Json);
	}

	return Jsons;
}

TArray<FMotion3JsonGroup> UCubismModelActorFactory::LoadMotion3Jsons(const TObjectPtr<UCubismModel3Json>& Model3Json)
{
	TArray<FMotion3JsonGroup> JsonGroups;

	const FString& LongPackagePath = FPackageName::GetLongPackagePath(Model3Json->GetOutermost()->GetPathName());

	for (const FMotionGroupEntry& Entry : Model3Json->Motions)
	{
		FMotion3JsonGroup JsonGroup;

		for (const FString& MotionPath : Entry.Paths)
		{
			const FString& AssetPath = GetAssetPath(LongPackagePath / MotionPath);

			TObjectPtr<UCubismMotion3Json> Json = LoadObject<UCubismMotion3Json>(nullptr, *AssetPath);

			JsonGroup.Motion3Jsons.Add(Json);
		}

		JsonGroup.Name = Entry.Name;

		JsonGroups.Add(JsonGroup);
	}

	return JsonGroups;
}

TObjectPtr<UCubismDisplayInfo3Json> UCubismModelActorFactory::LoadDisplayInfo3Json(const TObjectPtr<UCubismModel3Json>& Model3Json)
{
	if (Model3Json->DisplayInfoPath.IsEmpty())
	{
		return nullptr;
	}

	const FString& LongPackagePath = FPackageName::GetLongPackagePath(Model3Json->GetOutermost()->GetPathName());

	const FString& AssetPath = GetAssetPath(LongPackagePath / Model3Json->DisplayInfoPath);

	const TObjectPtr<UCubismDisplayInfo3Json>& Json = LoadObject<UCubismDisplayInfo3Json>(nullptr, *AssetPath);

	return Json;
}

TObjectPtr<UCubismUserData3Json> UCubismModelActorFactory::LoadUserData3Json(const TObjectPtr<UCubismModel3Json>& Model3Json)
{
	if (Model3Json->UserDataPath.IsEmpty())
	{
		return nullptr;
	}

	const FString& LongPackagePath = FPackageName::GetLongPackagePath(Model3Json->GetOutermost()->GetPathName());

	const FString& AssetPath = GetAssetPath(LongPackagePath / Model3Json->UserDataPath);

	const TObjectPtr<UCubismUserData3Json>& Json = LoadObject<UCubismUserData3Json>(nullptr, *AssetPath);

	return Json;
}

bool UCubismModelActorFactory::CanCreateActorFrom(const FAssetData& AssetData, FText& OutErrorMsg)
{
	return AssetData.IsValid() && AssetData.IsInstanceOf(UCubismModel3Json::StaticClass());
}
