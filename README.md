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
- Enemy contact removes one player health and destroys that enemy.
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
- Each room receives 1–4 procedural spawners, split evenly between circle and
  triangle output. Triangle spawners release 4–6 small one-health triangles.
  A spawner has 5 health and only counts down while the player is in its room;
  the initial room waits three seconds before its first burst, while later room
  entries prime their first burst for half a second. Doors never lock.
  New enemies appear close to their spawner and fade in for 0.125 seconds; they
  can be damaged during the fade but cannot move or hurt the player. Existing
  active enemies pursue the player through connected rooms.
- The circle size, speed, and health rooms are spread through the graph. Their
  shootable values cycle `10..1`, `10..1`, and `3..1`. These values affect only
  circle enemies. Circle speed ranges from 50% to 100% of its normal speed;
  triangles retain their own normal speed.
- Every stat display is enclosed by a shield of destructible rectangular
  blocks, each starting with 10 health. The `CIRCLE SHIELD MAX HEALTH` room
  cycles from 10 down through 0 and back to 10. Lowering it caps every existing
  block immediately; raising the cap never heals damaged or destroyed blocks.
- The core is placed in a farthest room. Shooting its spawn value from 1 to 0
  disables every spawner and wins the level.

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
