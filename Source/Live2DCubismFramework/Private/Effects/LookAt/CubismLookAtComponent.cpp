/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */


#include "Effects/LookAt/CubismLookAtComponent.h"

#include "CubismUpdateExecutionOrder.h"
#include "CubismUpdateControllerComponent.h"

#include "Effects/LookAt/CubismLookAtParameter.h"
#include "Model/CubismModelActor.h"
#include "Model/CubismModelComponent.h"
#include "Model/CubismParameterComponent.h"

UCubismLookAtComponent::UCubismLookAtComponent()
	: LastPosition(FVector::ZeroVector)
	, CurrentVelocity(FVector::ZeroVector)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	bTickInEditor = true;
}

void UCubismLookAtComponent::Setup(UCubismModelComponent* InModel)
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

	LastPosition = FVector::ZeroVector;
	CurrentVelocity = FVector::ZeroVector;

	if (Model->LookAt != this)
	{
		if (IsValid(Model->LookAt))
		{
			Model->LookAt->DestroyComponent();
		}
		Model->LookAt = this;
	}

	Model->AddTickPrerequisiteComponent(this); // model ticks after parameters are updated by components
}

bool UCubismLookAtComponent::HasValidModel() const
{
	return IsValid(Model) && Model->IsModelReady();
}

TObjectPtr<UCubismModelComponent> UCubismLookAtComponent::GetModel()
{
	return UCubismModelComponent::FindModelComponent(this);
}

// UObject interface
void UCubismLookAtComponent::PostLoad()
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
void UCubismLookAtComponent::OnComponentCreated()
{
	Super::OnComponentCreated();

	const TObjectPtr<UCubismModelComponent> ModelComp = GetModel();

	if (ModelComp)
	{
		Setup(ModelComp);
	}
}

void UCubismLookAtComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	if (IsValid(Model) && Model->LookAt == this)
	{
		Model->LookAt = nullptr;
	}

	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

#if WITH_EDITOR
void UCubismLookAtComponent::PostEditUndo()
{
	Super::PostEditUndo();

	if (const TObjectPtr<UCubismModelComponent> ModelComp = GetModel())
	{
		Setup(ModelComp);
	}
}
#endif

void UCubismLookAtComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// When an update controller drives this actor it calls OnCubismUpdate in execution order instead.
	if (IsControlledByUpdateController() && UCubismUpdateControllerComponent::FindController(this))
	{
		return;
	}

	OnCubismUpdate(DeltaTime);
}

int32 UCubismLookAtComponent::GetExecutionOrder() const
{
	return CUBISM_EXECUTION_ORDER_LOOKAT;
}

void UCubismLookAtComponent::OnCubismUpdate(float DeltaTime)
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

	LastPosition = SmoothDamp(LastPosition, DeltaTime);

	for (FCubismLookAtParameter& Parameter : Parameters)
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

		switch (Parameter.Axis)
		{
		case ECubismLookAtAxis::X:
		{
			Parameter.Value = Parameter.Factor * LastPosition.X;
			break;
		}
		case ECubismLookAtAxis::Y:
		{
			Parameter.Value = Parameter.Factor * LastPosition.Y;
			break;
		}
		case ECubismLookAtAxis::Z:
		{
			Parameter.Value = Parameter.Factor * LastPosition.Z;
			break;
		}
		default:
		{
			ensure(false);
			break;
		}
		}

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

FVector UCubismLookAtComponent::SmoothDamp(const FVector CurrentValue, const float DeltaTime)
{
	// global(world) coordinates to local(model) coordinates
	const FTransform Transform = Model->GetComponentTransform();
	FVector TargetValue = Transform.InverseTransformPosition(IsValid(Target) ? Target->GetActorLocation() : Transform.GetLocation());

	const float Scale = 100.0f / Model->GetPixelsPerUnit();
	TargetValue = FVector(-TargetValue.Y, TargetValue.Z, TargetValue.X) * Scale;

	const float Omega = 2.0f / FMath::Max(Smoothing, KINDA_SMALL_NUMBER);
	const float x = Omega * DeltaTime;
	const float Invexp = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);

	const FVector Damp = CurrentValue - TargetValue;

	const FVector Tmp = (CurrentVelocity + Omega * Damp) * DeltaTime;
	CurrentVelocity = (CurrentVelocity - Omega * Tmp) * Invexp;

	FVector NewDamp = (Damp + Tmp) * Invexp;

	if (FVector::DotProduct(Damp, NewDamp) < 0.0f)
	{
		NewDamp = FVector::ZeroVector;
		CurrentVelocity = FVector::ZeroVector;
	}

	return NewDamp + TargetValue;
}
