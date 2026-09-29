/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */


#include "Model/CubismDrawableComponent.h"

#include "Model/CubismModelActor.h"
#include "Model/CubismModelComponent.h"
#include "Rendering/CubismDrawableSceneProxy.h"
#include "UserData/CubismUserData3Json.h"
#include "CubismLog.h"
#include "Live2DCubismCore.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/Vector.h"

UCubismDrawableComponent::UCubismDrawableComponent()
	: Index(-1)
	, RenderOrder(0)
	, TextureIndex(0)
	, Opacity(1.0f)
	, BaseColor(FLinearColor::White)
	, bOverwriteFlagForDrawableMultiplyColors(false)
	, MultiplyColor(FLinearColor::White)
	, bOverwriteFlagForDrawableScreenColors(false)
	, ScreenColor(FLinearColor::Black)
	, bOverwriteFlagForDrawableIsTwoSided(false)
	, bTwoSided(false)
	, ParentPartIndex(-1)
	, BlendMode(ECubismDrawableBlendMode::Normal)
	, InvertedMask(false)
	, bBoundsDirty(true)
	, LocalBounds(ForceInit)
	, UserMultiplyColor(FLinearColor::White)
	, UserScreenColor(FLinearColor::Black)
	, bUserTwoSided(false)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_DuringPhysics;
	bTickInEditor = true;
}

