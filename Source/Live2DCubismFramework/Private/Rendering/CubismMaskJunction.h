/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */


#pragma once

#include "Rendering/CubismMaskTextureComponent.h"
#include "UObject/WeakObjectPtrTemplates.h"

class FCubismMaskRenderer;
class UCubismDrawableComponent;
class UTextureRenderTarget2D;

/**
 * A class that manages the address of the mask to be drawn.
 *
 * The junction is not a UObject, so it must not hold strong references to components:
 * weak pointers are used so that a destroyed drawable or render target is detected instead of dereferenced.
 */
class FCubismMaskJunction
{
public:
	struct FMaskDrawableData
	{
		/**
		 * The drawable used for masking.
		 */
		TWeakObjectPtr<UCubismDrawableComponent> Drawable;

		/**
		 * The buffers used to draw the mask of the drawable.
		 */
		TSharedPtr<FCubismMaskRenderer, ESPMode::ThreadSafe> Renderer;
	};

	/**
	 * The list of the drawables that use the same mask.
	 */
	TArray<TWeakObjectPtr<UCubismDrawableComponent>> Drawables;

	/**
	 * The list of the drawables for masking.
	 */
	TArray<FMaskDrawableData> MaskDrawables;

	/**
	 * The render target where the mask is drawn.
	 */
	TWeakObjectPtr<UTextureRenderTarget2D> RenderTarget;

	/**
	 * The offset of the mask to be drawn.
	 */
	FVector4 Offset = FVector4(0.0f, 0.0f, 0.0f, 0.0f);

	/**
	 * The channel of the mask to be drawn.
	 */
	FVector4 Channel = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
};
