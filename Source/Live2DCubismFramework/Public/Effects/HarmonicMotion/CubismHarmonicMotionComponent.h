/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */


#pragma once

#include "Components/ActorComponent.h"
#include "CubismUpdatableInterface.h"
#include "CubismHarmonicMotionComponent.generated.h"

class UCubismModelComponent;
struct FCubismHarmonicMotionParameter;

/**
 * A component to apply the harmonic motion effect to the specified parameters of the Cubism model.
 */
UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class LIVE2DCUBISMFRAMEWORK_API UCubismHarmonicMotionComponent : public UActorComponent, public ICubismUpdatableInterface
{
	GENERATED_BODY()

public:
	/**
	 * The parameters to control the harmonic motion effect.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Live2D Cubism")
	TArray<FCubismHarmonicMotionParameter> Parameters;

public:
	/**
	 * @brief The function to set up the component.
	 * @param InModel The model component that the component depends on.
	 * @note This function should be called after the component is attached to the model component.
	 */
	UFUNCTION(BlueprintCallable, Category = "Live2D Cubism")
	void Setup(UCubismModelComponent* InModel);

	// ICubismUpdatableInterface
	virtual bool IsControlledByUpdateController() const override { return true; }
	virtual int32 GetExecutionOrder() const override;
	virtual void OnCubismUpdate(float DeltaTime) override;

	/**
	 * @brief Whether the component is bound to a model that is ready to be queried.
	 */
	bool HasValidModel() const;

private:
	/**
	 * @brief The constructor of the component.
	 */
	UCubismHarmonicMotionComponent();

	TObjectPtr<UCubismModelComponent> GetModel();

	/**
	 * The model component that the component depends on.
	 * Tracked by the garbage collector so that it is cleared when the model is destroyed.
	 */
	UPROPERTY(Transient, DuplicateTransient)
	TObjectPtr<UCubismModelComponent> Model;

public:
	// UObject interface
	virtual void PostLoad() override;
	// End of UObject interface

	// UActorComponent interface
	virtual void OnComponentCreated() override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

#if WITH_EDITOR
	virtual void PostEditUndo() override;
#endif

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	// End of UActorComponent interface
};