void UCubismDrawableComponent::Setup(UCubismModelComponent* InModel)
{
	if (!InModel)
	{
		return;
	}

	if (!InModel->IsModelReady() && !InModel->EnsureModelBuilt())
	{
		return;
	}

	if (Model == InModel)
	{
		return;
	}

	if (Index < 0 || Index >= InModel->GetDrawableCount())
	{
		UE_LOG(LogCubism, Warning, TEXT("UCubismDrawableComponent::Setup: drawable index %d is out of range for model '%s'."), Index, *InModel->GetName());

		return;
	}

	// A component that already has an ID was loaded or duplicated: keep the values the user edited on it.
	const bool bFirstSetup = Id.IsEmpty();

	Model = InModel;

	Id = Model->GetDrawableId(Index);

	RenderOrder = Model->GetDrawableRenderOrder(Index);

	TextureIndex = Model->GetDrawableTextureIndex(Index);

	{
		const int32 VertexIndexCount(Model->GetDrawableVertexIndexCount(Index));
		const int32 VertexCount(Model->GetDrawableVertexCount(Index));
		const int32 MaskCount(Model->GetDrawableMaskCount(Index));

		const csmVector2* DrawableVertexUvs(Model->GetDrawableVertexUv(Index));
		VertexUvs.Empty();
		VertexUvs.Reserve(VertexCount);

		for (int32 i = 0; DrawableVertexUvs && i < VertexCount; i++)
		{
			VertexUvs.Add(FVector2D(DrawableVertexUvs[i].X, DrawableVertexUvs[i].Y));
		}

		const csmVector2* DrawableVertexPositions(Model->GetDrawableVertexPosition(Index));
		VertexPositions.Empty();
		VertexPositions.Reserve(VertexCount);

		for (int32 i = 0; DrawableVertexPositions && i < VertexCount; i++)
		{
			VertexPositions.Add(FVector2D(-DrawableVertexPositions[i].X, DrawableVertexPositions[i].Y));
		}

		const uint16* DrawableVertexIndices(Model->GetDrawableVertexIndex(Index));
		VertexIndices.Empty();
		VertexIndices.Reserve(VertexIndexCount);

		for (int32 i = 0; DrawableVertexIndices && i < VertexIndexCount; ++i)
		{
			VertexIndices.Add(static_cast<int32>(DrawableVertexIndices[i]));
		}

		const int32* DrawableMasks(Model->GetDrawableMask(Index));
		Masks.Empty();
		Masks.Reserve(MaskCount);
		for (int32 MaskIndex = 0; DrawableMasks && MaskIndex < MaskCount; ++MaskIndex)
		{
			Masks.Add(DrawableMasks[MaskIndex]);
		}
	}

	bBoundsDirty = true;

	if (bFirstSetup)
	{
		Opacity = Model->GetDrawableOpacity(Index);
		BaseColor = FLinearColor::White;
		MultiplyColor = Model->GetDrawableMultiplyColor(Index);
		ScreenColor = Model->GetDrawableScreenColor(Index);
		bTwoSided = Model->GetDrawableIsTwoSided(Index);
		UserMultiplyColor = MultiplyColor;
		UserScreenColor = ScreenColor;
		bUserTwoSided = bTwoSided;

		bOverwriteFlagForDrawableMultiplyColors = false;
		bOverwriteFlagForDrawableScreenColors = false;
		bOverwriteFlagForDrawableIsTwoSided = false;
	}
	else
	{
		// The user caches are not serialized; restore them from the current values.
		UserMultiplyColor = MultiplyColor;
		UserScreenColor = ScreenColor;
		bUserTwoSided = bTwoSided;

		if (!bOverwriteFlagForDrawableMultiplyColors)
		{
			MultiplyColor = Model->GetDrawableMultiplyColor(Index);
		}

		if (!bOverwriteFlagForDrawableScreenColors)
		{
			ScreenColor = Model->GetDrawableScreenColor(Index);
		}

		if (!bOverwriteFlagForDrawableIsTwoSided)
		{
			bTwoSided = Model->GetDrawableIsTwoSided(Index);
		}
	}

	ParentPartIndex = Model->GetDrawableParentPartIndex(Index);
	BlendMode = Model->GetDrawableBlendMode(Index);
	InvertedMask = Model->GetDrawableInvertedMask(Index);

	FString MaterialName;

	switch(BlendMode)
	{
		case ECubismDrawableBlendMode::Normal:
		{
			MaterialName = !IsMasked()? TEXT("CustomUnlitNormal") : InvertedMask? TEXT("CustomUnlitNormalMaskedInverted") : TEXT("CustomUnlitNormalMasked");
			break;
		}
		case ECubismDrawableBlendMode::Additive:
		{
			MaterialName = !IsMasked()? TEXT("CustomUnlitAdditive") : InvertedMask? TEXT("CustomUnlitAdditiveMaskedInverted") : TEXT("CustomUnlitAdditiveMasked");
			break;
		}
		case ECubismDrawableBlendMode::Multiplicative:
		{
			MaterialName = !IsMasked()? TEXT("CustomUnlitMultiplicative") : InvertedMask? TEXT("CustomUnlitMultiplicativeMaskedInverted") : TEXT("CustomUnlitMultiplicativeMasked");
			break;
		}
		default:
		{
			ensure(false);
			break;
		}
	}

	UMaterial* Material = Cast<UMaterial>(StaticLoadObject(UMaterial::StaticClass(), nullptr, *(TEXT("/Live2DCubismSDK/Materials") / MaterialName)));

	if (Material)
	{
		UMaterialInstanceDynamic* MaterialInstance = UMaterialInstanceDynamic::Create(Material, this);

		SetMaterial(0, static_cast<UMaterialInterface*>(MaterialInstance));
	}
	else
	{
		UE_LOG(LogCubism, Error, TEXT("UCubismDrawableComponent::Setup: material '%s' was not found in the plugin content."), *MaterialName);
	}

	const FCubismUserDataEntry* UserDataEntry = Model->UserDataJson ? Model->UserDataJson->Data.Find(ECubismUserDataTargetType::ArtMesh) : nullptr;
	const FString* Tag = UserDataEntry ? UserDataEntry->Tags.Find(Id) : nullptr;

	if (bFirstSetup || Tag)
	{
		UserDataTag = Tag ? *Tag : TEXT("");
	}

	AddTickPrerequisiteComponent(Model); // must be updated after model updated

	MarkRenderStateDirty();
}

TArray<int32> UCubismDrawableComponent::GetVertexIndices() const
{
	return VertexIndices;
}

TArray<FVector2D> UCubismDrawableComponent::GetVertexPositions() const
{
	return VertexPositions;
}

FVector UCubismDrawableComponent::ToGlobalPosition(const FVector2D VertexPosition) const
{
	const float Scale = IsValid(Model) ? 0.01f * Model->GetPixelsPerUnit() : 1.0f;

	// align the model on the y-z plane
	return FVector(0.0f, Scale * VertexPosition.X, Scale * VertexPosition.Y);
}

