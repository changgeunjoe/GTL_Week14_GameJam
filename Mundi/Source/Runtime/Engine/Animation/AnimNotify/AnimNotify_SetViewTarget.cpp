#include "pch.h"
#include "AnimNotify_SetViewTarget.h"
#include "Source/Runtime/Engine/Components/SkeletalMeshComponent.h"
#include "Source/Runtime/Engine/Animation/AnimInstance.h"
#include "Source/Runtime/Core/Object/Actor.h"
#include "Source/Runtime/Engine/GameFramework/World.h"
#include "Source/Runtime/Engine/GameFramework/PlayerCameraManager.h"
#include "Source/Runtime/Engine/GameFramework/GameModeBase.h"
#include "Source/Runtime/Engine/GameFramework/GameState.h"
#include "Source/Runtime/Engine/GameFramework/CameraActor.h"
#include "Source/Runtime/Engine/Components/CameraComponent.h"
#include "Source/Runtime/Game/Player/PlayerCharacter.h"

IMPLEMENT_CLASS(UAnimNotify_SetViewTarget)

UAnimNotify_SetViewTarget::UAnimNotify_SetViewTarget()
{
    // Initialize properties with default values
    BlendTime = 1.0f;
    bBlendBackToDefault = false;
    RelativeLocation = FVector(0.f, -500.f, 200.f);
    RelativeRotation = FQuat(0.f, 0.f, 0.f, 0.f);
}

void UAnimNotify_SetViewTarget::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation)
{
    Super::Notify(MeshComp, Animation);

    // --- Get all necessary objects with validation ---
    if (!MeshComp) return;
    
    UAnimInstance* AnimInstance = MeshComp->GetAnimInstance();
    UWorld* World = MeshComp->GetWorld();
    APlayerCharacter* Player = Cast<APlayerCharacter>(MeshComp->GetOwner());

    if (!AnimInstance || !World || !Player)
    {
        UE_LOG("[AnimNotify_SetViewTarget] Error: AnimInstance, World, or Player is not valid.");
        return;
    }

    // --- Caching Logic for CameraManager ---
   
    APlayerCameraManager* CameraManager = GWorld->GetPlayerCameraManager();
    if (!CameraManager)
    {
        UE_LOG("[AnimNotify_SetViewTarget] Error: Cached PlayerCameraManager is not valid.");
        return;
    }

    // --- Branch based on the notify's role ---
    if (bBlendBackToDefault)
    {
        // ROLE: End Notify - Blend back to the default player camera
        
        // 1. Get the original camera from the anim instance cache
        UCameraComponent* OriginalPlayerCamera = CameraManager->GetSpringArmCamera();
        if (!OriginalPlayerCamera)
        {
            return;
        }

        // 2. Blend view back to the original camera
        UE_LOG("[AnimNotify_SetViewTarget] Blending back to original camera (BlendTime: %.2fs)", BlendTime);
        CameraManager->SetViewCameraWithBlend(OriginalPlayerCamera, BlendTime);

        // 3. Find and destroy the temporary camera actor stored from the start notify
        if (AnimInstance->TempViewTarget.IsValid())
        {
            UE_LOG("[AnimNotify_SetViewTarget] Destroying temporary camera actor.");
            AnimInstance->TempViewTarget->Destroy();
        }

        // 4. Clear the pointers in the anim instance
        AnimInstance->TempViewTarget = nullptr;
        AnimInstance->CachedOriginalPlayerCamera = nullptr;
    }
    else
    {
        // ROLE: Start Notify - Blend to a new temporary camera
        
        // 0. Cache the current camera before it changes
        AnimInstance->CachedOriginalPlayerCamera = TWeakObjectPtr<UCameraComponent>(CameraManager->GetViewCamera());
        UE_LOG("[AnimNotify_SetViewTarget] Cached original player camera.");

        AGameState* GameState = World->GetGameMode() ? Cast<AGameState>(World->GetGameMode()->GetGameState()) : nullptr;
        if (!GameState)
        {
            UE_LOG("[AnimNotify_SetViewTarget] Error: GameState not found.");
            // Clear cache if we fail
            AnimInstance->CachedOriginalPlayerCamera = nullptr;
            return;
        }
        
        // 1. Spawn a new temporary camera actor
        ACameraActor* NewCamActor = World->SpawnActor<ACameraActor>();
        if (!NewCamActor)
        {
            UE_LOG("[AnimNotify_SetViewTarget] Error: Failed to spawn ACameraActor.");
            // Clear cache if we fail
            AnimInstance->CachedOriginalPlayerCamera = nullptr;
            return;
        }

        // 2. Get the character's transform AT THE MOMENT OF THE NOTIFY
        const FTransform& OriginTransform = Player->GetActorTransform();

        // 3. Calculate the target world transform for the new camera
        FTransform RelativeTransform(RelativeLocation, RelativeRotation, FVector(1.0f, 1.0f, 1.0f));
        FTransform TargetWorldTransform = RelativeTransform.ToMatrix() * OriginTransform.ToMatrix();
        NewCamActor->SetActorTransform(TargetWorldTransform);

        // 4. Store a weak pointer to this new camera in the AnimInstance for the 'end' notify to find
        AnimInstance->TempViewTarget = TWeakObjectPtr<ACameraActor>(NewCamActor);

        // 5. Blend to the new camera
        UCameraComponent* NewCamComp = NewCamActor->GetCameraComponent();
        UE_LOG("[AnimNotify_SetViewTarget] Blending to new temp camera (BlendTime: %.2fs)", BlendTime);
        CameraManager->SetViewCameraWithBlend(NewCamComp, BlendTime);
    }
}
