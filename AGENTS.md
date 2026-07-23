# Project guidance

- This is a Win32 C++ renderer with a sandboxed Lua data layer. Build with `build.bat`.
- Keep behavior separated under `src`: platform startup in `app`, shared data in
  `state`, Lua/world generation in `world`, simulation in `gameplay`, and drawing
  in `rendering`. Preserve Makefile dependency-based incremental builds.
- Immutable release data lives in `release/golden_scripts`; runtime copies and
  persistent player mutations live in the ignored `release/game` directory.
- Root WAV sources build into `release/golden_audio`; playback uses writable
  copies in `release/game/audio` so future levels can corrupt sound safely.
- The selected enemy sprite is the room graph (currently `circle`): occupied
  cells are square rooms, cardinal neighbors are exits, and each cell's RGB
  values color its walls.
- Visible Lua text is composed from lists of shared mutable word-table references.
  Sprite RGB, word bytes, and organ values persist through `mutations.lua`.
- Procedural room spawners are active only while the player occupies their room;
  the farthest core room controls global spawning and the win state.
- Keep game logic/data in Lua where practical and keep the C++ host stable enough
  to recover corrupt Lua state.
- Keep project-authored code dedicated to the public domain under the Unlicense.
  Vendored dependencies retain their own licenses.
