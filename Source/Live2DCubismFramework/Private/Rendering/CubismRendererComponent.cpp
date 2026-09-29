/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */


#include "Rendering/CubismRendererComponent.h"

#include "CubismMaskRenderer.h"
#include "CubismUpdateExecutionOrder.h"
#include "CubismUpdateControllerComponent.h"
#include "Model/CubismDrawableComponent.h"
#include "Model/CubismPartComponent.h"
#include "Model/CubismModelActor.h"
#include "Model/CubismModelComponent.h"
#include "Rendering/CubismMaskTexture.h"
#include "Rendering/CubismMaskTextureComponent.h"
#include "Rendering/CubismMaskJunction.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "SceneInterface.h"
#include "CubismLog.h"

UCubismRendererComponent::UCubismRendererComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	bTickInEditor = true;
}

void UCubismRendererComponent::BeginPlay()
{
	Super::BeginPlay();

	SpawnMaskTexture();

	if (!IsValid(Model))
	{
		if (const TObjectPtr<UCubismModelComponent> ModelComp = GetModel())
		{
			Setup(ModelComp);
		}
	}
	else if (MaskTexture && MaskTexture->MaskTextureComponent)
	{
		// Make sure the render targets are assigned to the junctions of this model.
		MaskTexture->MaskTextureComponent->ResolveMaskLayout();
	}
}

void UCubismRendererComponent::Setup(UCubismModelComponent* InModel)
{
	if (!InModel)
	{
		return;
	}

	if (!InModel->IsModelReady() && !InModel->EnsureModelBuilt())
	{
		return;
	}

	Model = InModel;

	UCubismUpdateControllerComponent::RequestRefresh(this);

	NumMasks = 0;
	Junctions.Empty();

	for (UCubismDrawableComponent* Drawable : Model->Drawables)
	{
		if (!IsValid(Drawable))
		{
			continue;
		}

		TSharedPtr<FCubismMaskJunction> TargetJunction = nullptr;

		for (const TSharedPtr<FCubismMaskJunction>& Junction : Junctions)
		{
			if (Junction->MaskDrawables.Num() == Drawable->Masks.Num())
			{
				bool bAny = true;
				for (int32 i = 0, num = Drawable->Masks.Num(); bAny && i < num; i++)
				{
					const int32 MaskDrawableIndex = Drawable->Masks[i];
					bAny &= Junction->MaskDrawables[i].Drawable.Get() == Model->GetDrawable(MaskDrawableIndex);
				}

				if (bAny)
				{
					TargetJunction = Junction;
					break;
				}
			}
		}

		if (TargetJunction == nullptr)
		{
			TargetJunction = MakeShared<FCubismMaskJunction>();

			if (Drawable->Masks.Num() > 0)
			{
				TargetJunction->MaskDrawables.Reserve(Drawable->Masks.Num());
				for (const int32 MaskDrawableIndex : Drawable->Masks)
				{
					UCubismDrawableComponent* MaskDrawable = Model->GetDrawable(MaskDrawableIndex);

					if (!IsValid(MaskDrawable))
					{
						continue;
					}

					const int32 NumVertices = MaskDrawable->GetVertexPositions().Num();
					const int32 NumIndices = MaskDrawable->GetVertexIndices().Num();

					if (NumVertices == 0 || NumIndices == 0)
					{
						continue;
					}

					FCubismMaskJunction::FMaskDrawableData MaskDrawableData;
					MaskDrawableData.Drawable = MaskDrawable;
					MaskDrawableData.Renderer = MakeUnique<FCubismMaskRenderer>(NumVertices, NumIndices);

					TargetJunction->MaskDrawables.Add(MoveTemp(MaskDrawableData));
				}

				NumMasks++;
			}

			Junctions.Add(TargetJunction);
		}

		TargetJunction->Drawables.AddUnique(TWeakObjectPtr<UCubismDrawableComponent>(Drawable));
	}

	if (Model->Renderer != this)
	{
		if (IsValid(Model->Renderer))
		{
			Model->Renderer->DestroyComponent();
		}
		Model->Renderer = this;
	}

	ApplyRenderOrder();

	if (MaskTexture && MaskTexture->MaskTextureComponent)
	{
		if (AActor* Owner = GetOwner())
		{
			MaskTexture->MaskTextureComponent->AddModel(Owner);
		}

		MaskTexture->MaskTextureComponent->ResolveMaskLayout();
		AddTickPrerequisiteComponent(MaskTexture->MaskTextureComponent); // must render after mask texture updated
	}

	AddTickPrerequisiteComponent(Model); // must render after model updated
}

