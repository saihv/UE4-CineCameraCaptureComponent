// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "ShowFlags.h"
#include "Components/SceneCaptureComponent.h"
#include "CineCameraComponent.h"
#include "CineCameraCaptureComponent.generated.h"

class FSceneViewStateInterface;

/**
 * 
 */
UCLASS(Blueprintable, ClassGroup = Rendering, meta = (BlueprintSpawnableComponent))
class CINEMATICCAMERA_API UCineCameraCaptureComponent : public UCineCameraComponent
{
	GENERATED_BODY()

protected:
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void Serialize(FArchive& Ar);
	virtual void SendRenderTransform_Concurrent() override;
	virtual bool RequiresGameThreadEndOfFrameUpdates() const override
	{
		// this method could probably be removed allowing them to run on any thread, but it isn't worth the trouble
		return true;
	}
	
	void CaptureSceneDeferred();
	
	void UpdateSceneCaptureContents(FSceneInterface* Scene);
	void UpdateCameraLensCapture(float DeltaTime);
	/** Update the show flags from our show flags settings (ideally, you'd be able to set this more directly, but currently unable to make FEngineShowFlags a UStruct to use it as a UProperty...) */
	void UpdateShowFlags();
	

	/**
	* The view state holds persistent scene rendering state and enables occlusion culling in scene captures.
	* NOTE: This object is used by the rendering thread. When the game thread attempts to destroy it, FDeferredCleanupInterface will keep the object around until the RT is done accessing it.
	*/
	TArray<FSceneViewStateReference> ViewStates;

public:
	UCineCameraCaptureComponent();

