/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#pragma once

#include "Components/ActorComponent.h"
#include "CubismUpdateControllerComponent.generated.h"

class ICubismUpdatableInterface;

/**
 * A component that updates every Cubism component of its actor in a deterministic order.
 * The model component creates one automatically when it is set up.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class LIVE2DCUBISMFRAMEWORK_API UCubismUpdateControllerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCubismUpdateControllerComponent();

	/**
	 * If true, the Cubism components keep updating while the game is paused (menus, dialogs).
	 * The drawables and masks never tick while paused, so with this on the model jumps ahead when the game resumes.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Live2D Cubism")
	bool bUpdateWhenPaused = false;

	/**
	 * @brief Finds the update controller on the actor that owns the given component.
	 * @return The controller, or nullptr when the actor is not driven by one.
	 */
	static UCubismUpdateControllerComponent* FindController(const UActorComponent* Component);

	/**
	 * @brief Asks the controller of the given component's actor (if any) to rescan the actor before its next update.
	 * Call it whenever a Cubism component is created or bound to a model after the controller was created.
	 */
	static void RequestRefresh(const UActorComponent* Component);

protected:
	virtual void BeginPlay() override;
	virtual void OnComponentCreated() override;
	virtual void OnRegister() override;

public:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/**
	 * @brief Collects the components of the actor that implement ICubismUpdatableInterface and sorts them by execution order.
	 */
	UFUNCTION(BlueprintCallable, Category = "Live2D Cubism")
	void RefreshUpdatables();

private:
	UPROPERTY(Transient)
	TArray<TScriptInterface<ICubismUpdatableInterface>> Updatables;

	/** Set when the list of updatables may be stale. */
	bool bRefreshRequested = true;
};
