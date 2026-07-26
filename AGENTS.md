# Project guidance

- This is a Win32 C++ renderer with a sandboxed Lua data layer. Build with
  `build.bat`. The Makefile uses NMake batch-mode inference rules plus MSVC
  `/MP` so stale translation units compile together; preserve batching when
  changing compile rules. Release executables embed the shared application
  manifest and per-binary version resources from `src`; compile resources
  before linking, and apply any Authenticode signature only after the final
  link. Pass `DEBUG_COMMANDS=0` to `build.bat` to compile out the O/P debug
  shortcuts. Each configuration caches its own objects and linked game binary;
  the selected binary is copied to `release/the-meta-game.exe` on every build.
  For signed releases, pass `-DebugCommands 0` to `build-signed.bat`; the
  signing script forwards that selection into the build before signing. The
  signed ZIP excludes writable `release/game` state and the standalone reset
  utility even though both executables are signed and left under `release`.
- Keep behavior separated under `src`: platform startup in `app`, shared data in
  `state`, Lua/world generation in the `world_*` units, simulation in
  `gameplay`/`gameplay_*`, and drawing in `rendering`/`rendering_*`. Preserve
  Makefile dependency-based incremental builds.
- Rendering uses a fixed 1000x720 logical backbuffer, uniformly scaled with
  letterboxing into the Win32 client area. Keep camera, HUD, simulation range,
  mouse input, and controller aiming in logical coordinates; map client mouse
  coordinates through the presentation rectangle.
- Immutable release data lives in `release/golden_scripts`; runtime copies and
  persistent player mutations live in the ignored `release/game` directory.
- WAV sources under `assets/audio` build into `release/golden_audio`; playback
  uses writable copies in `release/game/audio`. Level 9 treats two PCM samples
  as one mutable geometry pixel, updates DirectSound immediately, and batches
  WAV persistence after three dirty seconds and on shutdown. Playback duplicates
  an independent DirectSound voice so overlapping effects do not interrupt one
  another.
- Lua levels occupy non-overlapping regions in one world coordinate space.
  Keep each level's simulation and geometry state separate, and sleep gameplay
  entities beyond five screen lengths from the player. Sleeping enemies and
  spawners must stay out of crowding, targeting, collision, and rendering hot
  paths; clear map-owned enemies and spawners whenever the simulation map
  changes.
- An Enemy Interior's selected archetype sprite is its room graph: occupied
  cells are square rooms, cardinal neighbors are exits, and each cell's RGB
  values color its walls.
- Visible Lua text is composed from lists of shared mutable word-table references.
  Printable 5x7 glyph shapes also live in Lua. Sprite RGB, glyph RGB, and word
  bytes persist through `mutations.lua`; run enemy-difficulty stages and their
  organ value displays are session-only.
- Procedural room spawners are active only while the player occupies their room.
  Every room contains at least one current-archetype spawner. The persistent
  level selector is exposed by its dedicated level geometry.
- Enemy stats, sprites, burst sizes, and special-attack tuning live in Lua.
  C++ owns charger/shooter state machines and rail collision.
- The opening arena is always baseline (level 10 with the golden defaults).
  Non-boss arenas at levels 9-5 raise one enemy stat by one stage; levels 4-2
  raise one stat by two stages; level 1 raises three distinct stats by two
  stages. Negative non-boss levels always raise three distinct stats using the
  multiplier described below. Spawn speed is a seventh enemy organ: Lua stores
  a normalized value whose minimum and baseline are 1; each difficulty stage
  adds one to the displayed value but only 10% to the actual spawn rate.
  Child capacity is an eighth normalized enemy organ: Lua value 1 caps each
  spawner at twice its current maximum herd size, and each stage adds one more
  herd-size multiple.
  Default spawn delays are 6-9 seconds. Base herd ranges are circle 1-3,
  triangle 3-5, charger 2-4, and shooter 1.
- Keep audio decoding/playback/persistence in `audio` and audio-derived world
  collision/layout in `audio_level`.
