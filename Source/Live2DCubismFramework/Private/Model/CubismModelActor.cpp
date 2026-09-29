/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */


#include "Model/CubismModelActor.h"

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
#include "Engine/Texture2D.h"
#include "UObject/Package.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "CubismLog.h"

ACubismModel::ACubismModel()
{
	#if WITH_EDITORONLY_DATA
		bIsSpatiallyLoaded = false;
	#endif
}

void ACubismModel::Initialize(UCubismModel3Json* Model3Json)
{
	if (!Model3Json)
	{
		UE_LOG(LogCubism, Warning, TEXT("ACubismModel::Initialize: no model asset given."));
		return;
	}

#if WITH_EDITOR
	// Placed in the editor: record hard references so that the cooker packages every asset the model needs at runtime.
	if (GIsEditor && GetWorld() && !GetWorld()->IsGameWorld())
	{
		Model3Json->CollectReferencedAssets();
	}
#endif

	// Model Component
	if (!IsValid(Model))
	{
		Model = NewObject<UCubismModelComponent>(this, TEXT("CubismModel"), RF_Transactional);
		SetRootComponent(Model);
	}

	{
		// load .moc3
		Model->Moc = LoadMoc(Model3Json);

		if (!Model->Moc)
		{
			UE_LOG(LogCubism, Error, TEXT("ACubismModel::Initialize: the moc asset of '%s' could not be loaded."), *Model3Json->GetName());
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

		// The json assets below are turned into their components by the model component itself (see UCubismModelComponent::ComponentSetup).
		Model->MotionJsons.Empty();
		for (const FMotion3JsonGroup& Group : LoadMotion3Jsons(Model3Json))
		{
			Model->MotionJsons.Append(Group.Motion3Jsons);
		}

		Model->ExpressionJsons = LoadExp3Jsons(Model3Json);
		Model->PoseJson = LoadPose3Json(Model3Json);
		Model->PhysicsJson = LoadPhysics3Json(Model3Json);

		if (!Model->IsRegistered())
		{
			Model->RegisterComponent();
		}
		AddInstanceComponent(Model);

		// Creates the raw model, the drawables/parameters/parts and the helper components (parameter store, motion, expression, pose, physics, renderer)
		// in case the registration above could not do it yet.
		Model->EnsureModelBuilt();
	}

	// setup eye blink if exists
	if (Model3Json->EyeBlinks.Num() > 0)
	{
		if (IsValid(Model->EyeBlink))
		{
			// Re-initialised with another model asset: refresh the ids of the existing component.
			Model->EyeBlink->Json = Model3Json;
			Model->EyeBlink->Setup(Model);
		}
		else
		{
			UCubismEyeBlinkComponent* EyeBlink = NewObject<UCubismEyeBlinkComponent>(Model, TEXT("CubismEyeBlink"), RF_Transactional);

			EyeBlink->Json = Model3Json;

			EyeBlink->RegisterComponent();
			AddInstanceComponent(EyeBlink);
		}
	}

	// setup lip sync if exists
	if (Model3Json->LipSyncs.Num() > 0)
	{
		if (IsValid(Model->LipSync))
		{
			// Re-initialised with another model asset: refresh the ids of the existing component.
			Model->LipSync->Json = Model3Json;
			Model->LipSync->Setup(Model);
		}
		else
		{
			UCubismLipSyncComponent* LipSync = NewObject<UCubismLipSyncComponent>(Model, TEXT("CubismLipSync"), RF_Transactional);

			LipSync->Json = Model3Json;

			LipSync->RegisterComponent();
			AddInstanceComponent(LipSync);
		}
	}

	// setup raycast if exists
	if (Model3Json->HitAreas.Num() > 0)
	{
		if (IsValid(Model->Raycast))
		{
			// Re-initialised with another model asset: refresh the ids of the existing component.
			Model->Raycast->Json = Model3Json;
			Model->Raycast->Setup(Model);
		}
		else
		{
			UCubismRaycastComponent* Raycast = NewObject<UCubismRaycastComponent>(Model, TEXT("CubismRaycast"), RF_Transactional);

			Raycast->Json = Model3Json;

			Raycast->RegisterComponent();
			AddInstanceComponent(Raycast);
		}
	}
}

TObjectPtr<UCubismMoc3> ACubismModel::LoadMoc(const TObjectPtr<UCubismModel3Json>& Model3Json)
{
	if (Model3Json->MocPath.IsEmpty())
	{
		return nullptr;
	}


	const FString& AssetPath = Model3Json->ResolveAssetPath(Model3Json->MocPath);

	const TObjectPtr<UCubismMoc3>& Moc = LoadObject<UCubismMoc3>(nullptr, *AssetPath);

	return Moc;
}

TArray<TObjectPtr<UTexture2D>> ACubismModel::LoadTextures(const TObjectPtr<UCubismModel3Json>& Model3Json)
{
	TArray<TObjectPtr<UTexture2D>> Textures;


	for (const FString& TexturePath : Model3Json->TexturePaths)
	{
		const FString& AssetPath = Model3Json->ResolveAssetPath(TexturePath);

		TObjectPtr<UTexture2D> Texture = LoadObject<UTexture2D>(nullptr, *AssetPath);

		if (!Texture)
		{
			UE_LOG(LogCubism, Warning, TEXT("ACubismModel::LoadTextures: texture '%s' was not found."), *AssetPath);

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

TObjectPtr<UCubismPhysics3Json> ACubismModel::LoadPhysics3Json(const TObjectPtr<UCubismModel3Json>& Model3Json)
{
	if (Model3Json->PhysicsPath.IsEmpty())
	{
		return nullptr;
	}


	const FString& AssetPath = Model3Json->ResolveAssetPath(Model3Json->PhysicsPath);

	const TObjectPtr<UCubismPhysics3Json>& Json = LoadObject<UCubismPhysics3Json>(nullptr, *AssetPath);

	return Json;
}

TObjectPtr<UCubismPose3Json> ACubismModel::LoadPose3Json(const TObjectPtr<UCubismModel3Json>& Model3Json)
{
	if (Model3Json->PosePath.IsEmpty())
	{
		return nullptr;
	}


	const FString& AssetPath = Model3Json->ResolveAssetPath(Model3Json->PosePath);

	const TObjectPtr<UCubismPose3Json>& Json = LoadObject<UCubismPose3Json>(nullptr, *AssetPath);

	return Json;
}

TArray<TObjectPtr<UCubismExp3Json>> ACubismModel::LoadExp3Jsons(const TObjectPtr<UCubismModel3Json>& Model3Json)
{
	TArray<TObjectPtr<UCubismExp3Json>> Jsons;


	for (const FExpressionEntry& Entry : Model3Json->Expressions)
	{
		const FString& AssetPath = Model3Json->ResolveAssetPath(Entry.Path);

		TObjectPtr<UCubismExp3Json> Json = LoadObject<UCubismExp3Json>(nullptr, *AssetPath);

		Jsons.Add(Json);
	}

	return Jsons;
}

TArray<FMotion3JsonGroup> ACubismModel::LoadMotion3Jsons(const TObjectPtr<UCubismModel3Json>& Model3Json)
{
	TArray<FMotion3JsonGroup> JsonGroups;


	for (const FMotionGroupEntry& Entry : Model3Json->Motions)
	{
		FMotion3JsonGroup JsonGroup;

		for (const FString& MotionPath : Entry.Paths)
		{
			const FString& AssetPath = Model3Json->ResolveAssetPath(MotionPath);

			TObjectPtr<UCubismMotion3Json> Json = LoadObject<UCubismMotion3Json>(nullptr, *AssetPath);

			JsonGroup.Motion3Jsons.Add(Json);
		}

		JsonGroup.Name = Entry.Name;

		JsonGroups.Add(JsonGroup);
	}

	return JsonGroups;
}

TObjectPtr<UCubismDisplayInfo3Json> ACubismModel::LoadDisplayInfo3Json(const TObjectPtr<UCubismModel3Json>& Model3Json)
{
	if (Model3Json->DisplayInfoPath.IsEmpty())
	{
		return nullptr;
	}


	const FString& AssetPath = Model3Json->ResolveAssetPath(Model3Json->DisplayInfoPath);

	const TObjectPtr<UCubismDisplayInfo3Json>& Json = LoadObject<UCubismDisplayInfo3Json>(nullptr, *AssetPath);

	return Json;
}

TObjectPtr<UCubismUserData3Json> ACubismModel::LoadUserData3Json(const TObjectPtr<UCubismModel3Json>& Model3Json)
{
	if (Model3Json->UserDataPath.IsEmpty())
	{
		return nullptr;
	}


	const FString& AssetPath = Model3Json->ResolveAssetPath(Model3Json->UserDataPath);

	const TObjectPtr<UCubismUserData3Json>& Json = LoadObject<UCubismUserData3Json>(nullptr, *AssetPath);

	return Json;
}
