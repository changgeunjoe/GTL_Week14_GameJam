#include "pch.h"
#include "InputComponent.h"
#include "InputManager.h"
#include <windows.h>

IMPLEMENT_CLASS(UInputComponent)

UInputComponent::UInputComponent()
{
}

UInputComponent::~UInputComponent()
{
}

void UInputComponent::TickComponent(float DeltaSeconds)
{
    Super::TickComponent(DeltaSeconds);

    if (!bInputEnabled)
    {
        return;
    }

    ProcessActionBindings();
    ProcessAxisBindings();
}

// ============================================================================
// 액션 바인딩
// ============================================================================

void UInputComponent::BindAction(const FName& ActionName, EInputEvent EventType, std::function<void()> Callback)
{
    FInputActionBinding Binding;
    Binding.ActionName = ActionName;
    Binding.EventType = EventType;
    Binding.Callback = Callback;
    ActionBindings.Add(Binding);
}

// ============================================================================
// 축 바인딩
// ============================================================================

void UInputComponent::BindAxis(const FName& AxisName, std::function<void(float)> Callback)
{
    FInputAxisBinding Binding;
    Binding.AxisName = AxisName;
    Binding.Callback = Callback;
    AxisBindings.Add(Binding);
}

// ============================================================================
// 키 매핑
// ============================================================================

void UInputComponent::MapActionToKey(const FName& ActionName, int32 Key, bool bShift, bool bCtrl, bool bAlt)
{
    FInputActionKeyMapping Mapping;
    Mapping.ActionName = ActionName;
    Mapping.Key = Key;
    Mapping.bShift = bShift;
    Mapping.bCtrl = bCtrl;
    Mapping.bAlt = bAlt;
    ActionKeyMappings.Add(Mapping);
}

void UInputComponent::MapAxisToKey(const FName& AxisName, int32 Key, float Scale)
{
    FInputAxisKeyMapping Mapping;
    Mapping.AxisName = AxisName;
    Mapping.Key = Key;
    Mapping.Scale = Scale;
    AxisKeyMappings.Add(Mapping);
}

void UInputComponent::MapAxisToMouseX(const FName& AxisName, float Scale)
{
    MouseXAxisName = AxisName;
    MouseXScale = Scale;
}

void UInputComponent::MapAxisToMouseY(const FName& AxisName, float Scale)
{
    MouseYAxisName = AxisName;
    MouseYScale = Scale;
}

// ============================================================================
// 바인딩 해제
// ============================================================================

void UInputComponent::ClearActionBindings()
{
    ActionBindings.Empty();
}

void UInputComponent::ClearAxisBindings()
{
    AxisBindings.Empty();
}

void UInputComponent::ClearAllBindings()
{
    ClearActionBindings();
    ClearAxisBindings();
    ActionKeyMappings.Empty();
    AxisKeyMappings.Empty();
}

// ============================================================================
// 액션 블록
// ============================================================================

void UInputComponent::BlockAction(const FName& ActionName)
{
    if (!BlockedActions.Contains(ActionName))
    {
        BlockedActions.Add(ActionName);
    }
}

void UInputComponent::UnblockAction(const FName& ActionName)
{
    BlockedActions.Remove(ActionName);
}

bool UInputComponent::IsActionBlocked(const FName& ActionName) const
{
    return BlockedActions.Contains(ActionName);
}

// ============================================================================
// 내부 처리
// ============================================================================