- Keep level 8 rotated glyph-border layout and collision in `glyph_level`.
  Arena glyph pixels share the atlas used by normal text rendering.
- Keep deterministic run-choice generation in `roguelite`, generalized sealed
  collision/openings in `arena_level`, run combat in `gameplay`, and run
  presentation in `rendering`. Portal and shop labels reuse the shared Lua world
  vocabulary instead of allocating new mutable `Word` records. Shop
  `RESET WORDS` restores only the pre-mutation Word-byte baseline.
- Roguelite tuning and reusable portal/powerup icon masks live in the immutable
  Lua world. Run choices, coins, offers, pickups, timers, and enemy-difficulty
  stages are session-only. Starting a new run from the title screen creates a
  fresh seed and restores baseline enemy difficulty / organ displays while
  preserving persisted asset, world-constant, and Player Internals mutations.
  R only returns to the title screen; `release/reset-game.exe` restores writable
  Lua/audio assets and removes `mutations.lua`.
- Run controls remain WASD/arrows, held left-click primary fire, and right-click
  secondary fire.
  Controller input maps the left/right sticks to movement/aim, the right
  trigger to held primary fire, and the left trigger to secondary fire.
  Player weapon base cadence, count, spread, speed, damage, range, width/radius,
  homing, and explosive behavior live in Lua; C++ resolves upgrades, powerups,
  and Player Internals alterations through the shared weapon-stat pipeline.
  Completed arenas expose direct portal choices; shops return to their source
  arena, while interior exits lead to independently randomized arenas.
  Shop offers and `RESET WORDS` are purchased by standing in their purchase
  areas for the Lua-configured duration.
- Enemy Interiors are offered after at most two misses or on a one-in-three
  roll; Boss Interiors use a one-in-four roll. Player Interiors can appear at
  most once before level 0 and once per negative ten-level band, with a
  three-in-fourteen chance from levels 10-5 and two-in-fourteen otherwise.
  Their persistent 3x3 room grid starts in the center. The first visit must
  unlock regeneration; later visits permit at most one permanent doorway break
  and one previously locked alteration. An alteration seals its room for a
  rewardless wave, then exposes an exit. Powers and opened doorways persist in
  `mutations.lua`; unlocking all nine powers makes later visits immediately
  traversable.
- A Boss Interior builds only its assigned quadrant of the boss sprite graph.
  Destroying all turret targets marks that quadrant disabled for the next boss;
  taking the post-boss continue portal clears the disabled-quadrant state.
- Recurring bosses are large moving ovals in sealed arenas with sweeping burst
  turrets and lock-then-ballistic rockets. Bosses occupy levels 0, -10, -20,
  and so on. Golden-default base health is 180; each negative decade adds 180
  health and a +2 damage bonus to contact, bullets, and rockets. Other boss
  tuning remains Lua-backed. Defeating a boss opens separate tuning-room and
  continue-descent portals across the arena's horizontal midline.
- Negative non-boss levels raise three distinct enemy stats. Their
  multiplier is two plus the completed negative decade, with another step at
  digits 5 and 9: -5 is X3, -9 is X4, -15 is X4, and -19 is X5.
- The post-boss tuning room is a dedicated, non-debug arena owned by
  `gameplay_tuning`. It lists scalar gameplay configuration exported by
  `world.lua` (excluding word/glyph/sprite assets); numeric values are
  shootable, decrement the actual Lua-backed scalar, and persist as
  `mutations.lua` overrides. Its exit returns directly to the title screen.
- Debug-enabled builds use P to enter the run-local debug arena and O to clear
  the current encounter. Repeatable upgrades, respawning powerups, enemy
  spawner toggles, and debug clears must not award coins or advance progression.
- Keep game logic/data in Lua where practical and keep the C++ host stable enough
  to recover corrupt Lua state.
- Keep project-authored code dedicated to the public domain under the Unlicense.
  Vendored dependencies retain their own licenses.
