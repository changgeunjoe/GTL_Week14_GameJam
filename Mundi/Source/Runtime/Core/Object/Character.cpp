#include "pch.h"
#include "Character.h"
#include "CapsuleComponent.h"
#include "SkeletalMeshComponent.h"
#include "CharacterMovementComponent.h"
#include "StaticMeshComponent.h"
#include "ObjectMacros.h"
#include "StaticMeshActor.h"
#include "World.h"
#include "Source/Runtime/Engine/Physics/PhysScene.h"
#include "Collision.h" 
ACharacter::ACharacter()
{
	CapsuleComponent = CreateDefaultSubobject<UCapsuleComponent>("CapsuleComponent");
	//CapsuleComponent->SetSize();
    WeaponMeshComp = CreateDefaultSubobject<UStaticMeshComponent>("WeaponMeshComponent");
	SetRootComponent(CapsuleComponent);

	SubWeaponMeshComp = CreateDefaultSubobject<UStaticMeshComponent>("SubWeaponMeshComponent");

	if (SkeletalMeshComp)
	{
		SkeletalMeshComp->SetupAttachment(CapsuleComponent);

		//SkeletalMeshComp->SetRelativeLocation(FVector());
		//SkeletalMeshComp->SetRelativeScale(FVector());
	}
    if (WeaponMeshComp)
    {
        WeaponMeshComp->SetupAttachment(SkeletalMeshComp);
    }
	 

	if (SubWeaponMeshComp)
	{
		SubWeaponMeshComp->SetupAttachment(SkeletalMeshComp);
	}
	 
	CharacterMovement = CreateDefaultSubobject<UCharacterMovementComponent>("CharacterMovement");
	if (CharacterMovement)
	{
		CharacterMovement->SetUpdatedComponent(CapsuleComponent);
	} 
}

ACharacter::~ACharacter()
{

}

void ACharacter::Tick(float DeltaSecond)
{
	Super::Tick(DeltaSecond);
    if (InitFrameCounter < 3) {
        InitFrameCounter++;
        return;
    }
	// 무기 트랜스폼 업데이트 (본 따라가기)
	UpdateWeaponTransform();

	// 무기 Sweep 판정 (활성화된 경우)
	if (bWeaponTraceActive)
	{
		PerformWeaponTrace();
	}
}

void ACharacter::BeginPlay()
{
    Super::BeginPlay();
}

void ACharacter::Serialize(const bool bInIsLoading, JSON& InOutHandle)
{
    Super::Serialize(bInIsLoading, InOutHandle);

    if (bInIsLoading)
    {
        // Rebind important component pointers after load (prefab/scene)
        CapsuleComponent = nullptr;
        CharacterMovement = nullptr;
        WeaponMeshComp = nullptr;
        SubWeaponMeshComp = nullptr;
        SkeletalMeshComp = nullptr;

        // 1차 패스: 기본 컴포넌트들 찾기 (CapsuleComponent, CharacterMovement, SkeletalMeshComp)
        for (UActorComponent* Comp : GetOwnedComponents())
        {
            if (auto* Move = Cast<UCharacterMovementComponent>(Comp))
            {
                CharacterMovement = Move;
            }
            else if (auto* Skel = Cast<USkeletalMeshComponent>(Comp))
            {
                SkeletalMeshComp = Skel;
            }
            else if (auto* Cap = Cast<UCapsuleComponent>(Comp))
            {
                // 부모가 없는 캡슐 = 루트 캡슐
                if (!Cap->GetAttachParent())
                {
                    CapsuleComponent = Cap;
                }
            }
        }

        // 2차 패스: 이름으로 무기 관련 컴포넌트 찾기
        for (UActorComponent* Comp : GetOwnedComponents())
        {
            if (auto* StaticMesh = Cast<UStaticMeshComponent>(Comp))
            {
                // 이름으로 구분
                FName CompName = Comp->GetName();
                if (CompName == FName("WeaponMeshComponent"))
                {
                    WeaponMeshComp = StaticMesh;
                }
                else if (CompName == FName("SubWeaponMeshComponent"))
                {
                    SubWeaponMeshComp = StaticMesh;
                }
            }
        }

        if (CharacterMovement)
        {
            USceneComponent* Updated = CapsuleComponent ? reinterpret_cast<USceneComponent*>(CapsuleComponent)
                                                        : GetRootComponent();
            CharacterMovement->SetUpdatedComponent(Updated);
        }
    }
}

