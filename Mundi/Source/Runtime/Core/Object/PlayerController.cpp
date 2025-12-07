#include "pch.h"
#include "PlayerController.h"
#include "Pawn.h"
#include "CameraComponent.h"
#include "SpringArmComponent.h"
#include "TargetingComponent.h"
#include "InputComponent.h"
#include <windows.h>
#include <cmath>
#include "Character.h"
#include "CharacterMovementComponent.h"
#include "Source/Runtime/Game/Player/PlayerCharacter.h"
#include "SkeletalMeshComponent.h"
#include "Source/Runtime/Engine/Animation/AnimInstance.h"

APlayerController::APlayerController()
{
    // Create targeting component
    TargetingComponent = CreateDefaultSubobject<UTargetingComponent>("TargetingComponent");

    // Create input component
    InputComponent = CreateDefaultSubobject<UInputComponent>("InputComponent");
}

APlayerController::~APlayerController()
{
}

void APlayerController::BeginPlay()
{
    Super::BeginPlay();

    // 입력 설정
    SetupInput();
}

void APlayerController::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    if (Pawn == nullptr) return;

    // 매 프레임 축 입력 초기화 (콜백에서 갱신됨)
    MoveForwardValue = 0.0f;
    MoveRightValue = 0.0f;
    LookUpValue = 0.0f;
    LookRightValue = 0.0f;

    // InputComponent 입력 처리
    if (InputComponent)
    {
        InputComponent->TickComponent(DeltaSeconds);
    }

    // 이동 적용
    ApplyMovement(DeltaSeconds);

    // 회전 처리
    ProcessRotationInput(DeltaSeconds);

    // Lock-on 상태 동기화
    bIsLockOn = TargetingComponent && TargetingComponent->IsLockedOn();
}

void APlayerController::SetupInput()
{
    UE_LOG("[PlayerController] SetupInput() called!");

    if (!InputComponent) return;

    // 기존 바인딩 클리어 (중복 방지)
    InputComponent->ClearAllBindings();

    // ========================================================================
    // 키 매핑 설정
    // ========================================================================

    // 이동 축
    InputComponent->MapAxisToKey(FName("MoveForward"), 'W', 1.0f);
    InputComponent->MapAxisToKey(FName("MoveForward"), 'S', -1.0f);
    InputComponent->MapAxisToKey(FName("MoveRight"), 'D', 1.0f);
    InputComponent->MapAxisToKey(FName("MoveRight"), 'A', -1.0f);

    // 마우스 축
    InputComponent->MapAxisToMouseX(FName("LookRight"), 1.0f);
    InputComponent->MapAxisToMouseY(FName("LookUp"), 1.0f);

    // 액션 키
    InputComponent->MapActionToKey(FName("Jump"), 'F');
    InputComponent->MapActionToKey(FName("Dodge"), VK_SPACE);
    InputComponent->MapActionToKey(FName("ToggleLockOn"), VK_MBUTTON);
    InputComponent->MapActionToKey(FName("SwitchTargetLeft"), 'Q');
    InputComponent->MapActionToKey(FName("DashAttack"), 'E');
    InputComponent->MapActionToKey(FName("UltimateAttack"), 'R');
    InputComponent->MapActionToKey(FName("ToggleMouseLook"), VK_F11);
    InputComponent->MapActionToKey(FName("Attack"), VK_LBUTTON);
    InputComponent->MapActionToKey(FName("Sprint"), VK_SHIFT);
    InputComponent->MapActionToKey(FName("Block"), VK_RBUTTON);
    InputComponent->MapActionToKey(FName("Charging"), 'Y');

    // ========================================================================
    // 바인딩 설정
    // ========================================================================

    // 축 바인딩
    InputComponent->BindAxis(FName("MoveForward"), this, &APlayerController::OnMoveForward);
    InputComponent->BindAxis(FName("MoveRight"), this, &APlayerController::OnMoveRight);
    InputComponent->BindAxis(FName("LookUp"), this, &APlayerController::OnLookUp);
    InputComponent->BindAxis(FName("LookRight"), this, &APlayerController::OnLookRight);

    // 액션 바인딩
    InputComponent->BindAction(FName("Jump"), EInputEvent::Pressed, this, &APlayerController::OnJump);
    InputComponent->BindAction(FName("Jump"), EInputEvent::Released, this, &APlayerController::OnStopJump);
    InputComponent->BindAction(FName("Dodge"), EInputEvent::Pressed, this, &APlayerController::OnDodge);
    InputComponent->BindAction(FName("ToggleLockOn"), EInputEvent::Pressed, this, &APlayerController::OnToggleLockOn);
    InputComponent->BindAction(FName("SwitchTargetLeft"), EInputEvent::Pressed, this, &APlayerController::OnSwitchTargetLeft);
    InputComponent->BindAction(FName("ToggleMouseLook"), EInputEvent::Pressed, this, &APlayerController::OnToggleMouseLook);
    InputComponent->BindAction(FName("Attack"), EInputEvent::Pressed, this, &APlayerController::OnAttack);
    InputComponent->BindAction(FName("DashAttack"), EInputEvent::Pressed, this, &APlayerController::OnDashAttack);
    InputComponent->BindAction(FName("UltimateAttack"), EInputEvent::Pressed, this, &APlayerController::OnUltimateAttack);
    InputComponent->BindAction(FName("Sprint"), EInputEvent::Pressed, this, &APlayerController::OnStartSprint);
    InputComponent->BindAction(FName("Sprint"), EInputEvent::Released, this, &APlayerController::OnStopSprint);
    InputComponent->BindAction(FName("Block"), EInputEvent::Pressed, this, &APlayerController::OnStartBlock);
    InputComponent->BindAction(FName("Block"), EInputEvent::Released, this, &APlayerController::OnStopBlock);
    InputComponent->BindAction(FName("Charging"), EInputEvent::Pressed, this, &APlayerController::OnStartCharging);
    InputComponent->BindAction(FName("Charging"), EInputEvent::Released, this, &APlayerController::OnStopCharging);
}

