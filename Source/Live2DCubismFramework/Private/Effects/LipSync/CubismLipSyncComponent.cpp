/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */


#include "Effects/LipSync/CubismLipSyncComponent.h"

#include "CubismUpdateExecutionOrder.h"
#include "CubismUpdateControllerComponent.h"

#include "Model/CubismModelActor.h"
#include "Model/CubismModelComponent.h"
#include "Model/CubismParameterComponent.h"
#include "Model/CubismModel3Json.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundWave.h"
#include "Sound/SoundBase.h"

const float FrameRate = 30.0f;
const float Epsilon = 0.01f;

UCubismLipSyncComponent::UCubismLipSyncComponent()
	: Time(0.0f)
	, TargetValue(0.0f)
	, CurrentVelocity(0.0f)
	, LipSyncTargetValue(0.0f)
	, LipSyncValue(0.0f)
	, LipSyncVValue(0.0f)
	, LastTimeSeconds(0.0f)
	, UserTimeSeconds(0.0f)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	bTickInEditor = true;
}

void UCubismLipSyncComponent::Setup(UCubismModelComponent* InModel)
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
	TargetValue = 0.0f;
	CurrentVelocity = 0.0f;

	if (Json)
	{
		Ids.Empty();

		Ids.Append(Json->LipSyncs);
	}

	if (!IsValid(Audio))
	{
		Audio = CreateAudioComponent();
	}

	if (Model->LipSync != this)
	{
		if (IsValid(Model->LipSync))
		{
			Model->LipSync->DestroyComponent();
		}
		Model->LipSync = this;
	}

	Model->AddTickPrerequisiteComponent(this); // must update parameters on memory after parameter updated
}

bool UCubismLipSyncComponent::HasValidModel() const
{
	return IsValid(Model) && Model->IsModelReady();
}

UAudioComponent* UCubismLipSyncComponent::GetAudioComponent()
{
	return Audio;
}

void UCubismLipSyncComponent::OnEnvelopeValue(const USoundWave* InSoundWave, const float InEnvelopeValue)
{
	TargetValue = FMath::Clamp(Gain * InEnvelopeValue, 0.0f, 1.0f);
}

void UCubismLipSyncComponent::OnAudioFinished()
{
	// No more envelope values will arrive; close the mouth instead of freezing on the last value.
	TargetValue = 0.0f;
}

void UCubismLipSyncComponent::SetSource(USoundWave* InSource, const bool bPlayImmediately)
{
	Source = InSource;

	if (!IsValid(Audio) && IsValid(Model))
	{
		Audio = CreateAudioComponent();
	}

	if (!IsValid(Audio))
	{
		return;
	}

	Audio->Stop();
	Audio->SetSound(Source);

	TargetValue = 0.0f;

	if (bPlayImmediately && Source)
	{
		Audio->Play();
	}
}

void UCubismLipSyncComponent::Play(const float StartTime)
{
	if (!IsValid(Audio) && IsValid(Model))
	{
		Audio = CreateAudioComponent();
	}

	if (!IsValid(Audio) || !Source)
	{
		return;
	}

	if (Audio->Sound != Source)
	{
		Audio->SetSound(Source);
	}

	TargetValue = 0.0f;

	Audio->Play(StartTime);
}

void UCubismLipSyncComponent::Stop()
{
	if (IsValid(Audio))
	{
		Audio->Stop();
	}

	TargetValue = 0.0f;
}

bool UCubismLipSyncComponent::IsPlaying() const
{
	return IsValid(Audio) && Audio->IsPlaying();
}

TObjectPtr<UAudioComponent> UCubismLipSyncComponent::CreateAudioComponent()
{
	if (!IsValid(Model))
	{
		return nullptr;
	}

	TObjectPtr<UAudioComponent> NewAudio = NewObject<UAudioComponent>(Model, NAME_None, RF_Transactional | RF_Transient);

	NewAudio->OnAudioSingleEnvelopeValue.AddUniqueDynamic(this, &UCubismLipSyncComponent::OnEnvelopeValue);
	NewAudio->OnAudioFinished.AddUniqueDynamic(this, &UCubismLipSyncComponent::OnAudioFinished);

	if (!NewAudio->GetAttachParent() && !NewAudio->IsAttachedTo(Model))
	{
		AActor* Owner = GetOwner();

		if (!Owner || !Owner->GetWorld())
		{
			if (UWorld* World = GetWorld())
			{
				NewAudio->RegisterComponentWithWorld(World);
				NewAudio->AttachToComponent(Model, FAttachmentTransformRules::KeepRelativeTransform);
			}
			else
			{
				NewAudio->SetupAttachment(Model);
			}
		}
		else
		{
			NewAudio->AttachToComponent(Model, FAttachmentTransformRules::KeepRelativeTransform);
			NewAudio->RegisterComponent();
		}
	}

	NewAudio->bAutoActivate = !bAutoEnabled;
	NewAudio->bStopWhenOwnerDestroyed = true;
	NewAudio->bShouldRemainActiveIfDropped = true;
	NewAudio->Mobility = EComponentMobility::Movable;

	#if WITH_EDITORONLY_DATA
	NewAudio->bVisualizeComponent = false;
	#endif

	NewAudio->SetSound(Source);

	return NewAudio;
}

void UCubismLipSyncComponent::ApplyValue()
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

