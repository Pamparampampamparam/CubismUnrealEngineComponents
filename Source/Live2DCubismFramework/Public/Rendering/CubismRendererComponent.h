/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */


#pragma once

#include "Components/ActorComponent.h"
#include "CubismUpdatableInterface.h"
#include "CubismRendererComponent.generated.h"

class ACubismModel;
class ACubismMaskTexture;
class FCubismMaskJunction;
class UCubismModelComponent;
class UCubismDrawableComponent;

/**
 * The render order mode of the model.
 */
UENUM(BlueprintType)
enum class ECubismRendererSortingOrder : uint8
{
	FrontToBack,
	BackToFront,
};

/**
 * A component to render Live2D Cubism models.
 */
UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class LIVE2DCUBISMFRAMEWORK_API UCubismRendererComponent : public UActorComponent, public ICubismUpdatableInterface
{
	GENERATED_BODY()

public:	
	/**
	 * The mask texture to apply to the model.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Live2D Cubism")
	TObjectPtr<ACubismMaskTexture> MaskTexture;

	/**
	 * The render order mode of the model.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Live2D Cubism")
	ECubismRendererSortingOrder SortingOrder = ECubismRendererSortingOrder::FrontToBack;

	/**
	 * The flag to enable the Z-sorting of the model.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Live2D Cubism")
	bool bZSort = false;

	/**
	 * The render order of the model.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Live2D Cubism")
	int32 RenderOrder = 0;

	/**
	 * The epsilon value to sort the drawables along the Z-axis.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Live2D Cubism", meta = (EditCondition = "bZSort"))
	float Epsilon = 0.1f;

	/**
	 * The number of masks used by the model.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Live2D Cubism")
	int32 NumMasks;

	/**
	 * The list of the junctions that group the drawables sharing the same mask.
	 */
	TArray<TSharedPtr<FCubismMaskJunction>> Junctions;

	// ICubismUpdatableInterface implementation
	virtual bool IsControlledByUpdateController() const override { return true; }
	virtual int32 GetExecutionOrder() const override;
	virtual void OnCubismUpdate(float DeltaTime) override;

public:
	/**
	 * @brief The function to set up the component.
	 * @param InModel The model component that the component depends on.
	 * @note This function should be called after the component is attached to the model component.
	 */
	UFUNCTION(BlueprintCallable, Category = "Live2D Cubism")
	void Setup(UCubismModelComponent* InModel);

	/**
	 * @brief Applies the sorting settings to the drawables of the model.
	 */
	UFUNCTION(BlueprintCallable, Category = "Live2D Cubism")
	void ApplyRenderOrder();

	/**
	 * @brief Sets the render order of the whole model and applies it to the drawables.
	 * Give each model on screen its own range (e.g. 0, 1000, 2000, ...) so that overlapping models do not interleave.
	 */
	UFUNCTION(BlueprintCallable, Category = "Live2D Cubism")
	void SetRenderOrder(const int32 InRenderOrder);

	/**
	 * @brief The highest translucent sort priority used by the drawables of this model.
	 * Give VFX that must appear in front of the character a higher priority than this.
	 */
	UFUNCTION(BlueprintPure, Category = "Live2D Cubism")
	int32 GetMaxRenderOrder() const;

	/**
	 * @brief The function to calculate the render order of the drawable.
	 * @param Drawable The drawable to calculate the render order for.
	 * @return The render order of the drawable.
	 */
	UFUNCTION(BlueprintCallable, Category = "Live2D Cubism")
	int32 CalcRenderOrder(const UCubismDrawableComponent* Drawable) const;

	/**
	 * @brief Whether the component is bound to a model that is ready to be queried.
	 */
	bool HasValidModel() const;

private:
	friend class UCubismModelComponent;

	/**
	 * @brief The constructor of the component.
	 */
	UCubismRendererComponent();

	/**
	 * @brief Finds or spawns the mask texture actor of the world and registers the owner with it.
	 */
	void SpawnMaskTexture();

	TObjectPtr<UCubismModelComponent> GetModel();

	/**
	 * The model component that the component depends on.
	 * Tracked by the garbage collector so that it is cleared when the model is destroyed.
	 */
	UPROPERTY(Transient, DuplicateTransient)
	TObjectPtr<UCubismModelComponent> Model;

public:
	virtual void BeginPlay() override;

	// UObject interface
	virtual void PostLoad() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
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
