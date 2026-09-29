/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */


#include "Effects/EyeBlink/CubismEyeBlinkComponent.h"

#include "CubismUpdateExecutionOrder.h"
#include "CubismUpdateControllerComponent.h"

#include "Model/CubismModelActor.h"
#include "Model/CubismModelComponent.h"
#include "Model/CubismModel3Json.h"
#include "Model/CubismParameterComponent.h"

UCubismEyeBlinkComponent::UCubismEyeBlinkComponent()
	: Phase(ECubismEyeBlinkPhase::Idle)
	, Time(0.0f)
	, StartTime(0.0f)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	bTickInEditor = true;
}

void UCubismEyeBlinkComponent::Setup(UCubismModelComponent* InModel)
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

	Phase = ECubismEyeBlinkPhase::Idle;
	Time = 0.0f;
	StartTime = Mean + FMath::FRandRange(-MaximumDeviation, MaximumDeviation);

	if (Json)
	{
		Ids.Empty();

		Ids.Append(Json->EyeBlinks);
	}

	if (Model->EyeBlink != this)
	{
		if (IsValid(Model->EyeBlink))
		{
			Model->EyeBlink->DestroyComponent();
		}
		Model->EyeBlink = this;
	}

	Model->AddTickPrerequisiteComponent(this); // model ticks after parameters are updated by components
}

bool UCubismEyeBlinkComponent::HasValidModel() const
{
	return IsValid(Model) && Model->IsModelReady();
}

void UCubismEyeBlinkComponent::ApplyValue()
{
	if (!HasValidModel())
	{
		return;
	}

	for (const FString& Id : Ids)
	{
		UCubismParameterComponent* Destination = Model->GetParameter(Id);

		if (!Destination)
		{
			continue;
		}

		switch (BlendMode)
		{
			case ECubismParameterBlendMode::Overwrite:
			{
				Destination->SetParameterValue(Value);
				break;
			}
			case ECubismParameterBlendMode::Additive:
			{
				Destination->AddParameterValue(Value);
				break;
			}
			case ECubismParameterBlendMode::Multiplicative:
			{
				Destination->MultiplyParameterValue(Value);
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

TObjectPtr<UCubismModelComponent> UCubismEyeBlinkComponent::GetModel()
{
	return UCubismModelComponent::FindModelComponent(this);
}

// UObject interface
void UCubismEyeBlinkComponent::PostLoad()
{
	Super::PostLoad();

	const TObjectPtr<UCubismModelComponent> ModelComp = GetModel();

	if (ModelComp)
	{
		Setup(ModelComp);
	}
}

#if WITH_EDITOR
void UCubismEyeBlinkComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.GetPropertyName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UCubismEyeBlinkComponent, Value))
	{
		ApplyValue();
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UCubismEyeBlinkComponent, bAutoEnabled))
	{
		Time = 0.0f;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UCubismEyeBlinkComponent, Json))
	{
		if (Json)
		{
			Ids.Empty();
			Ids.Append(Json->EyeBlinks);
		}
	}
}
#endif
// End of UObject interface

// UActorComponent interface
void UCubismEyeBlinkComponent::OnComponentCreated()
{
	Super::OnComponentCreated();

	const TObjectPtr<UCubismModelComponent> ModelComp = GetModel();

	if (ModelComp)
	{
		Setup(ModelComp);
	}
}

void UCubismEyeBlinkComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	if (IsValid(Model) && Model->EyeBlink == this)
	{
		Model->EyeBlink = nullptr;
	}

	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

#if WITH_EDITOR
void UCubismEyeBlinkComponent::PostEditUndo()
{
	Super::PostEditUndo();

	if (const TObjectPtr<UCubismModelComponent> ModelComp = GetModel())
	{
		Setup(ModelComp);
	}
}
#endif

void UCubismEyeBlinkComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// When an update controller drives this actor it calls OnCubismUpdate in execution order instead.
	if (IsControlledByUpdateController() && UCubismUpdateControllerComponent::FindController(this))
	{
		return;
	}

	OnCubismUpdate(DeltaTime);
}

int32 UCubismEyeBlinkComponent::GetExecutionOrder() const
{
	return CUBISM_EXECUTION_ORDER_EYEBLINK;
}

void UCubismEyeBlinkComponent::OnCubismUpdate(float DeltaTime)
{
#if WITH_EDITOR
	if (GetWorld() && GetWorld()->WorldType == EWorldType::Editor && !bEnableEyeBlinkInEditor)
	{
		return;
	}
#endif

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

	Update(DeltaTime);

	ApplyValue();
}
// End of UActorComponent interface

void UCubismEyeBlinkComponent::Update(const float DeltaTime)
{
	if (!bAutoEnabled)
	{
		return;
	}

	Time += DeltaTime;

	const float ElapsedTime = TimeScale * (Time - StartTime);

	float NewValue = 0.0f;

	switch (Phase)
	{
		case ECubismEyeBlinkPhase::Idle:
		{
			if (Time >= StartTime)
			{
				Phase = ECubismEyeBlinkPhase::Closing;
				StartTime = Time;
			}

			NewValue = 1.0f;

			break;
		}
		case ECubismEyeBlinkPhase::Closing:
		{
			float t = ClosingPeriod > 0.0f ? ElapsedTime / ClosingPeriod : 1.0f;

			if (t >= 1.0f)
			{
				t = 1.0f;
				Phase = ECubismEyeBlinkPhase::Closed;
				StartTime = Time;
			}

			NewValue = 1.0f - t;

			break;
		}
		case ECubismEyeBlinkPhase::Closed:
		{
			float t = ClosedPeriod > 0.0f ? ElapsedTime / ClosedPeriod : 1.0f;

			if (t >= 1.0f)
			{
				t = 1.0f;
				Phase = ECubismEyeBlinkPhase::Opening;
				StartTime = Time;
			}

			NewValue = 0.0f;

			break;
		}
		case ECubismEyeBlinkPhase::Opening:
		{
			float t = OpeningPeriod > 0.0f ? ElapsedTime / OpeningPeriod : 1.0f;

			if (t >= 1.0f)
			{
				t = 1.0f;
				Phase = ECubismEyeBlinkPhase::Idle;
				Time = 0.0f;
				StartTime = Mean + FMath::FRandRange(-MaximumDeviation, MaximumDeviation);
			}

			NewValue = t;

			break;
		}
		default:
		{
			ensure(false);
			break;
		}
	}

	Value = NewValue;
}
