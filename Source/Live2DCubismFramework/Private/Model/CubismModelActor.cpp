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
#include "Effects/LookAt/CubismLookAtComponent.h"
#include "Effects/LookAt/CubismLookAtParameter.h"
#include "Sound/SoundWave.h"
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

	ModelAsset = Model3Json;
	InitializedAsset = Model3Json;

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

// ---- AActor interface ------------------------------------------------------------------------------------

void ACubismModel::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (IsTemplate())
	{
		return;
	}

	// A Blueprint child with `Model Asset` set in its defaults builds itself when placed, spawned or edited.
	if (ModelAsset && (!IsValid(Model) || InitializedAsset != ModelAsset))
	{
		Initialize(ModelAsset);
	}
}

// ---- Component access -------------------------------------------------------------------------------------

UCubismModelComponent* ACubismModel::GetModelComponent() const
{
	return IsValid(Model) ? Model.Get() : nullptr;
}

UCubismMotionComponent* ACubismModel::GetMotionComponent() const
{
	return IsValid(Model) && IsValid(Model->Motion) ? Model->Motion.Get() : nullptr;
}

UCubismExpressionComponent* ACubismModel::GetExpressionComponent() const
{
	return IsValid(Model) && IsValid(Model->Expression) ? Model->Expression.Get() : nullptr;
}

UCubismLipSyncComponent* ACubismModel::GetLipSyncComponent() const
{
	return IsValid(Model) && IsValid(Model->LipSync) ? Model->LipSync.Get() : nullptr;
}

UCubismEyeBlinkComponent* ACubismModel::GetEyeBlinkComponent() const
{
	return IsValid(Model) && IsValid(Model->EyeBlink) ? Model->EyeBlink.Get() : nullptr;
}

UCubismLookAtComponent* ACubismModel::GetLookAtComponent() const
{
	return IsValid(Model) && IsValid(Model->LookAt) ? Model->LookAt.Get() : nullptr;
}

UCubismPhysicsComponent* ACubismModel::GetPhysicsComponent() const
{
	return IsValid(Model) && IsValid(Model->Physics) ? Model->Physics.Get() : nullptr;
}

UCubismPoseComponent* ACubismModel::GetPoseComponent() const
{
	return IsValid(Model) && IsValid(Model->Pose) ? Model->Pose.Get() : nullptr;
}

UCubismRendererComponent* ACubismModel::GetRendererComponent() const
{
	return IsValid(Model) && IsValid(Model->Renderer) ? Model->Renderer.Get() : nullptr;
}

UCubismRaycastComponent* ACubismModel::GetRaycastComponent() const
{
	return IsValid(Model) && IsValid(Model->Raycast) ? Model->Raycast.Get() : nullptr;
}

// ---- Character control -------------------------------------------------------------------------------------

static FString CubismStripAssetSuffix(const UObject* Asset, const TCHAR* Suffix)
{
	if (!Asset)
	{
		return FString();
	}

	FString Name = Asset->GetName();
	Name.RemoveFromEnd(Suffix, ESearchCase::IgnoreCase);

	return Name;
}

TArray<FString> ACubismModel::GetMotionNames() const
{
	TArray<FString> Names;

	if (const UCubismMotionComponent* Motion = GetMotionComponent())
	{
		for (const TObjectPtr<UCubismMotion3Json>& Json : Motion->Jsons)
		{
			Names.Add(CubismStripAssetSuffix(Json, TEXT("_motion3")));
		}
	}

	return Names;
}

TArray<FString> ACubismModel::GetExpressionNames() const
{
	TArray<FString> Names;

	if (const UCubismExpressionComponent* Expression = GetExpressionComponent())
	{
		for (const TObjectPtr<UCubismExp3Json>& Json : Expression->Jsons)
		{
			Names.Add(CubismStripAssetSuffix(Json, TEXT("_exp3")));
		}
	}

	return Names;
}

bool ACubismModel::PlayMotionByName(const FString& Name, const ECubismMotionPriority Priority)
{
	UCubismMotionComponent* Motion = GetMotionComponent();

	if (!Motion)
	{
		UE_LOG(LogCubism, Warning, TEXT("%s has no motion component (the model asset has no motions)."), *GetName());
		return false;
	}

	return Motion->PlayMotionByName(Name, 0.0f, Priority);
}

bool ACubismModel::PlayExpressionByName(const FString& Name)
{
	UCubismExpressionComponent* Expression = GetExpressionComponent();

	if (!Expression)
	{
		UE_LOG(LogCubism, Warning, TEXT("%s has no expression component (the model asset has no expressions)."), *GetName());
		return false;
	}

	return Expression->PlayExpressionByName(Name);
}

UCubismLipSyncComponent* ACubismModel::FindOrCreateLipSync()
{
	if (UCubismLipSyncComponent* Existing = GetLipSyncComponent())
	{
		return Existing;
	}

	if (!IsValid(Model))
	{
		return nullptr;
	}

	UCubismLipSyncComponent* LipSync = NewObject<UCubismLipSyncComponent>(Model, TEXT("CubismLipSync"), RF_Transactional);

	LipSync->Json = ModelAsset;

	LipSync->RegisterComponent();
	AddInstanceComponent(LipSync);

	if (LipSync->Ids.Num() == 0)
	{
		// The model asset did not declare lip sync parameters; the standard mouth parameter is the sensible default.
		LipSync->Ids.Add(TEXT("ParamMouthOpenY"));
	}

	return LipSync;
}

