/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */


#include "Physics/CubismPhysicsComponent.h"

#include "CubismUpdateExecutionOrder.h"
#include "CubismUpdateControllerComponent.h"

#include "Model/CubismParameterComponent.h"
#include "Model/CubismModelActor.h"
#include "Model/CubismModelComponent.h"
#include "Physics/CubismPhysicsRig.h"
#include "Physics/CubismPhysics3Json.h"
#include "CubismMath.h"
#include "CubismLog.h"
#include "Live2DCubismCore.h"

const float AirResistance = 5.0f;
const float MaximumWeight = 100.0f;
const float MovementThreshold = 0.001f;
const float MaxDeltaTime = 5.0f;

UCubismPhysicsComponent::UCubismPhysicsComponent()
	: CurrentRemainTime(0.0f)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	bTickInEditor = true;
}

void UCubismPhysicsComponent::Setup(UCubismModelComponent* InModel)
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

	CurrentRemainTime = 0.0f;

	Rigs.Empty();

	if (Json)
	{
		Gravity = Json->Gravity;
		Wind = Json->Wind;
		Fps = Json->Fps;

		for (const FCubismPhysicsSetting& Setting : Json->PhysicsSettings)
		{
			FCubismPhysicsRig Rig;

			Rig.NormalizationPosition = Setting.NormalizationPosition;
			Rig.NormalizationAngle = Setting.NormalizationAngle;

			for (const FCubismPhysicsInput& Input : Setting.Inputs)
			{
				FCubismPhysicsRigInput RigInput;

				RigInput.ParameterIndex = Model->GetParameterIndex(Input.Source.Id);
				RigInput.Parameter = Model->GetParameter(RigInput.ParameterIndex);
				RigInput.Weight = Input.Weight / MaximumWeight;
				RigInput.bReflect = Input.bReflect;
				RigInput.Type = Input.Type;
				RigInput.Source = Input.Source;

				if (RigInput.ParameterIndex < 0 || !RigInput.Parameter.IsValid())
				{
					UE_LOG(LogCubism, Warning, TEXT("UCubismPhysicsComponent::Setup: input parameter '%s' was not found."), *Input.Source.Id);

					continue;
				}

				Rig.Inputs.Add(RigInput);
			}

			for (const FCubismPhysicsOutput& Output : Setting.Outputs)
			{
				FCubismPhysicsRigOutput RigOutput;

				RigOutput.ParameterIndex = Model->GetParameterIndex(Output.Destination.Id);
				RigOutput.Parameter = Model->GetParameter(RigOutput.ParameterIndex);
				RigOutput.ParticleIndex = Output.VertexIndex;
				RigOutput.AngleScale = Output.AngleScale;
				RigOutput.Weight = Output.Weight / MaximumWeight;
				RigOutput.bReflect = Output.bReflect;
				RigOutput.Type = Output.Type;
				RigOutput.Destination = Output.Destination;
				RigOutput.TranslationScale = FVector2D::ZeroVector;
				RigOutput.ValueBelowMinimum = 0.0f;
				RigOutput.ValueExceededMaximum = 0.0f;
				RigOutput.PreviousValue = 0.0f;
				RigOutput.CurrentValue = 0.0f;

				if (RigOutput.ParameterIndex < 0 || !RigOutput.Parameter.IsValid())
				{
					UE_LOG(LogCubism, Warning, TEXT("UCubismPhysicsComponent::Setup: output parameter '%s' was not found."), *Output.Destination.Id);

					continue;
				}

				Rig.Outputs.Add(RigOutput);
			}

			for (const FCubismPhysicsParticle& Particle : Setting.Particles)
			{
				FCubismPhysicsRigParticle RigParticle;

				RigParticle.Mobility = Particle.Mobility;
				RigParticle.Delay = Particle.Delay;
				RigParticle.Acceleration = Particle.Acceleration;
				RigParticle.Radius = Particle.Radius;
				RigParticle.Position = Particle.Position;
				RigParticle.InitialPosition = FVector2D::ZeroVector;
				RigParticle.LastPosition = FVector2D::ZeroVector;
				RigParticle.LastGravity = FVector2D(0.0f, 1.0f);
				RigParticle.Force = FVector2D::ZeroVector;
				RigParticle.Velocity = FVector2D::ZeroVector;

				Rig.Particles.Add(RigParticle);
			}

			// A rig needs at least the root particle and one moving particle to produce output.
			if (Rig.Particles.Num() < 2)
			{
				continue;
			}

			// Outputs referencing particles that do not exist would read out of bounds.
			for (int32 OutputIndex = Rig.Outputs.Num() - 1; OutputIndex >= 0; --OutputIndex)
			{
				const int32 ParticleIndex = Rig.Outputs[OutputIndex].ParticleIndex;

				if (ParticleIndex < 1 || ParticleIndex >= Rig.Particles.Num())
				{
					UE_LOG(LogCubism, Warning, TEXT("UCubismPhysicsComponent::Setup: output '%s' references particle %d which does not exist."), *Rig.Outputs[OutputIndex].Destination.Id, ParticleIndex);

					Rig.Outputs.RemoveAt(OutputIndex);
				}
			}

			Rigs.Add(Rig);
		}

		Initialize();
	}

	// Resolving the rig parameters above may have registered parameters that are not part of the moc,
	// so the caches are sized after the rigs are built and cover the non-native parameters too.
	{
		const int32 ParameterCount = Model->Parameters.Num();

		ParameterCaches.SetNum(ParameterCount);
		ParameterInputCaches.SetNum(ParameterCount);

		for (int32 ParameterIndex = 0; ParameterIndex < ParameterCount; ++ParameterIndex)
		{
			const UCubismParameterComponent* Parameter = Model->GetParameter(ParameterIndex);
			const float Value = Parameter ? Parameter->Value : 0.0f;

			ParameterCaches[ParameterIndex] = Value;
			ParameterInputCaches[ParameterIndex] = Value;
		}
	}

	if (Model->Physics != this)
	{
		if (IsValid(Model->Physics))
		{
			Model->Physics->DestroyComponent();
		}
		Model->Physics = this;
	}

	Model->AddTickPrerequisiteComponent(this); // model ticks after parameters are updated by components
}

