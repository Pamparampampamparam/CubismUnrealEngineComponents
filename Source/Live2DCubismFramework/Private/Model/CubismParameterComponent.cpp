/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */


#include "Model/CubismParameterComponent.h"

#include "Model/CubismParameterStoreComponent.h"
#include "Model/CubismModelActor.h"
#include "CubismLog.h"

#include "Live2DCubismCore.h"

UCubismParameterComponent::UCubismParameterComponent()
	: Index(-1)
	, Type(ECubismParameterType::Normal)
	, MinimumValue(0.0f)
	, MaximumValue(1.0f)
	, DefaultValue(0.0f)
	, Value(0.0f)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_DuringPhysics;
	bTickInEditor = true;
}

void UCubismParameterComponent::Setup(UCubismModelComponent* InModel)
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

	const bool bNative = Index >= 0 && Index < InModel->GetParameterCount();

	if (!bNative && !InModel->NonNativeParameterIds.Contains(Index))
	{
		UE_LOG(LogCubism, Warning, TEXT("UCubismParameterComponent::Setup: parameter index %d is unknown to model '%s'."), Index, *InModel->GetName());

		return;
	}

	// A component that already has an ID was loaded or duplicated: its value is the one the user saved.
	const bool bFirstSetup = Id.IsEmpty();

	Model = InModel;

	if (bNative)
	{
		Id = Model->GetParameterId(Index);
		Type = Model->GetParameterType(Index);
		MaximumValue = Model->GetParameterMaximumValue(Index);
		MinimumValue = Model->GetParameterMinimumValue(Index);
		DefaultValue = Model->GetParameterDefaultValue(Index);
	}
	else
	{
		Id = Model->NonNativeParameterIds[Index];
		Type = ECubismParameterType::Normal;
		MaximumValue = 1.0f;
		MinimumValue = 0.0f;
		DefaultValue = 0.0f;
	}

	if (FGenericPlatformMath::IsNaN(MinimumValue) || FGenericPlatformMath::IsNaN(MaximumValue) || !(MaximumValue > MinimumValue))
	{
		MinimumValue = 0.0f;
		MaximumValue = 1.0f;
	}

	if (bFirstSetup)
	{
		Value = Model->GetParameterValue(Index);
	}
	else
	{
		// Push the saved value into the freshly created raw model.
		Value = FMath::Clamp(Value, MinimumValue, MaximumValue);
		Model->SetParameterValue(Index, Value);
	}
}

bool UCubismParameterComponent::HasValidModel() const
{
	return IsValid(Model) && Model->IsModelReady();
}

void UCubismParameterComponent::SetParameterValue(float TargetValue, const float Weight)
{
	if (!HasValidModel())
	{
		return;
	}

	float CurrentValue = Weight == 1.0f? TargetValue : Model->GetParameterValue(Index) * (1.0f - Weight) + TargetValue * Weight;

	if (!FGenericPlatformMath::IsNaN(MinimumValue) && !FGenericPlatformMath::IsNaN(MaximumValue))
	{
		CurrentValue = FMath::Clamp(CurrentValue, MinimumValue, MaximumValue);
	}

	Value = CurrentValue;

	Model->SetParameterValue(Index, CurrentValue);
}

void UCubismParameterComponent::AddParameterValue(float TargetValue, const float Weight)
{
	if (!HasValidModel())
	{
		return;
	}

	float CurrentValue = Model->GetParameterValue(Index) + TargetValue * Weight;

	if (!FGenericPlatformMath::IsNaN(MinimumValue) && !FGenericPlatformMath::IsNaN(MaximumValue))
	{
		CurrentValue = FMath::Clamp(CurrentValue, MinimumValue, MaximumValue);
	}

	Value = CurrentValue;

	Model->SetParameterValue(Index, CurrentValue);
}

void UCubismParameterComponent::MultiplyParameterValue(float TargetValue, const float Weight)
{
	if (!HasValidModel())
	{
		return;
	}

	float CurrentValue = Model->GetParameterValue(Index) * (1.0f + (TargetValue - 1.0f) * Weight);

	if (!FGenericPlatformMath::IsNaN(MinimumValue) && !FGenericPlatformMath::IsNaN(MaximumValue))
	{
		CurrentValue = FMath::Clamp(CurrentValue, MinimumValue, MaximumValue);
	}

	Value = CurrentValue;

	Model->SetParameterValue(Index, CurrentValue);
}

TObjectPtr<UCubismModelComponent> UCubismParameterComponent::GetModel()
{
	return UCubismModelComponent::FindModelComponent(this);
}

// UObject interface
void UCubismParameterComponent::PostLoad()
{
	Super::PostLoad();

	const TObjectPtr<UCubismModelComponent> ModelComp = GetModel();

	if (ModelComp)
	{
		Setup(ModelComp);
	}
}

#if WITH_EDITOR
void UCubismParameterComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.GetPropertyName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UCubismParameterComponent, Value))
	{
		if (!HasValidModel())
		{
			return;
		}

		Value = FMath::Clamp(Value, MinimumValue, MaximumValue);

		Model->SetParameterValue(Index, Value);

		if (IsValid(Model->ParameterStore))
		{
			Model->ParameterStore->SaveParameterValue(Index);
		}
	}
}
#endif
// End of UObject interface

// UActorComponent interface
void UCubismParameterComponent::OnComponentCreated()
{
	Super::OnComponentCreated();

	const TObjectPtr<UCubismModelComponent> ModelComp = GetModel();

	if (ModelComp)
	{
		Setup(ModelComp);
	}
}
// End of UActorComponent interface
