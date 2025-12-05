#include "pch.h"
#include "GameState.h"
#include "GameStateBase.h"
#include "GameModeBase.h"
#include "World.h"
#include "PlayerController.h"
#include "Pawn.h"

void AGameState::SetGameFlowState(EGameFlowState NewState)
{
    if (GameFlowState == NewState)
    {
        return;
    }
    GameFlowState = NewState;
    // Reset the base timer for state-based animations/logic
    StateTimeSeconds = 0.0f; // from AGameStateBase (protected)
}

void AGameState::EnterStartMenu()
{
    ShowEndScreen(false, false);
    ShowStartScreen(true);
    SetPaused(true);
    SetGameFlowState(EGameFlowState::StartMenu);
}

void AGameState::StartFight()
{
    ShowStartScreen(false);
    ShowEndScreen(false, false);
    SetPaused(false);
    SetGameFlowState(EGameFlowState::Fighting);
}

void AGameState::EnterBossIntro()
{
    ShowStartScreen(false);
    SetPaused(false);
    SetGameFlowState(EGameFlowState::BossIntro);
}

void AGameState::EnterVictory()
{
    bPlayerWon = true;
    ShowEndScreen(true, /*bPlayerWon*/true);
    SetPaused(true);
    SetGameFlowState(EGameFlowState::Victory);
}

void AGameState::EnterDefeat()
{
    bPlayerWon = false;
    ShowEndScreen(true, /*bPlayerWon*/false);
    SetPaused(true);
    SetGameFlowState(EGameFlowState::Defeat);
}

void AGameState::OnPlayerLogin(APlayerController* InController)
{
    AGameStateBase::OnPlayerLogin(InController);
}

void AGameState::OnPawnPossessed(APawn* InPawn)
{
    AGameStateBase::OnPawnPossessed(InPawn);
}

void AGameState::OnPlayerDied()
{
    bPlayerAlive = false;
    EnterDefeat();
}

void AGameState::OnPlayerHealthChanged(float Current, float Max)
{
    PlayerHealth.Set(Current, Max);
    bPlayerAlive = (Current > 0.0f);
    if (!bPlayerAlive)
    {
        OnPlayerDied();
    }
}

void AGameState::RegisterBoss(const FString& InBossName, float BossMaxHealth)
{
    BossName = InBossName;
    BossHealth.Set(BossMaxHealth, BossMaxHealth);
    bBossActive = true;
}

void AGameState::UnregisterBoss()
{
    bBossActive = false;
    BossName.clear();
    BossHealth.Set(0.0f, 0.0f);
}

void AGameState::OnBossHealthChanged(float Current)
{
    BossHealth.Current = Current;
    if (BossHealth.Current <= 0.0f && bBossActive)
    {
        bBossActive = false;
        EnterVictory();
    }
}

void AGameState::ShowStartScreen(bool bShow)
{
    bStartScreenVisible = bShow;
}

void AGameState::ShowEndScreen(bool bShow, bool bInPlayerWon)
{
    bEndScreenVisible = bShow;
    if (bShow)
    {
        bPlayerWon = bInPlayerWon;
    }
}

void AGameState::HandleStateTick(float DeltaTime)
{
    // Simple timed transition from BossIntro to Fighting
    if (GameFlowState == EGameFlowState::BossIntro)
    {
        if (StateTimeSeconds >= BossIntroBannerTime)
        {
            StartFight();
        }
    }

    // Clamp health values for safety
    if (PlayerHealth.Current < 0.0f) PlayerHealth.Current = 0.0f;
    if (PlayerHealth.Current > PlayerHealth.Max) PlayerHealth.Current = PlayerHealth.Max;
    if (BossHealth.Current < 0.0f) BossHealth.Current = 0.0f;
    if (BossHealth.Current > BossHealth.Max) BossHealth.Current = BossHealth.Max;
}

