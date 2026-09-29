/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#include "CubismUpdateControllerComponent.h"
#include "CubismUpdatableInterface.h"
#include "CubismLog.h"
#include "GameFramework/Actor.h"

UCubismUpdateControllerComponent::UCubismUpdateControllerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.bTickEvenWhenPaused = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;

#if WITH_EDITOR
	bTickInEditor = true;
#endif
}

UCubismUpdateControllerComponent* UCubismUpdateControllerComponent::FindController(const UActorComponent* Component)
{
	if (!IsValid(Component))
	{
		return nullptr;
	}

	const AActor* Owner = Component->GetOwner();

	if (!IsValid(Owner))
	{
		return nullptr;
	}

	return Owner->FindComponentByClass<UCubismUpdateControllerComponent>();
}

void UCubismUpdateControllerComponent::RequestRefresh(const UActorComponent* Component)
{
	if (UCubismUpdateControllerComponent* Controller = FindController(Component))
	{
		Controller->bRefreshRequested = true;
	}
}

void UCubismUpdateControllerComponent::OnComponentCreated()
{
	Super::OnComponentCreated();

	bRefreshRequested = true;
}

void UCubismUpdateControllerComponent::OnRegister()
{
	Super::OnRegister();

	bRefreshRequested = true;
}

void UCubismUpdateControllerComponent::BeginPlay()
{
	Super::BeginPlay();

	RefreshUpdatables();
}

void UCubismUpdateControllerComponent::RefreshUpdatables()
{
	Updatables.Empty();

	bRefreshRequested = false;

	const AActor* Owner = GetOwner();

	if (!IsValid(Owner))
	{
		return;
	}

	TArray<UActorComponent*> Components;
	Owner->GetComponents(Components);

	for (UActorComponent* Component : Components)
	{
		if (!IsValid(Component) || !Component->GetClass()->ImplementsInterface(UCubismUpdatableInterface::StaticClass()))
		{
			continue;
		}

		ICubismUpdatableInterface* Interface = Cast<ICubismUpdatableInterface>(Component);

		if (!Interface || !Interface->IsControlledByUpdateController())
		{
			continue;
		}

		TScriptInterface<ICubismUpdatableInterface> Updatable;
		Updatable.SetObject(Component);
		Updatable.SetInterface(Interface);

		UE_LOG(LogCubism, Verbose, TEXT("UCubismUpdateControllerComponent: registered %s"), *Component->GetName());

		Updatables.Add(Updatable);
	}

	Updatables.StableSort([](const TScriptInterface<ICubismUpdatableInterface>& A, const TScriptInterface<ICubismUpdatableInterface>& B)
	{
		return A->GetExecutionOrder() < B->GetExecutionOrder();
	});
}

void UCubismUpdateControllerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bRefreshRequested)
	{
		RefreshUpdatables();
	}

	// Iterate over a copy: an update may create or destroy components, which requests a refresh of the list.
	const TArray<TScriptInterface<ICubismUpdatableInterface>> CurrentUpdatables = Updatables;

	for (const TScriptInterface<ICubismUpdatableInterface>& Updatable : CurrentUpdatables)
	{
		const UActorComponent* Component = Cast<UActorComponent>(Updatable.GetObject());

		if (!IsValid(Component) || !Updatable.GetInterface())
		{
			bRefreshRequested = true;
			continue;
		}

		// Respect the per-component tick switch (e.g. the editor toggles of the effect components).
		if (!Component->IsComponentTickEnabled())
		{
			continue;
		}

		Updatable->OnCubismUpdate(DeltaTime);
	}
}
