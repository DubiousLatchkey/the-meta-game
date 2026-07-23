# The Meta Game — Giant Enemy Interior

The game is a Win32 C++ renderer and supervisor over a sandboxed Lua data layer.
The giant enemy's sprite is interpreted as a map: every occupied sprite pixel is
a screen-sized organ room, and cardinally adjacent pixels become doorways. A
camera follows the player through this room network.

Build with `build.bat`, then run `release/the-meta-game.exe`. The executable
resolves `golden_scripts` and the writable `game` directory beside itself, so
the whole `release` directory can be distributed as one unit.

The C++ host is split by behavior under `src/`: `app.cpp` owns Win32 startup,
`world.cpp` owns Lua data, persistence, and room generation, `gameplay.cpp`
owns simulation and combat, `rendering.cpp` owns presentation, and `state.cpp`
owns shared runtime state. The nmake build compiles only changed modules and
keeps the vendored Lua library cached between game-only builds.

Root WAV files are immutable audio sources copied by the build into
`release/golden_audio`. At runtime they seed writable copies under
`release/game/audio`, and playback always uses those altered copies. Pressing
`R` restores both the golden Lua world and golden audio. Every sound uses an
independent mixer voice, so rapid shots and simultaneous impacts finish without
interrupting one another.

## Controls and combat

- Move with WASD or the arrow keys.
- Hold the left mouse button to fire.
- Right-click to launch a bomb. Bombs travel 192 pixels, bounce off room walls,
  live spawners, shields, and physical text, pass through enemies, then explode
  in a 52.5-pixel radius that can also corrupt nearby text. Their cooldown is
  3 seconds.
- Circle and triangle contact removes one player health and destroys that
  enemy. Chargers deal contact damage only while dashing.
- Press F5 to reload the writable Lua world.
- Press R to restore the golden world and clear all persistent mutations.

## The enemy interior

- Room topology is generated from `enemies.circle.sprite` on each load. Empty
  sprite cells do not create rooms. Depleting a room's color does not remove it
  from the graph.
- The circle and its miniature copies begin as solid white sprites. Other rooms'
  walls show each pixel's composite color; the current room decompiles its walls
  into red, green, and blue thirds. A projectile hitting the left, middle, or
  right horizontal third decrements R, G, or B respectively.
- Each room receives 1–4 procedural spawners and always has at least one circle
  spawner while the current interior is a circle. `CIRCLE SPAWN PERCENT` begins
  at 50 and drops by 10 per hit, deterministically changing that percentage of
  additional spawners to circle output. Non-circle spawners are 60% triangle,
  25% charger, and 15% shooter. Triangles spawn in batches of 4–6, chargers in
  batches of 3–5, and shooters alone.
  A spawner has 5 health and only counts down while the player is in its room;
  the initial room waits three seconds before its first burst, while later room
  entries prime their first burst for half a second. Doors never lock.
  New enemies appear close to their spawner and fade in for 0.125 seconds; they
  can be damaged during the fade but cannot move or hurt the player. Existing
  active enemies pursue the player through connected rooms.
- One-health shooters are vertical rectangles. In the player's room they hold
  about 320 pixels away, track the player with a dotted one-second telegraph,
  lock their aim for the final quarter-second, and fire a 6-pixel-wide,
  660-pixel rail shot. Rails damage only the player and stop at walls or shields.
- Two-health chargers have square bodies with pointed heads. Within 240 pixels
  they lock the player's position for a half-second windup shown by inward
  particles and vibration, then dash 375 pixels toward that point at 600 pixels
  per second.
- The circle size, speed, and health rooms are spread through the graph. Their
  shootable values cycle `10..1`, `10..1`, and `3..1`. These values affect only
  circle enemies. Circle speed ranges from 50% to 100% of its normal speed;
  triangles, chargers, and shooters retain their own normal speeds.
- Every stat display is enclosed by a shield of destructible rectangular
  blocks, each starting with 10 health. The `CIRCLE SHIELD MAX HEALTH` room
  cycles from 10 down through 0 and back to 10. Lowering it caps every existing
  block immediately; raising the cap never heals damaged or destroyed blocks.
- The spawn-percentage room is placed far from the start. At 0%, it exposes the
  persistent `LEVEL 10` world value. Shooting that value decrements it to 9,
  selects level 9 from Lua's `levels` table, and currently displays its
  placeholder map.

## Shared mutable language

Every visible word in `world.lua` is a mutable table, and every phrase is a list
of references to those word tables. Shooting a character decrements its byte.
If several labels reference the same word, all of them immediately display the
same corruption. Organ value words are also physical, shootable text.

Player-caused sprite, word, and organ mutations are stored as readable Lua calls
in `release/game/mutations.lua`. The writable game directory is intentionally
ignored by Git; immutable defaults remain in `release/golden_scripts`.

Lua 5.4.8 is vendored in `third_party/lua` under its own MIT license. Project code
is released under the Unlicense.
