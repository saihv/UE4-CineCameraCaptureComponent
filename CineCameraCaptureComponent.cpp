// Fill out your copyright notice in the Description page of Project Settings.

#include "CineCameraCaptureComponent.h"
#include "Engine/World.h"
#include "StereoRendering.h"
#include "SceneInterface.h"
#include "Logging/MessageLog.h"
#include "SceneManagement.h"

#define LOCTEXT_NAMESPACE "CineCameraCaptureComponent"

static TMultiMap<TWeakObjectPtr<UWorld>, TWeakObjectPtr<UCineCameraCaptureComponent> > SceneCapturesToUpdateMap;

UCineCameraCaptureComponent::UCineCameraCaptureComponent() : Super(), ShowFlags(ESFIM_Game)
{
	bCaptureEveryFrame = true;
	bCaptureOnMovement = true;
	bAutoActivate = true;
	bTickInEditor = true;
	bAlwaysPersistRenderingState = false;
	bUseCustomProjectionMatrix = false;
	CaptureSource = SCS_SceneColorHDR;
	CustomProjectionMatrix.SetIdentity();
	ProjectionType = ECameraProjectionMode::Perspective;
	LODDistanceFactor = 1.0f;
	MaxViewDistanceOverride = -1;
	ClipPlaneNormal = FVector(0, 0, 1);
	bCameraCutThisFrame = false;
	bEnableClipPlane = false;
	CaptureSortPriority = 0;
	CaptureStereoPass = EStereoscopicPass::eSSP_FULL;

	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_DuringPhysics;

	// Disable features that are not desired when capturing the scene
	ShowFlags.SetMotionBlur(0); // motion blur doesn't work correctly with scene captures.
	ShowFlags.SetSeparateTranslucency(0);
	ShowFlags.SetHMDDistortion(0);
}

void UCineCameraCaptureComponent::OnRegister()
{
	Super::OnRegister();

	// Make sure any loaded saved flag settings are reflected in our FEngineShowFlags
	UpdateShowFlags();
#if WITH_EDITOR
	// Update content on register to have at least one frames worth of good data.
	// Without updating here this component would not work in a blueprint construction script which recreates the component after each move in the editor
	CaptureSceneDeferred();
#endif
}

void UCineCameraCaptureComponent::UpdateShowFlags()
{
	UCineCameraCaptureComponent* Archetype = Cast<UCineCameraCaptureComponent>(GetArchetype());
	if (Archetype)
	{
		ShowFlags = Archetype->ShowFlags;
	}

	for (FEngineShowFlagsSetting ShowFlagSetting : ShowFlagSettings)
	{
		int32 SettingIndex = ShowFlags.FindIndexByName(*(ShowFlagSetting.ShowFlagName));
		if (SettingIndex != INDEX_NONE)
		{
			ShowFlags.SetSingleFlag(SettingIndex, ShowFlagSetting.Enabled);
		}
	}
}

void UCineCameraCaptureComponent::OnUnregister()
{
	for (int32 ViewIndex = 0; ViewIndex < ViewStates.Num(); ViewIndex++)
	{
		ViewStates[ViewIndex].Destroy();
	}

	Super::OnUnregister();
}

void UCineCameraCaptureComponent::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FRenderingObjectVersion::GUID);

	if (Ar.CustomVer(FRenderingObjectVersion::GUID) < FRenderingObjectVersion::AddedbUseShowOnlyList)
	{
		if (ShowOnlyActors.Num() > 0 || ShowOnlyComponents.Num() > 0)
		{
			PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
		}
	}

	if (Ar.IsLoading())
	{

#if WITH_EDITORONLY_DATA
		PostProcessSettings.OnAfterLoad();
#endif

		if (Ar.CustomVer(FRenderingObjectVersion::GUID) < FRenderingObjectVersion::MotionBlurAndTAASupportInSceneCapture2d)
		{
			ShowFlags.TemporalAA = false;
			ShowFlags.MotionBlur = false;
		}
	}
}

#if WITH_EDITOR

bool UCineCameraCaptureComponent::CanEditChange(const UProperty* InProperty) const
{
	if (InProperty)
	{
		FString PropertyName = InProperty->GetName();

		if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(USceneCaptureComponent, HiddenActors))
		{
			return PrimitiveRenderMode == ESceneCapturePrimitiveRenderMode::PRM_LegacySceneCapture ||
				PrimitiveRenderMode == ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
		}
		else if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(USceneCaptureComponent, ShowOnlyActors))
		{
			return PrimitiveRenderMode == ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
		}
	}


	return true;
}

