/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */


#include "Expression/CubismExpressionComponent.h"

#include "Expression/CubismExpression.h"
#include "Model/CubismParameterComponent.h"
#include "Model/CubismPartComponent.h"
#include "Model/CubismModelActor.h"
#include "Model/CubismModelComponent.h"
#include "CubismLog.h"

UCubismExpressionComponent::UCubismExpressionComponent()
	: Time(0.0f)
	, bWasPlaying(false)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_DuringPhysics;
	bTickInEditor = true;
}

void UCubismExpressionComponent::Setup(UCubismModelComponent* InModel)
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

	Time = 0.0f;
	ExpressionQueue.Empty();
	ParameterValues.Empty();
	bWasPlaying = false;

	if (Model->Expression != this)
	{
		if (IsValid(Model->Expression))
		{
			Model->Expression->DestroyComponent();
		}
		Model->Expression = this;
	}

	Model->AddTickPrerequisiteComponent(this); // model ticks after parameters are updated by components
}

bool UCubismExpressionComponent::HasValidModel() const
{
	return IsValid(Model) && Model->IsModelReady();
}

void UCubismExpressionComponent::PlayExpression(const int32 InIndex)
{
	if (!Jsons.IsValidIndex(InIndex))
	{
		UE_LOG(LogCubism, Warning, TEXT("Expression cannot be played. Index %d is out of range."), InIndex);

		return;
	}

	const TObjectPtr<UCubismExp3Json>& Json = Jsons[InIndex];

	if (!Json)
	{
		UE_LOG(LogCubism, Warning, TEXT("Expression cannot be played. The expression asset at index %d is not set."), InIndex);

		return;
	}

	for (const TSharedPtr<FCubismExpression>& Expression : ExpressionQueue)
	{
		if (Expression.IsValid())
		{
			Expression->StartFadeout(Time);
		}
	}

	TSharedPtr<FCubismExpression> NextExpression = MakeShared<FCubismExpression>(Json);

	ExpressionQueue.Add(NextExpression);
}

void UCubismExpressionComponent::StopAllExpressions(const bool bForce)
{
	if (bForce)
	{
		ExpressionQueue.Empty();
		ParameterValues.Empty();
	}
	else
	{
		for (const TSharedPtr<FCubismExpression>& Expression : ExpressionQueue)
		{
			if (Expression.IsValid())
			{
				Expression->StartFadeout(Time);
			}
		}
	}
}

bool UCubismExpressionComponent::IsPlaying() const
{
	return ExpressionQueue.Num() > 0;
}

TObjectPtr<UCubismModelComponent> UCubismExpressionComponent::GetModel()
{
	return UCubismModelComponent::FindModelComponent(this);
}


// UObject interface
void UCubismExpressionComponent::PostLoad()
{
	Super::PostLoad();

	const TObjectPtr<UCubismModelComponent> ModelComp = GetModel();

	if (ModelComp)
	{
		Setup(ModelComp);
	}
}

#if WITH_EDITOR
void UCubismExpressionComponent::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.Property? PropertyChangedEvent.Property->GetFName() : NAME_None;

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UCubismExpressionComponent, Index))
	{
		if (Jsons.IsValidIndex(Index))
		{
			PlayExpression(Index);
		}
		else
		{
			StopAllExpressions();
		}
	}
}
#endif
// End of UObject interface

// UActorComponent interface
void UCubismExpressionComponent::OnComponentCreated()
{
	Super::OnComponentCreated();

	const TObjectPtr<UCubismModelComponent> ModelComp = GetModel();

	if (ModelComp)
	{
		Setup(ModelComp);
	}
}

void UCubismExpressionComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	if (IsValid(Model) && Model->Expression == this)
	{
		Model->Expression = nullptr;
	}

	ExpressionQueue.Empty();

	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void UCubismExpressionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
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

	Time += DeltaTime;

	float ExpressionWeight = 0.0f;

	for (int32 ExpressionIndex = 0; ExpressionIndex < ExpressionQueue.Num(); ExpressionIndex++)
	{
		const TSharedPtr<FCubismExpression>& Expression = ExpressionQueue[ExpressionIndex];

		if (!Expression.IsValid())
		{
			continue;
		}

		UpdateExpression(ExpressionIndex, Expression);

		ExpressionWeight += Expression->CalcExpressionWeight(Time);
	}

	// Once the latest expression is fully faded in, the older ones no longer contribute.
	if (ExpressionQueue.Num() > 1)
	{
		const TSharedPtr<FCubismExpression>& LatestExpression = ExpressionQueue.Last();

		if (LatestExpression.IsValid() && LatestExpression->FadeWeight >= 1.0f)
		{
			for (int32 i = ExpressionQueue.Num()-2; i >= 0; i--)
			{
				ExpressionQueue.RemoveAt(i);
			}
		}
	}

	// Drop the expressions that finished fading out.
	for (int32 i = ExpressionQueue.Num() - 1; i >= 0; i--)
	{
		if (!ExpressionQueue[i].IsValid() || ExpressionQueue[i]->IsFinished(Time))
		{
			ExpressionQueue.RemoveAt(i);
		}
	}

	const bool bIsPlaying = ExpressionQueue.Num() > 0;

	// Only notify on the transition from playing to finished, not every idle frame.
	if (bWasPlaying && !bIsPlaying)
	{
		OnExpressionPlaybackFinished.Broadcast();
	}

	bWasPlaying = bIsPlaying;

	if (!bIsPlaying)
	{
		ParameterValues.Empty();

		return;
	}

	const float Weight = FMath::Min(ExpressionWeight, 1.0f);

	for (FCubismExpressionParameterValue& ParameterValue : ParameterValues)
	{
		UCubismParameterComponent* DstParameter = Model->GetParameter(ParameterValue.Id);

		if (!DstParameter)
		{
			continue;
		}

		const float Value = (ParameterValue.OverwriteValue + ParameterValue.AdditiveValue) * ParameterValue.MultiplyValue;

		DstParameter->SetParameterValue(Value, Weight);

		ParameterValue.AdditiveValue = 0.0f;
		ParameterValue.MultiplyValue = 1.0f;
	}
}
// End of UActorComponent interface

