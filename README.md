# The Meta Game — Giant Enemy Interior

I always wanted to make a game where the environment was the game itself.  I had some idea of making it a twin stick shooter, but I never could nail down the gameplay.  However, when GMTK release their 2026 game jam theme "Count Down", I knew what I had to do.  See, I was limited by the different ways a meta game could be made - index manipulation, file manipulation, the space is ripe for experiementation.  However, with "Count Down" as a theme, it made the design simple - shooting reduces the number of something, whether that is enemy health, rgb values in pixels, or the sample amplitude in a wav file.  The core gameplay involves reducing the coefficients of enemies as they grow to overwhelm you.

The main app is written in C++ with a lua script that acts as the mutable world which gets overwritten as you play.

## Playing

A run starts at level 10 and descends through combat arenas, shops, enemy
interiors, Player Internals, and Boss Interiors. Cleared arenas expose physical
portal choices instead of a separate map screen.

The first boss appears at level 0. Defeating a boss opens one portal to the
world-constant tuning room and another that continues into negative levels.  The tuning room exposes every constant possible and is very unsafe.
Further bosses appear every ten negative levels. Each negative decade adds 180
boss health and 2 damage to boss contact, bullets, and rockets.

Several parts of the world are directly mutable:

- Enemy organ values alter enemy behavior.
- Enemy sprite pixels form rooms and supply their wall colors.
- Shared Lua words can be corrupted everywhere they are referenced.
- The glyph arena edits the colors used by normal text rendering.
- The audio arena edits PCM samples used by later sound-effect voices.
- Numeric values in the post-boss tuning room alter Lua-backed world constants.

Sprite, wall-asset, glyph, word, world-constant, and Player Internals mutations
persist in `release/game/mutations.lua`. Run routes, currency, temporary
powerups, and enemy-difficulty stages are session-only. To restore the writable
world, audio, and mutations to their packaged defaults, close the game and run
`release/reset-game.exe`.

## Controls

- Move: WASD, arrow keys, or controller left stick.
- Aim: mouse or controller right stick.
- Primary fire: hold left mouse or the controller right trigger.
- Secondary weapon: right mouse or the controller left trigger.
- Reload the writable Lua world and return to the title screen: F5.
- Return to the title screen: R.
- Quit: Escape.

Developer builds also provide `P` for the run-local debug room and `O` to clear
the current encounter. Public no-debug builds compile both shortcuts out.

## Building

Run:

```bat
build.bat
```

Then launch `release/the-meta-game.exe`. To compile out the O/P debug commands:

```bat
build.bat DEBUG_COMMANDS=0
```

For a signed public package, authenticate once with `az login`, then run:

```bat
build-signed.bat -DebugCommands 0
```

The signing pipeline builds the selected configuration, signs and timestamps
both executables with Microsoft Artifact Signing, verifies their signatures,
and creates a clean ZIP under `dist`. Run signing last because another ordinary
build replaces the signed executables.

## Project layout

Project code is under `src`, immutable Lua and audio defaults are published
under `release/golden_scripts` and `release/golden_audio`, and writable runtime
copies live under the ignored `release/game` directory. Lua 5.4.8 is vendored
under `third_party/lua`.

Contributor and coding-agent invariants are maintained in
[AGENTS.md](AGENTS.md).

## Acknowledgments and licenses

- [Lua 5.4.8](https://www.lua.org/) is vendored in `third_party/lua`.
  Copyright © 1994–2025 Lua.org, PUC-Rio; Lua is distributed under the
  [MIT license](https://www.lua.org/license.html).
- The game's sound effects were created with
  [jsfxr](https://sfxr.me/), the HTML5 port of sfxr created by Eric Fredricksen
  and maintained by Chris McCormick.

Project-authored code is released under the Unlicense. Vendored and external
software retains its own license.