bool UCubismPhysicsComponent::HasValidModel() const
{
	return IsValid(Model) && Model->IsModelReady();
}

void UCubismPhysicsComponent::Initialize()
{
	for (FCubismPhysicsRig& Rig : Rigs)
	{
		for (int32 ParticleIndex = 0; ParticleIndex < Rig.Particles.Num(); ++ParticleIndex)
		{
			FCubismPhysicsRigParticle& Particle = Rig.Particles[ParticleIndex];

			Particle.InitialPosition.X = ParticleIndex == 0? 0.0f : Rig.Particles[ParticleIndex-1].InitialPosition.X;
			Particle.InitialPosition.Y = ParticleIndex == 0? 0.0f : Rig.Particles[ParticleIndex-1].InitialPosition.Y + Particle.Radius;

			Particle.Position = Particle.InitialPosition;
			Particle.LastPosition = Particle.InitialPosition;
			Particle.LastGravity = FVector2D(0.0f, 1.0f);
			Particle.Velocity = FVector2D::ZeroVector;
			Particle.Force = FVector2D::ZeroVector;
		}
	}
}

void UCubismPhysicsComponent::Stabilization()
{
	if (!HasValidModel())
	{
		return;
	}

	for (FCubismPhysicsRig& Rig : Rigs)
	{
		if (Rig.Particles.Num() == 0)
		{
			continue;
		}

		FVector2D TotalTranslation = FVector2D::ZeroVector;
		float TotalAngle = 0.0f;

		for (FCubismPhysicsRigInput& Input : Rig.Inputs)
		{
			if (!Input.Parameter.IsValid() || !ParameterCaches.IsValidIndex(Input.ParameterIndex))
			{
				continue;
			}

			const float Value = Input.Parameter->Value;

			Input.GetNormalizedParameterValue(TotalTranslation, TotalAngle, Value, Rig.NormalizationPosition, Rig.NormalizationAngle);

			ParameterCaches[Input.ParameterIndex] = Value;
		}

		const float RadAngle = -TotalAngle * (PI / 180.0f);

		const FVector2D RotatedTranslation(
			FMath::Cos(RadAngle) * TotalTranslation.X - FMath::Sin(RadAngle) * TotalTranslation.Y,
			FMath::Sin(RadAngle) * TotalTranslation.X + FMath::Cos(RadAngle) * TotalTranslation.Y
		);
		TotalTranslation = RotatedTranslation;

		UpdateParticlesForStabilization(
			Rig.Particles,
			TotalTranslation,
			TotalAngle,
			MovementThreshold * Rig.NormalizationPosition.Maximum
		);

		for (FCubismPhysicsRigOutput& Output : Rig.Outputs)
		{
			if (!Output.Parameter.IsValid() || !ParameterCaches.IsValidIndex(Output.ParameterIndex))
			{
				continue;
			}

			if (1 <= Output.ParticleIndex && Output.ParticleIndex < Rig.Particles.Num())
			{
				const float OutputValue = Output.GetValue(Rig.Particles, Gravity);

				Output.PreviousValue = OutputValue;
				Output.CurrentValue = OutputValue;

				float TargetValue = Output.Parameter->Value;

				Output.UpdateOutputParameterValue(TargetValue, OutputValue);

				Output.Parameter->SetParameterValue(TargetValue);

				ParameterCaches[Output.ParameterIndex] = TargetValue;
			}
		}
	}
}

