/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */


#include "Motion/CubismMotionComponent.h"

#include "CubismUpdateExecutionOrder.h"
#include "CubismUpdateControllerComponent.h"

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

	UCubismUpdateControllerComponent::RequestRefresh(this);

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

bool UCubismMotionComponent::PlayMotion(const int32 InIndex, const float OffsetTime, const ECubismMotionPriority Priority)
{
	if (!Jsons.IsValidIndex(InIndex))
	{
		UE_LOG(LogCubism, Warning, TEXT("Motion cannot be played. Index %d is out of range."), InIndex);

		return false;
	}

	const TObjectPtr<UCubismMotion3Json>& Json = Jsons[InIndex];

	if (!Json)
	{
		UE_LOG(LogCubism, Warning, TEXT("Motion cannot be played. The motion asset at index %d is not set."), InIndex);

		return false;
	}

	if (Priority != ECubismMotionPriority::Force)
	{
		// A request of lower priority than the playing (or reserved) motion is ignored: the idle never interrupts a
		// gesture. Equal priority replaces it, so a new Normal motion cuts a running Normal one (the native framework
		// would reject that too, which makes sample UIs and dialogue scripts feel unresponsive).
		const bool bReservedForThis = ReservedPriority != ECubismMotionPriority::None && Priority == ReservedPriority;

		if (!bReservedForThis && ((IsPlaying() && Priority < CurrentPriority) || Priority < ReservedPriority))
		{
			UE_LOG(LogCubism, Verbose, TEXT("Motion %d ignored: priority %d does not exceed the current (%d) or reserved (%d) priority."), InIndex, (int32)Priority, (int32)CurrentPriority, (int32)ReservedPriority);

			return false;
		}
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

	return true;
}

int32 UCubismMotionComponent::FindMotionIndex(const FString& Name) const
{
	if (Name.IsEmpty())
	{
		return -1;
	}

	FString Wanted = Name;
	Wanted.RemoveFromEnd(TEXT(".motion3.json"), ESearchCase::IgnoreCase);
	Wanted.RemoveFromEnd(TEXT("_motion3"), ESearchCase::IgnoreCase);
	Wanted.ReplaceInline(TEXT(" "), TEXT("_"));
	Wanted.ReplaceInline(TEXT("."), TEXT("_"));

	for (int32 MotionIndex = 0; MotionIndex < Jsons.Num(); MotionIndex++)
	{
		if (!Jsons[MotionIndex])
		{
			continue;
		}

		FString AssetName = Jsons[MotionIndex]->GetName();
		AssetName.RemoveFromEnd(TEXT("_motion3"), ESearchCase::IgnoreCase);

		if (AssetName.Equals(Wanted, ESearchCase::IgnoreCase))
		{
			return MotionIndex;
		}
	}

	return -1;
}

bool UCubismMotionComponent::PlayMotionByName(const FString& Name, const float OffsetTime, const ECubismMotionPriority Priority)
{
	const int32 MotionIndex = FindMotionIndex(Name);

	if (MotionIndex < 0)
	{
		UE_LOG(LogCubism, Warning, TEXT("Motion '%s' was not found on '%s'."), *Name, *GetName());

		return false;
	}

	return PlayMotion(MotionIndex, OffsetTime, Priority);
}

void UCubismMotionComponent::PlayIdleMotion()
{
	if (Jsons.Num() == 0)
	{
		return;
	}

	int32 PlayIndex = Jsons.IsValidIndex(IdleIndex) ? IdleIndex : (Jsons.IsValidIndex(Index) ? Index : 0);

	if (Jsons[PlayIndex])
	{
		PlayMotion(PlayIndex, 0.0f, ECubismMotionPriority::Idle);
	}
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

#if WITH_EDITOR
void UCubismMotionComponent::PostEditUndo()
{
	Super::PostEditUndo();

	if (const TObjectPtr<UCubismModelComponent> ModelComp = GetModel())
	{
		Setup(ModelComp);
	}
}
#endif

void UCubismMotionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// When an update controller drives this actor it calls OnCubismUpdate in execution order instead.
	if (IsControlledByUpdateController() && UCubismUpdateControllerComponent::FindController(this))
	{
		return;
	}

	OnCubismUpdate(DeltaTime);
}

int32 UCubismMotionComponent::GetExecutionOrder() const
{
	return CUBISM_EXECUTION_ORDER_MOTION;
}

void UCubismMotionComponent::OnCubismUpdate(float DeltaTime)
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
			Motion->FadeInAnchorTime = Motion->StartTime;
		}

		float Elapsed = Time - Motion->StartTime;
		if (Elapsed < 0.0f)
		{
			Elapsed = 0.0f;
		}

		float Phase = Elapsed;
		if (Motion->State == ECubismMotionState::PlayInLoop && Motion->Duration > 0.0f)
		{
			Phase = FMath::Fmod(Elapsed, Motion->Duration);
			if (Phase < 0.0f)
			{
				Phase += Motion->Duration;
			}
		}

		// A loop seam is detected when the phase wraps around; the fade-in is restarted from there when requested.
		const bool bLoopSeam =
			(Motion->State == ECubismMotionState::PlayInLoop) &&
			(Motion->Duration > 0.0f) &&
			(Phase + KINDA_SMALL_NUMBER < Motion->PrevPhaseTime);

		if (bLoopSeam && Motion->bLoopFadeIn)
		{
			Motion->FadeInAnchorTime = Time - Phase;
		}

		UpdateMotion(Time, Motion);
		Motion->PrevPhaseTime = Phase;

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
	const ECubismMotionPriority FinishedPriority = CurrentPriority;

	if (!bIsPlaying)
	{
		CurrentPriority = ECubismMotionPriority::None;
	}

	// Only notify on the transition from playing to finished, not every idle frame.
	// With bReturnToIdle the idle cycles are not reported, so the event means "the requested motion ended".
	if (bWasPlaying && !bIsPlaying && !(bReturnToIdle && FinishedPriority == ECubismMotionPriority::Idle))
	{
		OnMotionPlaybackFinished.Broadcast();
	}

	bWasPlaying = bIsPlaying;

	// Resume the idle motion once nothing is queued anymore (only while the game runs, not in the editor viewport).
	if (!bIsPlaying && bReturnToIdle && HasBegunPlay())
	{
		PlayIdleMotion();

		bWasPlaying = MotionQueue.Num() > 0;
	}
}
// End of UActorComponent interface