// ============================================================================
// 축 입력 콜백
// ============================================================================

void APlayerController::OnMoveForward(float Value)
{
    MoveForwardValue = Value;
}

void APlayerController::OnMoveRight(float Value)
{
    MoveRightValue = Value;
}

void APlayerController::OnLookUp(float Value)
{
    LookUpValue = Value;
}

void APlayerController::OnLookRight(float Value)
{
    LookRightValue = Value;
}

// ============================================================================
// 액션 입력 콜백
// ============================================================================

void APlayerController::OnJump()
{
    // 사망 시 점프 불가
    if (auto* PlayerChar = Cast<APlayerCharacter>(Pawn))
    {
        if (!PlayerChar->IsAlive())
        {
            return;
        }
    }

    if (auto* Character = Cast<ACharacter>(Pawn))
    {
        Character->Jump();
    }
}

void APlayerController::OnStopJump()
{
    if (auto* Character = Cast<ACharacter>(Pawn))
    {
        Character->StopJumping();
    }
}

void APlayerController::OnDodge()
{
    if (auto* PlayerChar = Cast<APlayerCharacter>(Pawn))
    {
        PlayerChar->Dodge();
    }
}

void APlayerController::OnToggleLockOn()
{
    if (!TargetingComponent) return;

    bool bWasLockedOn = TargetingComponent->IsLockedOn();
    TargetingComponent->ToggleLockOn();
    bool bIsNowLockedOn = TargetingComponent->IsLockedOn();

    // When unlocking, sync ControlRotation to current camera rotation
    if (bWasLockedOn && !bIsNowLockedOn && Pawn)
    {
        if (UActorComponent* C = Pawn->GetComponent(USpringArmComponent::StaticClass()))
        {
            if (USpringArmComponent* SpringArm = Cast<USpringArmComponent>(C))
            {
                FQuat SpringArmRot = SpringArm->GetWorldRotation();
                SetControlRotation(SpringArmRot);
            }
        }
    }
}

void APlayerController::OnSwitchTargetLeft()
{
    if (TargetingComponent && TargetingComponent->IsLockedOn())
    {
        TargetingComponent->SwitchTarget(ETargetSwitchDirection::Left);
    }
}

void APlayerController::OnSwitchTargetRight()
{
    if (TargetingComponent && TargetingComponent->IsLockedOn())
    {
        TargetingComponent->SwitchTarget(ETargetSwitchDirection::Right);
    }
}

