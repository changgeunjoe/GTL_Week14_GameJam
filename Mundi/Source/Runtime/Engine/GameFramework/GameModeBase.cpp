#include "pch.h"
#include "GameModeBase.h"
#include "PlayerController.h"
#include "Pawn.h"
#include "World.h"
#include "CameraComponent.h"
#include "SpringArmComponent.h"
#include "PlayerCameraManager.h"
#include "Character.h"
#include "Source/Runtime/Core/Misc/PathUtils.h"
#include "GameState.h"
#include "InputManager.h"
#include "Source/Runtime/Game/Enemy/BossEnemy.h"
#include "Source/Runtime/Game/Combat/StatsComponent.h"
#include "UIManager.h"
#include <Windows.h>

AGameModeBase::AGameModeBase()
{
	//DefaultPawnClass = APawn::StaticClass();
	DefaultPawnClass = ACharacter::StaticClass();
	PlayerControllerClass = APlayerController::StaticClass();
    GameStateClass = AGameState::StaticClass();
}

AGameModeBase::~AGameModeBase()
{
}

void AGameModeBase::StartPlay()
{
	// Ensure GameState exists (spawn using configured class)
	if (!GameStateClass)
	{
		GameStateClass = AGameState::StaticClass();
	}
	if (!GameState)
	{
		if (AActor* GSActor = GetWorld()->SpawnActor(GameStateClass, FTransform()))
		{
			GameState = Cast<AGameStateBase>(GSActor);
			if (GameState)
			{
				GameState->Initialize(GetWorld(), this);
			}
		}
	}

	// Player login + spawn/pawn possession
	Login();
	PostLogin(PlayerController);

	// Notify GameState of initial controller/pawn
	if (GameState && PlayerController)
	{
		GameState->OnPlayerLogin(PlayerController);
		if (APawn* P = PlayerController->GetPawn())
		{
			GameState->OnPawnPossessed(P);
		}
	}

	// Initialize flow: PIE shows StartMenu; non-PIE goes straight to intro/fight
	if (GameState)
	{
		if (GWorld && GWorld->bPie)
		{
			if (AGameState* GS = Cast<AGameState>(GameState))
			{
				GS->EnterStartMenu();
			}
		}
		else
		{
			if (AGameState* GS = Cast<AGameState>(GameState))
			{
				GS->EnterBossIntro();
			}
		}
	}
}

APlayerController* AGameModeBase::Login()
{
	if (PlayerControllerClass)
	{
		PlayerController = Cast<APlayerController>(GWorld->SpawnActor(PlayerControllerClass, FTransform())); 
	}
	else
	{
		PlayerController = GWorld->SpawnActor<APlayerController>(FTransform());
	}

	return PlayerController;
}

void AGameModeBase::PostLogin(APlayerController* NewPlayer)
{
	// 스폰 위치 찾기
	AActor* StartSpot = FindPlayerStart(NewPlayer);
	APawn* NewPawn = NewPlayer->GetPawn();

	// Pawn이 없으면 생성
    if (!NewPawn && NewPlayer)
    {
        // Try spawning a default prefab as the player's pawn first
        {
            FWideString PrefabPath = UTF8ToWide(GDataDir) + L"/Prefabs/Shinobi.prefab";
            if (AActor* PrefabActor = GWorld->SpawnPrefabActor(PrefabPath))
            {
                if (APawn* PrefabPawn = Cast<APawn>(PrefabActor))
                {
                    NewPawn = PrefabPawn;
                }
            }
        }

        // Fallback to default pawn class if prefab did not yield a pawn
        if (!NewPawn)
        {
            NewPawn = SpawnDefaultPawnFor(NewPlayer, StartSpot);
        }

        if (NewPawn)
        {
            NewPlayer->Possess(NewPawn);
        }

    }

	// SpringArm + Camera 구조로 부착
	if (NewPawn && !NewPawn->GetComponent(USpringArmComponent::StaticClass()))
	{
		// 1. SpringArm 생성 및 RootComponent에 부착
		USpringArmComponent* SpringArm = nullptr;
		if (UActorComponent* SpringArmComp = NewPawn->AddNewComponent(USpringArmComponent::StaticClass(), NewPawn->GetRootComponent()))
		{
			SpringArm = Cast<USpringArmComponent>(SpringArmComp);
			SpringArm->SetRelativeLocation(FVector(0, 0, 0.8f));  // 캐릭터 머리 위쪽
			SpringArm->SetTargetArmLength(3.0f);                  // 카메라 거리 (스프링 암에서 카메라 컴포넌트까지의)
			SpringArm->SetDoCollisionTest(true);                  // 충돌 체크 활성화
			SpringArm->SetUsePawnControlRotation(true);           // 컨트롤러 회전 사용
		}

		// 2. Camera를 SpringArm에 부착
		if (SpringArm)
		{
			if (UActorComponent* CameraComp = NewPawn->AddNewComponent(UCameraComponent::StaticClass(), SpringArm))
			{
				auto* Camera = Cast<UCameraComponent>(CameraComp);
				// 카메라는 SpringArm 끝에 위치 (SpringArm이 위치 계산)
				Camera->SetRelativeLocation(FVector(0, 0, 0));
				Camera->SetRelativeRotationEuler(FVector(0, 0, 0));
			}
		}
	}

	if (auto* PCM = GWorld->GetPlayerCameraManager())
	{
		if (auto* Camera = Cast<UCameraComponent>(NewPawn->GetComponent(UCameraComponent::StaticClass())))
		{
			PCM->SetViewCamera(Camera);
		}
	}

	// Possess를 수행 
	if (NewPlayer)
	{
		NewPlayer->Possess(NewPlayer->GetPawn());
	}

	// Inform GameState of the possessed pawn
	if (GameState && NewPlayer)
	{
		if (APawn* P = NewPlayer->GetPawn())
		{
			GameState->OnPawnPossessed(P);
		}
	}
}

