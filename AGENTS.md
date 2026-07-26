# Project guidance

- This is a Win32 C++ renderer with a sandboxed Lua data layer. Build with `build.bat`.
- Keep behavior separated under `src`: platform startup in `app`, shared data in
  `state`, Lua/world generation in the `world_*` units, simulation in
  `gameplay`/`gameplay_*`, and drawing in `rendering`/`rendering_*`. Preserve
  Makefile dependency-based incremental builds.
- Immutable release data lives in `release/golden_scripts`; runtime copies and
  persistent player mutations live in the ignored `release/game` directory.
- Root WAV sources build into `release/golden_audio`; playback uses writable
  copies in `release/game/audio`. Level 9 treats two PCM samples as one mutable
  geometry pixel, updates DirectSound immediately, and batches WAV persistence.
- Lua levels occupy non-overlapping regions in one world coordinate space.
  Keep each level's simulation and geometry state separate, and sleep gameplay
  entities beyond five screen lengths from the player.
- The selected enemy sprite is the room graph (currently `circle`): occupied
  cells are square rooms, cardinal neighbors are exits, and each cell's RGB
  values color its walls.
- Visible Lua text is composed from lists of shared mutable word-table references.
  Printable 5x7 glyph shapes also live in Lua. Sprite RGB, glyph RGB, and word
  bytes persist through `mutations.lua`; run enemy-difficulty stages and their
  organ value displays are session-only.
- Procedural room spawners are active only while the player occupies their room.
  Every room contains at least one current-archetype spawner. The persistent
  level selector is exposed by its dedicated level geometry.
- Enemy stats, sprites, burst sizes, spawn weights, and special-attack tuning
  live in Lua. C++ owns charger/shooter state machines and rail collision.
- Keep audio decoding/playback/persistence in `audio` and audio-derived world
  collision/layout in `audio_level`.
- Keep level 8 rotated glyph-border layout and collision in `glyph_level`.
  Arena glyph pixels share the atlas used by normal text rendering.
- Keep deterministic run-choice generation in `roguelite`, generalized sealed
  collision/openings in `arena_level`, run combat in `gameplay`, and run
  presentation in `rendering`. Run-node UI must not create mutable `Word`
  records or persistence entries. Shop `RESET WORDS` restores only the
  pre-mutation Word-byte baseline.
- Roguelite tuning and reusable portal/powerup icon masks live in the immutable
  Lua world. Run choices, coins, offers, pickups, timers, and enemy-difficulty
  stages are session-only; death creates a fresh seed and restores baseline
  enemy difficulty / organ value displays while preserving cosmetic asset
  mutations and Player Internals progression. Only R resets writable Lua/audio
  assets and `mutations.lua`.
- Run controls remain WASD/arrows, held left-click fire, and right-click bomb.
  Player weapon base cadence, count, spread, speed, damage, range, width/radius,
  homing, and explosive behavior live in Lua; C++ resolves upgrades, powerups,
  and Player Internals alterations through the shared weapon-stat pipeline.
  Completed arenas expose direct portal choices; shops return to their source
  arena, while interior exits lead to independently randomized arenas.
  Shop purchases are made by shooting their numbered gameplay targets.
- Player Internals are offered about once per seven arena choices, enemy
  interiors once per three, and Boss Interiors once per four.
  Their persistent 3x3 arena-room grid allows one shield doorway break and one
  alteration per visit. Alterations seal the room for a rewardless wave, then
  expose an arena exit; repeatable ranks, one-time powers, and opened doorways
  persist in `mutations.lua` across deaths and reset only with R.
  Each alteration exposes its normalized Lua-backed numeric value; shooting it
  reduces the actual value by one, and every lower value improves the player.
- Boss Interior choices render the full boss oval room graph but only connect
  the assigned quadrant; clearing every turret target there disables that
  quadrant for the final boss of the current run only.
- The terminal Boss is a large sealed-arena oval with sweeping burst turrets and
  lock-then-ballistic rocket turrets. Destroying the body wins the run.
- P enters the run-local debug arena; repeatable upgrades, respawning powerups,
  and enemy spawner toggles must not award coins or advance progression.
- Keep game logic/data in Lua where practical and keep the C++ host stable enough
  to recover corrupt Lua state.
- Keep project-authored code dedicated to the public domain under the Unlicense.
  Vendored dependencies retain their own licenses.