bool UCubismRendererComponent::HasValidModel() const
{
	return IsValid(Model) && Model->IsModelReady();
}

void UCubismRendererComponent::ApplyRenderOrder()
{
	if (!HasValidModel())
	{
		return;
	}

	for (const TObjectPtr<UCubismDrawableComponent>& Drawable : Model->Drawables)
	{
		if (!IsValid(Drawable))
		{
			continue;
		}

		const int32 NewRenderOrder = CalcRenderOrder(Drawable);

		if (bZSort)
		{
			Drawable->SetTranslucentSortPriority(0);
			Drawable->SetRelativeLocation(FVector(NewRenderOrder * Epsilon, 0.0f, 0.0f));
		}
		else
		{
			Drawable->SetTranslucentSortPriority(NewRenderOrder);
			Drawable->SetRelativeLocation(FVector(0.0f, 0.0f, 0.0f));
		}
	}
}

int32 UCubismRendererComponent::CalcRenderOrder(const UCubismDrawableComponent* Drawable) const
{
	if (!Drawable)
	{
		return RenderOrder;
	}

	int32 NewRenderOrder = Drawable->RenderOrder + Drawable->RenderOrderOffset;

	switch (SortingOrder)
	{
		case ECubismRendererSortingOrder::FrontToBack:
		{
			break;
		}
		case ECubismRendererSortingOrder::BackToFront:
		{
			const int32 DrawableCount = HasValidModel() ? Model->GetDrawableCount() : 0;

			NewRenderOrder = DrawableCount - NewRenderOrder - 1;

			break;
		}
		default:
		{
			ensure(false);
			break;
		}
	}

	NewRenderOrder += RenderOrder;

	return NewRenderOrder;
}

void UCubismRendererComponent::SpawnMaskTexture()
{
	AActor* Owner = GetOwner();
	UWorld* World = Owner ? Owner->GetWorld() : GetWorld();

	if (!World)
	{
		return;
	}

	if (MaskTexture == nullptr)
	{
		// Preview worlds (e.g. the Blueprint editor viewport) and inactive worlds must not get actors spawned into them.
		if (World->WorldType != EWorldType::Game && World->WorldType != EWorldType::PIE && World->WorldType != EWorldType::Editor)
		{
			return;
		}

		TArray<AActor*> FoundActors;
		UGameplayStatics::GetAllActorsOfClass(World, ACubismMaskTexture::StaticClass(), FoundActors);
		if (FoundActors.Num() == 0)
		{
			MaskTexture = World->SpawnActor<ACubismMaskTexture>();

#if WITH_EDITOR
			if (MaskTexture)
			{
				MaskTexture->SetActorLabel(TEXT("CubismMaskTexture"));
				MaskTexture->SetFlags(RF_Transactional);
			}
#endif
		}
		else
		{
			MaskTexture = Cast<ACubismMaskTexture>(FoundActors[0]);
		}
	}

	if (MaskTexture && MaskTexture->MaskTextureComponent && Owner)
	{
		// AddModel is idempotent, it just marks the layout dirty.
		MaskTexture->MaskTextureComponent->AddModel(Owner);
	}
}

TObjectPtr<UCubismModelComponent> UCubismRendererComponent::GetModel()
{
	return UCubismModelComponent::FindModelComponent(this);
}

// UObject interface
void UCubismRendererComponent::PostLoad()
{
	Super::PostLoad();

	const TObjectPtr<UCubismModelComponent> ModelComp = GetModel();

	if (ModelComp)
	{
		Setup(ModelComp);
	}
}

#if WITH_EDITOR
void UCubismRendererComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (!HasValidModel())
	{
		return;
	}

	const FName PropertyName = PropertyChangedEvent.GetPropertyName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UCubismRendererComponent, MaskTexture))
	{
		if (MaskTexture && MaskTexture->MaskTextureComponent)
		{
			if (AActor* Owner = GetOwner())
			{
				MaskTexture->MaskTextureComponent->AddModel(Owner);
			}

			AddTickPrerequisiteComponent(MaskTexture->MaskTextureComponent);
		}
	}

	if (
		PropertyName == GET_MEMBER_NAME_CHECKED(UCubismRendererComponent, SortingOrder) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(UCubismRendererComponent, bZSort) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(UCubismRendererComponent, RenderOrder) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(UCubismRendererComponent, Epsilon))
	{
		ApplyRenderOrder();
	}
}
#endif
// End of UObject interface

