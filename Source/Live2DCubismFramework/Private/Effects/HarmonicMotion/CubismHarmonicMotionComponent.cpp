/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */


#include "Effects/HarmonicMotion/CubismHarmonicMotionComponent.h"

#include "CubismUpdateExecutionOrder.h"
#include "CubismUpdateControllerComponent.h"

#include "Effects/HarmonicMotion/CubismHarmonicMotionParameter.h"
#include "Model/CubismModelActor.h"
#include "Model/CubismModelComponent.h"
#include "Model/CubismParameterComponent.h"

UCubismHarmonicMotionComponent::UCubismHarmonicMotionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	bTickInEditor = true;
}

void UCubismHarmonicMotionComponent::Setup(UCubismModelComponent* InModel)
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

	for (FCubismHarmonicMotionParameter& Parameter : Parameters)
	{
		Parameter.ElapsedTime = 0.0f;
	}

	if (Model->HarmonicMotion != this)
	{
		if (IsValid(Model->HarmonicMotion))
		{
			Model->HarmonicMotion->DestroyComponent();
		}
		Model->HarmonicMotion = this;
	}

	Model->AddTickPrerequisiteComponent(this); // model ticks after parameters are updated by components
}

bool UCubismHarmonicMotionComponent::HasValidModel() const
{
	return IsValid(Model) && Model->IsModelReady();
}

TObjectPtr<UCubismModelComponent> UCubismHarmonicMotionComponent::GetModel()
{
	return UCubismModelComponent::FindModelComponent(this);
}

// UObject interface
void UCubismHarmonicMotionComponent::PostLoad()
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
void UCubismHarmonicMotionComponent::OnComponentCreated()
{
	Super::OnComponentCreated();

	const TObjectPtr<UCubismModelComponent> ModelComp = GetModel();

	if (ModelComp)
	{
		Setup(ModelComp);
	}
}

void UCubismHarmonicMotionComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	if (IsValid(Model) && Model->HarmonicMotion == this)
	{
		Model->HarmonicMotion = nullptr;
	}

	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

#if WITH_EDITOR
void UCubismHarmonicMotionComponent::PostEditUndo()
{
	Super::PostEditUndo();

	if (const TObjectPtr<UCubismModelComponent> ModelComp = GetModel())
	{
		Setup(ModelComp);
	}
}
#endif

void UCubismHarmonicMotionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// When an update controller drives this actor it calls OnCubismUpdate in execution order instead.
	if (IsControlledByUpdateController() && UCubismUpdateControllerComponent::FindController(this))
	{
		return;
	}

	OnCubismUpdate(DeltaTime);
}

int32 UCubismHarmonicMotionComponent::GetExecutionOrder() const
{
	return CUBISM_EXECUTION_ORDER_HARMONICMOTION;
}

void UCubismHarmonicMotionComponent::OnCubismUpdate(float DeltaTime)
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

	for (FCubismHarmonicMotionParameter& Parameter : Parameters)
	{
		if (!Parameter.bEnabled)
		{
			continue;
		}

		UCubismParameterComponent* Destination = Model->GetParameter(Parameter.Id);

		if (!Destination)
		{
			continue;
		}

		// Each parameter keeps its own clock so that several parameters do not speed each other up.
		Parameter.ElapsedTime += DeltaTime * Parameter.TimeScale;
		Parameter.Value = Parameter.CalcValue(Parameter.ElapsedTime, Destination->MinimumValue, Destination->MaximumValue);

		switch (Parameter.BlendMode)
		{
			case ECubismParameterBlendMode::Overwrite:
			{
				Destination->SetParameterValue(Parameter.Value);
				break;
			}
			case ECubismParameterBlendMode::Additive:
			{
				Destination->AddParameterValue(Parameter.Value);
				break;
			}
			case ECubismParameterBlendMode::Multiplicative:
			{
				Destination->MultiplyParameterValue(Parameter.Value);
				break;
			}
			default:
			{
				ensure(false);
				break;
			}
		}
	}
}
// End of UActorComponent interface