void ACubismModel::Speak(USoundWave* VoiceLine)
{
	UCubismLipSyncComponent* LipSync = FindOrCreateLipSync();

	if (!LipSync)
	{
		return;
	}

	LipSync->bAutoEnabled = false;
	LipSync->SetSource(VoiceLine, true);
}

void ACubismModel::StopSpeaking()
{
	if (UCubismLipSyncComponent* LipSync = GetLipSyncComponent())
	{
		LipSync->Stop();
	}
}

void ACubismModel::SetAutoLipSync(const bool bEnabled)
{
	UCubismLipSyncComponent* LipSync = FindOrCreateLipSync();

	if (!LipSync)
	{
		return;
	}

	if (bEnabled)
	{
		LipSync->Stop();
	}

	LipSync->bAutoEnabled = bEnabled;
}

void ACubismModel::SetOpacity(const float Opacity)
{
	if (IsValid(Model))
	{
		Model->Opacity = FMath::Clamp(Opacity, 0.0f, 1.0f);
	}
}

void ACubismModel::SetDimmed(const bool bDimmed, const FLinearColor DimColor)
{
	if (!IsValid(Model))
	{
		return;
	}

	Model->bOverwriteFlagForModelMultiplyColors = bDimmed;
	Model->MultiplyColor = bDimmed ? DimColor : FLinearColor::White;
}

void ACubismModel::SetRenderOrder(const int32 RenderOrder)
{
	if (UCubismRendererComponent* Renderer = GetRendererComponent())
	{
		Renderer->SetRenderOrder(RenderOrder);
	}
}

void ACubismModel::SetLookAtTarget(AActor* Target)
{
	if (!IsValid(Model))
	{
		return;
	}

	UCubismLookAtComponent* LookAt = GetLookAtComponent();

	if (!LookAt)
	{
		if (!Target)
		{
			return;
		}

		LookAt = NewObject<UCubismLookAtComponent>(Model, TEXT("CubismLookAt"), RF_Transactional);

		// The look-at component measures the target offset in model units (canvas pixels): X is horizontal, Y vertical.
		// Scale the standard Cubism parameters so that a target at the edge of the canvas turns them fully.
		const FVector2D CanvasSize = Model->IsModelReady() ? Model->GetCanvasSize() : FVector2D(2000.0f, 2000.0f);
		const float HalfWidth = FMath::Max(CanvasSize.X * 0.5f, 1.0f);
		const float HalfHeight = FMath::Max(CanvasSize.Y * 0.5f, 1.0f);

		struct FDefaultLookAt { const TCHAR* Id; ECubismLookAtAxis Axis; float Factor; };
		const FDefaultLookAt Defaults[] =
		{
			{ TEXT("ParamAngleX"),     ECubismLookAtAxis::X, 30.0f / HalfWidth },
			{ TEXT("ParamAngleY"),     ECubismLookAtAxis::Y, 30.0f / HalfHeight },
			{ TEXT("ParamEyeBallX"),   ECubismLookAtAxis::X,  1.0f / HalfWidth },
			{ TEXT("ParamEyeBallY"),   ECubismLookAtAxis::Y,  1.0f / HalfHeight },
			{ TEXT("ParamBodyAngleX"), ECubismLookAtAxis::X, 10.0f / HalfWidth },
		};

		for (const FDefaultLookAt& Default : Defaults)
		{
			FCubismLookAtParameter Parameter;
			Parameter.bEnabled = true;
			Parameter.Id = Default.Id;
			Parameter.Axis = Default.Axis;
			Parameter.Factor = Default.Factor;

			LookAt->Parameters.Add(Parameter);
		}

		LookAt->RegisterComponent();
		AddInstanceComponent(LookAt);
	}

	LookAt->Target = Target;
}

// ---- Editor testing ----------------------------------------------------------------------------------------

void ACubismModel::TestPlayMotion()
{
#if WITH_EDITORONLY_DATA
	if (!PlayMotionByName(TestMotionName, ECubismMotionPriority::Force))
	{
		TestListNames();
	}
#endif
}

void ACubismModel::TestPlayExpression()
{
#if WITH_EDITORONLY_DATA
	if (!PlayExpressionByName(TestExpressionName))
	{
		TestListNames();
	}
#endif
}

void ACubismModel::TestSpeak()
{
#if WITH_EDITORONLY_DATA
	if (TestVoiceLine)
	{
		Speak(TestVoiceLine);
	}
	else
	{
		SetAutoLipSync(true);
	}
#endif
}

void ACubismModel::TestStop()
{
	StopSpeaking();
	SetAutoLipSync(false);

	if (UCubismMotionComponent* Motion = GetMotionComponent())
	{
		Motion->StopAllMotions();
	}

	if (UCubismExpressionComponent* Expression = GetExpressionComponent())
	{
		Expression->StopAllExpressions();
	}
}

void ACubismModel::TestListNames()
{
	UE_LOG(LogCubism, Display, TEXT("%s motions: %s"), *GetName(), *FString::Join(GetMotionNames(), TEXT(", ")));
	UE_LOG(LogCubism, Display, TEXT("%s expressions: %s"), *GetName(), *FString::Join(GetExpressionNames(), TEXT(", ")));
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