void ACharacter::DuplicateSubObjects()
{
    Super::DuplicateSubObjects();

    CapsuleComponent = nullptr;
    CharacterMovement = nullptr;
    WeaponMeshComp = nullptr;
    SubWeaponMeshComp = nullptr;
    SkeletalMeshComp = nullptr;

    // 1차 패스: 기본 컴포넌트들 찾기
    for (UActorComponent* Comp : GetOwnedComponents())
    {
        if (auto* Move = Cast<UCharacterMovementComponent>(Comp))
        {
            CharacterMovement = Move;
        }
        else if (auto* Skel = Cast<USkeletalMeshComponent>(Comp))
        {
            SkeletalMeshComp = Skel;
        }
        else if (auto* Cap = Cast<UCapsuleComponent>(Comp))
        {
            // 부모가 없는 캡슐 = 루트 캡슐
            if (!Cap->GetAttachParent())
            {
                CapsuleComponent = Cap;
            }
        }
    }

    // 2차 패스: 부모-자식 관계로 무기 관련 컴포넌트 찾기
    for (UActorComponent* Comp : GetOwnedComponents())
    {
        if (auto* StaticMesh = Cast<UStaticMeshComponent>(Comp))
        {
            // SkeletalMeshComp의 자식 StaticMeshComponent = 무기
            USceneComponent* Parent = StaticMesh->GetAttachParent();
            if (Parent == SkeletalMeshComp)
            {
                if (!WeaponMeshComp)
                {
                    WeaponMeshComp = StaticMesh;
                }
                else if (!SubWeaponMeshComp)
                {
                    SubWeaponMeshComp = StaticMesh;
                }
            }
        }
    }

    // Ensure movement component tracks the correct updated component
    if (CharacterMovement)
    {
        USceneComponent* Updated = CapsuleComponent ? reinterpret_cast<USceneComponent*>(CapsuleComponent)
                                                    : GetRootComponent();
        CharacterMovement->SetUpdatedComponent(Updated);
    }
}

void ACharacter::Jump()
{
	if (CharacterMovement)
	{
		CharacterMovement->DoJump();
	}
}

void ACharacter::StopJumping()
{
	if (CharacterMovement)
	{
		// 점프 scale을 조절할 때 사용,
		// 지금은 비어있음
		CharacterMovement->StopJump();
	}
}

// ============================================================================
// 무기 어태치 시스템
// ============================================================================

void ACharacter::UpdateWeaponTransform()
{
	if (!WeaponMeshComp || !SkeletalMeshComp)
	{
		return;
	}

	// 스켈레탈 메시가 로드되었는지 확인
	if (!SkeletalMeshComp->GetSkeletalMesh())
	{
		return;
	}

	// 본 인덱스 찾기
	int32 BoneIndex = SkeletalMeshComp->GetBoneIndexByName(FName(WeaponBoneName));
	if (BoneIndex < 0)
	{
		return;
	}

	// 본의 월드 트랜스폼 가져오기
	FTransform BoneTransform = SkeletalMeshComp->GetBoneWorldTransform(BoneIndex);

	// 오프셋 적용
	FVector FinalLocation = BoneTransform.Translation +
		BoneTransform.Rotation.RotateVector(WeaponOffset);

	// 회전 오프셋 적용 (오일러 → 쿼터니언)
	FQuat RotOffset = FQuat::MakeFromEulerZYX(WeaponRotationOffset);
	FQuat FinalRotation = BoneTransform.Rotation * RotOffset;

	// 무기 트랜스폼 설정
	WeaponMeshComp->SetWorldLocation(FinalLocation);
	WeaponMeshComp->SetWorldRotation(FinalRotation);
}