	static void UpdateDeferredCaptures(FSceneInterface* Scene);
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);
	/** Returns the view state, if any, and allocates one if needed. This function can return NULL, e.g. when bCaptureEveryFrame is false. */
	FSceneViewStateInterface* GetViewState(int32 ViewIndex);
	/** To leverage a component's bOwnerNoSee/bOnlyOwnerSee properties, the capture view requires an "owner". Override this to set a "ViewActor" for the scene. */
	virtual const AActor* GetViewOwner() const { return nullptr; }

	/** ShowFlags for the SceneCapture's ViewFamily, to control rendering settings for this view. Hidden but accessible through details customization */
	UPROPERTY(EditAnywhere, interp, Category = SceneCapture)
		TArray<FEngineShowFlagsSetting> ShowFlagSettings;

	FEngineShowFlags ShowFlags;
	/** Indicates which stereo pass this component is capturing for, if any */
	EStereoscopicPass CaptureStereoPass;

	FPostProcessSettings CameraLensPostProcessSettings;

	/** Controls what primitives get rendered into the scene capture. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SceneCapture)
		ESceneCapturePrimitiveRenderMode PrimitiveRenderMode;

	/** Name of the profiling event. */
	UPROPERTY(EditAnywhere, interp, Category = SceneCapture)
		FString ProfilingEventName;

	/** The components won't rendered by current component.*/
	UPROPERTY()
		TArray<TWeakObjectPtr<UPrimitiveComponent> > HiddenComponents;

	/** The actors to hide in the scene capture. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SceneCapture)
		TArray<AActor*> HiddenActors;

	/** The only components to be rendered by this scene capture, if PrimitiveRenderMode is set to UseShowOnlyList. */
	UPROPERTY()
		TArray<TWeakObjectPtr<UPrimitiveComponent> > ShowOnlyComponents;

	/** The only actors to be rendered by this scene capture, if PrimitiveRenderMode is set to UseShowOnlyList.*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SceneCapture)
		TArray<AActor*> ShowOnlyActors;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Projection, meta = (DisplayName = "Projection Type"))
		TEnumAsByte<ECameraProjectionMode::Type> ProjectionType;

	/** Output render target of the scene capture that can be read in materals. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SceneCapture)
		class UTextureRenderTarget2D* TextureTarget;

	/** Whether to persist the rendering state even if bCaptureEveryFrame==false.  This allows velocities for Motion Blur and Temporal AA to be computed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SceneCapture, meta = (editcondition = "!bCaptureEveryFrame"))
		bool bAlwaysPersistRenderingState;

	/** Whether to update the capture's contents every frame.  If disabled, the component will render once on load and then only when moved. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SceneCapture)
		bool bCaptureEveryFrame;

	/** Whether to update the capture's contents on movement.  Disable if you are going to capture manually from blueprint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SceneCapture)
		bool bCaptureOnMovement;

	UPROPERTY(interp, Category = SceneCapture, meta = (DisplayName = "Capture Source"))
		TEnumAsByte<enum ESceneCaptureSource> CaptureSource;

	/** When enabled, the scene capture will composite into the render target instead of overwriting its contents. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SceneCapture)
		TEnumAsByte<enum ESceneCaptureCompositeMode> CompositeMode;

	/** Whether a custom projection matrix will be used during rendering. Use with caution. Does not currently affect culling */
	UPROPERTY(BlueprintReadWrite, AdvancedDisplay, Category = Projection)
		bool bUseCustomProjectionMatrix;

	/** The custom projection matrix to use */
	UPROPERTY(BlueprintReadWrite, AdvancedDisplay, Category = Projection)
		FMatrix CustomProjectionMatrix;

	/** Scales the distance used by LOD. Set to values greater than 1 to cause the scene capture to use lower LODs than the main view to speed up the scene capture pass. */
	UPROPERTY(EditAnywhere, Category = PlanarReflection, meta = (UIMin = ".1", UIMax = "10"), AdvancedDisplay)
		float LODDistanceFactor;

	/** if > 0, sets a maximum render distance override.  Can be used to cull distant objects from a reflection if the reflecting plane is in an enclosed area like a hallway or room */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SceneCapture, meta = (UIMin = "100", UIMax = "10000"))
		float MaxViewDistanceOverride;

	/**
	* Enables a clip plane while rendering the scene capture which is useful for portals.
	* The global clip plane must be enabled in the renderer project settings for this to work.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = SceneCapture)
		bool bEnableClipPlane;

	/** Base position for the clip plane, can be any position on the plane. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = SceneCapture)
		FVector ClipPlaneBase;

	/** Normal for the plane. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = SceneCapture)
		FVector ClipPlaneNormal;

	/** Capture priority within the frame to sort scene capture on GPU to resolve interdependencies between multiple capture components. Highest come first. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = SceneCapture)
		int32 CaptureSortPriority;

	/**
	* True if we did a camera cut this frame. Automatically reset to false at every capture.
	* This flag affects various things in the renderer (such as whether to use the occlusion queries from last frame, and motion blur).
	* Similar to UPlayerCameraManager::bGameCameraCutThisFrame.
	*/
	UPROPERTY(Transient, BlueprintReadWrite, Category = SceneCapture)
		uint32 bCameraCutThisFrame : 1;

	/** Adds the component to our list of hidden components. */
	UFUNCTION(BlueprintCallable, Category = "Rendering|SceneCapture")
		void HideComponent(UPrimitiveComponent* InComponent);

	/** Adds all primitive components in the actor to our list of hidden components. */
	UFUNCTION(BlueprintCallable, Category = "Rendering|SceneCapture")
		void HideActorComponents(AActor* InActor);

	/** Adds the component to our list of show-only components. */
	UFUNCTION(BlueprintCallable, Category = "Rendering|SceneCapture")
		void ShowOnlyComponent(UPrimitiveComponent* InComponent);

	/** Adds all primitive components in the actor to our list of show-only components. */
	UFUNCTION(BlueprintCallable, Category = "Rendering|SceneCapture")
		void ShowOnlyActorComponents(AActor* InActor);

	/** Removes a component from the Show Only list. */
	UFUNCTION(BlueprintCallable, Category = "Rendering|SceneCapture")
		void RemoveShowOnlyComponent(UPrimitiveComponent* InComponent);

	/** Removes a actor's components from the Show Only list. */
	UFUNCTION(BlueprintCallable, Category = "Rendering|SceneCapture")
		void RemoveShowOnlyActorComponents(AActor* InActor);

	/** Clears the Show Only list. */
	UFUNCTION(BlueprintCallable, Category = "Rendering|SceneCapture")
		void ClearShowOnlyComponents(UPrimitiveComponent* InComponent);

	/** Clears the hidden list. */
	UFUNCTION(BlueprintCallable, Category = "Rendering|SceneCapture")
		void ClearHiddenComponents();

	UFUNCTION(BlueprintCallable, Category = "Rendering|SceneCapture")
		void CaptureScene();

#if WITH_EDITOR
	virtual bool CanEditChange(const UProperty* InProperty) const override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif	
	
	
};