void APlayerController::OnToggleMouseLook()
{
    UInputManager& IM = UInputManager::GetInstance();

    bMouseLookEnabled = !bMouseLookEnabled;
    if (bMouseLookEnabled)
    {
        IM.SetCursorVisible(false);
        IM.LockCursor();
        IM.LockCursorToCenter();
    }
    else
    {
        IM.SetCursorVisible(true);
        IM.ReleaseCursor();
    }
}

void APlayerController::OnAttack()
{
    if (auto* PlayerChar = Cast<APlayerCharacter>(Pawn))
    {
        PlayerChar->LightAttack();
    }
}

void APlayerController::OnDashAttack()
{
    if (auto* PlayerChar = Cast<APlayerCharacter>(Pawn))
    {
        PlayerChar->DashAttack();
    }
}

void APlayerController::OnUltimateAttack()
{
    if (auto* PlayerChar = Cast<APlayerCharacter>(Pawn))
    {
        PlayerChar->UltimateAttack();
    }
}

void APlayerController::OnStartSprint()
{
    // 사망 시 스프린트 불가
    if (auto* PlayerChar = Cast<APlayerCharacter>(Pawn))
    {
        if (!PlayerChar->IsAlive())
        {
            return;
        }
    }

    if (auto* Character = Cast<ACharacter>(Pawn))
    {
        if (auto* MovementComp = Character->GetCharacterMovement())
        {
            MovementComp->SetSprinting(true);
        }
    }
}

void APlayerController::OnStopSprint()
{
    if (auto* Character = Cast<ACharacter>(Pawn))
    {
        if (auto* MovementComp = Character->GetCharacterMovement())
        {
            MovementComp->SetSprinting(false);
        }
    }
}

void APlayerController::OnStartBlock()
{
    if (auto* PlayerChar = Cast<APlayerCharacter>(Pawn))
    {
        PlayerChar->StartBlock();
    }
}

void APlayerController::OnStopBlock()
{
    if (auto* PlayerChar = Cast<APlayerCharacter>(Pawn))
    {
        PlayerChar->StopBlock();
    }
}

void APlayerController::OnStartCharging()
{
    if (auto* PlayerChar = Cast<APlayerCharacter>(Pawn))
    {
        PlayerChar->StartCharging();
    }
}

void APlayerController::OnStopCharging()
{
    if (auto* PlayerChar = Cast<APlayerCharacter>(Pawn))
    {
        PlayerChar->StopCharging();
    }
}

// ============================================================================
// 이동 적용
// ============================================================================

void APlayerController::ApplyMovement(float DeltaTime)
{
    if (!Pawn) return;

    // 사망 시 이동 불가
    if (auto* PlayerChar = Cast<APlayerCharacter>(Pawn))
    {
        if (!PlayerChar->IsAlive())
        {
            return;
        }
    }

    // 몽타주 재생 중이면 이동 입력 무시
    bool bIsMontaguePlaying = false;
    if (auto* PlayerChar = Cast<APlayerCharacter>(Pawn))
    {
        if (USkeletalMeshComponent* Mesh = PlayerChar->GetMesh())
        {
            if (UAnimInstance* AnimInst = Mesh->GetAnimInstance())
            {
                bIsMontaguePlaying = AnimInst->Montage_IsPlaying();
            }
        }
    }

    if (bIsMontaguePlaying)
    {
        return;
    }

    // 입력 방향 계산
    FVector InputDir = FVector(MoveForwardValue, MoveRightValue, 0.0f);

    if (InputDir.IsZero())
    {
        return;
    }

    InputDir.Normalize();

    // 카메라 기준 월드 방향 계산
    FVector WorldDir;
    bool bIsLockedOn = TargetingComponent && TargetingComponent->IsLockedOn();

    if (bIsLockedOn)
    {
        // Lock-on 상태: SpringArm 회전 기준
        if (UActorComponent* C = Pawn->GetComponent(USpringArmComponent::StaticClass()))
        {
            if (USpringArmComponent* SpringArm = Cast<USpringArmComponent>(C))
            {
                FVector SpringArmEuler = SpringArm->GetWorldRotation().ToEulerZYXDeg();
                FQuat YawOnlyRotation = FQuat::MakeFromEulerZYX(FVector(0.0f, 0.0f, SpringArmEuler.Z));
                WorldDir = YawOnlyRotation.RotateVector(InputDir);
            }
            else
            {
                WorldDir = InputDir;
            }
        }
        else
        {
            WorldDir = InputDir;
        }
    }
    else
    {
        // 일반 이동: ControlRotation 기준
        FVector ControlEuler = GetControlRotation().ToEulerZYXDeg();
        FQuat YawOnlyRotation = FQuat::MakeFromEulerZYX(FVector(0.0f, 0.0f, ControlEuler.Z));
        WorldDir = YawOnlyRotation.RotateVector(InputDir);
    }

    WorldDir.Z = 0.0f;
    WorldDir.Normalize();

    // 회전 처리
    if (bIsLockedOn)
    {
        ProcessLockedMovement(DeltaTime, WorldDir);
    }
    else
    {
        // 이동 방향으로 캐릭터 회전
        float TargetYaw = std::atan2(WorldDir.Y, WorldDir.X) * (180.0f / PI);
        FQuat TargetRotation = FQuat::MakeFromEulerZYX(FVector(0.0f, 0.0f, TargetYaw));

        FQuat CurrentRotation = Pawn->GetActorRotation();
        FQuat NewRotation = FQuat::Slerp(CurrentRotation, TargetRotation, FMath::Clamp(DeltaTime * 3.0f, 0.0f, 1.0f));
        Pawn->SetActorRotation(NewRotation);
    }

    // 이동 적용
    Pawn->AddMovementInput(WorldDir * (Pawn->GetVelocity() * DeltaTime));
}