TArray<FVector2D> UCubismDrawableComponent::GetVertexUvs() const
{
	return VertexUvs;
}


const TArray<int32> UCubismDrawableComponent::GetDrawableMask() const
{
	return Masks;
}

int32 UCubismDrawableComponent::GetDrawableMaskCount() const
{
	return Masks.Num();
}

bool UCubismDrawableComponent::HasValidModel() const
{
	return IsValid(Model) && Model->IsModelReady() && Index >= 0 && Index < Model->GetDrawableCount();
}

TObjectPtr<UCubismModelComponent> UCubismDrawableComponent::GetModel()
{
	return UCubismModelComponent::FindModelComponent(this);
}

// UObject interface
void UCubismDrawableComponent::PostLoad()
{
	Super::PostLoad();

	const TObjectPtr<UCubismModelComponent> ModelComp = GetModel();

	if (ModelComp)
	{
		Setup(ModelComp);
	}
}

#if WITH_EDITOR
void UCubismDrawableComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (!Model)
	{
		return;
	}

	const FName PropertyName = PropertyChangedEvent.GetPropertyName();

	const bool bModelReady = HasValidModel();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UCubismDrawableComponent, bOverwriteFlagForDrawableMultiplyColors))
	{
		// If the flag is changed from false to true, the stored color is applied.
		if (bOverwriteFlagForDrawableMultiplyColors)
		{
			MultiplyColor = UserMultiplyColor;
		}
		// If the flag is changed from true to false, the color from the model is applied.
		else if (bModelReady)
		{
			MultiplyColor = Model->GetDrawableMultiplyColor(Index);
		}
	}

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UCubismDrawableComponent, MultiplyColor))
	{
		UserMultiplyColor = MultiplyColor;
	}

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UCubismDrawableComponent, bOverwriteFlagForDrawableScreenColors))
	{
		// If the flag is changed from false to true, the stored color is applied.
		if (bOverwriteFlagForDrawableScreenColors)
		{
			ScreenColor = UserScreenColor;
		}
		// If the flag is changed from true to false, the color from the model is applied.
		else if (bModelReady)
		{
			ScreenColor = Model->GetDrawableScreenColor(Index);
		}
	}

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UCubismDrawableComponent, ScreenColor))
	{
		UserScreenColor = ScreenColor;
	}

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UCubismDrawableComponent, bOverwriteFlagForDrawableIsTwoSided))
	{
		// If the flag is changed from false to true, the stored value is applied.
		if (bOverwriteFlagForDrawableIsTwoSided)
		{
			bTwoSided = bUserTwoSided;
		}
		// If the flag is changed from true to false, the value from the model is applied.
		else if (bModelReady)
		{
			bTwoSided = Model->GetDrawableIsTwoSided(Index);
		}

		MarkRenderDynamicDataDirty();
	}

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UCubismDrawableComponent, bTwoSided))
	{
		bUserTwoSided = bTwoSided;

		MarkRenderDynamicDataDirty();
	}
}
#endif
// End of UObject interface

// UActorComponent interface
void UCubismDrawableComponent::OnComponentCreated()
{
	Super::OnComponentCreated();

	const TObjectPtr<UCubismModelComponent> ModelComp = GetModel();

	if (ModelComp)
	{
		Setup(ModelComp);
	}
}

#if WITH_EDITOR
void UCubismDrawableComponent::PostEditUndo()
{
	Super::PostEditUndo();

	if (const TObjectPtr<UCubismModelComponent> ModelComp = GetModel())
	{
		Setup(ModelComp);
	}
}
#endif

