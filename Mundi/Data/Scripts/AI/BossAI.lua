-- ============================================================================
-- BossAI.lua - 보스 전용 AI (Behavior Tree 기반)
-- ============================================================================

-- BT 프레임워크 로드
dofile("Data/Scripts/AI/BehaviorTree.lua")

-- ============================================================================
-- 디버그 설정
-- ============================================================================
local DEBUG_LOG = true  -- false로 바꾸면 로그 끔

local function Log(msg)
    if DEBUG_LOG then
        -- print("[BossAI] " .. msg)
    end
end

-- ============================================================================
-- Context (AI 상태 저장)
-- ============================================================================

local ctx = {
    self_actor = nil,
    target = nil,
    stats = nil,
    phase = 1,
    time = 0,
    delta_time = 0,                 -- 이번 프레임 delta time
    last_pattern = -1,
    -- Strafe 관련
    strafe_direction = 1,           -- 1: 오른쪽, -1: 왼쪽
    strafe_last_change_time = 0,    -- 마지막 방향 전환 시간
    strafe_start_time = 0,          -- Strafe 시작 시간
    strafe_duration = 0,            -- 이번 Strafe 지속 시간
    is_strafing = false,            -- Strafe 중인지
    -- 접근 행동 관련
    is_approaching = false,         -- 플레이어에게 접근 중인지
    approach_start_time = 0,        -- 접근 시작 시간
    -- 공격 쿨다운
    last_attack_time = 0,           -- 마지막 공격 시간
    -- 몽타주 상태 추적
    was_attacking = false,          -- 이전 프레임 공격 상태 (몽타주 종료 감지용)
    -- 후퇴 관련
    is_retreating = false,          -- 후퇴 중인지
    retreat_end_time = 0,           -- 후퇴 종료 시간
    -- 차지 공격 관련
    is_charging = false,            -- ChargeStart 재생 중인지

    -- 콤보 공격 관련
    is_combo_attacking = false,     -- 콤보 공격 중인지
    current_combo = nil,            -- 현재 실행 중인 콤보 테이블
    combo_index = 0,                -- 현재 콤보 인덱스 (1부터 시작)
    combo_next_time = 0,            -- 다음 콤보 타격 시간

    -- 가드 브레이크 관련
    is_guard_breaking = false,      -- 가드 브레이크 중인지
    guard_break_start_time = 0,     -- 가드 브레이크 시작 시간

    -- 예비 동작(Wind-up) 관련
    is_winding_up = false,          -- 예비 동작 중인지
    wind_up_start_time = 0,         -- 예비 동작 시작 시간

    -- 죽음 관련
    is_death_animation_played = false,  -- 죽음 애니메이션 재생 여부

    -- ========================================================================
    -- 플레이어 상태 추적 (Reactive AI용)
    -- ========================================================================
    player_state = {
        current = "Idle",           -- 현재 상태: Idle, Attacking, Dodging, Blocking, etc.
        previous = "Idle",          -- 이전 상태
        state_changed = false,      -- 이번 프레임에 상태가 변경됐는지
        state_change_time = 0,      -- 상태 변경 시간

        -- 행동 감지 플래그 (이번 프레임)
        just_dodged = false,        -- 방금 회피 시작했는지
        just_attacked = false,      -- 방금 공격 시작했는지
        just_blocked = false,       -- 방금 가드 시작했는지
        dodge_end_time = 0,         -- 회피 종료 예상 시간 (징벌 타이밍)

        -- 누적 상태
        is_invincible = false,      -- 무적 상태
        is_blocking = false,        -- 가드 중
        is_parrying = false,        -- 패리 윈도우
        is_staggered = false,       -- 경직 중
        is_alive = true,            -- 생존 여부
    },

    -- 플레이어 위치 추적 (이동 방향 감지용)
    player_tracking = {
        last_position = nil,        -- 이전 위치
        velocity = { X = 0, Y = 0, Z = 0 },  -- 이동 속도
        is_approaching = false,     -- 보스에게 접근 중인지
        is_retreating = false,      -- 보스에게서 멀어지는 중인지
        last_distance = 0,          -- 이전 거리
    }
}

-- ============================================================================
-- 설정값
-- ============================================================================

local Config = {
    DetectionRange = 2000,
    AttackRange = 4,            -- 4m 이내면 공격
    LoseTargetRange = 3000,
    MoveSpeed = 250,
    Phase2MoveSpeed = 350,
    Phase2HealthThreshold = 0.5,

    -- 좌우 이동(Strafe) 설정 (미터 단위)
    StrafeMinRange = 5,         -- 5m 이상일 때 좌우 이동 시작 (AttackRange보다 커야함)
    StrafeMaxRange = 8,         -- 8m 이하일 때 좌우 이동
    StrafeSpeed = 2,            -- 좌우 이동 속도 (m/s)
    StrafeChangeInterval = 1.5, -- 방향 전환 간격 (초)

    -- 접근 행동 설정
    StrafeMinDuration = 1.5,    -- 최소 Strafe 시간 (초)
    StrafeMaxDuration = 3.5,    -- 최대 Strafe 시간 (초)
    ApproachChance = 0.55,       -- Strafe 후 접근할 확률 (0~1), 낮을수록 Strafe 더 많이 함

    -- 후퇴 설정 (미터 단위)
    RetreatRange = 2,           -- 이 거리보다 가까우면 후퇴 (AttackRange보다 작아야함)
    RetreatChance = 0.3,        -- 공격 후 후퇴할 확률 (0~1)
    RetreatDuration = 1.0,      -- 후퇴 지속 시간 (초)

    -- 공격 쿨다운 (초)
    AttackCooldown = 2.0,       -- 공격 후 다음 공격까지 대기 시간

    -- 회전 보간 속도 (도/초)
    TurnSpeed = 180,            -- 초당 회전할 수 있는 최대 각도

    -- 콤보 취소 조건
    ComboMaxDistance = 6,       -- 콤보 중 플레이어와 최대 거리 (m) - 이 거리를 넘으면 콤보 취소
    ComboMaxAngle = 120,        -- 콤보 중 플레이어와 최대 각도 (도) - 이 각도를 넘으면 콤보 취소

    -- 차지 공격 설정 (ChargeStart 끝나면 ChargeAttack 재생)

    -- 가드 브레이크 설정
    GuardBreakSlowDuration = 0.5,   -- 느린 속도 유지 시간 (초)
    GuardBreakSlowRate = 0.3,       -- 시작 재생 속도 (GlobalAnimSpeed 곱해짐)
    GuardBreakNormalRate = 1.0,     -- 정상 재생 속도 (GlobalAnimSpeed 곱해짐)

    -- 전역 애니메이션 속도 배율
    GlobalAnimSpeed = 0.8,          -- 모든 공격 애니메이션에 적용되는 속도 배율

    -- 소울라이크 스타일: 예비 동작 느리게 -> 공격 빠르게
    WindUpEnabled = true,           -- 예비 동작 시스템 활성화
    WindUpSlowRate = 0.3,           -- 예비 동작 속도 (느리게)
    WindUpDuration = 0.4,           -- 예비 동작 지속 시간 (초)
    AttackSpeedRate = 1.0,          -- 실제 공격 속도 (빠르게)
}

-- ============================================================================
-- 헬퍼 함수: 글로벌 애니메이션 속도 적용
-- ============================================================================

