/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */


#pragma once

#include "GameFramework/Actor.h"

#include "Model/CubismMoc3.h"
#include "Physics/CubismPhysics3Json.h"
#include "Pose/CubismPose3Json.h"
#include "DisplayInfo/CubismDisplayInfo3Json.h"
#include "Expression/CubismExp3Json.h"
#include "Motion/CubismMotion3Json.h"
#include "Motion/CubismMotionComponent.h"
#include "UserData/CubismUserData3Json.h"
#include "Model/CubismModel3Json.h"

#include "CubismModelActor.generated.h"

class UCubismModelComponent;
class UCubismExpressionComponent;
class UCubismLipSyncComponent;
class UCubismEyeBlinkComponent;
class UCubismLookAtComponent;
class UCubismPhysicsComponent;
class UCubismPoseComponent;
class UCubismRendererComponent;
class UCubismRaycastComponent;
class USoundWave;

/**
 * An instance of a Live2D Cubism model in a level.
 *
 * Create a Blueprint with this class as parent, set `Model Asset` in its defaults and drop it into a level:
 * the actor builds the model component and every helper component (motion, expression, physics, pose,
 * eye blink, lip sync, raycast, renderer) from the model asset by itself.
 * The same happens when the actor is spawned at runtime, or when a model asset is dragged into the viewport.
 */
UCLASS(Blueprintable)
class LIVE2DCUBISMFRAMEWORK_API ACubismModel : public AActor
{
	GENERATED_BODY()

public:
	/**
	 * The model asset (the imported model3.json) this actor shows. Everything else is derived from it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Live2D Cubism", meta = (DisplayName = "Model Asset"))
	TObjectPtr<UCubismModel3Json> ModelAsset;

	/**
	 * A component to control the model.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Live2D Cubism")
	TObjectPtr<UCubismModelComponent> Model;

	/**
	 * Initializes the model actor with the given model asset. Safe to call again with another asset.
	 *
	 * @param Model3Json The model asset to load
	 */
	UFUNCTION(BlueprintCallable, Category = "Live2D Cubism", meta = (WorldContext = "WorldContextObject"))
	void Initialize(UCubismModel3Json* Model3Json);

	// ---- Component access -------------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Live2D Cubism|Components")
	UCubismModelComponent* GetModelComponent() const;

	UFUNCTION(BlueprintPure, Category = "Live2D Cubism|Components")
	UCubismMotionComponent* GetMotionComponent() const;

	UFUNCTION(BlueprintPure, Category = "Live2D Cubism|Components")
	UCubismExpressionComponent* GetExpressionComponent() const;

	UFUNCTION(BlueprintPure, Category = "Live2D Cubism|Components")
	UCubismLipSyncComponent* GetLipSyncComponent() const;

	UFUNCTION(BlueprintPure, Category = "Live2D Cubism|Components")
	UCubismEyeBlinkComponent* GetEyeBlinkComponent() const;

	UFUNCTION(BlueprintPure, Category = "Live2D Cubism|Components")
	UCubismLookAtComponent* GetLookAtComponent() const;

	UFUNCTION(BlueprintPure, Category = "Live2D Cubism|Components")
	UCubismPhysicsComponent* GetPhysicsComponent() const;

	UFUNCTION(BlueprintPure, Category = "Live2D Cubism|Components")
	UCubismPoseComponent* GetPoseComponent() const;

	UFUNCTION(BlueprintPure, Category = "Live2D Cubism|Components")
	UCubismRendererComponent* GetRendererComponent() const;

	UFUNCTION(BlueprintPure, Category = "Live2D Cubism|Components")
	UCubismRaycastComponent* GetRaycastComponent() const;

	// ---- Character control -------------------------------------------------------------------------------

	/**
	 * @brief The names of the motions of this model (asset names without the `_motion3` suffix).
	 */
	UFUNCTION(BlueprintPure, Category = "Live2D Cubism|Control")
	TArray<FString> GetMotionNames() const;

	/**
	 * @brief The names of the expressions of this model (asset names without the `_exp3` suffix).
	 */
	UFUNCTION(BlueprintPure, Category = "Live2D Cubism|Control")
	TArray<FString> GetExpressionNames() const;

	/**
	 * @brief Plays a motion by name (see GetMotionNames). The idle motion resumes afterwards.
	 */
	UFUNCTION(BlueprintCallable, Category = "Live2D Cubism|Control")
	bool PlayMotionByName(const FString& Name, const ECubismMotionPriority Priority = ECubismMotionPriority::Normal);

	/**
	 * @brief Plays an expression by name (see GetExpressionNames).
	 */
	UFUNCTION(BlueprintCallable, Category = "Live2D Cubism|Control")
	bool PlayExpressionByName(const FString& Name);

