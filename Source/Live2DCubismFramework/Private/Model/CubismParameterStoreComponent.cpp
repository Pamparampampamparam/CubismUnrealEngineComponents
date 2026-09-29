/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */


#include "Model/CubismParameterStoreComponent.h"

#include "CubismUpdateExecutionOrder.h"
#include "CubismUpdateControllerComponent.h"

#include "Model/CubismModelComponent.h"
#include "Model/CubismParameterComponent.h"
#include "Model/CubismPartComponent.h"
#include "Model/CubismModelActor.h"

UCubismParameterStoreComponent::UCubismParameterStoreComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	bTickInEditor = true;
}

void UCubismParameterStoreComponent::Setup(UCubismModelComponent* InModel)
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

	ParameterValues.Empty();

	PartOpacities.Empty();

	SaveParameters();

	if (Model->ParameterStore != this)
	{
		if (IsValid(Model->ParameterStore))
		{
			Model->ParameterStore->DestroyComponent();
		}
		Model->ParameterStore = this;
	}
}

bool UCubismParameterStoreComponent::HasValidModel() const
{
	return IsValid(Model) && Model->IsModelReady();
}

void UCubismParameterStoreComponent::SaveParameterValue(const int32 ParameterIndex)
{
	if (!HasValidModel())
	{
		return;
	}

	if (const UCubismParameterComponent* Parameter = Model->GetParameter(ParameterIndex))
	{
		ParameterValues.Add(ParameterIndex, Parameter->Value);
	}
}

void UCubismParameterStoreComponent::SavePartOpacity(const int32 PartIndex)
{
	if (!HasValidModel())
	{
		return;
	}

	if (const UCubismPartComponent* Part = Model->GetPart(PartIndex))
	{
		PartOpacities.Add(PartIndex, Part->Opacity);
	}
}

void UCubismParameterStoreComponent::SaveParameters()
{
	if (!HasValidModel())
	{
		return;
	}

	for (const TObjectPtr<UCubismParameterComponent>& Parameter : Model->Parameters)
	{
		if (!IsValid(Parameter))
		{
			continue;
		}

		ParameterValues.Add(Parameter->Index, Parameter->Value);
	}

	for (const TObjectPtr<UCubismPartComponent>& Part : Model->Parts)
	{
		if (!IsValid(Part))
		{
			continue;
		}

		PartOpacities.Add(Part->Index, Part->Opacity);
	}
}

void UCubismParameterStoreComponent::LoadParameters()
{
	if (!HasValidModel())
	{
		return;
	}

	for (const TObjectPtr<UCubismParameterComponent>& Parameter : Model->Parameters)
	{
		if (!IsValid(Parameter))
		{
			continue;
		}

		if (const float* SavedValue = ParameterValues.Find(Parameter->Index))
		{
			Parameter->SetParameterValue(*SavedValue);
		}
		else
		{
			ParameterValues.Add(Parameter->Index, Parameter->Value);
		}
	}

	for (const TObjectPtr<UCubismPartComponent>& Part : Model->Parts)
	{
		if (!IsValid(Part))
		{
			continue;
		}

		if (const float* SavedOpacity = PartOpacities.Find(Part->Index))
		{
			Part->SetPartOpacity(*SavedOpacity);
		}
		else
		{
			PartOpacities.Add(Part->Index, Part->Opacity);
		}
	}
}

TObjectPtr<UCubismModelComponent> UCubismParameterStoreComponent::GetModel()
{
	return UCubismModelComponent::FindModelComponent(this);
}

// UObject interface
void UCubismParameterStoreComponent::PostLoad()
{
	Super::PostLoad();

	const TObjectPtr<UCubismModelComponent> ModelComp = GetModel();

	if (ModelComp)
	{
		Setup(ModelComp);
	}
}
// End of UObject interface

// UActorComponent interface
void UCubismParameterStoreComponent::OnComponentCreated()
{
	Super::OnComponentCreated();

	const TObjectPtr<UCubismModelComponent> ModelComp = GetModel();

	if (ModelComp)
	{
		Setup(ModelComp);
	}
}

void UCubismParameterStoreComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	if (IsValid(Model) && Model->ParameterStore == this)
	{
		Model->ParameterStore = nullptr;
	}

	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

#if WITH_EDITOR
void UCubismParameterStoreComponent::PostEditUndo()
{
	Super::PostEditUndo();

	if (const TObjectPtr<UCubismModelComponent> ModelComp = GetModel())
	{
		Setup(ModelComp);
	}
}
#endif

void UCubismParameterStoreComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// When an update controller drives this actor it calls OnCubismUpdate in execution order instead.
	if (IsControlledByUpdateController() && UCubismUpdateControllerComponent::FindController(this))
	{
		return;
	}

	OnCubismUpdate(DeltaTime);
}

int32 UCubismParameterStoreComponent::GetExecutionOrder() const
{
	return CUBISM_EXECUTION_ORDER_PARAMETER_STORE;
}

void UCubismParameterStoreComponent::OnCubismUpdate(float DeltaTime)
{
	if (!IsValid(Model))
	{
		// The model may have been created after this component (e.g. Blueprint construction order).
		if (const TObjectPtr<UCubismModelComponent> ModelComp = GetModel())
		{
			Setup(ModelComp);
		}
	}

	LoadParameters();
}
// End of UActorComponent interface
