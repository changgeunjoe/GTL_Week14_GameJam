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
    -- 공격 쿨다운
    last_attack_time = 0,           -- 마지막 공격 시간
    -- 몽타주 상태 추적
    was_attacking = false,          -- 이전 프레임 공격 상태 (몽타주 종료 감지용)
    -- 후퇴 관련
    is_retreating = false,          -- 후퇴 중인지
    retreat_end_time = 0,           -- 후퇴 종료 시간
    -- 차지 공격 관련
    is_charging = false,            -- ChargeStart 재생 중인지

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
    AttackRange = 3,            -- 3m 이내면 공격
    LoseTargetRange = 3000,
    MoveSpeed = 250,
    Phase2MoveSpeed = 350,
    Phase2HealthThreshold = 0.5,

    -- 좌우 이동(Strafe) 설정 (미터 단위)
    StrafeMinRange = 4,         -- 4m 이상일 때 좌우 이동 시작 (AttackRange보다 커야함)
    StrafeMaxRange = 8,         -- 8m 이하일 때 좌우 이동
    StrafeSpeed = 2,            -- 좌우 이동 속도 (m/s)
    StrafeChangeInterval = 1.5, -- 방향 전환 간격 (초)

    -- 후퇴 설정 (미터 단위)
    RetreatRange = 2,           -- 이 거리보다 가까우면 후퇴 (AttackRange보다 작아야함)
    RetreatChance = 0.3,        -- 공격 후 후퇴할 확률 (0~1)
    RetreatDuration = 1.0,      -- 후퇴 지속 시간 (초)

    -- 공격 쿨다운 (초)
    AttackCooldown = 2.0,       -- 공격 후 다음 공격까지 대기 시간

    -- 회전 보간 속도 (도/초)
    TurnSpeed = 180,            -- 초당 회전할 수 있는 최대 각도

    -- 차지 공격 설정 (ChargeStart 끝나면 ChargeAttack 재생)
}

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
    -- 몽타주 재생 중이면 공격 중으로 판단
    local bMontagePlayling = IsMontagePlayling(Obj)
    local result = not bMontagePlayling
    Log("  [Cond] IsNotAttacking: " .. tostring(result) .. " (montage=" .. tostring(bMontagePlayling) .. ")")
    return result
end

local function IsAttacking(c)
    -- 몽타주 재생 중이면 공격 중으로 판단
    local bMontagePlayling = IsMontagePlayling(Obj)
    Log("  [Cond] IsAttacking: " .. tostring(bMontagePlayling))
    return bMontagePlayling
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

    -- 타겟을 바라보도록 부드러운 회전
    local targetYaw = CalcYaw(myPos, targetPos)
    local currentYaw = Obj.Rotation.Z
    local maxTurnDelta = Config.TurnSpeed * c.delta_time
    local newYaw = SmoothRotateToTarget(currentYaw, targetYaw, maxTurnDelta)
    Obj.Rotation = Vector(0, 0, newYaw)

    -- CharacterMovementComponent 사용
    local moveDir = Vector(dir.X, dir.Y, dir.Z)
    AddMovementInput(Obj, moveDir, 1.0)

    Log("  [Action] DoChase -> Moving (dist=" .. string.format("%.1f", dist) .. ", yaw=" .. string.format("%.1f", newYaw) .. " -> " .. string.format("%.1f", targetYaw) .. ")")
    return BT_SUCCESS
end

local function DoIdle(c)
    Log("  [Action] DoIdle")
    return BT_SUCCESS
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
    Log("  [Action] DoStrafe -> Moving " .. (c.strafe_direction == 1 and "Right" or "Left") .. " (dist=" .. string.format("%.2f", dist) .. "m)")
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
-- 리액티브 공격 (플레이어 행동에 반응)
-- ============================================================================

-- 징벌 공격 (플레이어가 일찍 회피했을 때)
local function DoPunishAttack(c)
    Log("  [Action] DoPunishAttack - Punishing early dodge!")

    -- 몽타주 재생 중이면 불가
    if IsMontagePlayling(Obj) then
        return BT_FAILURE
    end

    -- 타겟 바라보기
    if c.target then
        local myPos = Obj.Location
        local targetPos = c.target.Location
        local yaw = CalcYaw(myPos, targetPos)
        Obj.Rotation = Vector(0, 0, yaw)
    end

    -- 빠른 공격으로 징벌 (LightCombo 또는 ChargeAttack)
    local success = PlayMontage(Obj, "LightCombo")
    if success then
        StartAttack(c, "PunishAttack (LightCombo)")
        return BT_SUCCESS
    end
    return BT_FAILURE
end