	/**
	 * @brief Plays a voice line and moves the mouth with it. Bind the lip sync component's OnSourceFinished to know when it ended.
	 */
	UFUNCTION(BlueprintCallable, Category = "Live2D Cubism|Control")
	void Speak(USoundWave* VoiceLine);

	/**
	 * @brief Stops the current voice line and closes the mouth.
	 */
	UFUNCTION(BlueprintCallable, Category = "Live2D Cubism|Control")
	void StopSpeaking();

	/**
	 * @brief Sets the opacity of the whole character (0 = invisible, 1 = opaque). Drive it from a timeline to fade in or out.
	 */
	UFUNCTION(BlueprintCallable, Category = "Live2D Cubism|Control")
	void SetOpacity(const float Opacity);

	/**
	 * @brief Darkens the character (e.g. the one who is not speaking) or restores its colors.
	 */
	UFUNCTION(BlueprintCallable, Category = "Live2D Cubism|Control")
	void SetDimmed(const bool bDimmed, const FLinearColor DimColor = FLinearColor(0.5f, 0.5f, 0.5f, 1.0f));

	/**
	 * @brief Gives this character its own sort range so that overlapping characters do not interleave (use 0, 1000, 2000, ...).
	 */
	UFUNCTION(BlueprintCallable, Category = "Live2D Cubism|Control")
	void SetRenderOrder(const int32 RenderOrder);

	/**
	 * @brief Makes the character look at an actor (nullptr looks straight ahead again).
	 * Creates a look-at component with the standard head/eye/body parameters on first use.
	 */
	UFUNCTION(BlueprintCallable, Category = "Live2D Cubism|Control")
	void SetLookAtTarget(AActor* Target);

	/**
	 * @brief Switches the automatic mouth movement on or off (useful while text is being typed out without a voice line).
	 */
	UFUNCTION(BlueprintCallable, Category = "Live2D Cubism|Control")
	void SetAutoLipSync(const bool bEnabled);

	// ---- Editor testing ----------------------------------------------------------------------------------

#if WITH_EDITORONLY_DATA
	/** Name of the motion the "Test Play Motion" button plays (see GetMotionNames). */
	UPROPERTY(EditAnywhere, Category = "Live2D Cubism|Test")
	FString TestMotionName;

	/** Name of the expression the "Test Play Expression" button plays (see GetExpressionNames). */
	UPROPERTY(EditAnywhere, Category = "Live2D Cubism|Test")
	FString TestExpressionName;

	/** Voice line the "Test Speak" button plays. */
	UPROPERTY(EditAnywhere, Category = "Live2D Cubism|Test")
	TObjectPtr<USoundWave> TestVoiceLine;
#endif

	UFUNCTION(CallInEditor, Category = "Live2D Cubism|Test")
	void TestPlayMotion();

	UFUNCTION(CallInEditor, Category = "Live2D Cubism|Test")
	void TestPlayExpression();

	UFUNCTION(CallInEditor, Category = "Live2D Cubism|Test")
	void TestSpeak();

	UFUNCTION(CallInEditor, Category = "Live2D Cubism|Test")
	void TestStop();

	UFUNCTION(CallInEditor, Category = "Live2D Cubism|Test")
	void TestListNames();

	// AActor interface
	virtual void OnConstruction(const FTransform& Transform) override;
	// End of AActor interface

private:
	ACubismModel();

	/**
	 * The asset the components were last built from; used to rebuild when `ModelAsset` changes.
	 */
	UPROPERTY()
	TObjectPtr<UCubismModel3Json> InitializedAsset;

	UCubismLipSyncComponent* FindOrCreateLipSync();

	static TObjectPtr<UCubismMoc3> LoadMoc(const TObjectPtr<UCubismModel3Json>& Model3Json);
	static TArray<TObjectPtr<UTexture2D>> LoadTextures(const TObjectPtr<UCubismModel3Json>& Model3Json);
	static TObjectPtr<UCubismPhysics3Json> LoadPhysics3Json(const TObjectPtr<UCubismModel3Json>& Model3Json);
	static TObjectPtr<UCubismPose3Json> LoadPose3Json(const TObjectPtr<UCubismModel3Json>& Model3Json);
	static TArray<TObjectPtr<UCubismExp3Json>> LoadExp3Jsons(const TObjectPtr<UCubismModel3Json>& Model3Json);
	static TArray<FMotion3JsonGroup> LoadMotion3Jsons(const TObjectPtr<UCubismModel3Json>& Model3Json);
	static TObjectPtr<UCubismDisplayInfo3Json> LoadDisplayInfo3Json(const TObjectPtr<UCubismModel3Json>& Model3Json);
	static TObjectPtr<UCubismUserData3Json> LoadUserData3Json(const TObjectPtr<UCubismModel3Json>& Model3Json);
};
