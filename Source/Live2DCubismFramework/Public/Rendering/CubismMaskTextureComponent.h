/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */


#pragma once

#include "Components/ActorComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "CubismMaskTextureComponent.generated.h"

class FCubismMaskJunction;
class UCubismRendererComponent;
class UCubismModelComponent;

/**
 * A component to manage the render targets that the masks of the models are drawn to.
 */
UCLASS(BlueprintType)
class LIVE2DCUBISMFRAMEWORK_API UCubismMaskTextureComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/**
	 * The size of each render target.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Live2D Cubism")
	int32 Size = 4096;

	/**
	 * The flag to use several render targets.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Live2D Cubism")
	bool bUseMultiRenderTargets = false;

	/**
	 * The number of render targets when `bUseMultiRenderTargets` is set.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1", SliderMin = "1", EditCondition = "bUseMultiRenderTargets"), Category = "Live2D Cubism")
	int32 RenderTargetCount = 1;

	/**
	 * The level of detail of the mask layout when `bUseMultiRenderTargets` is set.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0", SliderMin = "0", EditCondition = "bUseMultiRenderTargets"), Category = "Live2D Cubism")
	int32 LOD = 0;

	/**
	 * The total number of masks drawn.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Live2D Cubism")
	int32 NumMasks;

	/**
	 * The actors carrying a model component whose masks are drawn into these render targets.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Live2D Cubism")
	TArray<TObjectPtr<AActor>> Models;

	/**
	 * The render targets the masks are drawn to.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Live2D Cubism")
	TArray<TObjectPtr<UTextureRenderTarget2D>> RenderTargets;

	/**
	 * @brief Registers the actor of a model with this mask texture.
	 * @param Model Any actor that has a UCubismModelComponent.
	 */
	UFUNCTION(BlueprintCallable, Category = "Live2D Cubism")
	void AddModel(AActor* Model);

	/**
	 * @brief Unregisters the actor of a model from this mask texture.
	 * @param Model Any actor that has a UCubismModelComponent.
	 */
	UFUNCTION(BlueprintCallable, Category = "Live2D Cubism")
	void RemoveModel(AActor* Model);

	/**
	 * @brief Assigns a render target, an offset and a channel to every mask junction of the registered models.
	 */
	UFUNCTION(BlueprintCallable, Category = "Live2D Cubism")
	void ResolveMaskLayout();

private:
	/**
	 * @brief The constructor of the component.
	 */
	UCubismMaskTextureComponent();

	static TObjectPtr<UCubismModelComponent> GetModel(AActor* Model);

	inline int32 CalcOptimalLOD() const;

	inline void AllocateRenderTargets(const int32 RequiredRTs);

	/**
	 * The flag to indicate whether the mask layout needs to be resolved again.
	 */
	bool bDirty = true;

public:
	// UObject interface
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
