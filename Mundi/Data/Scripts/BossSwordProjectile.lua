-- BossSwordProjectile.lua
-- 보스 이기어검 스타일 칼 투사체
-- RotatingMovementComponent와 ProjectileMovementComponent 활용

local projectileComp = nil   -- ProjectileMovementComponent
local rotatingComp = nil     -- RotatingMovementComponent
local hitboxComp = nil       -- HitboxComponent

local hoverDuration = 0.6    -- 머리 위에서 대기하는 시간
local hoverTimer = 0         -- 대기 타이머
local isHovering = true      -- 현재 대기 중인지
local hasLaunched = false    -- 발사됐는지

local damage = 60            -- 데미지
local speed = 30             -- 발사 속도 (m/s)

function BeginPlay()
    Obj.Tag = "BossSwordProjectile"
    hoverTimer = 0
    isHovering = true
    hasLaunched = false

    -- 컴포넌트 가져오기
    projectileComp = GetComponent(Obj, "UProjectileMovementComponent")
    rotatingComp = GetComponent(Obj, "URotatingMovementComponent")
    hitboxComp = GetComponent(Obj, "UHitboxComponent")

    -- 초기 상태: 회전만 활성화, 이동은 비활성화
    if rotatingComp then
        rotatingComp.bIsActive = true
        rotatingComp.RotationRate = Vector(0, 0, 720)  -- 빠른 회전
    end

    if projectileComp then
        projectileComp.bIsActive = false
    end

    print("[BossSword] Spawned - hovering")
end

function EndPlay()
    print("[BossSword] Destroyed")
end

function Tick(dt)
    if isHovering then
        hoverTimer = hoverTimer + dt

        -- 살짝 위아래 흔들림
        local wobble = math.sin(hoverTimer * 10) * 0.02
        local loc = Obj.Location
        Obj.Location = Vector(loc.X, loc.Y, loc.Z + wobble)

        -- 대기 시간 완료 -> 발사
        if hoverTimer >= hoverDuration and not hasLaunched then
            LaunchTowardPlayer()
        end
    end
end

function LaunchTowardPlayer()
    hasLaunched = true
    isHovering = false

    -- 플레이어 방향 계산
    local player = GetPlayer()
    if player then
        local myPos = Obj.Location
        local targetPos = player.Location

        -- 방향 벡터
        local dirX = targetPos.X - myPos.X
        local dirY = targetPos.Y - myPos.Y
        local dirZ = (targetPos.Z + 1.0) - myPos.Z  -- 플레이어 중심 높이

        -- 정규화
        local len = math.sqrt(dirX*dirX + dirY*dirY + dirZ*dirZ)
        if len > 0 then
            dirX = dirX / len
            dirY = dirY / len
            dirZ = dirZ / len
        end

        -- ProjectileMovementComponent 활성화 및 발사
        if projectileComp then
            projectileComp.bIsActive = true
            projectileComp.InitialSpeed = speed
            projectileComp.MaxSpeed = speed
            projectileComp.Gravity = 0  -- 중력 없음
            projectileComp:FireInDirection(Vector(dirX, dirY, dirZ))
        end

        -- 칼 회전 (진행 방향)
        local yaw = math.deg(math.atan2(dirY, dirX))
        Obj.Rotation = Vector(0, 90, yaw)  -- 칼날이 진행 방향

        print("[BossSword] Launched toward player!")
    end

    -- 히트박스 활성화
    EnableHitbox(Obj, damage, "Heavy", 0.3, 0.3, 1.5)

    -- 발사 후 회전 느리게
    if rotatingComp then
        rotatingComp.RotationRate = Vector(0, 0, 180)
    end
end

function OnBeginOverlap(OtherActor)
    if OtherActor and OtherActor.Tag == "Player" then
        print("[BossSword] Hit player!")
        -- 히트 후 삭제
        DeleteObject(Obj)
    end
end

function OnEndOverlap(OtherActor)
end

-- 외부에서 호출 가능한 함수들
function SetHoverDuration(duration)
    hoverDuration = duration
end

function SetDamage(newDamage)
    damage = newDamage
end

function SetSpeed(newSpeed)
    speed = newSpeed
end
