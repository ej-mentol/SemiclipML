# SemiclipML

**Warning:** This is an experimental version. Not recommended for production use on public servers. The build targets Windows.

You can still execute the stuck command (L) and walk through obstacles. v2.4 narrows the window for this (see changelog), but it has not been fully re-tested yet.

*Note: This project was developed with AI assistance. Collision logic and edict handling were fine-tuned for Sven Co-op.*

SemiclipML is a Metamod plugin for Sven Co-op that provides advanced player-to-player collision management. It allows players to pass through each other in tight spaces while maintaining tactical solid collision for boosting and landing.

## Features
* **Smart Collision Logic**: Automatically toggles collision based on player velocity, height difference, and proximity.
* **Fall Damage Protection**: Prevents accidental fall damage deaths by disabling collision during high-speed vertical movement.
* **Stacking Support**: Allows players to stand on each other's heads (boosting) while crouching or when standing still.
* **Dynamic Transparency**: Fades player models as they approach each other to improve visibility in crowded areas. Custom render modes set by maps/scripts are left untouched.
* **Multi-Mode Operation**: Includes a standard radius-based mode and a specialized "platform" mode for precise head-stacking.

## Configuration (CVars)
* `smc_enabled` (0/1): Toggle the plugin. Default: 1.
* `smc_mode` (0/1): Collision mode. 0 = Radius (Standard), 1 = Platform (Precise head-stacking). Default: 0.
* `smc_dist` (units): Maximum distance for semiclip activation. Default: 64.
* `smc_trans_dist` (units): Distance at which transparency fading begins. Default: 120.
* `smc_alpha` (0-255): Minimum transparency level when close to another player. Default: 120.

## Installation (server admins)
1. Grab `SemiclipML.dll` from [Releases](../../releases) (Windows).
2. Copy it to your Metamod plugins folder.
3. Add the plugin to your Metamod `plugins.ini` file.
4. Verify with `meta list` — you should see `SemiclipML v2.4`.

## Building from Source
Third-party headers (Metamod-P + patched HLSDK) come in as a git submodule, so clone recursively:

```
git clone --recursive https://github.com/ej-mentol/SemiclipML.git
cd SemiclipML
compile_win.bat
```

If you already cloned without `--recursive`, run `git submodule update --init --recursive` (the build script will also attempt this automatically).

Requirements: Visual Studio 2017–2026 with the "Desktop development with C++" workload (x86 tools), CMake 3.10+. The script auto-detects VS via vswhere, builds Release x86, and offers to deploy the DLL to your server (the chosen path is remembered in `deploy_path.local.txt`).

## Technical Overview
* **Logic Split**: Processing is divided between `PM_Move` (movement-time physents filtering) and `AddToFullPack` post (networked solid state and visuals per client).
* **Solid-State Sync (2.4)**: In radius mode, `pev->solid` is flipped for the duration of each player move and restored in the `PM_Move` post-hook, so server-side traces (lag compensation, unstuck logic) agree with movement physics. A runtime invariant self-heals and logs an error if the restore ever fails to run.
* **Entity Detection**: Uses `MOVETYPE_TOSS` and `deadflag` checks (resolved from the live edict) to distinguish between active players and corpses.
* **Sven Co-op Compatibility**: Skips `deadflag` entities in `AddToFullPack` to ensure transparency doesn't interfere with the game's revival system.

## Known Limitations
* **Engine Prediction**: Clients don't know about the plugin, so extreme network latency may cause minor visual jitter during semiclip transitions.
* **Water Occlusion**: Transparency is disabled underwater to prevent engine rendering glitches where players might pop through the water surface.

## Changelog
* **2.4** — Correctness release, no intentional physics changes: registered the missing `PM_Move` post-hook (fixes permanent `SOLID_NOT` leak), unambiguous solid-state tracking, fixed bounds aliasing in cap mode, `AddToFullPack` now respects the game dll's original return value, transparency no longer stomps custom render modes, thirdparty moved to a pinned submodule.
* **2.3** — Initial public version.

---
Third-party headers are provided by the [Metamod-P](https://github.com/Bots-United/metamod-p) submodule (pinned to `v1.21p109`), which bundles the patched HLSDK.