-- PlayMontage 래퍼: GlobalAnimSpeed + Wind-up 시스템 적용
-- useWindUp: true면 예비 동작 시스템 사용 (기본값 true)
local function PlayBossMontage(obj, montageName, playRate, useWindUp)
    -- 기본값 설정
    if useWindUp == nil then useWindUp = true end

    local baseRate = (playRate or 1.0) * Config.GlobalAnimSpeed

    -- Wind-up 시스템 적용
    if Config.WindUpEnabled and useWindUp then
        -- 예비 동작: 느린 속도로 시작
        local windUpRate = Config.WindUpSlowRate * Config.GlobalAnimSpeed
        local success = PlayMontage(obj, montageName, 0.1, 0.1, windUpRate)
        if success then
            -- 컨텍스트에 wind-up 상태 설정 (전역 ctx 사용)
            ctx.is_winding_up = true
            ctx.wind_up_start_time = ctx.time
        end
        return success
    else
        -- Wind-up 없이 바로 재생
        return PlayMontage(obj, montageName, 0.1, 0.1, baseRate)
    end
end

-- ============================================================================
-- Conditions (조건 함수들)
-- ============================================================================

local function HasTarget(c)
    local result = c.target ~= nil
    Log("  [Cond] HasTarget: " .. tostring(result))
    return result
end

local function IsTargetInDetectionRange(c)
    if not c.target or not c.self_actor then
        Log("  [Cond] IsTargetInDetectionRange: false (no target)")
        return false
    end
    local dist = VecDistance(Obj.Location, c.target.Location)
    local result = dist <= Config.DetectionRange
    Log("  [Cond] IsTargetInDetectionRange: " .. tostring(result) .. " (dist=" .. string.format("%.1f", dist) .. ")")
    return result
end

local function IsTargetInAttackRange(c)
    if not c.target or not c.self_actor then
        Log("  [Cond] IsTargetInAttackRange: false (no target)")
        return false
    end
    local dist = VecDistance(Obj.Location, c.target.Location)
    local result = dist <= Config.AttackRange
    Log("  [Cond] IsTargetInAttackRange: " .. tostring(result) .. " (dist=" .. string.format("%.1f", dist) .. ", range=" .. Config.AttackRange .. ")")
    return result
end

local function IsTargetLost(c)
    if not c.target or not c.self_actor then
        Log("  [Cond] IsTargetLost: true (no target)")
        return true
    end
    local dist = VecDistance(Obj.Location, c.target.Location)
    local result = dist > Config.LoseTargetRange
    Log("  [Cond] IsTargetLost: " .. tostring(result) .. " (dist=" .. string.format("%.1f", dist) .. ")")
    return result
end

local function IsPhase2(c)
    local result = c.phase >= 2
    Log("  [Cond] IsPhase2: " .. tostring(result) .. " (phase=" .. c.phase .. ")")
    return result
end

local function IsLowHealth(c)
    if not c.stats then
        Log("  [Cond] IsLowHealth: false (no stats)")
        return false
    end
    local healthPercent = c.stats.CurrentHealth / c.stats.MaxHealth
    local result = healthPercent < 0.3
    Log("  [Cond] IsLowHealth: " .. tostring(result) .. " (hp=" .. string.format("%.1f%%", healthPercent * 100) .. ")")
    return result
end

local function IsNotAttacking(c)
    -- 몽타주 재생 중이거나 콤보 공격 중이면 공격 중으로 판단
    local bMontagePlayling = IsMontagePlayling(Obj)
    local bComboAttacking = c.is_combo_attacking
    local result = not bMontagePlayling and not bComboAttacking
    Log("  [Cond] IsNotAttacking: " .. tostring(result) .. " (montage=" .. tostring(bMontagePlayling) .. ", combo=" .. tostring(bComboAttacking) .. ")")
    return result
end

local function IsAttacking(c)
    -- 몽타주 재생 중이거나 콤보 공격 중이면 공격 중으로 판단
    local bMontagePlayling = IsMontagePlayling(Obj)
    local bComboAttacking = c.is_combo_attacking
    local result = bMontagePlayling or bComboAttacking
    Log("  [Cond] IsAttacking: " .. tostring(result) .. " (montage=" .. tostring(bMontagePlayling) .. ", combo=" .. tostring(bComboAttacking) .. ")")
    return result
end

local function IsInStrafeRange(c)
    if not c.target or not c.self_actor then
        Log("  [Cond] IsInStrafeRange: false (no target)")
        return false
    end
    local dist = VecDistance(Obj.Location, c.target.Location)
    local result = dist >= Config.StrafeMinRange and dist <= Config.StrafeMaxRange
    Log("  [Cond] IsInStrafeRange: " .. tostring(result) .. " (dist=" .. string.format("%.2f", dist) .. "m, range=" .. Config.StrafeMinRange .. "-" .. Config.StrafeMaxRange .. "m)")
    return result
end

local function CanAttack(c)
    -- 몽타주 재생 중이면 불가
    local bMontagePlayling = IsMontagePlayling(Obj)
    if bMontagePlayling then
        Log("  [Cond] CanAttack: false (montage playing)")
        return false
    end
    -- 후퇴 중이면 불가
    if c.is_retreating then
        Log("  [Cond] CanAttack: false (retreating)")
        return false
    end
    -- 쿨다운 체크
    local timeSinceLastAttack = c.time - c.last_attack_time
    if timeSinceLastAttack < Config.AttackCooldown then
        Log("  [Cond] CanAttack: false (cooldown=" .. string.format("%.1f", Config.AttackCooldown - timeSinceLastAttack) .. "s)")
        return false
    end
    Log("  [Cond] CanAttack: true")
    return true
end

local function IsTooClose(c)
    if not c.target or not c.self_actor then
        return false
    end
    local dist = VecDistance(Obj.Location, c.target.Location)
    local result = dist < Config.RetreatRange
    Log("  [Cond] IsTooClose: " .. tostring(result) .. " (dist=" .. string.format("%.2f", dist) .. "m, range=" .. Config.RetreatRange .. "m)")
    return result
end

local function IsRetreating(c)
    local result = c.is_retreating and c.time < c.retreat_end_time
    Log("  [Cond] IsRetreating: " .. tostring(result))
    return result
end

local function ShouldRetreatAfterAttack(c)
    -- 공격 직후에 일정 확률로 후퇴
    -- was_attacking이 true였다가 false가 되는 순간 (공격 종료 직후)
    return math.random() < Config.RetreatChance
end

-- Strafe 중인지 (시간 기반)
local function IsCurrentlyStrafing(c)
    if not c.is_strafing then
        return false
    end
    -- Strafe 지속 시간 체크
    local elapsed = c.time - c.strafe_start_time
    local result = elapsed < c.strafe_duration
    Log("  [Cond] IsCurrentlyStrafing: " .. tostring(result) .. " (elapsed=" .. string.format("%.1f", elapsed) .. "/" .. string.format("%.1f", c.strafe_duration) .. "s)")
    return result
end

-- 접근 중인지
local function IsCurrentlyApproaching(c)
    local result = c.is_approaching
    Log("  [Cond] IsCurrentlyApproaching: " .. tostring(result))
    return result
end

-- Strafe가 끝났는지 (접근 결정 필요)
local function ShouldDecideNextAction(c)
    if not c.is_strafing then
        return false
    end
    local elapsed = c.time - c.strafe_start_time
    return elapsed >= c.strafe_duration
end

-- ============================================================================
-- 플레이어 상태 추적 시스템
-- ============================================================================

local DODGE_DURATION = 0.6  -- 회피 지속 시간 (대략)