// UActorComponent interface
void UCubismRendererComponent::OnComponentCreated()
{
	Super::OnComponentCreated();

	SpawnMaskTexture();

	const TObjectPtr<UCubismModelComponent> ModelComp = GetModel();

	if (ModelComp)
	{
		Setup(ModelComp);
	}
}

void UCubismRendererComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	// Only unregister the owner from the mask texture if no other renderer took over the model.
	const bool bIsActiveRenderer = !IsValid(Model) || Model->Renderer == this;

	if (bIsActiveRenderer && MaskTexture && MaskTexture->MaskTextureComponent)
	{
		if (AActor* Owner = GetOwner())
		{
			MaskTexture->MaskTextureComponent->RemoveModel(Owner);
		}
	}

	if (IsValid(Model) && Model->Renderer == this)
	{
		Model->Renderer = nullptr;
	}

	Junctions.Empty();

	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

#if WITH_EDITOR
void UCubismRendererComponent::PostEditUndo()
{
	Super::PostEditUndo();

	if (const TObjectPtr<UCubismModelComponent> ModelComp = GetModel())
	{
		Setup(ModelComp);
	}
}
#endif

void UCubismRendererComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// When an update controller drives this actor it calls OnCubismUpdate in execution order instead.
	if (IsControlledByUpdateController() && UCubismUpdateControllerComponent::FindController(this))
	{
		return;
	}

	OnCubismUpdate(DeltaTime);
}

void UCubismRendererComponent::OnCubismUpdate(float DeltaTime)
{
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

	// The junctions are rebuilt whenever the model regenerates its drawables.
	if (Junctions.Num() == 0 && Model->Drawables.Num() > 0)
	{
		Setup(Model);
	}

	for (const TSharedPtr<FCubismMaskJunction>& Junction : Junctions)
	{
		if (!Junction.IsValid())
		{
			continue;
		}

		for (const TWeakObjectPtr<UCubismDrawableComponent>& WeakDrawable : Junction->Drawables)
		{
			UCubismDrawableComponent* Drawable = WeakDrawable.Get();

			if (!IsValid(Drawable))
			{
				continue;
			}

			UMaterialInstanceDynamic* MaterialInstance = Cast<UMaterialInstanceDynamic>(Drawable->GetMaterial(0));

			if (!MaterialInstance)
			{
				continue;
			}

			const TObjectPtr<UTexture2D>& MainTexture   = Model->Textures.IsValidIndex(Drawable->TextureIndex)? Model->Textures[Drawable->TextureIndex] : nullptr;
			FLinearColor BaseColor     = Drawable->BaseColor;
			FLinearColor MultiplyColor = Drawable->MultiplyColor;
			FLinearColor ScreenColor   = Drawable->ScreenColor;

			{
				if (Model->bOverwriteFlagForModelMultiplyColors)
				{
					MultiplyColor = Model->MultiplyColor;
				}

				if (Model->bOverwriteFlagForModelScreenColors)
				{
					ScreenColor = Model->ScreenColor;
				}
			}

			if (const UCubismPartComponent* ParentPart = Model->GetPart(Drawable->ParentPartIndex))
			{
				if (ParentPart->bOverwriteFlagForPartMultiplyColors)
				{
					MultiplyColor = ParentPart->MultiplyColor;
				}

				if (ParentPart->bOverwriteFlagForPartScreenColors)
				{
					ScreenColor = ParentPart->ScreenColor;
				}
			}

			BaseColor.A *= Model->Opacity * Drawable->Opacity;

			MaterialInstance->SetTextureParameterValue("MainTexture", MainTexture);
			MaterialInstance->SetVectorParameterValue("BaseColor", BaseColor);
			MaterialInstance->SetVectorParameterValue("MultiplyColor", MultiplyColor);
			MaterialInstance->SetVectorParameterValue("ScreenColor", ScreenColor);

			if (Drawable->IsMasked())
			{
				MaterialInstance->SetTextureParameterValue("MaskTexture", Junction->RenderTarget.Get());
				MaterialInstance->SetVectorParameterValue("Offset", Junction->Offset);
				MaterialInstance->SetVectorParameterValue("Channel", Junction->Channel);
			}
		}
	}
}

int32 UCubismRendererComponent::GetExecutionOrder() const
{
	return CUBISM_EXECUTION_ORDER_RENDERER;
}
// End of UActorComponent interface