void UCubismMotionComponent::UpdateMotion(float UserTimeSeconds, const TSharedPtr<FCubismMotion>& CubismMotion)
{
	if (!CubismMotion.IsValid() || !HasValidModel())
	{
		return;
	}

	float Elapsed = UserTimeSeconds - CubismMotion->StartTime;
	if (Elapsed < 0.0f)
	{
		Elapsed = 0.0f;
	}

	// 'Repeat' time as necessary.
	float MotionTime = Elapsed;
	if (CubismMotion->State == ECubismMotionState::PlayInLoop && CubismMotion->Duration > 0.0f)
	{
		MotionTime = FMath::Fmod(Elapsed, CubismMotion->Duration);
		if (MotionTime < 0.0f)
		{
			MotionTime += CubismMotion->Duration;
		}
	}

	// The absolute time (in component time) at which the current cycle ends, or -1 when it plays indefinitely.
	auto EndSecForThisCycle = [&]() -> float
	{
		if (CubismMotion->GetEndTime() >= 0.0f)
		{
			return CubismMotion->GetEndTime();
		}

		if (CubismMotion->State == ECubismMotionState::PlayInLoop && CubismMotion->Duration > 0.0f)
		{
			return CubismMotion->FadeInAnchorTime + CubismMotion->Duration;
		}

		return -1.0f;
	};

	// Blend the last frame of a loop into its first frame so that the seam does not pop.
	float TailT = -1.0f;
	if (CubismMotion->State == ECubismMotionState::PlayInLoop && CubismMotion->Duration > 0.0f && CubismMotion->Fps > 0.0f)
	{
		const float Delta = 1.0f / CubismMotion->Fps;
		if (MotionTime > CubismMotion->Duration - Delta)
		{
			TailT = (MotionTime - (CubismMotion->Duration - Delta)) / Delta;
			TailT = FMath::Clamp(TailT, 0.0f, 1.0f);
		}
	}

	const float MotionWeight = FMath::Clamp(CubismMotion->GetWeight(), 0.0f, 1.0f);
	const float TmpFadeIn = (CubismMotion->FadeInTime <= 0.0f)
		? 1.0f
		: FCubismMotion::EasingSin((UserTimeSeconds - CubismMotion->FadeInAnchorTime) / CubismMotion->FadeInTime);

	const float EndSecMotion = EndSecForThisCycle();
	const float TmpFadeOut = (CubismMotion->FadeOutTime <= 0.0f || EndSecMotion < 0.0f)
		? 1.0f
		: FCubismMotion::EasingSin((EndSecMotion - UserTimeSeconds) / CubismMotion->FadeOutTime);

	const float MotionFadeWeight = FMath::Clamp(MotionWeight * TmpFadeIn * TmpFadeOut, 0.0f, 1.0f);

	const TArray<FCubismMotionCurve>& Curves = CubismMotion->Curves;

	// Evaluate model curves.
	for (const FCubismMotionCurve& Curve : Curves)
	{
		if (Curve.Target != ECubismMotionCurveTarget::Model)
		{
			continue;
		}

		float Value = CubismMotion->GetValue(Curve.Id, MotionTime);
		if (TailT >= 0.0f)
		{
			const float Start = CubismMotion->GetValue(Curve.Id, 0.0f);
			Value = FMath::Lerp(Value, Start, TailT);
		}

		if (Curve.Id == "Opacity")
		{
			Model->Opacity = Value;
		}
	}

	// Evaluate parameter curves.
	for (const FCubismMotionCurve& Curve : Curves)
	{
		if (Curve.Target != ECubismMotionCurveTarget::Parameter)
		{
			continue;
		}

		UCubismParameterComponent* Parameter = Model->GetParameter(Curve.Id);

		// Skip curve evaluation if no value in sink.
		if (!Parameter)
		{
			continue;
		}

		const float SourceValue = Parameter->Value;

		float TargetNow = CubismMotion->GetValue(Curve.Id, MotionTime);
		if (Parameter->IsRepeat())
		{
			TargetNow = Parameter->GetParameterRepeatValue(TargetNow);
		}

		float ParamWeight = MotionFadeWeight;

		// If fade-in or fade-out is set for a parameter, apply it.
		if (Curve.FadeInTime >= 0.0f || Curve.FadeOutTime >= 0.0f)
		{
			float FadeInWeight = TmpFadeIn;
			float FadeOutWeight = TmpFadeOut;

			if (Curve.FadeInTime >= 0.0f)
			{
				FadeInWeight = (Curve.FadeInTime == 0.0f)
					? 1.0f
					: FCubismMotion::EasingSin((UserTimeSeconds - CubismMotion->FadeInAnchorTime) / Curve.FadeInTime);
			}

			if (Curve.FadeOutTime >= 0.0f)
			{
				FadeOutWeight = (Curve.FadeOutTime == 0.0f || EndSecMotion < 0.0f)
					? 1.0f
					: FCubismMotion::EasingSin((EndSecMotion - UserTimeSeconds) / Curve.FadeOutTime);
			}

			ParamWeight = FMath::Clamp(MotionWeight * FadeInWeight * FadeOutWeight, 0.0f, 1.0f);
		}

		float NewValue = SourceValue + (TargetNow - SourceValue) * ParamWeight;

		if (TailT >= 0.0f)
		{
			float TargetStart = CubismMotion->GetValue(Curve.Id, 0.0f);
			if (Parameter->IsRepeat())
			{
				TargetStart = Parameter->GetParameterRepeatValue(TargetStart);
			}
			const float NewValueStart = SourceValue + (TargetStart - SourceValue) * ParamWeight;
			NewValue = FMath::Lerp(NewValue, NewValueStart, TailT);
		}

		Parameter->SetParameterValue(NewValue);
	}

	// Evaluate part opacity curves.
	for (const FCubismMotionCurve& Curve : Curves)
	{
		if (Curve.Target != ECubismMotionCurveTarget::PartOpacity)
		{
			continue;
		}

		// Part opacities are driven through parameters of the same ID (mirrors the native framework and the pose component).
		UCubismParameterComponent* Parameter = Model->GetParameter(Curve.Id);

		if (!Parameter)
		{
			continue;
		}

		float Value = CubismMotion->GetValue(Curve.Id, MotionTime);
		if (TailT >= 0.0f)
		{
			const float Start = CubismMotion->GetValue(Curve.Id, 0.0f);
			Value = FMath::Lerp(Value, Start, TailT);
		}

		Parameter->SetParameterValue(Value);
	}

	// The end time is absolute (in component time) and negative while no fade-out has been requested.
	const float MotionEndTime = CubismMotion->GetEndTime();

	if ((MotionEndTime >= 0.0f) && (MotionEndTime < UserTimeSeconds))
	{
		CubismMotion->IsFinished(true);
	}

	if (CubismMotion->State != ECubismMotionState::PlayInLoop && Elapsed > CubismMotion->Duration)
	{
		CubismMotion->IsFinished(true);
	}
}