void UCubismPhysicsComponent::UpdateParticles(
	TArray<FCubismPhysicsRigParticle>& Strand,
	const FVector2D TotalTranslation,
	const float TotalAngle,
	const float ThresholdValue,
	const float DeltaTime,
	const float Resistance
)
{
	if (Strand.Num() == 0)
	{
		return;
	}

	Strand[0].Position.X = TotalTranslation.X;
	Strand[0].Position.Y = TotalTranslation.Y;

	const float TotalRadian = (TotalAngle / 180.0f)* PI;

	const FVector2D CurrentGravity = FVector2D(FMath::Sin(TotalRadian), FMath::Cos(TotalRadian));

	for (int32 i = 1; i < Strand.Num(); ++i)
	{
		Strand[i].Force = CurrentGravity * Strand[i].Acceleration + Wind;
		Strand[i].LastPosition = Strand[i].Position;

		// The Cubism Editor expects 30 FPS so we scale here by 30.
		const float Delay = Strand[i].Delay * DeltaTime * 30.0f;

		FVector2D Direction = Strand[i].Position - Strand[i - 1].Position;

		const float Radian = FCubismMath::DirectionToRadian(Strand[i].LastGravity, CurrentGravity) / Resistance;
		const FVector2D RotatedDirection(
			(FMath::Cos(Radian) * Direction.X) - (Direction.Y * FMath::Sin(Radian)),
			(FMath::Sin(Radian) * Direction.X) + (Direction.Y * FMath::Cos(Radian))
		);
		Direction = RotatedDirection;

		Strand[i].Position = Strand[i - 1].Position + Direction;

		const FVector2D Velocity = Strand[i].Velocity * Delay;
		const FVector2D Force = Strand[i].Force * Delay * Delay;

		Strand[i].Position = Strand[i].Position + Velocity + Force;

		FVector2D NewDirection = Strand[i].Position - Strand[i - 1].Position;

		NewDirection.Normalize();

		Strand[i].Position = Strand[i - 1].Position + (NewDirection * Strand[i].Radius);

		if (FMath::Abs(Strand[i].Position.X) < ThresholdValue)
		{
			Strand[i].Position.X = 0.0f;
		}

		if (Delay != 0.0f)
		{
			Strand[i].Velocity = (Strand[i].Position - Strand[i].LastPosition);
			Strand[i].Velocity /= Delay;
			Strand[i].Velocity *= Strand[i].Mobility;
		}

		Strand[i].Force = FVector2D(0.0f, 0.0f);
		Strand[i].LastGravity = CurrentGravity;
	}
}