TObjectPtr<UCubismModelComponent> UCubismLipSyncComponent::GetModel()
{
	return UCubismModelComponent::FindModelComponent(this);
}

// UObject interface
void UCubismLipSyncComponent::PostLoad()
{
	Super::PostLoad();

	const TObjectPtr<UCubismModelComponent> ModelComp = GetModel();

	if (ModelComp)
	{
		Setup(ModelComp);
	}
}

#if WITH_EDITOR
void UCubismLipSyncComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.GetPropertyName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UCubismLipSyncComponent, Value))
	{
		ApplyValue();
	}

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UCubismLipSyncComponent, bAutoEnabled))
	{
		Time = 0.0f;

		if (IsValid(Audio))
		{
			Audio->bAutoActivate = !bAutoEnabled;
		}
	}

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UCubismLipSyncComponent, Source))
	{
		SetSource(Source, false);
	}

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UCubismLipSyncComponent, Json))
	{
		if (Json)
		{
			Ids.Empty();
			Ids.Append(Json->LipSyncs);
		}
	}
}
#endif
// End of UObject interface

// UActorComponent interface.
void UCubismLipSyncComponent::OnComponentCreated()
{
	Super::OnComponentCreated();

	const TObjectPtr<UCubismModelComponent> ModelComp = GetModel();

	if (ModelComp)
	{
		Setup(ModelComp);
	}
}

void UCubismLipSyncComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	const AActor* Owner = GetOwner();

	if (IsValid(Audio) && !Audio->IsBeingDestroyed())
	{
		if (Owner && Owner->GetWorld())
		{
			Audio->DetachFromComponent(FDetachmentTransformRules::KeepRelativeTransform);
		}
		Audio->DestroyComponent();
	}
	Audio = nullptr;

	if (IsValid(Model) && Model->LipSync == this)
	{
		Model->LipSync = nullptr;
	}

	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

#if WITH_EDITOR
void UCubismLipSyncComponent::PostEditUndo()
{
	Super::PostEditUndo();

	if (const TObjectPtr<UCubismModelComponent> ModelComp = GetModel())
	{
		Setup(ModelComp);
	}
}
#endif

void UCubismLipSyncComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// When an update controller drives this actor it calls OnCubismUpdate in execution order instead.
	if (IsControlledByUpdateController() && UCubismUpdateControllerComponent::FindController(this))
	{
		return;
	}

	OnCubismUpdate(DeltaTime);
}

int32 UCubismLipSyncComponent::GetExecutionOrder() const
{
	return CUBISM_EXECUTION_ORDER_LIPSYNC;
}

void UCubismLipSyncComponent::OnCubismUpdate(float DeltaTime)
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

	Update(DeltaTime);

	ApplyValue();
}
// End of UActorComponent interface.

void UCubismLipSyncComponent::Update(const float DeltaTime)
{
	if (bAutoEnabled)
	{
		Time += DeltaTime;
		Value = FMath::Abs(FMath::Sin(TimeScale * Time));
	}
	else
	{
		// Envelope values only arrive while the sound plays; when it is stopped externally the mouth must close.
		if (!IsPlaying())
		{
			TargetValue = 0.0f;
		}

		Value = SmoothDamp(Value, DeltaTime);
	}
}

float UCubismLipSyncComponent::SmoothDamp(const float CurrentValue, const float DeltaTime)
{
	// The smoothed value chases the envelope target directly. Subtracting the current value here
	// (as the original code did) made the mouth converge to half of the target amplitude.
	LipSyncTargetValue = TargetValue;

	UserTimeSeconds += DeltaTime;

	const float FaceParamMaxV = 40.0f / 10.0f;
	const float MaxV = FaceParamMaxV * 1.0f / FrameRate;

	if (LastTimeSeconds == 0.0f)
	{
		LastTimeSeconds = UserTimeSeconds;
		return CurrentValue;
	}

	const float DeltaTimeWeight = (UserTimeSeconds - LastTimeSeconds) * FrameRate;
	LastTimeSeconds = UserTimeSeconds;

	const float TimeToMaxSpeed = 0.15f;
	const float FrameToMaxSpeed = TimeToMaxSpeed * FrameRate;     // sec * frame/sec
	const float MaxA = DeltaTimeWeight * MaxV / FrameToMaxSpeed;

	const float DX = LipSyncTargetValue - LipSyncValue;

	if (FMath::Abs(DX) <= Epsilon)
	{
		return CurrentValue;
	}

	const float D = FMath::Sqrt((DX * DX));

	const float VX = MaxV * DX / D;

	float AX = VX - LipSyncVValue;

	const float A = FMath::Sqrt((AX * AX));


	if (A > MaxA && A > 0.0f)
	{
		AX *= MaxA / A;
	}

	LipSyncVValue += AX;

	{
		const float MaxVelocity = 0.5f * (FMath::Sqrt((MaxA * MaxA) + 16.0f * MaxA * D - 8.0f * MaxA * D) - MaxA);
		const float CurVelocity = FMath::Sqrt(LipSyncVValue * LipSyncVValue);

		if (CurVelocity > MaxVelocity && CurVelocity > 0.0f)
		{
			LipSyncVValue *= MaxVelocity / CurVelocity;
		}
	}

	LipSyncValue += LipSyncVValue;

	return FMath::Clamp(LipSyncValue, 0.0f, 1.0f);
}
