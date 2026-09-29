/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */


#include "Motion/CubismMotionComponent.h"

#include "Motion/CubismMotion3Json.h"
#include "Motion/CubismMotion.h"
#include "Model/CubismModelComponent.h"
#include "Model/CubismParameterComponent.h"
#include "Model/CubismParameterStoreComponent.h"
#include "Model/CubismPartComponent.h"
#include "Model/CubismModelActor.h"
#include "CubismLog.h"

UCubismMotionComponent::UCubismMotionComponent()
	: Time(0.0f)
	, bWasPlaying(false)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	bTickInEditor = true;
}

void UCubismMotionComponent::Setup(UCubismModelComponent* InModel)
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
	MotionQueue.Empty();
	CurrentPriority = ECubismMotionPriority::None;
	bWasPlaying = false;

	if (Model->Motion != this)
	{
		if (IsValid(Model->Motion))
		{
			Model->Motion->DestroyComponent();
		}
		Model->Motion = this;
	}

	if (IsValid(Model->ParameterStore))
	{
		AddTickPrerequisiteComponent(Model->ParameterStore); // must be updated after parameters loaded
	}
}

bool UCubismMotionComponent::HasValidModel() const
{
	return IsValid(Model) && Model->IsModelReady();
}

bool UCubismMotionComponent::IsFinished() const
{
	for (const TSharedPtr<FCubismMotion>& Motion : MotionQueue)
	{
		if (Motion.IsValid() && Motion->State != ECubismMotionState::End)
		{
			return false;
		}
	}

	return true;
}

bool UCubismMotionComponent::ReserveMotion(const ECubismMotionPriority Priority)
{
	if (Priority <= ReservedPriority || Priority <= CurrentPriority)
	{
		return false;
	}

	ReservedPriority = Priority;

	return true;
}

void UCubismMotionComponent::PlayMotion(const int32 InIndex, const float OffsetTime, const ECubismMotionPriority Priority)
{
	if (!Jsons.IsValidIndex(InIndex))
	{
		UE_LOG(LogCubism, Warning, TEXT("Motion cannot be played. Index %d is out of range."), InIndex);

		return;
	}

	const TObjectPtr<UCubismMotion3Json>& Json = Jsons[InIndex];

	if (!Json)
	{
		UE_LOG(LogCubism, Warning, TEXT("Motion cannot be played. The motion asset at index %d is not set."), InIndex);

		return;
	}

	if (Priority == ReservedPriority || Priority == ECubismMotionPriority::Force)
	{
		ReservedPriority = ECubismMotionPriority::None;
	}

	CurrentPriority = Priority;

	for (const TSharedPtr<FCubismMotion>& Motion : MotionQueue)
	{
		if (Motion.IsValid())
		{
			Motion->SetFadeout(Motion->FadeOutTime);
		}
	}

	TSharedPtr<FCubismMotion> NextMotion = MakeShared<FCubismMotion>(Json, OffsetTime);

	MotionQueue.Add(NextMotion);
}

bool UCubismMotionComponent::IsPlaying() const
{
	return MotionQueue.Num() > 0;
}

void UCubismMotionComponent::StopAllMotions(const bool bForce)
{
	if (bForce)
	{
		MotionQueue.Empty();
		CurrentPriority = ECubismMotionPriority::None;
	}
	else
	{
		for (const TSharedPtr<FCubismMotion>& Motion : MotionQueue)
		{
			if (Motion.IsValid())
			{
				Motion->FadeOut(Time);
			}
		}
	}
}

TObjectPtr<UCubismModelComponent> UCubismMotionComponent::GetModel()
{
	return UCubismModelComponent::FindModelComponent(this);
}

// UObject interface
void UCubismMotionComponent::PostLoad()
{
	Super::PostLoad();

	const TObjectPtr<UCubismModelComponent> ModelComp = GetModel();

	if (ModelComp)
	{
		Setup(ModelComp);
	}
}

