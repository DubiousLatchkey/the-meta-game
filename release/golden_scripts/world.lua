-- Immutable baseline copied to game/world.lua on first launch or when R is
-- pressed. Every visible word is a mutable table. Phrases contain references
-- to those tables so corrupting one word changes every phrase that shares it.
local function word(value) return { text = value } end
local words = {
    move = word("MOVE"), wasd = word("WASD"), fire = word("FIRE"),
    mouse = word("MOUSE"), bomb = word("BOMB"), right = word("RIGHT"),
    reload = word("RELOAD"), reset = word("RESET"), health = word("HEALTH"),
    size = word("SIZE"), speed = word("SPEED"), core = word("CORE"),
    shield = word("SHIELD"), max = word("MAX"),
    spawn = word("SPAWN"), disabled = word("DISABLED"), victory = word("VICTORY"),
    room = word("ROOM"), circle = word("CIRCLE"), active = word("ACTIVE"),
    one = word("1"), three = word("3"), ten_size = word("10"),
    ten_speed = word("10"), ten_shield = word("10"),
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
    core = { words.circle, words.core, words.spawn },
    victory = { words.victory, words.spawn, words.disabled },
}

local _ = false
local w = { 255, 255, 255 }

return {
    words = words,
    text = text,

    enemies = {
        circle = {
            health = 3, speed = 3, contact_damage = 1, pixel_scale = 5,
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
            core = { label = text.core, value_text = { words.one }, value = 1, maximum = 1 },
        },
    },
}