local function UpdatePlayerState(c)
    local ps = c.player_state
    local pt = c.player_tracking

    -- 이전 상태 저장
    ps.previous = ps.current
    ps.state_changed = false
    ps.just_dodged = false
    ps.just_attacked = false
    ps.just_blocked = false

    -- 현재 플레이어 상태 조회 (C++ 함수 호출)
    ps.current = GetPlayerCombatState()
    ps.is_invincible = IsPlayerInvincible()
    ps.is_blocking = IsPlayerBlocking()
    ps.is_parrying = IsPlayerParrying()
    ps.is_staggered = IsPlayerStaggered()
    ps.is_alive = IsPlayerAlive()

    -- 상태 변경 감지
    if ps.current ~= ps.previous then
        ps.state_changed = true
        ps.state_change_time = c.time

        -- 회피 시작 감지
        if ps.current == "Dodging" and ps.previous ~= "Dodging" then
            ps.just_dodged = true
            ps.dodge_end_time = c.time + DODGE_DURATION
            Log("[PlayerState] Player started DODGING!")
        end

        -- 공격 시작 감지
        if ps.current == "Attacking" and ps.previous ~= "Attacking" then
            ps.just_attacked = true
            Log("[PlayerState] Player started ATTACKING!")
        end

        -- 가드 시작 감지
        if ps.current == "Blocking" and ps.previous ~= "Blocking" then
            ps.just_blocked = true
            Log("[PlayerState] Player started BLOCKING!")
        end

        Log("[PlayerState] State changed: " .. ps.previous .. " -> " .. ps.current)
    end

    -- 플레이어 위치 추적
    if c.target then
        local currentPos = c.target.Location
        local myPos = Obj.Location
        local currentDist = VecDistance(myPos, currentPos)

        if pt.last_position then
            -- 이동 속도 계산
            pt.velocity.X = (currentPos.X - pt.last_position.X) / c.delta_time
            pt.velocity.Y = (currentPos.Y - pt.last_position.Y) / c.delta_time
            pt.velocity.Z = (currentPos.Z - pt.last_position.Z) / c.delta_time

            -- 접근/후퇴 감지
            local distDelta = currentDist - pt.last_distance
            pt.is_approaching = distDelta < -0.1  -- 거리가 줄어들면 접근 중
            pt.is_retreating = distDelta > 0.1    -- 거리가 늘어나면 후퇴 중
        end

        pt.last_position = { X = currentPos.X, Y = currentPos.Y, Z = currentPos.Z }
        pt.last_distance = currentDist
    end
end

-- 플레이어가 회피 직후인지 (징벌 타이밍)
local function IsPlayerJustDodged(c)
    return c.player_state.just_dodged
end

-- 플레이어 회피가 끝났는지 (무적 해제 직후)
local function IsPlayerDodgeEnding(c)
    local ps = c.player_state
    -- 회피 종료 시점 직전 (0.1초 전부터 감지)
    return ps.current == "Dodging" and c.time >= (ps.dodge_end_time - 0.15)
end

-- 플레이어가 공격 중인지 (트레이드 가능)
local function IsPlayerCurrentlyAttacking(c)
    return c.player_state.current == "Attacking"
end

-- 플레이어가 가드 중인지
local function IsPlayerCurrentlyBlocking(c)
    return c.player_state.is_blocking
end

-- 플레이어가 접근 중인지
local function IsPlayerApproaching(c)
    return c.player_tracking.is_approaching
end

-- 플레이어가 후퇴 중인지
local function IsPlayerRetreating(c)
    return c.player_tracking.is_retreating
end

-- 플레이어가 경직 상태인지 (추가 공격 기회)
local function IsPlayerVulnerable(c)
    local ps = c.player_state
    return ps.is_staggered or (ps.current == "Idle" and not ps.is_blocking)
end

-- ============================================================================
-- Utility Functions (유틸리티 함수들)
-- ============================================================================

-- 각도 차이 계산 (-180 ~ 180 범위로 정규화)
local function NormalizeAngle(angle)
    while angle > 180 do angle = angle - 360 end
    while angle < -180 do angle = angle + 360 end
    return angle
end

-- 부드러운 회전 보간 (현재 Yaw에서 목표 Yaw로 delta만큼 회전)
local function SmoothRotateToTarget(currentYaw, targetYaw, maxDelta)
    local diff = NormalizeAngle(targetYaw - currentYaw)

    -- 회전량 제한
    if math.abs(diff) <= maxDelta then
        return targetYaw
    elseif diff > 0 then
        return currentYaw + maxDelta
    else
        return currentYaw - maxDelta
    end
end

-- ============================================================================
-- Actions (행동 함수들)
-- ============================================================================

local function DoFindTarget(c)
    Log("  [Action] DoFindTarget")
    -- GetPlayer() 우선 시도, 없으면 FindObjectByName 시도
    c.target = GetPlayer()
    if c.target then
        Log("    -> Target found via GetPlayer()")
        return BT_SUCCESS
    end
    c.target = FindObjectByName("Player")
    if c.target then
        Log("    -> Target found via FindObjectByName")
        return BT_SUCCESS
    end
    Log("    -> Target not found")
    return BT_FAILURE
end

local function DoLoseTarget(c)
    Log("  [Action] DoLoseTarget")
    c.target = nil
    return BT_SUCCESS
end

local function DoChase(c)
    if not c.target then
        Log("  [Action] DoChase -> FAILURE (no target)")
        return BT_FAILURE
    end

    -- 몽타주 재생 중이면 이동 불가
    if IsMontagePlayling(Obj) then
        Log("  [Action] DoChase -> SKIP (montage playing)")
        return BT_SUCCESS
    end

    -- FVector를 Lua 테이블로 변환
    local myPosRaw = Obj.Location
    local targetPosRaw = c.target.Location
    local myPos = { X = myPosRaw.X, Y = myPosRaw.Y, Z = myPosRaw.Z }
    local targetPos = { X = targetPosRaw.X, Y = targetPosRaw.Y, Z = targetPosRaw.Z }

    local dir = VecDirection(myPos, targetPos)
    dir.Z = 0  -- 수평 이동만

    local dist = VecDistance(myPos, targetPos)

    -- 공격 범위에 도달하면 멈춤 (BT Branch 10에서 공격 처리)
    if dist <= Config.AttackRange then
        Log("  [Action] DoChase -> Reached attack range, stopping")
        return BT_SUCCESS
    end

    -- 타겟을 바라보도록 부드러운 회전
    local targetYaw = CalcYaw(myPos, targetPos)
    local currentYaw = Obj.Rotation.Z
    local maxTurnDelta = Config.TurnSpeed * c.delta_time
    local newYaw = SmoothRotateToTarget(currentYaw, targetYaw, maxTurnDelta)
    Obj.Rotation = Vector(0, 0, newYaw)

    -- CharacterMovementComponent 사용
    local moveDir = Vector(dir.X, dir.Y, dir.Z)
    AddMovementInput(Obj, moveDir, 1.0)

    -- 디버그 상태 설정
    SetBossAIState(Obj, "Chasing")
    SetBossMovementType(Obj, "Run")
    SetBossDistanceToPlayer(Obj, dist)

    Log("  [Action] DoChase -> Moving (dist=" .. string.format("%.1f", dist) .. ", yaw=" .. string.format("%.1f", newYaw) .. " -> " .. string.format("%.1f", targetYaw) .. ")")
    return BT_SUCCESS
end

local function DoIdle(c)
    Log("  [Action] DoIdle")
    SetBossAIState(Obj, "Idle")
    SetBossMovementType(Obj, "None")
    return BT_SUCCESS
end

-- Strafe 시작 (랜덤 지속 시간 설정)
local function StartStrafe(c)
    c.is_strafing = true
    c.is_approaching = false
    c.strafe_start_time = c.time
    c.strafe_duration = Config.StrafeMinDuration + math.random() * (Config.StrafeMaxDuration - Config.StrafeMinDuration)
    c.strafe_direction = math.random() > 0.5 and 1 or -1  -- 랜덤 방향
    Log("  [Action] StartStrafe -> duration=" .. string.format("%.1f", c.strafe_duration) .. "s, dir=" .. (c.strafe_direction == 1 and "Right" or "Left"))