void UInputComponent::ProcessActionBindings()
{
    UInputManager& InputManager = UInputManager::GetInstance();

    for (const FInputActionKeyMapping& Mapping : ActionKeyMappings)
    {
        if (IsActionBlocked(Mapping.ActionName))
        {
            continue;
        }

        // 모디파이어 체크
        if (!CheckModifiers(Mapping))
        {
            continue;
        }

        // 해당 액션에 바인딩된 콜백 찾기
        for (const FInputActionBinding& Binding : ActionBindings)
        {
            if (Binding.ActionName == Mapping.ActionName)
            {
                if (CheckKeyState(Mapping.Key, Binding.EventType))
                {
                    if (Binding.Callback)
                    {
                        Binding.Callback();
                    }
                }
            }
        }
    }

    // 이전 키 상태 업데이트
    for (const FInputActionKeyMapping& Mapping : ActionKeyMappings)
    {
        bool bCurrentlyDown = false;
        if (Mapping.Key == VK_LBUTTON)
        {
            bCurrentlyDown = InputManager.IsMouseButtonDown(EMouseButton::LeftButton);
        }
        else if (Mapping.Key == VK_RBUTTON)
        {
            bCurrentlyDown = InputManager.IsMouseButtonDown(EMouseButton::RightButton);
        }
        else if (Mapping.Key == VK_MBUTTON)
        {
            bCurrentlyDown = InputManager.IsMouseButtonDown(EMouseButton::MiddleButton);
        }
        else
        {
            bCurrentlyDown = InputManager.IsKeyDown(Mapping.Key);
        }
        PreviousKeyStates[Mapping.Key] = bCurrentlyDown;
    }
}

void UInputComponent::ProcessAxisBindings()
{
    // 각 축에 대해 값 계산 후 콜백 호출
    for (const FInputAxisBinding& Binding : AxisBindings)
    {
        float Value = GetAxisValue(Binding.AxisName);
        if (Binding.Callback)
        {
            Binding.Callback(Value);
        }
    }
}

bool UInputComponent::CheckKeyState(int32 Key, EInputEvent EventType)
{
    UInputManager& InputManager = UInputManager::GetInstance();
    bool bCurrentlyDown = false;

    // 마우스 버튼 체크
    if (Key == VK_LBUTTON)
    {
        bCurrentlyDown = InputManager.IsMouseButtonDown(EMouseButton::LeftButton);
    }
    else if (Key == VK_RBUTTON)
    {
        bCurrentlyDown = InputManager.IsMouseButtonDown(EMouseButton::RightButton);
    }
    else if (Key == VK_MBUTTON)
    {
        bCurrentlyDown = InputManager.IsMouseButtonDown(EMouseButton::MiddleButton);
    }
    else
    {
        // 키보드 키
        bCurrentlyDown = InputManager.IsKeyDown(Key);
    }

    bool bWasDown = PreviousKeyStates.count(Key) ? PreviousKeyStates[Key] : false;

    switch (EventType)
    {
    case EInputEvent::Pressed:
        return bCurrentlyDown && !bWasDown;
    case EInputEvent::Released:
        return !bCurrentlyDown && bWasDown;
    case EInputEvent::Held:
        return bCurrentlyDown;
    default:
        return false;
    }
}

bool UInputComponent::CheckModifiers(const FInputActionKeyMapping& Mapping)
{
    UInputManager& InputManager = UInputManager::GetInstance();

    if (Mapping.bShift && !InputManager.IsKeyDown(VK_SHIFT))
    {
        return false;
    }
    if (Mapping.bCtrl && !InputManager.IsKeyDown(VK_CONTROL))
    {
        return false;
    }
    if (Mapping.bAlt && !InputManager.IsKeyDown(VK_MENU))
    {
        return false;
    }

    return true;
}

float UInputComponent::GetAxisValue(const FName& AxisName)
{
    UInputManager& InputManager = UInputManager::GetInstance();
    float Value = 0.0f;

    // 마우스 축 체크
    if (AxisName == MouseXAxisName)
    {
        FVector2D MouseDelta = InputManager.GetMouseDelta();
        return MouseDelta.X * MouseXScale;
    }
    if (AxisName == MouseYAxisName)
    {
        FVector2D MouseDelta = InputManager.GetMouseDelta();
        return MouseDelta.Y * MouseYScale;
    }

    // 키보드 축 체크 - 매핑된 모든 키의 값을 합산
    for (const FInputAxisKeyMapping& Mapping : AxisKeyMappings)
    {
        if (Mapping.AxisName == AxisName)
        {
            if (InputManager.IsKeyDown(Mapping.Key))
            {
                Value += Mapping.Scale;
            }
        }
    }

    // -1 ~ 1 범위로 클램프
    return FMath::Clamp(Value, -1.0f, 1.0f);
}