void UCineCameraCaptureComponent::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName Name = (PropertyChangedEvent.Property != nullptr) ? PropertyChangedEvent.Property->GetFName() : NAME_None;
	const FName MemberPropertyName = (PropertyChangedEvent.MemberProperty != NULL) ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;

	// If our ShowFlagSetting UStruct changed, (or if PostEditChange was called without specifying a property) update the actual show flags
	if (MemberPropertyName.IsEqual("ShowFlagSettings") || MemberPropertyName.IsNone())
	{
		UpdateShowFlags();
	}

	CaptureSceneDeferred();
}
#endif

void UCineCameraCaptureComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bCaptureEveryFrame)
	{
		CaptureSceneDeferred();
	}
}

void UCineCameraCaptureComponent::SendRenderTransform_Concurrent()
{
	if (bCaptureOnMovement)
	{
		CaptureSceneDeferred();
	}

	Super::SendRenderTransform_Concurrent();
}


void UCineCameraCaptureComponent::CaptureSceneDeferred()
{
	UWorld* World = GetWorld();
	if (World && World->Scene && IsVisible())
	{
		// Defer until after updates finish
		// Needs some CS because of parallel updates.
		UpdateCameraLensCapture(World->DeltaTimeSeconds);
		static FCriticalSection CriticalSection;
		FScopeLock ScopeLock(&CriticalSection);
		SceneCapturesToUpdateMap.AddUnique(World, this);
	}
}

void UCineCameraCaptureComponent::CaptureScene()
{
	UWorld* World = GetWorld();
	if (World && World->Scene && IsVisible())
	{
		World->SendAllEndOfFrameUpdates();
		World->Scene->UpdateSceneCaptureContents(this);
	}

	if (bCaptureEveryFrame)
	{
		FMessageLog("Blueprint").Warning(LOCTEXT("CaptureScene", "CaptureScene: Scene capture with bCaptureEveryFrame enabled was told to update - major inefficiency."));
	}
}

void UCineCameraCaptureComponent::UpdateCameraLensCapture(float DeltaTime)
{
	RecalcDerivedData();

	CameraLensPostProcessSettings = PostProcessSettings;

	if (FocusSettings.FocusMethod == ECameraFocusMethod::None)
	{
		CameraLensPostProcessSettings.bOverride_DepthOfFieldMethod = false;
		CameraLensPostProcessSettings.bOverride_DepthOfFieldFstop = false;
		CameraLensPostProcessSettings.bOverride_DepthOfFieldFocalDistance = false;
		CameraLensPostProcessSettings.bOverride_DepthOfFieldSensorWidth = false;
	}
	else
	{
		// Update focus/DoF
		CameraLensPostProcessSettings.bOverride_DepthOfFieldMethod = true;
		CameraLensPostProcessSettings.DepthOfFieldMethod = PostProcessSettings.DepthOfFieldMethod;

		CameraLensPostProcessSettings.bOverride_DepthOfFieldFstop = true;
		CameraLensPostProcessSettings.DepthOfFieldFstop = CurrentAperture;

		CurrentFocusDistance = GetDesiredFocusDistance(GetComponentLocation());

		// clamp to min focus distance
		float const MinFocusDistInWorldUnits = LensSettings.MinimumFocusDistance * (GetWorldToMetersScale() / 1000.f);	// convert mm to uu
		CurrentFocusDistance = FMath::Max(CurrentFocusDistance, MinFocusDistInWorldUnits);

		// smoothing, if desired
		if (FocusSettings.bSmoothFocusChanges)
		{
			if (bResetInterpolation == false)
			{
				CurrentFocusDistance = FMath::FInterpTo(LastFocusDistance, CurrentFocusDistance, DeltaTime, FocusSettings.FocusSmoothingInterpSpeed);
			}
		}
		LastFocusDistance = CurrentFocusDistance;

		CameraLensPostProcessSettings.bOverride_DepthOfFieldFocalDistance = true;
		CameraLensPostProcessSettings.DepthOfFieldFocalDistance = CurrentFocusDistance;

		CameraLensPostProcessSettings.bOverride_DepthOfFieldSensorWidth = true;
		CameraLensPostProcessSettings.DepthOfFieldSensorWidth = FilmbackSettings.SensorWidth;
	}

	bResetInterpolation = false;
}

void UCineCameraCaptureComponent::UpdateDeferredCaptures(FSceneInterface* Scene)
{
	UWorld* World = Scene->GetWorld();
	if (!World || SceneCapturesToUpdateMap.Num() == 0)
	{
		return;
	}

	TArray< TWeakObjectPtr<UCineCameraCaptureComponent> > CinemaCapturesToUpdate;
	SceneCapturesToUpdateMap.MultiFind(World, CinemaCapturesToUpdate);
	CinemaCapturesToUpdate.Sort([](const TWeakObjectPtr<UCineCameraCaptureComponent>& A, const TWeakObjectPtr<UCineCameraCaptureComponent>& B)
	{
		if (!A.IsValid())
		{
			return false;
		}
		else if (!B.IsValid())
		{
			return true;
		}
		return A->CaptureSortPriority > B->CaptureSortPriority;
	});

	for (TWeakObjectPtr<UCineCameraCaptureComponent> Component : CinemaCapturesToUpdate)
	{
		if (Component.IsValid())
		{
			Component->UpdateSceneCaptureContents(Scene);
		}
	}

	// All scene captures for this world have been updated
	SceneCapturesToUpdateMap.Remove(World);
}