// ============================================================================
// 무기 충돌 시스템 (Sweep 기반)
// ============================================================================

void ACharacter::StartWeaponTrace()
{
	// 기본 데미지 정보로 시작
	FDamageInfo DefaultDamage(this, 10.0f, EDamageType::Light);
	StartWeaponTrace(DefaultDamage);
}

void ACharacter::StartWeaponTrace(const FDamageInfo& InDamageInfo)
{
	if (!WeaponMeshComp)
	{
		return;
	}

	bWeaponTraceActive = true;
	HitActorsThisSwing.Empty();

	// 데미지 정보 설정
	CurrentWeaponDamageInfo = InDamageInfo;
	if (!CurrentWeaponDamageInfo.Instigator)
	{
		CurrentWeaponDamageInfo.Instigator = this;
	}

	// 현재 무기 위치를 이전 위치로 초기화
	FVector WeaponPos = WeaponMeshComp->GetWorldLocation();
	FQuat WeaponRot = WeaponMeshComp->GetWorldRotation();

	// 무기의 로컬 Z축 방향으로 베이스와 팁 위치 계산
	FVector WeaponUp = WeaponRot.RotateVector(FVector(0, 0, 1));
	PrevWeaponBasePos = WeaponPos;
	PrevWeaponTipPos = WeaponPos + WeaponUp * WeaponTraceLength;

	UE_LOG("[Character] Weapon trace started");
}

void ACharacter::EndWeaponTrace()
{
	bWeaponTraceActive = false;
	HitActorsThisSwing.Empty();
	ClearWeaponDebugData();

	UE_LOG("[Character] Weapon trace ended");
}

void ACharacter::PerformWeaponTrace()
{
	if (!WeaponMeshComp || !bWeaponTraceActive)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FPhysScene* PhysScene = World->GetPhysScene();
	if (!PhysScene)
	{
		return;
	}

	// 현재 무기 위치 계산
	FVector WeaponPos = WeaponMeshComp->GetWorldLocation();
	FQuat WeaponRot = WeaponMeshComp->GetWorldRotation();

	FVector WeaponUp = WeaponRot.RotateVector(FVector(0, 0, 1));
	FVector CurrentBasePos = WeaponPos;
	FVector CurrentTipPos = WeaponPos + WeaponUp * WeaponTraceLength;

	// ========== 디버그 데이터 저장 (RenderDebugVolume에서 렌더링) ==========
	if (bDrawWeaponDebug)
	{
		UpdateWeaponDebugData(CurrentBasePos, CurrentTipPos,
							  PrevWeaponBasePos, PrevWeaponTipPos, WeaponRot);
	}
	// ========== 디버그 데이터 저장 끝 ==========

	// 이전 프레임 → 현재 프레임 Sweep 수행
	// 베이스 위치와 팁 위치 모두에서 Sweep
	FHitResult HitResult;

	// 팁 위치 Sweep (무기 끝부분)
	if (PhysScene->SweepSphere(PrevWeaponTipPos, CurrentTipPos, WeaponTraceRadius, HitResult, this))
	{
		if (HitResult.HitActor && !HitActorsThisSwing.Contains(HitResult.HitActor))
		{
			HitActorsThisSwing.Add(HitResult.HitActor);
			OnWeaponHitDetected(HitResult.HitActor, HitResult.ImpactPoint, HitResult.ImpactNormal);
		}
	}

	// 베이스 위치 Sweep (무기 손잡이 쪽)
	if (PhysScene->SweepSphere(PrevWeaponBasePos, CurrentBasePos, WeaponTraceRadius, HitResult, this))
	{
		if (HitResult.HitActor && !HitActorsThisSwing.Contains(HitResult.HitActor))
		{
			HitActorsThisSwing.Add(HitResult.HitActor);
			OnWeaponHitDetected(HitResult.HitActor, HitResult.ImpactPoint, HitResult.ImpactNormal);
		}
	}

	// 현재 위치를 다음 프레임의 이전 위치로 저장
	PrevWeaponBasePos = CurrentBasePos;
	PrevWeaponTipPos = CurrentTipPos;
}