void UCubismExpressionComponent::UpdateExpression(const int32 ExpressionIndex, const TSharedPtr<FCubismExpression>& Expression)
{
	// add new parameters if not exist
	for (int32 i = 0; i < Expression->Parameters.Num(); i++)
	{
		const FCubismExpressionParameter& ExpressionParameter = Expression->Parameters[i];
		const FString Id = ExpressionParameter.Id;

		bool bFound = false;
		for (const FCubismExpressionParameterValue& ParameterValue : ParameterValues)
		{
			if (Id == ParameterValue.Id)
			{
				bFound = true;
				break;
			}
		}

		if (!bFound)
		{
			if (const UCubismParameterComponent* Parameter = Model->GetParameter(Id))
			{
				FCubismExpressionParameterValue ParameterValue = FCubismExpressionParameterValue();

				ParameterValue.Index = i;
				ParameterValue.Id = Id;
				ParameterValue.AdditiveValue = 0.0f;
				ParameterValue.MultiplyValue = 1.0f;
				ParameterValue.OverwriteValue = Parameter->Value;

				ParameterValues.Add(ParameterValue);
			}
		}
	}

	if (Expression->State == ECubismExpressionState::None)
	{
		Expression->Init(Time);
	}

	const float FadeWeight = Expression->UpdateWeight(Time);

	for (FCubismExpressionParameterValue& ParameterValue : ParameterValues)
	{
		const UCubismParameterComponent* DstParameter = Model->GetParameter(ParameterValue.Id);

		if (!DstParameter)
		{
			continue;
		}

		float NewAdditiveValue = 0.0f;
		float NewMultiplyValue = 1.0f;
		float NewOverwriteValue = DstParameter->Value;

		// Find the parameter in this expression; it may not be part of it.
		int32 ParameterIndex = -1;

		for (int32 i = 0; i < Expression->Parameters.Num(); ++i)
		{
			if (ParameterValue.Id != Expression->Parameters[i].Id)
			{
				continue;
			}
			ParameterIndex = i;

			break;
		}

		if (ParameterIndex < 0)
		{
			if (ExpressionIndex == 0)
			{
				ParameterValue.AdditiveValue = 0.0f;

				ParameterValue.MultiplyValue = 1.0f;

				ParameterValue.OverwriteValue = NewOverwriteValue;
			}
			else
			{
				ParameterValue.AdditiveValue =
					CalculateValue(ParameterValue.AdditiveValue, 0.0f, FadeWeight);

				ParameterValue.MultiplyValue =
					CalculateValue(ParameterValue.MultiplyValue, 1.0f, FadeWeight);

				ParameterValue.OverwriteValue =
					CalculateValue(ParameterValue.OverwriteValue, NewOverwriteValue, FadeWeight);
			}
			continue;
		}

		const FCubismExpressionParameter& Parameter = Expression->Parameters[ParameterIndex];

		switch (Parameter.Blend)
		{
			case ECubismParameterBlendMode::Additive:
			{
				NewAdditiveValue = Parameter.Value;
				NewMultiplyValue = 1.0f;
				break;
			}
			case ECubismParameterBlendMode::Multiplicative:
			{
				NewAdditiveValue = 0.0f;
				NewMultiplyValue = Parameter.Value;
				break;
			}
			case ECubismParameterBlendMode::Overwrite:
			{
				NewAdditiveValue = 0.0f;
				NewMultiplyValue = 1.0f;
				NewOverwriteValue = Parameter.Value;
				break;
			}
			default:
			{
				ensure(false);
				break;
			}
		}

		if (ExpressionIndex == 0)
		{
			ParameterValue.AdditiveValue  = NewAdditiveValue;
			ParameterValue.MultiplyValue  = NewMultiplyValue;
			ParameterValue.OverwriteValue = NewOverwriteValue;
		}
		else
		{
			ParameterValue.AdditiveValue = (ParameterValue.AdditiveValue * (1.0f - FadeWeight)) + NewAdditiveValue * FadeWeight;
			ParameterValue.MultiplyValue = (ParameterValue.MultiplyValue * (1.0f - FadeWeight)) + NewMultiplyValue * FadeWeight;
			ParameterValue.OverwriteValue = (ParameterValue.OverwriteValue * (1.0f - FadeWeight)) + NewOverwriteValue * FadeWeight;
		}
	}
}

float UCubismExpressionComponent::CalculateValue(float Source, float Destination, float FadeWeight)
{
	return (Source * (1.0f - FadeWeight)) + (Destination * FadeWeight);
}
