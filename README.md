# The Meta Game — Giant Enemy Interior

The game is a Win32 C++ renderer and supervisor over a sandboxed Lua data layer.
The giant enemy's sprite is interpreted as a map: every occupied sprite pixel is
a screen-sized organ room, and cardinally adjacent pixels become doorways. A
camera follows the player through this room network.

Build with `build.bat`, then run `release/the-meta-game.exe`. The executable
resolves `golden_scripts` and the writable `game` directory beside itself, so
the whole `release` directory can be distributed as one unit.

For a signed public release, first sign in with `az login`, then run
`build-signed.bat`. It builds the game, selects the configured paid Azure
subscription, signs and timestamps both executables with Microsoft Artifact
Signing, verifies the signatures, and writes a clean signed ZIP under `dist`.
Use `build-signed.bat -DebugCommands 0` for a public build that compiles out
the O/P debug shortcuts.
The package deliberately excludes writable `release/game` player state and the
standalone reset utility. Any later invocation of `build.bat` can replace the
signed executables, so always finish release builds with `build-signed.bat`.

The C++ host is split by behavior under `src/`: `app.cpp` owns Win32 startup,
`world.cpp` owns Lua data, persistence, and room generation, `gameplay.cpp`
owns simulation and combat, `roguelite.cpp` owns deterministic run graphs,
`arena_level.cpp` owns sealed arena collision and portal openings,
`audio_level.cpp` maps editable PCM to level geometry, `rendering.cpp` owns
presentation, and `state.cpp` owns shared runtime state. The nmake build
compiles only changed modules and keeps the vendored Lua library cached between
game-only builds.

Root WAV files are immutable audio sources copied by the build into
`release/golden_audio`. At runtime they seed writable copies under
`release/game/audio`, and playback always uses those altered copies. Pressing
`R` restores both the golden Lua world and golden audio. Every sound uses an
independent mixer voice, so rapid shots and simultaneous impacts finish without
interrupting one another. Sample edits update the DirectSound cache immediately
and flush altered WAV files every three seconds and on shutdown.

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
- Press P during a run to enter (or reinitialize) the bounded debug room. Its
  center portal returns to the progression map.

## Roguelite vertical slice

- Launch and death start a fresh deterministic ten-depth run without resetting
  mutable sprites, glyphs, words, organs, or audio. F5 reloads the writable Lua
  data and starts a new run; R is the explicit full asset reset.
  Death and every fresh run clear session enemy-difficulty stages (arena
  downsides and organ reductions) and rewrite organ value displays to baseline,
  while leaving cosmetic pixel/glyph/word mutations and Player Internals
  progression intact.
- Arena, shop, and final boss nodes use a sealed arena. Completed encounters
  expose one physical, room-centered `EXIT TO MAP` portal without cutting the
  border; the boss remains terminal. The session-only map
  displays the complete horizontal DAG, and only the current node's immediate
  successors are physical choices.
- The penultimate depth always contains one or two alternative Boss Interior
  nodes, each assigned a distinct NW/NE/SW/SE quadrant. A route can clear only
  one of them before the boss. Entering one renders the full boss oval room
  graph while solid quadrant boundaries confine traversal to the assigned
  sector. Destroy every burst/rocket turret target in that quadrant to disable
  those weapons for the final fight of the current run, then take the map exit.
  Ordinary room-local spawners remain active. Death restores a full boss.
- The terminal boss is a large horizontal oval (~5x circle scale) with edge
  turrets that sweep back and forth while firing bursts, plus secondary
  rockets that track the player for a short window before flying straight.
  Turrets from the cleared quadrant are absent. Depleting boss body health
  wins the run; contact and enemy shots damage the player.
- Interior nodes are assigned a deterministic triangle, charger, or shooter
  before entry, so map labels name the target explicitly. Entering one
  loads that enemy's existing sprite-shaped room graph, text, shields, and
  room collision without arena waves. Its map exit remains hidden until the
  first organ value edit; ordinary word corruption does not unlock it.
- Every node shows health, coins, seed/depth, wave transition state, active
  multishot/homing/auto-rocket timers, and a compact visited/current/reachable
  graph. Arena walls visibly use deterministic occupied enemy sprites, complete
  rotated 5×7 glyph silhouettes, or contiguous PCM waveform sections selected
  from the node seed. A hidden sealed envelope supplies player collision
  without a prominent rectangular wall backing.
