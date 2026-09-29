/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */


#include "Model/CubismPartComponent.h"

#include "Model/CubismParameterStoreComponent.h"
#include "Model/CubismModelActor.h"
#include "CubismLog.h"

UCubismPartComponent::UCubismPartComponent()
	: Index(-1)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_DuringPhysics;
	bTickInEditor = true;
}

void UCubismPartComponent::Setup(UCubismModelComponent* InModel)
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

	const bool bNative = Index >= 0 && Index < InModel->GetPartCount();

	if (!bNative && !InModel->NonNativePartIds.Contains(Index))
	{
		UE_LOG(LogCubism, Warning, TEXT("UCubismPartComponent::Setup: part index %d is unknown to model '%s'."), Index, *InModel->GetName());

		return;
	}

	// A component that already has an ID was loaded or duplicated: its opacity is the one the user saved.
	const bool bFirstSetup = Id.IsEmpty();

	Model = InModel;

	if (bNative)
	{
		Id = Model->GetPartId(Index);
	}
	else
	{
		Id = Model->NonNativePartIds[Index];
	}

	if (bFirstSetup)
	{
		Opacity = bNative ? Model->GetPartOpacity(Index) : 1.0f;

		if (FGenericPlatformMath::IsNaN(Opacity))
		{
			Opacity = 1.0f;
		}
	}
	else
	{
		Opacity = FMath::Clamp(Opacity, 0.0f, 1.0f);
		Model->SetPartOpacity(Index, Opacity);
	}
}

bool UCubismPartComponent::HasValidModel() const
{
	return IsValid(Model) && Model->IsModelReady();
}

void UCubismPartComponent::SetPartOpacity(float TargetOpacity)
{
	Opacity = TargetOpacity;

	if (!HasValidModel())
	{
		return;
	}

	Model->SetPartOpacity(Index, Opacity);
}

TObjectPtr<UCubismModelComponent> UCubismPartComponent::GetModel()
{
	return UCubismModelComponent::FindModelComponent(this);
}

// UObject interface
void UCubismPartComponent::PostLoad()
{
	Super::PostLoad();

	const TObjectPtr<UCubismModelComponent> ModelComp = GetModel();

	if (ModelComp)
	{
		Setup(ModelComp);
	}
}

#if WITH_EDITOR
void UCubismPartComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.GetPropertyName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UCubismPartComponent, Opacity))
	{
		if (!HasValidModel())
		{
			return;
		}

		Model->SetPartOpacity(Index, Opacity);

		if (IsValid(Model->ParameterStore))
		{
			Model->ParameterStore->SavePartOpacity(Index);
		}
	}
}
#endif
// End of UObject interface

// UActorComponent interface
void UCubismPartComponent::OnComponentCreated()
{
	Super::OnComponentCreated();

	const TObjectPtr<UCubismModelComponent> ModelComp = GetModel();

	if (ModelComp)
	{
		Setup(ModelComp);
	}
}

#if WITH_EDITOR
void UCubismPartComponent::PostEditUndo()
{
	Super::PostEditUndo();

	if (const TObjectPtr<UCubismModelComponent> ModelComp = GetModel())
	{
		Setup(ModelComp);
	}
}
#endif
// End of UActorComponent interface
