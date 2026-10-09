-- game.lua — пример игры: собери все монеты.
-- WASD — ходьба, Space — прыжок, мышь — камера, F5 — перезагрузка, Esc — выход.

local SPEED, GRAVITY, JUMP = 6, -22, 9
local CAM_DIST, MOUSE_SENS = 7, 0.003

local player, start = nil, {0, 0, 0}
local solids = {}         -- стены и платформы (имена начинаются с wall/box/pillar)
local coin_base = {}      -- id монеты -> исходная высота (для покачивания)
local vy, on_ground = 0, false
local yaw, pitch = 0, 0.4
local score, total = 0, 0

local function starts_with(s, prefix) return s:sub(1, #prefix) == prefix end

local function hits_solid()
    for _, id in ipairs(solids) do
        if overlaps(player, id) then return id end
    end
    return nil
end

-- Сдвигает игрока по одной оси; при столкновении откатывает. Возвращает id препятствия.
local function move_by(dx, dy, dz)
    local x, y, z = get_pos(player)
    set_pos(player, x + dx, y + dy, z + dz)
    local hit = hits_solid()
    if hit then set_pos(player, x, y, z) end
    return hit
end

function on_start()
    player = find("player")
    if not player then
        error("в карте нет объекта с именем 'player' (выберите куб и нажмите N в редакторе)")
    end
    start = {get_pos(player)}

    for _, id in ipairs(objects()) do
        local name = get_name(id)
        if starts_with(name, "wall") or starts_with(name, "box") or starts_with(name, "pillar") then
            solids[#solids + 1] = id
        elseif starts_with(name, "coin") then
            total = total + 1
            local _, y = get_pos(id)
            coin_base[id] = y
        end
    end

    sky(60, 120, 200, 210, 230, 250)
    light(-0.5, -1, -0.35)
    lock_mouse(true)
end

function on_update(dt)
    -- камера мышью
    local mx, my = mouse_delta()
    yaw = yaw - mx * MOUSE_SENS
    pitch = math.max(-0.2, math.min(1.2, pitch + my * MOUSE_SENS))

    -- ввод относительно камеры
    local fx, fz = -math.sin(yaw), -math.cos(yaw)
    local rx, rz = -fz, fx
    local ix, iz = 0, 0
    if key_down("W") then iz = iz + 1 end
    if key_down("S") then iz = iz - 1 end
    if key_down("D") then ix = ix + 1 end
    if key_down("A") then ix = ix - 1 end
    local len = math.sqrt(ix * ix + iz * iz)
    if len > 0 then ix, iz = ix / len, iz / len end

    move_by((fx * iz + rx * ix) * SPEED * dt, 0, 0)
    move_by(0, 0, (fz * iz + rz * ix) * SPEED * dt)

    -- прыжок и гравитация
    if on_ground and key_pressed("SPACE") then vy = JUMP end
    vy = vy + GRAVITY * dt
    on_ground = false

    local _, sy = get_size(player)
    local hit = move_by(0, vy * dt, 0)
    if hit then
        if vy < 0 then
            -- приземлились на верх препятствия
            local _, hy = get_pos(hit)
            local _, hsy = get_size(hit)
            local px, _, pz = get_pos(player)
            set_pos(player, px, hy + hsy / 2 + sy / 2, pz)
            on_ground = true
        end
        vy = 0
    end

    local px, py, pz = get_pos(player)
    if py - sy / 2 <= 0 then              -- земля
        set_pos(player, px, sy / 2, pz)
        vy, on_ground = 0, true
    end
    if py < -20 then                      -- упал за карту
        set_pos(player, start[1], start[2], start[3])
        vy = 0
    end

    -- монеты
    local t = time()
    for id, base in pairs(coin_base) do
        if exists(id) then
            local cx, _, cz = get_pos(id)
            set_pos(id, cx, base + math.sin(t * 3 + id) * 0.15, cz)
            if overlaps(player, id) then
                remove(id)
                score = score + 1
            end
        end
    end

    -- камера от третьего лица
    px, py, pz = get_pos(player)
    local cp = math.cos(pitch)
    camera(px + CAM_DIST * cp * math.sin(yaw), py + 1 + CAM_DIST * math.sin(pitch), pz + CAM_DIST * cp * math.cos(yaw),
           px, py + 0.5, pz, 60)
end

function on_draw2d()
    draw_text("Coins: " .. score .. "/" .. total, 20, 20, 32, 255, 255, 255)
    if score >= total and total > 0 then
        local w, h = screen_size()
        draw_text("YOU WIN!  (F5 = restart)", w / 2 - 170, h / 2 - 20, 36, 255, 230, 80)
    end
end
