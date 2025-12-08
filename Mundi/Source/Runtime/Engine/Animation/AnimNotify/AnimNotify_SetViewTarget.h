#pragma once

#include "AnimNotify.h"
#include "Vector.h"
#include "Name.h"

UCLASS(DisplayName = "Set View Target (Blend)")
class UAnimNotify_SetViewTarget : public UAnimNotify
{
public:
    DECLARE_CLASS(UAnimNotify_SetViewTarget, UAnimNotify)

    UAnimNotify_SetViewTarget();

    virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation) override;

    /** Time to blend cameras. */
    UPROPERTY(EditAnywhere, Category = "Camera Animation", meta = (ClampMin = "0.0"))
    float BlendTime = 1.0f;

    /** If true, this notify will blend back to the player's default follow camera. 
        If false, it will blend to a new temporary camera defined by the relative transform below. */
    UPROPERTY(EditAnywhere, Category = "Camera Animation")
    bool bBlendBackToDefault = false;

    /** The target location of the new camera, relative to the player's initial spawn transform. (Only used if bBlendBackToDefault is false) */
    UPROPERTY(EditAnywhere, Category = "Camera Animation")
    FVector RelativeLocation;

    /** The target rotation of the new camera, relative to the player's initial spawn transform. (Only used if bBlendBackToDefault is false) */
    UPROPERTY(EditAnywhere, Category = "Camera Animation")
    FQuat RelativeRotation;
};
