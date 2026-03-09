-- ============================================================
-- monster.lua
-- NPC AI 스크립트
-- C++에서 등록된 API:
--   API_get_x(id)              -> number  : 해당 id의 x좌표 반환
--   API_get_y(id)              -> number  : 해당 id의 y좌표 반환
--   API_SendMessage(npc_id, player_id, msg) : 채팅 메시지 전송
-- ============================================================

-- NPC 고유 정보 (set_uid 에서 초기화)
myid    = 99999
my_x    = 0
my_y    = 0

WORLD_HEIGHT = 2000
WORLD_WIDTH  = 2000

-- ============================================================
-- set_uid : NPC 초기화 시 C++에서 호출
--   id  : NPC 고유 ID
--   x   : 초기 x 좌표
--   y   : 초기 y 좌표
-- ============================================================
function set_uid(id, x, y)
    myid = id
    my_x = x
    my_y = y
end

-- ============================================================
-- event_player_move : 플레이어가 이동했을 때 C++에서 호출
--   player : 이동한 플레이어 ID
-- 동작: 플레이어와 같은 좌표에 있으면 인사 메시지 전송
-- ============================================================
function event_player_move(player)
    local player_x = API_get_x(player)
    local player_y = API_get_y(player)
    my_x = API_get_x(myid)
    my_y = API_get_y(myid)

    if player_x == my_x and player_y == my_y then
        API_SendMessage(myid, player, "HELLO")
    end
end

-- ============================================================
-- event_NPC_move : NPC 이동 타이머 이벤트 시 C++에서 호출
--   target : 추적할 플레이어 ID
-- 반환값: true  -> C++에서 do_npc_move 실행
--         false -> 이동 중단 (타겟 없거나 이미 도달)
-- ============================================================
function event_NPC_move(target)
    my_x = API_get_x(myid)
    my_y = API_get_y(myid)

    local target_x = API_get_x(target)
    local target_y = API_get_y(target)

    -- 타겟과 같은 위치면 이동 중단 + 메시지 전송
    if my_x == target_x and my_y == target_y then
        API_SendMessage(myid, target, "CAUGHT YOU!")
        return false
    end

    -- 이동 계속
    return true
end

-- ============================================================
-- Random_Move : 랜덤 이동 좌표 계산 (현재 미사용 / Grid 전환 시 활용 예정)
--   x, y : 현재 좌표
-- 반환값: 이동 후 x, y
-- ============================================================
function Random_Move(x, y)
    local dir = math.random(4)
    if dir == 1 then
        if y > 0 then y = y - 1 end
    elseif dir == 2 then
        if y < WORLD_HEIGHT - 1 then y = y + 1 end
    elseif dir == 3 then
        if x > 0 then x = x - 1 end
    elseif dir == 4 then
        if x < WORLD_WIDTH - 1 then x = x + 1 end
    end
    return x, y
end