void UCubismPhysicsComponent::UpdateParticlesForStabilization(
	TArray<FCubismPhysicsRigParticle>& Particles,
	const FVector2D TotalTranslation,
	const float TotalAngle,
	const float ThresholdValue
)
{
	if (Particles.Num() == 0)
	{
		return;
	}

	Particles[0].Position.X = TotalTranslation.X;
	Particles[0].Position.Y = TotalTranslation.Y;

	const float TotalRadian = TotalAngle * (PI / 180.0f);

	const FVector2D CurrentGravity = FVector2D(FMath::Sin(TotalRadian), FMath::Cos(TotalRadian));

	for (int32 ParticleIndex = 1; ParticleIndex < Particles.Num(); ++ParticleIndex)
	{
		FCubismPhysicsRigParticle& Particle = Particles[ParticleIndex];

		FVector2D NewPosition = CurrentGravity * Particle.Acceleration + Wind;
		NewPosition = Particle.Radius * NewPosition.GetSafeNormal();

		Particle.LastPosition = Particle.Position;
		Particle.Position = NewPosition + Particles[ParticleIndex - 1].Position;

		Particle.Velocity = FVector2D::ZeroVector;

		if (FMath::Abs(Particle.Position.X) < ThresholdValue)
		{
			Particle.Position.X = 0.0f;
		}

		Particle.LastGravity = CurrentGravity;
	}
}

TObjectPtr<UCubismModelComponent> UCubismPhysicsComponent::GetModel()
{
	return UCubismModelComponent::FindModelComponent(this);
}

// UObject interface
void UCubismPhysicsComponent::PostLoad()
{
	Super::PostLoad();

	const TObjectPtr<UCubismModelComponent> ModelComp = GetModel();

	if (ModelComp)
	{
		Setup(ModelComp);
	}
}

#if WITH_EDITOR
void UCubismPhysicsComponent::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.Property? PropertyChangedEvent.Property->GetFName() : NAME_None;

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UCubismPhysicsComponent, Json))
	{
		const TObjectPtr<UCubismModelComponent> ModelComp = IsValid(Model) ? Model : GetModel();

		if (ModelComp)
		{
			Setup(ModelComp);
		}
	}
}
#endif
// End of UObject interface

// UActorComponent interface
void UCubismPhysicsComponent::OnComponentCreated()
{
	Super::OnComponentCreated();

	const TObjectPtr<UCubismModelComponent> ModelComp = GetModel();

	if (ModelComp)
	{
		Setup(ModelComp);
	}
}

void UCubismPhysicsComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	if (IsValid(Model) && Model->Physics == this)
	{
		Model->Physics = nullptr;
	}

	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

#if WITH_EDITOR
void UCubismPhysicsComponent::PostEditUndo()
{
	Super::PostEditUndo();

	if (const TObjectPtr<UCubismModelComponent> ModelComp = GetModel())
	{
		Setup(ModelComp);
	}
}
#endif

void UCubismPhysicsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// When an update controller drives this actor it calls OnCubismUpdate in execution order instead.
	if (IsControlledByUpdateController() && UCubismUpdateControllerComponent::FindController(this))
	{
		return;
	}

	OnCubismUpdate(DeltaTime);
}

int32 UCubismPhysicsComponent::GetExecutionOrder() const
{
	return CUBISM_EXECUTION_ORDER_PHYSICS;
}