- Shops expose three shootable numbered targets. Each shows a distinct upgrade,
  its current downward-improving delay/time value, and a fixed coin price;
  purchased targets cannot charge twice. A separate `RESET WORDS` target
  restores only mutable Word bytes to the baseline captured from the loaded
  world script before persistent mutations are applied, then saves that repair.
  Glyph RGB, sprites/assets, organs, audio, and other mutation classes are
  untouched.
- Enemy deaths can drop reusable M/H/R powerup icons. The final node presents a
  live oval boss fight and a won-state HUD once the body is destroyed.
- Run graph state and coins are session-only. Existing asset mutations continue
  to persist only through `release/game/mutations.lua`; run UI markers never
  enter the mutable word table.
- Player Internals are distinct run nodes (about 12%, alongside the existing
  16% enemy Interior chance). Each visit uses a persistent 3x3 room network,
  starts at a deterministic edge room, and places the strongest alterations
  farthest away. Shooting one of nine targets seals that room for a large,
  deterministic rewardless wave; defeating it exposes the map exit.
- Move speed, fire interval, and extra projectile alterations stack without a
  cap. Regeneration, infinite multishot/homing/auto-rocket, dual
  Standard+Railgun primary fire, and dual Bomb+Homing Rocket secondary fire are
  one-time persistent unlocks. One doorway shield can be permanently broken
  per visit. These mutations persist in `mutations.lua` and are cleared by R.

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
  selects level 9 from Lua's `levels` table, and moves the player to that
  level's coordinates.

## Coordinate levels and audio corruption

- Lua levels are non-overlapping coordinate regions in one world space. Level
  10 begins at `(0, 0)` and the separate level 9 audio arena begins at
  `(12000, 0)`. The camera continues to use ordinary world coordinates.
- Enemy AI sleeps beyond five screen lengths from the player. Short-lived
  projectiles and attacks already expire well inside that radius.
- Level 9 parses `laserShoot.wav`, `hitEnemy.wav`, and `hitHurt.wav`; the longer
  `explosion.wav` is excluded. Every visible pixel averages two unsigned 8-bit
  PCM samples.
- Each sound appears as a cyan waveform mapping average sample value to vertical
  position. Shooting affects the hit audio pixel and two pixels on either side,
  subtracting 5 from all ten represented samples. New sound-effect voices use
  the edited DirectSound data, while altered WAV files are saved in batches
  under `release/game/audio`.
- Level 9 includes a shootable level selector leading to the level 8 glyph
  arena at `(17000, 0)`.

## Editable glyph arena

- Level 8 is a 2000×2160 arena—two initial screens wide and three tall—with all
  visible printable ASCII glyphs forming its border. Glyph tops rotate inward.
- Every occupied 5×7 glyph cell is a large 15-pixel square backed by mutable
  RGB. From the arena center cells use composite colors; approaching the border
  reveals separate red, green, and blue thirds.
- Projectiles always resolve the underlying RGB third and subtract 5. Bomb
  blasts damage nearby glyph cells the same way. These mutations immediately
  alter every normal text rendering of that character and persist through
  `set_glyph` entries in `mutations.lua`.
- Printable glyph shapes are defined by Lua row masks in `world.lua`; the C++
  text renderer consumes that loaded atlas rather than owning a hardcoded font.

## Shared mutable language

Every visible word in `world.lua` is a mutable table, and every phrase is a list
of references to those word tables. Shooting a character decrements its byte.
If several labels reference the same word, all of them immediately display the
same corruption. Organ value words are also physical, shootable text.
`EXIT TO MAP` uses these same shared Word/TextBox records, so its characters can
be corrupted without disabling the physical portal.

The P-key debug room has cost-free, repeatedly shootable controls for every run
upgrade, respawning multishot/homing/auto-rocket pickups, and run-local ON/OFF
spawner controls for circle, triangle, charger, and shooter enemies. Debug kills
do not grant coins or progression drops.

Player-caused sprite, glyph, word, and organ mutations are stored as readable
Lua calls in `release/game/mutations.lua`. The writable game directory is
intentionally ignored by Git; immutable defaults remain in
`release/golden_scripts`.

Lua 5.4.8 is vendored in `third_party/lua` under its own MIT license. Project code
is released under the Unlicense.