APawn* AGameModeBase::SpawnDefaultPawnFor(AController* NewPlayer, AActor* StartSpot)
{
	// 위치 결정
	FVector SpawnLoc = FVector::Zero();
	FQuat SpawnRot = FQuat::Identity();

	if (StartSpot)
	{
		SpawnLoc = StartSpot->GetActorLocation();
		SpawnRot = StartSpot->GetActorRotation();
	}
	 
	APawn* ResultPawn = nullptr;
	if (DefaultPawnClass)
	{
		ResultPawn = Cast<APawn>(GetWorld()->SpawnActor(DefaultPawnClass, FTransform(SpawnLoc, SpawnRot, FVector(1, 1, 1))));
	}

	return ResultPawn;
}

AActor* AGameModeBase::FindPlayerStart(AController* Player)
{
	// TODO: PlayerStart Actor를 찾아서 반환하도록 구현 필요
	return nullptr;
}

void AGameModeBase::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!GameState)
	{
		return;
	}

	AGameState* GS = Cast<AGameState>(GameState);
	if (!GS)
	{
		return;
	}

	EGameFlowState CurrentState = GS->GetGameFlowState();

	// ========================================================================
	// 입력 처리 (키를 눌렀다 뗐을 때만 반응)
	// ========================================================================

	// ESC - 일시정지 토글
	bool bEscPressed = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
	if (bEscPressed && !bWasEscPressed)
	{
		if (CurrentState == EGameFlowState::Fighting)
		{
			GS->SetGameFlowState(EGameFlowState::Paused);
			GWorld->SetPaused(true);
			// 마우스 커서 표시 및 잠금 해제
			UInputManager::GetInstance().SetCursorVisible(true);
			UInputManager::GetInstance().ReleaseCursor();
		}
		else if (CurrentState == EGameFlowState::Paused)
		{
			GS->SetGameFlowState(EGameFlowState::Fighting);
			GWorld->SetPaused(false);
			// 마우스 커서 숨김 및 잠금
			UInputManager::GetInstance().SetCursorVisible(false);
			UInputManager::GetInstance().LockCursor();
		}
	}
	bWasEscPressed = bEscPressed;

	// R - 게임 재시작
	bool bRPressed = (GetAsyncKeyState('G') & 0x8000) != 0;
	if (bRPressed && !bWasRPressed)
	{
		RestartGame();
	}
	bWasRPressed = bRPressed;

	// Q - 게임 종료
	bool bQPressed = (GetAsyncKeyState('Q') & 0x8000) != 0;
	if (bQPressed && !bWasQPressed)
	{
		QuitGame();
	}
	bWasQPressed = bQPressed;

	// StartMenu에서 아무 키나 누르면 게임 시작
	if (CurrentState == EGameFlowState::StartMenu)
	{
		bool bAnyKeyPressed = false;
		for (int i = 0; i < 256; i++)
		{
			if (GetAsyncKeyState(i) & 0x8000)
			{
				bAnyKeyPressed = true;
				break;
			}
		}

		if (bAnyKeyPressed && !bWasAnyKeyPressed)
		{
			GS->EnterBossIntro();
		}
		bWasAnyKeyPressed = bAnyKeyPressed;
	}

	// Victory/Defeat 화면에서 R키로 재시작, Q키로 종료
	if (CurrentState == EGameFlowState::Victory || CurrentState == EGameFlowState::Defeat)
	{
		// R키와 Q키는 위에서 이미 처리됨
	}
}

void AGameModeBase::RestartGame()
{
	if (!GWorld)
	{
		return;
	}

	AGameState* GS = Cast<AGameState>(GameState);
	if (!GS)
	{
		return;
	}

	// 0. 일시정지 해제 및 마우스 커서 숨김/잠금
	GWorld->SetPaused(false);
	UInputManager::GetInstance().SetCursorVisible(false);
	UInputManager::GetInstance().LockCursor();

	// 1. 플레이어 체력 회복 및 위치 초기화
	if (PlayerController)
	{
		if (APawn* PlayerPawn = PlayerController->GetPawn())
		{
			// 플레이어 위치 초기화 (원점으로)
			PlayerPawn->SetActorLocation(FVector(0, 0, 3));

			// 플레이어 체력 회복
			if (ACharacter* PlayerChar = Cast<ACharacter>(PlayerPawn))
			{
				if (UStatsComponent* Stats = Cast<UStatsComponent>(PlayerChar->GetComponent(UStatsComponent::StaticClass())))
				{
					Stats->CurrentHealth = Stats->MaxHealth;
					Stats->CurrentStamina = Stats->MaxStamina;
					Stats->CurrentFocus = 0.0f;
				}
			}
		}
	}

	// 2. 보스 체력 회복
	for (AActor* Actor : GWorld->GetActors())
	{
		if (ABossEnemy* Boss = Cast<ABossEnemy>(Actor))
		{
			if (UStatsComponent* Stats = Boss->GetStatsComponent())
			{
				Stats->CurrentHealth = Stats->MaxHealth;
			}
			// 보스 위치 초기화는 필요시 추가
			break;
		}
	}

	// 3. GameState 초기화 및 게임 시작
	GS->EnterBossIntro();
}

void AGameModeBase::QuitGame()
{
	// 게임 종료
	exit(0);
}