end

-- Strafe 종료 후 다음 행동 결정
local function DecideAfterStrafe(c)
    c.is_strafing = false

    -- 확률적으로 접근 or 다시 Strafe
    if math.random() < Config.ApproachChance then
        c.is_approaching = true
        c.approach_start_time = c.time
        Log("  [Decision] After strafe -> APPROACH")
    else
        -- 다시 Strafe 시작
        StartStrafe(c)
        Log("  [Decision] After strafe -> STRAFE AGAIN")
    end
end

local function DoStrafe(c)
    if not c.target then
        Log("  [Action] DoStrafe -> FAILURE (no target)")
        return BT_FAILURE
    end

    -- 몽타주 재생 중이면 이동 불가
    if IsMontagePlayling(Obj) then
        Log("  [Action] DoStrafe -> SKIP (montage playing)")
        return BT_SUCCESS
    end

    -- Strafe 시작 체크
    if not c.is_strafing then
        StartStrafe(c)
    end

    -- Strafe 지속 시간 체크
    local elapsed = c.time - c.strafe_start_time
    if elapsed >= c.strafe_duration then
        -- Strafe 종료 -> 다음 행동 결정
        DecideAfterStrafe(c)
        return BT_SUCCESS
    end

    -- 일정 시간마다 방향 전환
    if c.time - c.strafe_last_change_time >= Config.StrafeChangeInterval then
        c.strafe_direction = c.strafe_direction * -1  -- 방향 반전
        c.strafe_last_change_time = c.time
        Log("    -> Strafe direction changed to: " .. (c.strafe_direction == 1 and "Right" or "Left"))
    end

    local myPosRaw = Obj.Location
    local targetPosRaw = c.target.Location
    local myPos = { X = myPosRaw.X, Y = myPosRaw.Y, Z = myPosRaw.Z }
    local targetPos = { X = targetPosRaw.X, Y = targetPosRaw.Y, Z = targetPosRaw.Z }

    -- 타겟을 바라보도록 부드러운 회전
    local targetYaw = CalcYaw(myPos, targetPos)
    local currentYaw = Obj.Rotation.Z
    local maxTurnDelta = Config.TurnSpeed * c.delta_time
    local newYaw = SmoothRotateToTarget(currentYaw, targetYaw, maxTurnDelta)
    Obj.Rotation = Vector(0, 0, newYaw)

    -- 타겟 방향 벡터
    local toTarget = VecDirection(myPos, targetPos)
    toTarget.Z = 0

    -- 좌우 방향 벡터 (타겟 방향의 수직 벡터)
    local strafeDir = {
        X = -toTarget.Y * c.strafe_direction,
        Y = toTarget.X * c.strafe_direction,
        Z = 0
    }

    -- AddMovementInput 사용
    local moveDir = Vector(strafeDir.X, strafeDir.Y, strafeDir.Z)
    AddMovementInput(Obj, moveDir, 1.0)

    local dist = VecDistance(myPos, targetPos)
    local remaining = c.strafe_duration - elapsed

    -- 디버그 상태 설정
    SetBossAIState(Obj, "Strafing")
    SetBossMovementType(Obj, c.strafe_direction == 1 and "Strafe_R" or "Strafe_L")
    SetBossDistanceToPlayer(Obj, dist)

    Log("  [Action] DoStrafe -> Moving " .. (c.strafe_direction == 1 and "Right" or "Left") .. " (dist=" .. string.format("%.2f", dist) .. "m, remaining=" .. string.format("%.1f", remaining) .. "s)")
    return BT_SUCCESS
end

-- 접근 행동 (Strafe 후 플레이어에게 다가감)
local function DoApproach(c)
    if not c.target then
        Log("  [Action] DoApproach -> FAILURE (no target)")
        return BT_FAILURE
    end

    -- 몽타주 재생 중이면 이동 불가
    if IsMontagePlayling(Obj) then
        Log("  [Action] DoApproach -> SKIP (montage playing)")
        return BT_SUCCESS
    end

    local myPosRaw = Obj.Location
    local targetPosRaw = c.target.Location
    local myPos = { X = myPosRaw.X, Y = myPosRaw.Y, Z = myPosRaw.Z }
    local targetPos = { X = targetPosRaw.X, Y = targetPosRaw.Y, Z = targetPosRaw.Z }

    local dist = VecDistance(myPos, targetPos)

    -- 공격 범위에 도달하면 접근 종료 (BT Branch 10에서 공격 처리)
    if dist <= Config.AttackRange then
        c.is_approaching = false
        Log("  [Action] DoApproach -> Reached attack range, stopping")
        return BT_SUCCESS
    end

    -- 타겟을 바라보도록 부드러운 회전
    local targetYaw = CalcYaw(myPos, targetPos)
    local currentYaw = Obj.Rotation.Z
    local maxTurnDelta = Config.TurnSpeed * c.delta_time
    local newYaw = SmoothRotateToTarget(currentYaw, targetYaw, maxTurnDelta)
    Obj.Rotation = Vector(0, 0, newYaw)

    -- 타겟 방향으로 이동
    local dir = VecDirection(myPos, targetPos)
    dir.Z = 0
    local moveDir = Vector(dir.X, dir.Y, dir.Z)
    AddMovementInput(Obj, moveDir, 1.0)

    -- 디버그 상태 설정
    SetBossAIState(Obj, "Approaching")
    SetBossMovementType(Obj, "Walk")
    SetBossDistanceToPlayer(Obj, dist)

    Log("  [Action] DoApproach -> Moving toward player (dist=" .. string.format("%.2f", dist) .. "m)")
    return BT_SUCCESS
end

-- ============================================================================
-- Attack Actions (공격 행동들)
-- ============================================================================

local function StartAttack(c, patternName)
    c.last_attack_time = c.time
    Log("    -> StartAttack: " .. patternName)
end

-- 공격 패턴 정보 (히트박스는 애님 노티파이에서 처리)
local AttackPatterns = {
    [0] = { name = "LightCombo" },
    [1] = { name = "HeavySlam" },
    [2] = { name = "ChargeAttack" },
    [3] = { name = "SpinAttack" }
}

-- ============================================================================
-- 콤보 테이블 정의
-- montage: 재생할 몽타주 이름
-- delay: 이 타격 후 다음 타격까지 대기 시간 (초), 마지막은 0
-- ============================================================================

local ComboPatterns = {
    -- 콤보 1: 베기 3연타 (Slash_1 -> Slash_2 -> Slash_3)
    Combo_Slash3 = {
        { montage = "Slash_1", delay = 0.4 },
        { montage = "Slash_2", delay = 0.5 },
        { montage = "Slash_3", delay = 0 },  -- 마지막 (큰 빈틈)
    },

    -- 콤보 2: 베기 2연타 + 내려찍기 (Slash -> Slash -> Smash)
    Combo_SSH = {
        { montage = "Slash_1", delay = 0.3 },
        { montage = "Slash_2", delay = 0.4 },
        { montage = "Smash_1", delay = 0 },   -- 내려찍기로 마무리
    },

    -- 콤보 3: 베기 + 내려찍기 (짧은 콤보)
    Combo_SH = {
        { montage = "Slash_1", delay = 0.5 },
        { montage = "Smash_1", delay = 0 },
    },

    -- 콤보 4: 회전 2연타 (Phase 2 전용)
    Combo_Spin2 = {
        { montage = "Spin_1", delay = 0.6 },
        { montage = "Spin_2", delay = 0 },
    },

    -- 콤보 5: 내려찍기 2연타 (Phase 2 전용)
    Combo_Smash2 = {
        { montage = "Smash_1", delay = 0.6 },
        { montage = "Smash_2", delay = 0 },
    },

    -- 콤보 6: 베기 3연타 + 내려찍기 (Phase 2 전용, 긴 콤보)
    Combo_SSSH = {
        { montage = "Slash_1", delay = 0.3 },
        { montage = "Slash_2", delay = 0.3 },
        { montage = "Slash_3", delay = 0.4 },
        { montage = "Smash_2", delay = 0 },
    },

    -- 콤보 7: 회전 + 내려찍기 (Phase 2 전용)
    Combo_SpinSmash = {
        { montage = "Spin_1", delay = 0.5 },
        { montage = "Smash_2", delay = 0 },
    },
}