// ============================================================================
// 회전 처리
// ============================================================================

void APlayerController::ProcessRotationInput(float DeltaTime)
{
    if (!bMouseLookEnabled)
        return;

    bool bIsLockedOn = TargetingComponent && TargetingComponent->IsLockedOn();

    // Lock-on 상태가 아닐 때만 마우스 입력으로 ControlRotation 업데이트
    if (!bIsLockedOn)
    {
        if (LookRightValue != 0.0f || LookUpValue != 0.0f)
        {
            FVector Euler = GetControlRotation().ToEulerZYXDeg();

            // Yaw (좌우 회전)
            Euler.Z += LookRightValue * Sensitivity;

            // Pitch (상하 회전)
            Euler.Y += LookUpValue * Sensitivity;
            Euler.Y = FMath::Clamp(Euler.Y, -89.0f, 89.0f);

            // Roll 방지
            Euler.X = 0.0f;

            FQuat NewControlRotation = FQuat::MakeFromEulerZYX(Euler);
            SetControlRotation(NewControlRotation);
        }
    }

    // SpringArm 처리
    if (UActorComponent* C = Pawn->GetComponent(USpringArmComponent::StaticClass()))
    {
        if (USpringArmComponent* SpringArm = Cast<USpringArmComponent>(C))
        {
            if (bIsLockedOn)
            {
                SpringArm->SetLockOnTarget(TargetingComponent->GetLockedTarget());
            }
            else
            {
                SpringArm->ClearLockOnTarget();
                FVector Euler = GetControlRotation().ToEulerZYXDeg();
                FQuat SpringArmRot = FQuat::MakeFromEulerZYX(FVector(0.0f, Euler.Y, Euler.Z));
                SpringArm->SetWorldRotation(SpringArmRot);
            }
        }
    }
}

void APlayerController::ProcessLockedMovement(float DeltaTime, const FVector& WorldMoveDir)
{
    if (!Pawn || !TargetingComponent || !TargetingComponent->IsLockedOn()) return;

    // 타겟 방향으로 캐릭터 회전
    float TargetYaw = TargetingComponent->GetYawToTarget();
    FQuat TargetRotation = FQuat::MakeFromEulerZYX(FVector(0.0f, 0.0f, TargetYaw));

    // 부드러운 회전 (보간)
    FQuat CurrentRotation = Pawn->GetActorRotation();
    FQuat NewRotation = FQuat::Slerp(CurrentRotation, TargetRotation,
                                     FMath::Clamp(DeltaTime * CharacterRotationSpeed, 0.0f, 1.0f));
    Pawn->SetActorRotation(NewRotation);
}