void UCubismPhysicsComponent::OnCubismUpdate(float DeltaTime)
{
#if WITH_EDITOR
	if (GetWorld() && GetWorld()->WorldType == EWorldType::Editor && !bEnablePhysicsInEditor)
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

	if (!HasValidModel() || Rigs.Num() == 0)
	{
		return;
	}

	// A zero time step would never advance the fixed-step loop below.
	if (DeltaTime <= 0.0f)
	{
		return;
	}

	CurrentRemainTime += DeltaTime;
	if (CurrentRemainTime > MaxDeltaTime)
	{
		CurrentRemainTime = 0.0f;
	}

	// Includes parameters registered on demand (e.g. by the pose or motion components) after Setup.
	const int32 ParameterCount = Model->Parameters.Num();

	if (ParameterCaches.Num() < ParameterCount)
	{
		const int32 OldCount = ParameterCaches.Num();

		ParameterCaches.SetNum(ParameterCount);

		for (int32 ParameterIndex = OldCount; ParameterIndex < ParameterCount; ++ParameterIndex)
		{
			const UCubismParameterComponent* Parameter = Model->GetParameter(ParameterIndex);

			ParameterCaches[ParameterIndex] = Parameter ? Parameter->Value : 0.0f;
		}
	}
	if (ParameterInputCaches.Num() < ParameterCount)
	{
		const int32 OldCount = ParameterInputCaches.Num();

		ParameterInputCaches.SetNum(ParameterCount);

		for (int32 ParameterIndex = OldCount; ParameterIndex < ParameterCount; ++ParameterIndex)
		{
			ParameterInputCaches[ParameterIndex] = ParameterCaches[ParameterIndex];
		}
	}

	const float PhysicsDeltaTime = Fps > 0.0f? 1.0f / Fps : DeltaTime;

	if (PhysicsDeltaTime <= 0.0f)
	{
		return;
	}

	while (CurrentRemainTime >= PhysicsDeltaTime)
	{
		const float InputWeight = FMath::Clamp(PhysicsDeltaTime / CurrentRemainTime, 0.0f, 1.0f);

		for (int32 ParameterIndex = 0; ParameterIndex < ParameterCount; ++ParameterIndex)
		{
			const UCubismParameterComponent* Parameter = Model->GetParameter(ParameterIndex);

			if (!Parameter)
			{
				continue;
			}

			ParameterCaches[ParameterIndex] = ParameterInputCaches[ParameterIndex] * (1.0f - InputWeight) + Parameter->Value * InputWeight;
			ParameterInputCaches[ParameterIndex] = ParameterCaches[ParameterIndex];
		}

		// update each pendulum
		for (FCubismPhysicsRig& Rig : Rigs)
		{
			if (Rig.Particles.Num() == 0)
			{
				continue;
			}

			FVector2D TotalTranslation = FVector2D::ZeroVector;
			float TotalAngle = 0.0f;

			for (FCubismPhysicsRigInput& Input : Rig.Inputs)
			{
				if (!Input.Parameter.IsValid() || !ParameterCaches.IsValidIndex(Input.ParameterIndex))
				{
					continue;
				}

				const float Value = ParameterCaches[Input.ParameterIndex];

				Input.GetNormalizedParameterValue(TotalTranslation, TotalAngle, Value, Rig.NormalizationPosition, Rig.NormalizationAngle);
			}

			const float RadAngle = -TotalAngle * (PI / 180.0f);

			const FVector2D RotatedTranslation(
				FMath::Cos(RadAngle) * TotalTranslation.X - FMath::Sin(RadAngle) * TotalTranslation.Y,
				FMath::Sin(RadAngle) * TotalTranslation.X + FMath::Cos(RadAngle) * TotalTranslation.Y
			);
			TotalTranslation = RotatedTranslation;

			UpdateParticles(
				Rig.Particles,
				TotalTranslation,
				TotalAngle,
				MovementThreshold * Rig.NormalizationPosition.Maximum,
				PhysicsDeltaTime,
				AirResistance
			);

			for (FCubismPhysicsRigOutput& Output : Rig.Outputs)
			{
				if (!Output.Parameter.IsValid() || !ParameterCaches.IsValidIndex(Output.ParameterIndex))
				{
					continue;
				}

				if (Output.ParticleIndex < 1 || Output.ParticleIndex >= Rig.Particles.Num())
				{
					continue;
				}

				const float OutputValue = Output.GetValue(Rig.Particles, Gravity);

				Output.PreviousValue = Output.CurrentValue;
				Output.CurrentValue = OutputValue;

				Output.UpdateOutputParameterValue(ParameterCaches[Output.ParameterIndex], OutputValue);
			}
		}

		CurrentRemainTime -= PhysicsDeltaTime;
	}

	const float Weight = FMath::Clamp(CurrentRemainTime / PhysicsDeltaTime, 0.0f, 1.0f);

	for (FCubismPhysicsRig& Rig : Rigs)
	{
		for (FCubismPhysicsRigOutput& Output : Rig.Outputs)
		{
			if (Output.ParameterIndex < 0 || !Output.Parameter.IsValid())
			{
				continue;
			}

			const float OutputValue = Output.PreviousValue * (1.0f - Weight) + Output.CurrentValue * Weight;
			float TargetValue = Output.Parameter->Value;

			Output.UpdateOutputParameterValue(TargetValue, OutputValue);

			Output.Parameter->SetParameterValue(TargetValue);
		}
	}
}
// End of UActorComponent interface