void ACharacter::OnWeaponHitDetected(AActor* HitActor, const FVector& HitLocation, const FVector& HitNormal)
{
	// 데미지 정보에 충돌 정보 추가
	FDamageInfo DamageInfo = CurrentWeaponDamageInfo;
	DamageInfo.HitLocation = HitLocation;
	DamageInfo.DamageCauser = WeaponMeshComp ? WeaponMeshComp->GetOwner() : this;

	// 공격 방향 계산 (공격자 → 피격자)
	FVector AttackerPos = GetActorLocation();
	DamageInfo.HitDirection = (HitLocation - AttackerPos).GetNormalized();

	// 충돌 위치에 StaticMeshActor 스폰 (디버그용)
	UWorld* World = GetWorld();
	if (World && bDrawWeaponDebug)
	{
		FTransform SpawnTransform;
		SpawnTransform.Translation = HitLocation;
		AStaticMeshActor* HitMarker = World->SpawnActor<AStaticMeshActor>(SpawnTransform);
		if (HitMarker)
		{
			HitMarker->GetStaticMeshComponent()->SetStaticMesh(GDataDir + "/Model/Sphere8.obj");
			HitMarker->SetActorScale(FVector(0.1f, 0.1f, 0.1f));
		}
	}

	UE_LOG("[Character] Weapon hit: %s at (%.2f, %.2f, %.2f) - Damage: %.1f",
		   HitActor->GetName().c_str(), HitLocation.X, HitLocation.Y, HitLocation.Z, DamageInfo.Damage);

	// 델리게이트 브로드캐스트 (FDamageInfo 전달)
	OnWeaponHit.Broadcast(HitActor, DamageInfo);
}

void ACharacter::UpdateSubWeaponTransform()
{
	if (!SubWeaponMeshComp || !SkeletalMeshComp)
	{
		return;
	}

	// 스켈레탈 메시가 로드되었는지 확인
	if (!SkeletalMeshComp->GetSkeletalMesh())
	{
		return;
	}

	// 본 인덱스 찾기
	int32 BoneIndex = SkeletalMeshComp->GetBoneIndexByName(FName(SubWeaponBoneName));
	if (BoneIndex < 0)
	{
		return;
	}

	// 본의 월드 트랜스폼 가져오기
	FTransform BoneTransform = SkeletalMeshComp->GetBoneWorldTransform(BoneIndex);

	// 오프셋 적용
	FVector FinalLocation = BoneTransform.Translation +
		BoneTransform.Rotation.RotateVector(SubWeaponOffset);

	// 회전 오프셋 적용 (오일러 → 쿼터니언)
	FQuat RotOffset = FQuat::MakeFromEulerZYX(SubWeaponRotationOffset);
	FQuat FinalRotation = BoneTransform.Rotation * RotOffset;

	// 무기 트랜스폼 설정
	SubWeaponMeshComp->SetWorldLocation(FinalLocation);
	SubWeaponMeshComp->SetWorldRotation(FinalRotation);
}

// ============================================================================
// 무기 디버그 렌더링 데이터 관리
// ============================================================================

void ACharacter::UpdateWeaponDebugData(const FVector& BasePos, const FVector& TipPos,
									   const FVector& PrevBase, const FVector& PrevTip,
									   const FQuat& Rotation)
{
	WeaponDebugData.CurrentBasePos = BasePos;
	WeaponDebugData.CurrentTipPos = TipPos;
	WeaponDebugData.PrevBasePos = PrevBase;
	WeaponDebugData.PrevTipPos = PrevTip;
	WeaponDebugData.WeaponRotation = Rotation;
	WeaponDebugData.TraceRadius = WeaponTraceRadius;
	WeaponDebugData.bIsValid = true;
}

void ACharacter::ClearWeaponDebugData()
{
	WeaponDebugData.bIsValid = false;
}