void UCubismDrawableComponent::SendRenderDynamicData_Concurrent()
{
	Super::SendRenderDynamicData_Concurrent();

	if (FCubismDrawableSceneProxy* DrawableProxy = static_cast<FCubismDrawableSceneProxy*>(SceneProxy))
	{
		FCubismDrawableDynamicMeshData NewDynamicData;

		{
			FRWScopeLock ReadLock(DynamicDataGuard, SLT_ReadOnly);

			NewDynamicData.Index = Index;

			for (const FVector2D& LocalPosition : GetVertexPositions())
			{
				NewDynamicData.Positions.Add(FVector3f(ToGlobalPosition(LocalPosition)));
			}

			for (const FVector2D& UV : GetVertexUvs())
			{
				NewDynamicData.UVs.Add(FVector2f(UV));
			}

			NewDynamicData.Indices.Append(VertexIndices);
			NewDynamicData.bTwoSided = bTwoSided;
		}

		ENQUEUE_RENDER_COMMAND(DrawableUpdateDynamicData)(
			[DrawableProxy, NewDynamicData](FRHICommandListImmediate& RHICmdList)
			{
				DrawableProxy->UpdateDynamicData(RHICmdList, NewDynamicData);
			}
		);
	}
}

void UCubismDrawableComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!IsValid(Model))
	{
		// The model may have been created after this component (e.g. Blueprint construction order).
		if (const TObjectPtr<UCubismModelComponent> ModelComp = GetModel())
		{
			Setup(ModelComp);
		}
	}

	if (!HasValidModel())
	{
		return;
	}

	if (Model->GetDrawableDynamicFlagOpacityDidChange(Index))
	{
		Opacity = Model->GetDrawableOpacity(Index);
	}

	if (Model->GetDrawableDynamicFlagVertexPositionsDidChange(Index))
	{
		FRWScopeLock WriteLock(DynamicDataGuard, SLT_Write);

		const csmVector2* DrawableVertexPositions = Model->GetDrawableVertexPosition(Index);
		const int32 VertexCount = Model->GetDrawableVertexCount(Index);

		if (VertexPositions.Num() != VertexCount)
		{
			VertexPositions.SetNum(VertexCount);
		}


		for (int32 i = 0; DrawableVertexPositions && i < VertexCount; i++)
		{
			VertexPositions[i] = FVector2D(-DrawableVertexPositions[i].X, DrawableVertexPositions[i].Y);
		}

		bBoundsDirty = true;
		MarkRenderDynamicDataDirty();
		MarkRenderTransformDirty(); // pushes the recalculated bounds to the render thread
	}

	if (Model->GetDrawableDynamicFlagBlendColorDidChange(Index))
	{
		if (!bOverwriteFlagForDrawableMultiplyColors)
		{
			MultiplyColor = Model->GetDrawableMultiplyColor(Index);
		}

		if (!bOverwriteFlagForDrawableScreenColors)
		{
			ScreenColor = Model->GetDrawableScreenColor(Index);
		}
	}
}
// End of UActorComponent interface

//~ Begin USceneComponent Interface
FBoxSphereBounds UCubismDrawableComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	if (IsValid(Model))
	{
		if (bBoundsDirty)
		{
			FBox Box(ForceInit);

			for (const FVector2D& LocalPosition : VertexPositions)
			{
				Box += ToGlobalPosition(LocalPosition);
			}

			if (!Box.IsValid)
			{
				Box = FBox(FVector::ZeroVector, FVector::ZeroVector);
			}

			LocalBounds = FBoxSphereBounds(Box);
			bBoundsDirty = false;
		}
	}

	return LocalBounds.TransformBy(LocalToWorld);
}
//~ End USceneComponent Interface

//~ Begin UPrimitiveComponent Interface
FPrimitiveSceneProxy* UCubismDrawableComponent::CreateSceneProxy()
{
	if (!GetMaterial(0) || VertexPositions.Num() == 0 || VertexIndices.Num() == 0)
	{
		return nullptr;
	}

	FCubismDrawableDynamicMeshData DynamicData;

	DynamicData.Index = Index;

	for (const FVector2D& LocalPosition : VertexPositions)
	{
		DynamicData.Positions.Add(FVector3f(ToGlobalPosition(LocalPosition)));
	}

	for (const FVector2D& UV : VertexUvs)
	{
		DynamicData.UVs.Add(FVector2f(UV));
	}

	DynamicData.Indices.Append(VertexIndices);
	DynamicData.bTwoSided = bTwoSided;

	return new FCubismDrawableSceneProxy(this, DynamicData);
}
//~ End UPrimitiveComponent Interface