-- Phase별 사용 가능한 콤보 목록
local Phase1Combos = { "Combo_Slash3", "Combo_SSH", "Combo_SH" }
local Phase2Combos = { "Combo_Slash3", "Combo_SSH", "Combo_SH", "Combo_Spin2", "Combo_Smash2", "Combo_SSSH", "Combo_SpinSmash" }

-- 랜덤 콤보 선택
local function SelectRandomCombo(phase)
    local comboList = phase >= 2 and Phase2Combos or Phase1Combos
    local comboName = comboList[math.random(1, #comboList)]
    return ComboPatterns[comboName], comboName
end

-- 콤보 시작
local function StartCombo(c, comboName)
    local combo = ComboPatterns[comboName]
    if not combo then
        Log("[Combo] ERROR: Combo not found: " .. comboName)
        return false
    end

    c.is_combo_attacking = true
    c.current_combo = combo
    c.current_combo_name = comboName
    c.combo_index = 1
    c.combo_next_time = 0  -- 즉시 첫 타격

    SetBossPatternName(Obj, comboName .. " (1/" .. #combo .. ")")
    SetBossAIState(Obj, "Attacking")
    SetBossMovementType(Obj, "None")

    Log("[Combo] Started: " .. comboName .. " (" .. #combo .. " hits)")
    return true
end

-- 콤보 다음 타격 실행
local function ExecuteNextComboHit(c)
    if not c.is_combo_attacking or not c.current_combo then
        return false
    end

    local combo = c.current_combo
    local hit = combo[c.combo_index]
    if not hit then
        -- 콤보 종료
        c.is_combo_attacking = false
        c.current_combo = nil
        c.combo_index = 0
        Log("[Combo] Finished")
        return false
    end

    -- 타겟 바라보기 (부드러운 회전)
    if c.target then
        local myPos = Obj.Location
        local targetPos = c.target.Location
        local targetYaw = CalcYaw(myPos, targetPos)
        local currentYaw = Obj.Rotation.Z
        local maxTurnDelta = Config.TurnSpeed * c.delta_time
        local newYaw = SmoothRotateToTarget(currentYaw, targetYaw, maxTurnDelta)
        Obj.Rotation = Vector(0, 0, newYaw)
    end

    -- 몽타주 재생 (GlobalAnimSpeed 적용)
    local success = PlayBossMontage(Obj, hit.montage)
    if not success then
        Log("[Combo] Failed to play montage: " .. hit.montage)
        c.is_combo_attacking = false
        c.current_combo = nil
        return false
    end

    -- 디버그 표시 업데이트
    SetBossPatternName(Obj, c.current_combo_name .. " (" .. c.combo_index .. "/" .. #combo .. ")")

    Log("[Combo] Hit " .. c.combo_index .. "/" .. #combo .. ": " .. hit.montage)

    -- 다음 타격 시간 설정
    if c.combo_index < #combo then
        c.combo_next_time = c.time + hit.delay
        c.combo_index = c.combo_index + 1
    else
        -- 마지막 타격
        c.combo_next_time = 0
        c.combo_index = c.combo_index + 1  -- 인덱스 넘겨서 다음에 종료되도록
    end

    c.last_attack_time = c.time
    return true
end

-- 콤보 업데이트 (매 Tick 호출)
local function UpdateCombo(c)
    if not c.is_combo_attacking then
        return
    end

    -- current_combo가 nil이면 콤보 종료
    if not c.current_combo then
        c.is_combo_attacking = false
        c.combo_index = 0
        Log("[Combo] No current_combo, resetting")
        return
    end

    -- ========================================================================
    -- 콤보 취소 조건 체크: 거리 / 각도
    -- ========================================================================
    if c.target then
        local myPos = Obj.Location
        local targetPos = c.target.Location
        local dist = VecDistance(myPos, targetPos)

        -- 거리 체크
        if dist > Config.ComboMaxDistance then
            c.is_combo_attacking = false
            c.current_combo = nil
            c.combo_index = 0
            c.is_strafing = false
            c.is_approaching = false
            SetBossPatternName(Obj, "")
            Log("[Combo] CANCELLED - Distance too far: " .. string.format("%.2f", dist) .. "m (max: " .. Config.ComboMaxDistance .. "m)")
            return
        end

        -- 각도 체크
        local targetYaw = CalcYaw(myPos, targetPos)
        local currentYaw = Obj.Rotation.Z
        local angleDiff = math.abs(NormalizeAngle(targetYaw - currentYaw))

        if angleDiff > Config.ComboMaxAngle then
            c.is_combo_attacking = false
            c.current_combo = nil
            c.combo_index = 0
            c.is_strafing = false
            c.is_approaching = false
            SetBossPatternName(Obj, "")
            Log("[Combo] CANCELLED - Angle too large: " .. string.format("%.1f", angleDiff) .. "° (max: " .. Config.ComboMaxAngle .. "°)")
            return
        end
    end

    -- 몽타주가 끝났고 다음 타격 시간이 되었으면
    local bMontagePlayling = IsMontagePlayling(Obj)

    if not bMontagePlayling then
        -- 콤보 다 끝났는지 체크
        if c.combo_index > #c.current_combo then
            c.is_combo_attacking = false
            c.current_combo = nil
            c.combo_index = 0
            c.is_strafing = false      -- Strafe 상태 리셋
            c.is_approaching = false   -- 접근 상태 리셋
            SetBossPatternName(Obj, "")  -- 패턴 이름 리셋
            Log("[Combo] Combo ended")

            -- 콤보 끝난 후 확률적으로 후퇴
            if ShouldRetreatAfterAttack(c) then
                StartRetreat(c)
            end
            return
        end

        -- 다음 타격 대기 시간 체크
        if c.combo_next_time > 0 and c.time >= c.combo_next_time then
            ExecuteNextComboHit(c)
        elseif c.combo_next_time == 0 then
            -- 첫 타격 또는 즉시 실행
            ExecuteNextComboHit(c)
        end
    end
end

-- 랜덤 공격 패턴 선택 및 실행
local function DoAttack(c)
    Log("  [Action] DoAttack")

    -- 몽타주 재생 중이면 공격 불가
    if IsMontagePlayling(Obj) then
        Log("    -> FAILURE (montage playing)")
        return BT_FAILURE
    end

    -- 타겟 바라보기 (부드러운 회전)
    if c.target then
        local myPos = Obj.Location
        local targetPos = c.target.Location
        local targetYaw = CalcYaw(myPos, targetPos)
        local currentYaw = Obj.Rotation.Z
        local maxTurnDelta = Config.TurnSpeed * c.delta_time
        local newYaw = SmoothRotateToTarget(currentYaw, targetYaw, maxTurnDelta)
        Obj.Rotation = Vector(0, 0, newYaw)
    end

    -- 페이즈에 따라 패턴 선택
    local pattern
    local maxPattern
    if c.phase >= 2 then
        -- Phase 2: 모든 패턴 사용 (0~3)
        maxPattern = 3
        pattern = math.random(0, maxPattern)
    else
        -- Phase 1: 기본 패턴만 (0~1)
        maxPattern = 1
        pattern = math.random(0, maxPattern)
    end

    -- 같은 패턴 연속 방지 (페이즈 범위 내에서)
    if pattern == c.last_pattern then
        pattern = (pattern + 1) % (maxPattern + 1)
    end
    c.last_pattern = pattern

    local atk = AttackPatterns[pattern]
    if not atk then
        Log("    -> FAILURE (invalid pattern)")
        return BT_FAILURE
    end
    -- ChargeAttack (패턴 2) 특수 처리: ChargeStart 먼저 재생
    if pattern == 2 then
        -- ChargeStart 몽타주 재생
        local success = PlayBossMontage(Obj, "ChargeStart")
        if not success then
            Log("    -> FAILURE (montage not found: ChargeStart)")
            return BT_FAILURE
        end
        c.is_charging = true
        SetBossPatternName(Obj, "ChargeAttack")
        Log("    -> ChargeAttack: Playing ChargeStart animation")
    else
        -- 일반 공격: 정상 속도로 몽타주 재생
        local success = PlayBossMontage(Obj, atk.name)
        if not success then
            Log("    -> FAILURE (montage not found: " .. atk.name .. ")")
            return BT_FAILURE
        end
        SetBossPatternName(Obj, atk.name)
    end

    -- 디버그 상태 설정
    SetBossAIState(Obj, "Attacking")
    SetBossMovementType(Obj, "None")

    -- 히트박스는 애님 노티파이에서 처리됨
    StartAttack(c, atk.name)
    return BT_SUCCESS
end

-- 콤보 공격 액션
local function DoComboAttack(c)
    Log("  [Action] DoComboAttack")

    -- 이미 콤보 중이면 계속 진행
    if c.is_combo_attacking then
        return BT_SUCCESS
    end

    -- 몽타주 재생 중이면 불가
    if IsMontagePlayling(Obj) then
        Log("    -> FAILURE (montage playing)")
        return BT_FAILURE
    end

    -- 랜덤 콤보 선택 및 시작
    local combo, comboName = SelectRandomCombo(c.phase)
    if StartCombo(c, comboName) then
        ExecuteNextComboHit(c)
        return BT_SUCCESS
    end

    return BT_FAILURE
end

-- 단일 공격 또는 콤보 공격 (페이즈에 따라 선택)
local function DoAttackOrCombo(c)
    Log("  [Action] DoAttackOrCombo (phase=" .. c.phase .. ")")

    -- 이미 콤보 중이면 계속 진행
    if c.is_combo_attacking then
        return DoComboAttack(c)
    end

    -- 몽타주 재생 중이면 불가
    if IsMontagePlayling(Obj) then
        Log("    -> FAILURE (montage playing)")
        return BT_FAILURE
    end

    -- 페이즈에 따라 공격 방식 결정
    if c.phase >= 2 then
        -- Phase 2: 콤보 공격만
        Log("    -> Phase 2: DoComboAttack")
        return DoComboAttack(c)
    else
        -- Phase 1: 단일 공격만
        Log("    -> Phase 1: DoAttack")
        return DoAttack(c)
    end
end

-- ============================================================================
-- 리액티브 공격 (플레이어 행동에 반응)
-- ============================================================================

-- 징벌 공격 (플레이어가 일찍 회피했을 때)
local function DoPunishAttack(c)
    Log("  [Action] DoPunishAttack - Punishing early dodge!")

    -- 몽타주 재생 중이면 불가
    if IsMontagePlayling(Obj) then
        return BT_FAILURE
    end

    -- 타겟 바라보기 (부드러운 회전)
    if c.target then
        local myPos = Obj.Location
        local targetPos = c.target.Location
        local targetYaw = CalcYaw(myPos, targetPos)
        local currentYaw = Obj.Rotation.Z
        local maxTurnDelta = Config.TurnSpeed * c.delta_time
        local newYaw = SmoothRotateToTarget(currentYaw, targetYaw, maxTurnDelta)
        Obj.Rotation = Vector(0, 0, newYaw)
    end

    -- 빠른 공격으로 징벌 (LightCombo)
    local success = PlayBossMontage(Obj, "PunishAttack")
    if success then
        SetBossPatternName(Obj, "PunishAttack")
        StartAttack(c, "PunishAttack")
        return BT_SUCCESS
    end
    return BT_FAILURE
end

-- 가드 브레이크 공격 (플레이어가 가드 중일 때)
-- 처음 느리게 시작 -> 일정 시간 후 정상 속도
local function DoGuardBreakAttack(c)
    Log("  [Action] DoGuardBreakAttack - Breaking guard!")

    if IsMontagePlayling(Obj) then
        return BT_FAILURE
    end

    -- 타겟 바라보기 (부드러운 회전)
    if c.target then
        local myPos = Obj.Location
        local targetPos = c.target.Location
        local targetYaw = CalcYaw(myPos, targetPos)
        local currentYaw = Obj.Rotation.Z
        local maxTurnDelta = Config.TurnSpeed * c.delta_time
        local newYaw = SmoothRotateToTarget(currentYaw, targetYaw, maxTurnDelta)
        Obj.Rotation = Vector(0, 0, newYaw)
    end

    -- 강공격으로 가드 브레이크 시도 (느리게 시작, GlobalAnimSpeed도 적용)
    local success = PlayBossMontage(Obj, "HeavySlam", Config.GuardBreakSlowRate)  -- 0.3배속으로 시작
    if success then
        SetBossPatternName(Obj, "GuardBreak")
        StartAttack(c, "GuardBreak")
        c.is_guard_breaking = true
        c.guard_break_start_time = c.time
        return BT_SUCCESS
    end
    return BT_FAILURE
end

-- 트레이드 공격 (플레이어가 공격 중일 때 슈퍼아머로 맞받아침)
local function DoTradeAttack(c)
    Log("  [Action] DoTradeAttack - Trading with player!")

    if IsMontagePlayling(Obj) then
        return BT_FAILURE
    end

    -- 타겟 바라보기 (부드러운 회전)
    if c.target then
        local myPos = Obj.Location
        local targetPos = c.target.Location
        local targetYaw = CalcYaw(myPos, targetPos)
        local currentYaw = Obj.Rotation.Z
        local maxTurnDelta = Config.TurnSpeed * c.delta_time
        local newYaw = SmoothRotateToTarget(currentYaw, targetYaw, maxTurnDelta)
        Obj.Rotation = Vector(0, 0, newYaw)
    end

    -- 슈퍼아머가 있는 강공격 사용
    local success = PlayBossMontage(Obj, "HeavySlam")
    if success then
        SetBossPatternName(Obj, "TradeAttack")
        StartAttack(c, "TradeAttack")
        return BT_SUCCESS
    end
    return BT_FAILURE
end

-- 갭 클로저 (플레이어가 멀어질 때 추격)
local function DoGapCloser(c)
    Log("  [Action] DoGapCloser - Closing the gap!")

    if IsMontagePlayling(Obj) then
        return BT_FAILURE
    end

    -- 타겟 바라보기 (부드러운 회전)
    if c.target then
        local myPos = Obj.Location
        local targetPos = c.target.Location
        local targetYaw = CalcYaw(myPos, targetPos)
        local currentYaw = Obj.Rotation.Z
        local maxTurnDelta = Config.TurnSpeed * c.delta_time
        local newYaw = SmoothRotateToTarget(currentYaw, targetYaw, maxTurnDelta)
        Obj.Rotation = Vector(0, 0, newYaw)
    end

    -- 돌진 공격
    local success = PlayBossMontage(Obj, "ChargeStart")
    if success then
        c.is_charging = true
        SetBossPatternName(Obj, "GapCloser")
        StartAttack(c, "GapCloser")
        return BT_SUCCESS
    end
    return BT_FAILURE
end

local function DoRetreat(c)
    Log("  [Action] DoRetreat")
    if not c.target then
        Log("    -> FAILURE (no target)")
        return BT_FAILURE
    end

    -- 몽타주 재생 중이면 이동 불가
    if IsMontagePlayling(Obj) then
        Log("    -> SKIP (montage playing)")
        return BT_SUCCESS
    end

    -- 후퇴 시작
    if not c.is_retreating then
        c.is_retreating = true
        c.retreat_end_time = c.time + Config.RetreatDuration
        Log("    -> Retreat started (duration=" .. Config.RetreatDuration .. "s)")
    end

    -- 후퇴 종료 체크
    if c.time >= c.retreat_end_time then
        c.is_retreating = false
        SetBossAIState(Obj, "Idle")
        SetBossMovementType(Obj, "None")
        Log("    -> Retreat ended")
        return BT_SUCCESS
    end

    local myPosRaw = Obj.Location
    local targetPosRaw = c.target.Location
    local myPos = { X = myPosRaw.X, Y = myPosRaw.Y, Z = myPosRaw.Z }
    local targetPos = { X = targetPosRaw.X, Y = targetPosRaw.Y, Z = targetPosRaw.Z }

    local dist = VecDistance(myPos, targetPos)

    -- 타겟을 바라보면서 후퇴 (부드러운 회전)
    local targetYaw = CalcYaw(myPos, targetPos)
    local currentYaw = Obj.Rotation.Z
    local maxTurnDelta = Config.TurnSpeed * c.delta_time
    local newYaw = SmoothRotateToTarget(currentYaw, targetYaw, maxTurnDelta)
    Obj.Rotation = Vector(0, 0, newYaw)

    -- 뒤로 이동 (AddMovementInput 사용)
    local dir = VecDirection(targetPos, myPos)  -- 타겟 반대 방향
    dir.Z = 0
    local moveDir = Vector(dir.X, dir.Y, dir.Z)
    AddMovementInput(Obj, moveDir, 1.0)

    -- 디버그 상태 설정
    SetBossAIState(Obj, "Retreating")
    SetBossMovementType(Obj, "Retreat")
    SetBossDistanceToPlayer(Obj, dist)

    Log("    -> Retreating...")
    return BT_RUNNING  -- 후퇴 중에는 RUNNING 반환
end

-- 후퇴 시작 함수 (공격 후 호출)
local function StartRetreat(c)
    c.is_retreating = true
    c.retreat_end_time = c.time + Config.RetreatDuration
    Log("  [Action] StartRetreat -> duration=" .. Config.RetreatDuration .. "s")
end

-- ============================================================================
-- Phase System (페이즈 시스템)
-- ============================================================================

local function CheckPhaseTransition(c)
    if c.phase >= 2 then return end

    -- 새로운 Lua 바인딩 함수 사용
    local currentHP = GetCurrentHealth(Obj)
    local maxHP = GetMaxHealth(Obj)
    local healthPercent = GetHealthPercent(Obj)

    --print("[Phase] HP: " .. currentHP .. "/" .. maxHP .. " = " .. string.format("%.1f%%", healthPercent * 100))
    if healthPercent <= Config.Phase2HealthThreshold then
        c.phase = 2
        print("*** PHASE TRANSITION: 1 -> 2 ***")
        -- 페이즈 전환 시 공격 상태 리셋
        c.is_combo_attacking = false
        c.current_combo = nil
        c.combo_index = 0
        c.was_attacking = false
        c.is_charging = false
        c.is_winding_up = false
        c.is_guard_breaking = false
        SetBossPatternName(Obj, "")

        local camMgr = GetCameraManager()
        if camMgr then
            camMgr:StartCameraShake(0.5, 0.3, 0.3, 30)
        end
    end
end

-- Wind-up (예비 동작) 속도 전환 업데이트
local function UpdateWindUp(c)
    if not c.is_winding_up then
        return
    end

    local elapsed = c.time - c.wind_up_start_time

    -- 예비 동작 시간이 지나면 실제 공격 속도로 전환
    if elapsed >= Config.WindUpDuration then
        local attackRate = Config.AttackSpeedRate * Config.GlobalAnimSpeed
        SetMontagePlayRate(Obj, attackRate)
        c.is_winding_up = false
        Log("[WindUp] Speed changed to attack: " .. attackRate)
    end
end

-- 가드 브레이크 속도 전환 업데이트
local function UpdateGuardBreak(c)
    if not c.is_guard_breaking then
        return
    end

    local elapsed = c.time - c.guard_break_start_time

    -- 느린 속도 유지 시간이 지나면 정상 속도로 전환 (GlobalAnimSpeed 적용)
    if elapsed >= Config.GuardBreakSlowDuration then
        local normalRate = Config.GuardBreakNormalRate * Config.GlobalAnimSpeed
        SetMontagePlayRate(Obj, normalRate)
        c.is_guard_breaking = false  -- 더 이상 체크 안함
        Log("[GuardBreak] Speed changed to normal: " .. normalRate)
    end
end

local function UpdateAttackState(c)
    -- Wind-up 속도 전환 체크
    UpdateWindUp(c)

    -- 가드 브레이크 속도 전환 체크
    UpdateGuardBreak(c)

    -- 몽타주 기반 공격 종료 체크
    local bMontagePlayling = IsMontagePlayling(Obj)

    -- 이전에 공격 중이었는데 몽타주가 끝났으면
    if c.was_attacking and not bMontagePlayling then
        -- 차지 중이었으면 ChargeAttack으로 이어서 재생
        if c.is_charging then
            Log("ChargeStart finished -> Playing ChargeAttack")
            local success = PlayBossMontage(Obj, "ChargeAttack")
            if success then
                c.is_charging = false  -- 차지 완료
                c.was_attacking = true -- 아직 공격 중
                return  -- 여기서 리턴해서 공격 종료 처리 안함
            end
        end

        -- 콤보 중이면 공격 종료 처리하지 않음 (UpdateCombo에서 처리)
        if c.is_combo_attacking then
            c.was_attacking = false  -- 다음 타격 대기 상태
            return
        end

        -- 일반 공격 종료 처리
        c.was_attacking = false
        c.is_charging = false
        c.is_guard_breaking = false  -- 가드 브레이크 상태 리셋
        c.is_winding_up = false      -- Wind-up 상태 리셋
        c.is_strafing = false      -- Strafe 상태 리셋 (다음에 다시 Strafe 가능)
        c.is_approaching = false   -- 접근 상태 리셋
        SetBossPatternName(Obj, "")  -- 패턴 이름 리셋
        Log("Attack ended (montage finished)")

        -- 히트박스는 애님 노티파이의 Duration으로 자동 비활성화됨

        -- 확률적으로 후퇴 시작
        if ShouldRetreatAfterAttack(c) then
            StartRetreat(c)
        end
    end

    -- 현재 몽타주 재생 상태 저장
    c.was_attacking = bMontagePlayling

    -- 후퇴 상태 업데이트
    if c.is_retreating and c.time >= c.retreat_end_time then
        c.is_retreating = false
        Log("Retreat ended (timeout)")
    end
end

-- ============================================================================
-- Behavior Tree 구성
-- ============================================================================

local BossTree = Selector({
    -- 1. 타겟 소실 시 재탐색
    Sequence({
        Condition(function(c)
            local result = not HasTarget(c) or IsTargetLost(c)
            Log("[Branch 1] TargetLost? " .. tostring(result))
            return result
        end),
        Action(DoFindTarget)
    }),

    -- 2. 공격 중이면 대기
    Sequence({
        Condition(function(c)
            Log("[Branch 2] Attacking?")
            return IsAttacking(c)
        end),
        Action(DoIdle)
    }),

    -- 3. 후퇴 중이면 후퇴 계속
    Sequence({
        Condition(function(c)
            Log("[Branch 3] Retreating?")
            return IsRetreating(c)
        end),
        Action(DoRetreat)
    }),

    -- ========================================================================
    -- 리액티브 AI 브랜치 (플레이어 행동에 반응)
    -- ========================================================================

    -- 4. [리액티브] 플레이어 회피 끝나는 타이밍에 징벌 공격
    Sequence({
        Condition(function(c)
            Log("[Branch 4] PunishDodge?")
            local inRange = false
            if c.target then
                local dist = VecDistance(Obj.Location, c.target.Location)
                inRange = dist <= Config.AttackRange * 2  -- 공격 범위 2배 내
            end
            return HasTarget(c) and IsPlayerDodgeEnding(c) and inRange and IsNotAttacking(c)
        end),
        Action(DoPunishAttack)
    }),

    -- 5. [리액티브] 플레이어가 계속 가드하면 가드 브레이크
    Sequence({
        Condition(function(c)
            Log("[Branch 5] GuardBreak?")
            return HasTarget(c) and IsPlayerCurrentlyBlocking(c) and IsTargetInAttackRange(c) and CanAttack(c)
        end),
        Action(DoGuardBreakAttack)
    }),

    -- 6. [리액티브] 플레이어가 공격 중일 때 트레이드 (Phase 2에서만)
    Sequence({
        Condition(function(c)
            Log("[Branch 6] Trade?")
            return HasTarget(c) and IsPhase2(c) and IsPlayerCurrentlyAttacking(c)
                   and IsTargetInAttackRange(c) and CanAttack(c) and math.random() < 0.4  -- 40% 확률
        end),
        Action(DoTradeAttack)
    }),

    -- 7. [리액티브] 플레이어가 멀어지면 갭 클로저
    Sequence({
        Condition(function(c)
            Log("[Branch 7] GapCloser?")
            if not c.target then return false end
            local dist = VecDistance(Obj.Location, c.target.Location)
            return HasTarget(c) and IsPlayerRetreating(c) and dist > Config.StrafeMaxRange
                   and CanAttack(c) and math.random() < 0.5  -- 50% 확률
        end),
        Action(DoGapCloser)
    }),

    -- ========================================================================
    -- 기존 행동 브랜치
    -- ========================================================================

    -- 8. 너무 가까우면 후퇴 (공격 쿨다운 중일 때)
    Sequence({
        Condition(function(c)
            Log("[Branch 8] TooClose?")
            return HasTarget(c) and IsTooClose(c) and IsNotAttacking(c) and not CanAttack(c)
        end),
        Action(DoRetreat)
    }),

    -- 9. 콤보 공격 중이면 계속 진행
    Sequence({
        Condition(function(c)
            Log("[Branch 9] ComboAttacking?")
            return c.is_combo_attacking
        end),
        Action(DoComboAttack)
    }),

    -- 10. 공격 범위 내 + 쿨다운 완료 → 단일 공격 또는 콤보 (50/50)
    Sequence({
        Condition(function(c)
            Log("[Branch 10] Attack?")
            return HasTarget(c) and IsTargetInAttackRange(c) and CanAttack(c)
        end),
        Action(DoAttackOrCombo)
    }),

    -- 11. 접근 중이면 계속 접근 (공격 범위까지)
    Sequence({ 
        Condition(function(c)
            Log("[Branch 11] Approaching?")
            return HasTarget(c) and IsCurrentlyApproaching(c) and IsNotAttacking(c)
        end),
        Action(DoApproach)
    }),

    -- 12. 일정 거리(4~8m)일 때 좌우 이동 (Strafe)
    Sequence({
        Condition(function(c)
            Log("[Branch 12] Strafe?")
            return HasTarget(c) and IsInStrafeRange(c) and IsNotAttacking(c) and not IsCurrentlyApproaching(c)
        end),
        Action(DoStrafe)
    }),

    -- 13. 추적 (공격 중이 아닐 때만)
    Sequence({
        Condition(function(c)
            Log("[Branch 13] Chase?")
            return HasTarget(c) and IsNotAttacking(c)
        end),
        Action(DoChase)
    }),

    -- 14. 기본 - 대기
    Action(function(c)
        Log("[Branch 14] Idle (fallback)")
        return DoIdle(c)
    end)
})

-- ============================================================================
-- Callbacks (엔진 콜백)
-- ============================================================================

function BeginPlay()
    Log("========== BeginPlay ==========")

    ctx.self_actor = Obj
    ctx.stats = GetComponent(Obj, "UStatsComponent")
    ctx.phase = 1
    ctx.time = 0
    ctx.was_attacking = false
    ctx.is_charging = false

    if ctx.stats then
        Log("Stats found: HP=" .. ctx.stats.CurrentHealth .. "/" .. ctx.stats.MaxHealth)
    else
        Log("WARNING: No UStatsComponent found!")
    end

    -- GetPlayer() 우선 시도
    ctx.target = GetPlayer()
    if ctx.target then
        Log("Initial target found via GetPlayer()")
    else
        ctx.target = FindObjectByName("Player")
        if ctx.target then
            Log("Initial target found via FindObjectByName")
        else
            Log("WARNING: Player not found!")
        end
    end
end

function Tick(Delta)
    ctx.time = ctx.time + Delta
    ctx.delta_time = Delta

    -- 매 틱마다 stats 갱신 (Lua 캐싱 문제 방지)
    ctx.stats = GetComponent(Obj, "UStatsComponent")

    -- 보스가 죽었으면 죽음 애니메이션 재생 후 AI 동작 중지
    -- 타겟이 있을 때만 체크 (게임 시작 시 초기화 방지)
    if ctx.target and GetCurrentHealth(Obj) <= 0 then

        if not ctx.is_death_animation_played then
            print("Boss is dead, playing death animation")

            -- 죽음 애니메이션 재생 (DeathMontage 사용)
            local success = PlayBossMontage(Obj, "Death",1.3)
            if success then
                print("Death animation played successfully")
            else
                print("WARNING: Death animation failed to play - montage may not be initialized")
            end

            ctx.is_death_animation_played = true
        end
        return
    end

    --print("[BossAI] Phase: " .. ctx.phase)

    -- 플레이어 상태 추적 업데이트
    UpdatePlayerState(ctx)

    -- 페이즈 전환 체크
    CheckPhaseTransition(ctx)

    -- 콤보 업데이트
    UpdateCombo(ctx)

    -- 공격 상태 업데이트
    UpdateAttackState(ctx)

    -- BT 실행
    BossTree(ctx)
end

function OnHit(OtherActor, HitInfo)
    local name = "Unknown"
    if OtherActor and OtherActor.Name then
        name = OtherActor.Name
    end
    Log("OnHit from: " .. name)

    if OtherActor and (OtherActor.Tag == "Player" or OtherActor.Tag == "PlayerWeapon") then
        local player = FindObjectByName("Player")
        if player then
            ctx.target = player
            Log("Target updated to attacker")
        end
    end
end

function EndPlay()
    Log("========== EndPlay ==========")
end
