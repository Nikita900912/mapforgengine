-- game.lua — шутер от первого лица.
-- WASD — ходьба, мышь — прицел, ЛКМ — огонь (можно зажать), Space — прыжок,
-- R — перезарядка, F5 — перезапуск, Esc — выход.
-- Нужные имена в карте: player, enemy*, wall*/box*/pillar* (твёрдые), health* (аптечки), ground.

local SPEED, GRAVITY, JUMP, SENS = 7, -25, 9, 0.0025
local EYE = 0.7                                 -- высота глаз над центром игрока
local MAG, FIRE_DELAY, RELOAD_TIME, DAMAGE = 12, 0.14, 1.3, 1
local ENEMY_HP, ENEMY_SPEED, ENEMY_DMG, ENEMY_REACH = 3, 3.2, 10, 1.7

local player, spawn_pos
local solids, targets_static = {}, {}   -- твёрдые объекты; всё, во что можно попасть (кроме врагов)
local enemies, packs, effects = {}, {}, {}
local hp, ammo, kills, total_enemies = 100, MAG, 0, 0
local vy, on_ground = 0, false
local yaw, pitch = 0, 0
local cooldown, reload_left, hit_timer, hurt_timer = 0, 0, 0, 0
local dead = false

local function starts_with(s, p) return s:sub(1, #p) == p end

-- ограничивающая коробка объекта (как в движке)
local function bounds(id)
    local x, y, z = get_pos(id)
    local sx, sy, sz = get_size(id)
    local t = get_type(id)
    local hx, hy, hz
    if t == "sphere" then hx, hy, hz = sx / 2, sx / 2, sx / 2
    elseif t == "cylinder" then hx, hy, hz = sx / 2, sy / 2, sx / 2
    elseif t == "plane" then hx, hy, hz = sx / 2, 0.05, sz / 2
    else hx, hy, hz = sx / 2, sy / 2, sz / 2 end
    return x - hx, y - hy, z - hz, x + hx, y + hy, z + hz
end

-- пересечение луча с коробкой (метод "slab"); возвращает расстояние или nil
local function ray_box(o, d, id)
    local x0, y0, z0, x1, y1, z1 = bounds(id)
    local lo, hi = {x0, y0, z0}, {x1, y1, z1}
    local tmin, tmax = 0, math.huge
    for i = 1, 3 do
        if math.abs(d[i]) < 1e-8 then
            if o[i] < lo[i] or o[i] > hi[i] then return nil end
        else
            local t1, t2 = (lo[i] - o[i]) / d[i], (hi[i] - o[i]) / d[i]
            if t1 > t2 then t1, t2 = t2, t1 end
            if t1 > tmin then tmin = t1 end
            if t2 < tmax then tmax = t2 end
            if tmin > tmax then return nil end
        end
    end
    return tmin
end

local function look_dir()
    local cp = math.cos(pitch)
    return -math.sin(yaw) * cp, math.sin(pitch), -math.cos(yaw) * cp
end

local function hits_solid(id)
    for _, s in ipairs(solids) do
        if overlaps(id, s) then return s end
    end
    return nil
end

-- сдвиг с откатом при столкновении; возвращает препятствие
local function move_by(id, dx, dy, dz)
    local x, y, z = get_pos(id)
    set_pos(id, x + dx, y + dy, z + dz)
    local hit = hits_solid(id)
    if hit then set_pos(id, x, y, z) end
    return hit
end

local function spark(x, y, z)
    local id = spawn("sphere", x, y, z, 0.18, 0.18, 0.18, 255, 220, 90)
    effects[#effects + 1] = {id = id, life = 0.12}
end

local function shoot()
    local ex, ey, ez = get_pos(player)
    local o = {ex, ey + EYE, ez}
    local dx, dy, dz = look_dir()
    local d = {dx, dy, dz}

    local best_t, best_enemy = math.huge, nil
    for _, id in ipairs(targets_static) do
        local t = ray_box(o, d, id)
        if t and t < best_t then best_t, best_enemy = t, nil end
    end
    for id, _ in pairs(enemies) do
        local t = ray_box(o, d, id)
        if t and t < best_t then best_t, best_enemy = t, id end
    end

    if best_t < math.huge then
        spark(o[1] + d[1] * best_t, o[2] + d[2] * best_t, o[3] + d[3] * best_t)
    end
    if best_enemy then
        local e = enemies[best_enemy]
        e.hp = e.hp - DAMAGE
        e.flash = 0.08
        hit_timer = 0.15
        if e.hp <= 0 then
            remove(best_enemy)
            enemies[best_enemy] = nil
            kills = kills + 1
        end
    end
end

function on_start()
    player = find("player")
    if not player then error("в карте нет объекта с именем 'player'") end
    spawn_pos = {get_pos(player)}

    for _, id in ipairs(objects()) do
        local n = get_name(id)
        if starts_with(n, "wall") or starts_with(n, "box") or starts_with(n, "pillar") then
            solids[#solids + 1] = id
            targets_static[#targets_static + 1] = id
        elseif starts_with(n, "ground") then
            targets_static[#targets_static + 1] = id
        elseif starts_with(n, "enemy") then
            local r, g, b = get_color(id)
            enemies[id] = {hp = ENEMY_HP, flash = 0, attack = 0, color = {r, g, b}}
            total_enemies = total_enemies + 1
        elseif starts_with(n, "health") then
            packs[#packs + 1] = id
        end
    end

    sky(40, 60, 110, 170, 190, 220)
    light(-0.4, -1, -0.5)
    lock_mouse(true)
end

local function update_enemies(dt, px, pz)
    for id, e in pairs(enemies) do
        local x, y, z = get_pos(id)
        local dx, dz = px - x, pz - z
        local dist = math.sqrt(dx * dx + dz * dz)

        if dist > ENEMY_REACH - 0.3 and dist < 35 then
            local sx, sz = dx / dist * ENEMY_SPEED * dt, dz / dist * ENEMY_SPEED * dt
            move_by(id, sx, 0, 0)
            move_by(id, 0, 0, sz)
        end

        e.attack = e.attack - dt
        if dist < ENEMY_REACH and e.attack <= 0 then
            hp = hp - ENEMY_DMG
            hurt_timer = 0.25
            e.attack = 0.8
        end

        if e.flash > 0 then
            e.flash = e.flash - dt
            set_color(id, 255, 255, 255)
        else
            set_color(id, e.color[1], e.color[2], e.color[3])
        end
    end
end

function on_update(dt)
    -- временные эффекты
    for i = #effects, 1, -1 do
        local f = effects[i]
        f.life = f.life - dt
        if f.life <= 0 then
            remove(f.id)
            table.remove(effects, i)
        end
    end
    hit_timer = math.max(0, hit_timer - dt)
    hurt_timer = math.max(0, hurt_timer - dt)
    cooldown = cooldown - dt

    -- прицел мышью
    local mx, my = mouse_delta()
    yaw = yaw - mx * SENS
    pitch = math.max(-1.45, math.min(1.45, pitch - my * SENS))

    local px, py, pz = get_pos(player)

    if not dead then
        -- движение относительно взгляда
        local fx, fz = -math.sin(yaw), -math.cos(yaw)
        local rx, rz = -fz, fx
        local ix, iz = 0, 0
        if key_down("W") then iz = iz + 1 end
        if key_down("S") then iz = iz - 1 end
        if key_down("D") then ix = ix + 1 end
        if key_down("A") then ix = ix - 1 end
        local len = math.sqrt(ix * ix + iz * iz)
        if len > 0 then ix, iz = ix / len, iz / len end
        move_by(player, (fx * iz + rx * ix) * SPEED * dt, 0, 0)
        move_by(player, 0, 0, (fz * iz + rz * ix) * SPEED * dt)

        -- прыжок и гравитация
        if on_ground and key_pressed("SPACE") then vy = JUMP end
        vy = vy + GRAVITY * dt
        on_ground = false
        local _, sy = get_size(player)
        local hit = move_by(player, 0, vy * dt, 0)
        if hit then
            if vy < 0 then
                local _, hy = get_pos(hit)
                local _, hsy = get_size(hit)
                local cx, _, cz = get_pos(player)
                set_pos(player, cx, hy + hsy / 2 + sy / 2, cz)
                on_ground = true
            end
            vy = 0
        end
        px, py, pz = get_pos(player)
        if py - sy / 2 <= 0 then
            set_pos(player, px, sy / 2, pz)
            vy, on_ground = 0, true
        end
        if py < -20 then
            set_pos(player, spawn_pos[1], spawn_pos[2], spawn_pos[3])
            vy = 0
        end
        px, py, pz = get_pos(player)

        -- оружие
        if reload_left > 0 then
            reload_left = reload_left - dt
            if reload_left <= 0 then ammo = MAG end
        else
            if key_pressed("R") and ammo < MAG then reload_left = RELOAD_TIME end
            if mouse_down(1) and cooldown <= 0 then
                if ammo > 0 then
                    shoot()
                    ammo = ammo - 1
                    cooldown = FIRE_DELAY
                    if ammo == 0 then reload_left = RELOAD_TIME end
                end
            end
        end

        update_enemies(dt, px, pz)

        -- аптечки
        for i = #packs, 1, -1 do
            if overlaps(player, packs[i]) then
                hp = math.min(100, hp + 25)
                remove(packs[i])
                table.remove(packs, i)
            end
        end

        if hp <= 0 then hp, dead = 0, true end
    end

    -- камера от первого лица
    local dx, dy, dz = look_dir()
    local ex, ey, ez = px, py + EYE, pz
    camera(ex, ey, ez, ex + dx, ey + dy, ez + dz, 75)
end

function on_draw2d()
    local w, h = screen_size()

    -- прицел (краснеет при попадании)
    if hit_timer > 0 then
        draw_text("X", w / 2 - 9, h / 2 - 14, 28, 255, 60, 60)
    else
        draw_text("+", w / 2 - 8, h / 2 - 14, 28, 255, 255, 255)
    end

    local alive = total_enemies - kills
    if hurt_timer > 0 then
        draw_text("HP: " .. hp, 20, h - 50, 32, 255, 70, 70)
    else
        draw_text("HP: " .. hp, 20, h - 50, 32, 255, 255, 255)
    end
    local ammo_text = reload_left > 0 and "RELOADING..." or ("Ammo: " .. ammo .. "/" .. MAG)
    draw_text(ammo_text, w - 260, h - 50, 32, 255, 230, 120)
    draw_text("Enemies: " .. alive, 20, 20, 28, 255, 255, 255)

    if dead then
        draw_text("YOU DIED  (F5 = restart)", w / 2 - 230, h / 2 - 80, 40, 255, 80, 80)
    elseif alive == 0 then
        draw_text("ALL ENEMIES DOWN!  (F5 = restart)", w / 2 - 300, h / 2 - 80, 40, 255, 230, 80)
    end
end
