/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */


#include "Pose/CubismPoseComponent.h"

#include "Model/CubismModelComponent.h"
#include "Model/CubismParameterComponent.h"
#include "Model/CubismPartComponent.h"
#include "Model/CubismModelActor.h"
#include "Model/CubismParameterStoreComponent.h"
#include "Motion/CubismMotionComponent.h"
#include "Pose/CubismPose3Json.h"

const float Epsilon = 0.001f;
const float DefaultFadeInSeconds = 0.5f;

const float Phi = 0.5f;
const float BackOpacityThreshold = 0.15f;

UCubismPoseComponent::UCubismPoseComponent()
	: FadeInTime(DefaultFadeInSeconds)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	bTickInEditor = true;
}

void UCubismPoseComponent::Setup(UCubismModelComponent* InModel)
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

	PartGroups.Empty();

	if (Json)
	{
		FadeInTime = Json->FadeInTime;

		if (FadeInTime <= 0.0f)
		{
			FadeInTime = DefaultFadeInSeconds;
		}

		for (const FCubismPosePartGroup& PartGroup : Json->PartGroups)
		{
			FCubismPosePartGroupParameter Group;

			for (const FCubismPosePart& Part : PartGroup.Parts)
			{
				FCubismPosePartParameter PartParam;

				PartParam.Part = Model->GetPart(Part.Id);
				PartParam.Parameter = Model->GetParameter(Part.Id);

				if (!PartParam.Part.IsValid() || !PartParam.Parameter.IsValid())
				{
					continue;
				}

				for (const FString& LinkId : Part.Links)
				{
					if (UCubismPartComponent* LinkPart = Model->GetPart(LinkId))
					{
						PartParam.LinkParts.Add(LinkPart);
					}
				}

				Group.Parts.Add(PartParam);
			}

			if (Group.Parts.Num() > 0)
			{
				PartGroups.Add(Group);
			}
		}
	}

	if (Model->Pose != this)
	{
		if (IsValid(Model->Pose))
		{
			Model->Pose->DestroyComponent();
		}
		Model->Pose = this;
	}

	if (IsValid(Model->ParameterStore))
	{
		AddTickPrerequisiteComponent(Model->ParameterStore); // must be updated after parameters loaded
	}

	if (IsValid(Model->Motion))
	{
		AddTickPrerequisiteComponent(Model->Motion); // must be updated at first because motions overwrite parameters
	}
}

bool UCubismPoseComponent::HasValidModel() const
{
	return IsValid(Model) && Model->IsModelReady();
}

TObjectPtr<UCubismModelComponent> UCubismPoseComponent::GetModel()
{
	return UCubismModelComponent::FindModelComponent(this);
}

// UObject interface
void UCubismPoseComponent::PostLoad()
{
	Super::PostLoad();

	const TObjectPtr<UCubismModelComponent> ModelComp = GetModel();

	if (ModelComp)
	{
		Setup(ModelComp);
	}
}

#if WITH_EDITOR
void UCubismPoseComponent::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.Property? PropertyChangedEvent.Property->GetFName() : NAME_None;

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UCubismPoseComponent, Json))
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
void UCubismPoseComponent::OnComponentCreated()
{
	Super::OnComponentCreated();

	const TObjectPtr<UCubismModelComponent> ModelComp = GetModel();

	if (ModelComp)
	{
		Setup(ModelComp);
	}
}

void UCubismPoseComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	if (IsValid(Model) && Model->Pose == this)
	{
		Model->Pose = nullptr;
	}

	PartGroups.Empty();

	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void UCubismPoseComponent::DoFade(float DeltaTime)
{
	UCubismParameterStoreComponent* ParameterStore = Model->ParameterStore;

	for (const FCubismPosePartGroupParameter& PartGroup : PartGroups)
	{
		if (PartGroup.Parts.Num() == 0)
		{
			continue;
		}

		// default visible part and its opacity
		UCubismPartComponent* VisiblePart = PartGroup.Parts[0].Part.Get();

		float NewOpacity = 1.0f;

		// find the visible part in a group
		for (const FCubismPosePartParameter& PartParam : PartGroup.Parts)
		{
			UCubismPartComponent* Part = PartParam.Part.Get();
			const UCubismParameterComponent* Parameter = PartParam.Parameter.Get();

			if (!IsValid(Part) || !IsValid(Parameter) || !(Parameter->Value > Epsilon))
			{
				continue;
			}

			if (Parameter->Value > 0.0f)
			{
				VisiblePart = Part;
				NewOpacity = Part->Opacity;

				break;
			}
		}

		if (!IsValid(VisiblePart))
		{
			continue;
		}

		if (FadeInTime > 0.0f)
		{
			NewOpacity += DeltaTime / FadeInTime;
		}
		else
		{
			NewOpacity = 1.0f;
		}

		if (NewOpacity > 1.0f)
		{
			NewOpacity = 1.0f;
		}

		for (const FCubismPosePartParameter& PartParam : PartGroup.Parts)
		{
			UCubismPartComponent* Part = PartParam.Part.Get();

			if (!IsValid(Part))
			{
				continue;
			}

			//  The settings for the visible parts.
			if (Part == VisiblePart)
			{
				Part->SetPartOpacity(NewOpacity);
			}
			// The settings for the hidden parts.
			else
			{
				float Opacity = Part->Opacity;
				float A1;          // The opacity calculated through the computation.

				if (NewOpacity < Phi)
				{
					A1 = NewOpacity * (Phi - 1.0f) / Phi + 1.0f; // a line through (0, 1) and (phi, phi)
				}
				else
				{
					A1 = (1.0f - NewOpacity) * Phi / (1.0f - Phi); // a line through (1, 0) and (phi, phi)
				}

				const float BackOpacity = (1.0f - A1) * (1.0f - NewOpacity);

				if (BackOpacity > BackOpacityThreshold && NewOpacity < 1.0f)
				{
					A1 = 1.0f - BackOpacityThreshold / (1.0f - NewOpacity);
				}

				if (Opacity > A1)
				{
					Opacity = A1; // If the calculated opacity is greater (more opaque) than the computed opacity, increase the opacity.
				}

				Part->SetPartOpacity(Opacity);
			}

			if (IsValid(ParameterStore))
			{
				ParameterStore->SavePartOpacity(Part->Index);
			}
		}
	}
}

void UCubismPoseComponent::CopyPartOpacities()
{
	UCubismParameterStoreComponent* ParameterStore = Model->ParameterStore;

	// apply opacity to linked parts
	for (const FCubismPosePartGroupParameter& PartGroup : PartGroups)
	{
		for (const FCubismPosePartParameter& PartParam : PartGroup.Parts)
		{
			const UCubismPartComponent* Part = PartParam.Part.Get();

			if (PartParam.LinkParts.Num() == 0 || !IsValid(Part))
			{
				continue; // no linked parts
			}

			const float Opacity = Part->Opacity;

			for (const TWeakObjectPtr<UCubismPartComponent>& WeakLinkPart : PartParam.LinkParts)
			{
				UCubismPartComponent* LinkPart = WeakLinkPart.Get();

				if (!IsValid(LinkPart))
				{
					continue;
				}

				LinkPart->SetPartOpacity(Opacity);

				if (IsValid(ParameterStore))
				{
					ParameterStore->SavePartOpacity(LinkPart->Index);
				}
			}
		}
	}
}

void UCubismPoseComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
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

	DoFade(DeltaTime);

	CopyPartOpacities();
}
// End of UActorComponent interface