-- 가드 브레이크 공격 (플레이어가 가드 중일 때)
local function DoGuardBreakAttack(c)
    Log("  [Action] DoGuardBreakAttack - Breaking guard!")

    if IsMontagePlayling(Obj) then
        return BT_FAILURE
    end

    if c.target then
        local myPos = Obj.Location
        local targetPos = c.target.Location
        local yaw = CalcYaw(myPos, targetPos)
        Obj.Rotation = Vector(0, 0, yaw)
    end

    -- 강공격으로 가드 브레이크 시도 (HeavySlam 또는 SpinAttack)
    local success = PlayMontage(Obj, "HeavySlam")
    if success then
        StartAttack(c, "GuardBreak (HeavySlam)")
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

    if c.target then
        local myPos = Obj.Location
        local targetPos = c.target.Location
        local yaw = CalcYaw(myPos, targetPos)
        Obj.Rotation = Vector(0, 0, yaw)
    end

    -- 슈퍼아머가 있는 강공격 사용
    local success = PlayMontage(Obj, "HeavySlam")
    if success then
        StartAttack(c, "Trade (HeavySlam)")
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

    if c.target then
        local myPos = Obj.Location
        local targetPos = c.target.Location
        local yaw = CalcYaw(myPos, targetPos)
        Obj.Rotation = Vector(0, 0, yaw)
    end

    -- 돌진 공격
    local success = PlayMontage(Obj, "ChargeStart")
    if success then
        c.is_charging = true
        StartAttack(c, "GapCloser (ChargeAttack)")
        return BT_SUCCESS
    end
    return BT_FAILURE
end

-- 랜덤 공격 패턴 선택 및 실행
local function DoAttack(c)
    Log("  [Action] DoAttack")

    -- 몽타주 재생 중이면 공격 불가
    if IsMontagePlayling(Obj) then
        Log("    -> FAILURE (montage playing)")
        return BT_FAILURE
    end

    -- 타겟 바라보기
    if c.target then
        local myPos = Obj.Location
        local targetPos = c.target.Location
        local yaw = CalcYaw(myPos, targetPos)
        Obj.Rotation = Vector(0, 0, yaw)
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

   -- pattern=2;
    local atk = AttackPatterns[pattern]
    if not atk then
        Log("    -> FAILURE (invalid pattern)")
        return BT_FAILURE
    end
    -- ChargeAttack (패턴 2) 특수 처리: ChargeStart 먼저 재생
    if pattern == 2 then
        -- ChargeStart 몽타주 재생
        local success = PlayMontage(Obj, "ChargeStart")
        if not success then
            Log("    -> FAILURE (montage not found: ChargeStart)")
            return BT_FAILURE
        end
        c.is_charging = true
        Log("    -> ChargeAttack: Playing ChargeStart animation")
    else
        -- 일반 공격: 정상 속도로 몽타주 재생
        local success = PlayMontage(Obj, atk.name)
        if not success then
            Log("    -> FAILURE (montage not found: " .. atk.name .. ")")
            return BT_FAILURE
        end
    end

    -- 히트박스는 애님 노티파이에서 처리됨
    StartAttack(c, atk.name)
    return BT_SUCCESS
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
        Log("    -> Retreat ended")
        return BT_SUCCESS
    end

    local myPosRaw = Obj.Location
    local targetPosRaw = c.target.Location
    local myPos = { X = myPosRaw.X, Y = myPosRaw.Y, Z = myPosRaw.Z }
    local targetPos = { X = targetPosRaw.X, Y = targetPosRaw.Y, Z = targetPosRaw.Z }

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
    if not c.stats or c.phase >= 2 then return end

    local healthPercent = c.stats.CurrentHealth / c.stats.MaxHealth
    if healthPercent <= Config.Phase2HealthThreshold then
        c.phase = 2
        Log("*** PHASE TRANSITION: 1 -> 2 ***")

        local camMgr = GetCameraManager()
        if camMgr then
            camMgr:StartCameraShake(0.5, 0.3, 0.3, 30)
        end
    end
end

local function UpdateAttackState(c)
    -- 몽타주 기반 공격 종료 체크
    local bMontagePlayling = IsMontagePlayling(Obj)

    -- 이전에 공격 중이었는데 몽타주가 끝났으면
    if c.was_attacking and not bMontagePlayling then
        -- 차지 중이었으면 ChargeAttack으로 이어서 재생
        if c.is_charging then
            Log("ChargeStart finished -> Playing ChargeAttack")
            local success = PlayMontage(Obj, "ChargeAttack")
            if success then
                c.is_charging = false  -- 차지 완료
                c.was_attacking = true -- 아직 공격 중
                return  -- 여기서 리턴해서 공격 종료 처리 안함
            end
        end

        -- 일반 공격 종료 처리
        c.was_attacking = false
        c.is_charging = false
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

    -- 9. 공격 범위 내 + 쿨다운 완료 → 공격
    Sequence({
        Condition(function(c)
            Log("[Branch 9] Attack?")
            return HasTarget(c) and IsTargetInAttackRange(c) and CanAttack(c)
        end),
        Action(DoAttack)
    }),

    -- 10. 일정 거리(4~8m)일 때 좌우 이동
    Sequence({
        Condition(function(c)
            Log("[Branch 10] Strafe?")
            return HasTarget(c) and IsInStrafeRange(c) and IsNotAttacking(c)
        end),
        Action(DoStrafe)
    }),

    -- 11. 추적 (공격 중이 아닐 때만)
    Sequence({
        Condition(function(c)
            Log("[Branch 11] Chase?")
            return HasTarget(c) and IsNotAttacking(c)
        end),
        Action(DoChase)
    }),

    -- 12. 기본 - 대기
    Action(function(c)
        Log("[Branch 12] Idle (fallback)")
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

    -- 플레이어 상태 추적 업데이트
    UpdatePlayerState(ctx)

    -- 페이즈 전환 체크
    CheckPhaseTransition(ctx)

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
