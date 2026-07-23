-- Immutable baseline copied to game/world.lua on first launch or when R is
-- pressed. Every visible word is a mutable table. Phrases contain references
-- to those tables so corrupting one word changes every phrase that shares it.
local function word(value) return { text = value } end
local words = {
    move = word("MOVE"), wasd = word("WASD"), fire = word("FIRE"),
    mouse = word("MOUSE"), bomb = word("BOMB"), right = word("RIGHT"),
    reload = word("RELOAD"), reset = word("RESET"), health = word("HEALTH"),
    size = word("SIZE"), speed = word("SPEED"), level = word("LEVEL"),
    shield = word("SHIELD"), max = word("MAX"), percent = word("PERCENT"),
    spawn = word("SPAWN"), placeholder = word("PLACEHOLDER"),
    room = word("ROOM"), circle = word("CIRCLE"), active = word("ACTIVE"),
    one = word("1"), three = word("3"), ten_size = word("10"),
    ten_speed = word("10"), ten_shield = word("10"),
    fifty_spawn = word("50"), ten_level = word("10"),
}
local text = {
    help = { words.move, words.wasd, words.fire, words.mouse,
             words.bomb, words.right, words.reload, words.reset },
    hud_health = { words.health },
    room = { words.room },
    size = { words.circle, words.size },
    speed = { words.circle, words.speed },
    health = { words.circle, words.health },
    shield = { words.circle, words.shield, words.max, words.health },
    spawn = { words.circle, words.spawn, words.percent },
    level = { words.level },
    placeholder = { words.placeholder },
}

local _ = false
local w = { 255, 255, 255 }

return {
    words = words,
    text = text,
    level = { label = text.level, value_text = { words.ten_level }, value = 10 },
    levels = {
        [10] = { map = "interior" },
        [9] = { map = "placeholder" },
    },

    enemies = {
        circle = {
            health = 3, speed = 3, contact_damage = 1, pixel_scale = 5,
            burst_min = 1, burst_max = 3,
            sprite = {
                { _, _, w, w, w, _, _ },
                { _, w, w, w, w, w, _ },
                { w, w, w, w, w, w, w },
                { w, w, w, w, w, w, w },
                { w, w, w, w, w, w, w },
                { _, w, w, w, w, w, _ },
                { _, _, w, w, w, _, _ },
            },
        },
        triangle = {
            -- Complete sprite data for a future triangle interior. In this
            -- level it is only rendered as a small spawned enemy.
            health = 1, speed = 5, contact_damage = 1, pixel_scale = 4,
            burst_min = 4, burst_max = 6, spawn_weight = 60,
            sprite = {
                { _, _, _, w, _, _, _ },
                { _, _, w, w, w, _, _ },
                { _, _, w, w, w, _, _ },
                { _, w, w, w, w, w, _ },
                { _, w, w, w, w, w, _ },
                { w, w, w, w, w, w, w },
                { w, w, w, w, w, w, w },
            },
        },
        charger = {
            health = 2, speed = 5, contact_damage = 1, pixel_scale = 4,
            burst_min = 3, burst_max = 5, spawn_weight = 25,
            attack_range = 240, windup_seconds = 0.5,
            attack_cooldown = 1, attack_distance = 375,
            attack_speed = 600,
            sprite = {
                { _, _, _, w, _, _, _ },
                { _, _, w, w, w, _, _ },
                { _, w, w, w, w, w, _ },
                { _, w, w, w, w, w, _ },
                { _, w, w, w, w, w, _ },
                { _, w, w, w, w, w, _ },
                { _, w, w, w, w, w, _ },
            },
        },
        shooter = {
            health = 1, speed = 4, contact_damage = 1, pixel_scale = 4,
            burst_min = 1, burst_max = 1, spawn_weight = 15,
            preferred_distance = 320, windup_seconds = 1,
            aim_lock_seconds = 0.25,
            attack_cooldown = 1.5, attack_distance = 660,
            sprite = {
                { _, w, w, w, _ },
                { _, w, w, w, _ },
                { _, w, w, w, _ },
                { _, w, w, w, _ },
                { _, w, w, w, _ },
                { _, w, w, w, _ },
                { _, w, w, w, _ },
            },
        },
    },

    interior = {
        archetype = "circle",
        enemy = "circle",
        alternate_enemy = "triangle",
        room_size = 960,
        seed = 7331,
        spawners_min = 1,
        spawners_max = 4,
        burst_min = 1,
        burst_max = 3,
        alternate_burst_min = 4,
        alternate_burst_max = 6,
        spawn_seconds_min = 3,
        spawn_seconds_max = 6,
        speed_unit = 4,
        organs = {
            size = { label = text.size, value_text = { words.ten_size }, value = 10, maximum = 10 },
            speed = { label = text.speed, value_text = { words.ten_speed }, value = 10, maximum = 10 },
            health = { label = text.health, value_text = { words.three }, value = 3, maximum = 3 },
            shield = { label = text.shield, value_text = { words.ten_shield }, value = 10, maximum = 10 },
            spawn = { label = text.spawn, value_text = { words.fifty_spawn }, value = 50, maximum = 100, decrement = 10 },
        },
    },
}