#if WITH_EDITOR
void UCubismMotionComponent::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UCubismMotionComponent, Index))
	{
		if (Jsons.IsValidIndex(Index))
		{
			PlayMotion(Index, 0.0f, ECubismMotionPriority::Force);
		}
		else
		{
			StopAllMotions();
		}
	}
}
#endif
// End of UObject interface

// UActorComponent interface
void UCubismMotionComponent::OnComponentCreated()
{
	Super::OnComponentCreated();

	const TObjectPtr<UCubismModelComponent> ModelComp = GetModel();

	if (ModelComp)
	{
		Setup(ModelComp);
	}
}

void UCubismMotionComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!IsValid(Model))
	{
		if (const TObjectPtr<UCubismModelComponent> ModelComp = GetModel())
		{
			Setup(ModelComp);
		}
	}

	// Motions played in the editor are not carried into the game, start the configured one.
	if (bAutoPlay && MotionQueue.Num() == 0 && Jsons.Num() > 0)
	{
		const int32 PlayIndex = Jsons.IsValidIndex(Index) ? Index : 0;

		if (Jsons[PlayIndex])
		{
			PlayMotion(PlayIndex, 0.0f, ECubismMotionPriority::Idle);
		}
	}
}

void UCubismMotionComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	if (IsValid(Model) && Model->Motion == this)
	{
		Model->Motion = nullptr;
	}

	MotionQueue.Empty();

	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void UCubismMotionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
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

	Time += Speed * DeltaTime;

	for (int32 i = 0; i < MotionQueue.Num();)
	{
		TSharedPtr<FCubismMotion> Motion = MotionQueue[i];

		if (!Motion.IsValid())
		{
			MotionQueue.RemoveAt(i);
			continue;
		}

		if (Motion->State == ECubismMotionState::None)
		{
			// Initialize the motion.
			Motion->Init(Time);
		}

		const float FadeWeight = Motion->UpdateFadeWeight(Motion, Time);

		UpdateMotion(Time, FadeWeight, Motion);

		if (Motion->IsFinished())
		{
			MotionQueue.RemoveAt(i);
		}
		else
		{
			if (Motion->IsTriggeredFadeOut())
			{
				Motion->StartFadeout(Motion->GetFadeOutSeconds(), Time);
			}

			i++;
		}
	}

	const bool bIsPlaying = MotionQueue.Num() > 0;

	if (!bIsPlaying)
	{
		CurrentPriority = ECubismMotionPriority::None;
	}

	// Only notify on the transition from playing to finished, not every idle frame.
	if (bWasPlaying && !bIsPlaying)
	{
		OnMotionPlaybackFinished.Broadcast();
	}

	bWasPlaying = bIsPlaying;
}
// End of UActorComponent interface