void UCineCameraCaptureComponent::UpdateSceneCaptureContents(FSceneInterface* Scene)
{
	Scene->UpdateSceneCaptureContents(this);
}

FSceneViewStateInterface* UCineCameraCaptureComponent::GetViewState(int32 ViewIndex)
{
	if (ViewIndex >= ViewStates.Num())
	{
		ViewStates.AddZeroed(ViewIndex - ViewStates.Num() + 1);
	}

	FSceneViewStateInterface* ViewStateInterface = ViewStates[ViewIndex].GetReference();
	if ((bCaptureEveryFrame || bAlwaysPersistRenderingState) && ViewStateInterface == NULL)
	{
		ViewStates[ViewIndex].Allocate();
		ViewStateInterface = ViewStates[ViewIndex].GetReference();
	}
	else if (!bCaptureEveryFrame && ViewStateInterface && !bAlwaysPersistRenderingState)
	{
		ViewStates[ViewIndex].Destroy();
		ViewStateInterface = NULL;
	}
	return ViewStateInterface;
}

void UCineCameraCaptureComponent::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	UCineCameraCaptureComponent* This = CastChecked<UCineCameraCaptureComponent>(InThis);

	for (int32 ViewIndex = 0; ViewIndex < This->ViewStates.Num(); ViewIndex++)
	{
		FSceneViewStateInterface* Ref = This->ViewStates[ViewIndex].GetReference();
		if (Ref)
		{
			Ref->AddReferencedObjects(Collector);
		}
	}

	Super::AddReferencedObjects(This, Collector);
}

void UCineCameraCaptureComponent::HideComponent(UPrimitiveComponent* InComponent)
{
	if (InComponent)
	{
		HiddenComponents.AddUnique(InComponent);
	}
}

void UCineCameraCaptureComponent::HideActorComponents(AActor* InActor)
{
	if (InActor)
	{
		TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents;
		InActor->GetComponents(PrimitiveComponents);
		for (int32 ComponentIndex = 0, NumComponents = PrimitiveComponents.Num(); ComponentIndex < NumComponents; ++ComponentIndex)
		{
			HiddenComponents.AddUnique(PrimitiveComponents[ComponentIndex]);
		}
	}
}

void UCineCameraCaptureComponent::ShowOnlyComponent(UPrimitiveComponent* InComponent)
{
	if (InComponent)
	{
		// Backward compatibility - set PrimitiveRenderMode to PRM_UseShowOnlyList if BP / game code tries to add a ShowOnlyComponent
		PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
		ShowOnlyComponents.Add(InComponent);
	}
}

void UCineCameraCaptureComponent::ShowOnlyActorComponents(AActor* InActor)
{
	if (InActor)
	{
		// Backward compatibility - set PrimitiveRenderMode to PRM_UseShowOnlyList if BP / game code tries to add a ShowOnlyComponent
		PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;

		TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents;
		InActor->GetComponents(PrimitiveComponents);
		for (int32 ComponentIndex = 0, NumComponents = PrimitiveComponents.Num(); ComponentIndex < NumComponents; ++ComponentIndex)
		{
			ShowOnlyComponents.Add(PrimitiveComponents[ComponentIndex]);
		}
	}
}

void UCineCameraCaptureComponent::RemoveShowOnlyComponent(UPrimitiveComponent* InComponent)
{
	ShowOnlyComponents.Remove(InComponent);
}

void UCineCameraCaptureComponent::RemoveShowOnlyActorComponents(AActor* InActor)
{
	if (InActor)
	{
		TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents;
		InActor->GetComponents(PrimitiveComponents);
		for (int32 ComponentIndex = 0, NumComponents = PrimitiveComponents.Num(); ComponentIndex < NumComponents; ++ComponentIndex)
		{
			ShowOnlyComponents.Remove(PrimitiveComponents[ComponentIndex]);
		}
	}
}

void UCineCameraCaptureComponent::ClearShowOnlyComponents(UPrimitiveComponent* InComponent)
{
	ShowOnlyComponents.Reset();
}

void UCineCameraCaptureComponent::ClearHiddenComponents()
{
	HiddenComponents.Reset();
}


#undef LOCTEXT_NAMESPACE