void UCubismMotionComponent::UpdateMotion(float UserTimeSeconds, float FadeWeight, const TSharedPtr<FCubismMotion>& CubismMotion)
{
	float TimeOffsetSeconds = UserTimeSeconds - CubismMotion->StartTime;

	if (TimeOffsetSeconds < 0.0f)
	{
		TimeOffsetSeconds = 0.0f;
	}

	// The end time is absolute (in component time) and negative while no fade-out has been requested.
	const float MotionEndTime = CubismMotion->GetEndTime();

	const float TmpFadeIn = (CubismMotion->FadeInTime <= 0.0f)
		? 1.0f
		: FCubismMotion::EasingSin((UserTimeSeconds - CubismMotion->StartTime) / CubismMotion->FadeInTime);

	const float TmpFadeOut = (CubismMotion->FadeOutTime <= 0.0f || MotionEndTime < 0.0f)
		? 1.0f
		: FCubismMotion::EasingSin((MotionEndTime - UserTimeSeconds) / CubismMotion->FadeOutTime);

	// 'Repeat' time as necessary.
	float MotionTime = TimeOffsetSeconds;

	if (CubismMotion->State == ECubismMotionState::PlayInLoop && CubismMotion->Duration > 0.0f)
	{
		while (MotionTime > CubismMotion->Duration)
		{
			MotionTime -= CubismMotion->Duration;
		}
	}

	const TArray<FCubismMotionCurve>& Curves = CubismMotion->Curves;

	// Evaluate model curves.
	for (const FCubismMotionCurve& Curve : Curves)
	{
		if (Curve.Target != ECubismMotionCurveTarget::Model)
		{
			continue;
		}

		// Evaluate curve and call handler.
		const float Value = CubismMotion->GetValue(Curve.Id, MotionTime);

		if (Curve.Id == "Opacity")
		{
			Model->Opacity = Value;
		}
	}

	for (const FCubismMotionCurve& Curve : Curves)
	{
		if (Curve.Target != ECubismMotionCurveTarget::Parameter)
		{
			continue;
		}

		// Find parameter.
		UCubismParameterComponent* Parameter = Model->GetParameter(Curve.Id);

		// Skip curve evaluation if no value in sink.
		if (!Parameter)
		{
			continue;
		}

		const float SourceValue = Parameter->Value;

		// Evaluate curve and apply value.
		const float Value = CubismMotion->GetValue(Curve.Id, MotionTime);

		float NewValue;
		// Fade per parameter.
		if (Curve.FadeInTime < 0.0f && Curve.FadeOutTime < 0.0f)
		{
			//Apply motion fade.
			NewValue = SourceValue + (Value - SourceValue) * FadeWeight;
		}
		else
		{
			// If fade-in or fade-out is set for a parameter, apply it.
			float FadeInWeight;
			float FadeOutWeight;

			if (Curve.FadeInTime < 0.0f)
			{
				FadeInWeight = TmpFadeIn;
			}
			else
			{
				FadeInWeight = Curve.FadeInTime == 0.0f
					? 1.0f
					: FCubismMotion::EasingSin((UserTimeSeconds - CubismMotion->StartTime) / Curve.FadeInTime);
			}

			if (Curve.FadeOutTime < 0.0f)
			{
				FadeOutWeight = TmpFadeOut;
			}
			else
			{
				FadeOutWeight = (Curve.FadeOutTime == 0.0f || MotionEndTime < 0.0f)
					? 1.0f
					: FCubismMotion::EasingSin((MotionEndTime - UserTimeSeconds) / Curve.FadeOutTime);
			}

			const float ParamWeight = CubismMotion->GetWeight() * FadeInWeight * FadeOutWeight;

			// Apply fade per parameter.
			NewValue = SourceValue + (Value - SourceValue) * ParamWeight;
		}

		Parameter->SetParameterValue(NewValue);
	}

	for (const FCubismMotionCurve& Curve : Curves)
	{
		if (Curve.Target != ECubismMotionCurveTarget::PartOpacity)
		{
			continue;
		}

		// Part opacities are driven through parameters of the same ID (mirrors the native framework and the pose component).
		UCubismParameterComponent* Parameter = Model->GetParameter(Curve.Id);

		// Skip curve evaluation if no value in sink.
		if (!Parameter)
		{
			continue;
		}

		// Evaluate curve and apply value.
		const float Value = CubismMotion->GetValue(Curve.Id, MotionTime);

		Parameter->SetParameterValue(Value);
	}

	if ((MotionEndTime >= 0.0f) && (MotionEndTime < UserTimeSeconds))
	{
		CubismMotion->IsFinished(true);
	}

	if (UserTimeSeconds - CubismMotion->StartTime > CubismMotion->Duration)
	{
		if (CubismMotion->State == ECubismMotionState::PlayInLoop)
		{
			CubismMotion->StartTime = UserTimeSeconds;
		}
		else
		{
			CubismMotion->IsFinished(true);
		}
	}
}
