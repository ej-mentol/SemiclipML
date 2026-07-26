# SemiclipML

**Warning:** Windows build. Tested on a LAN server with bots; broader multiplayer testing is ongoing.

*Note: This project was developed with AI assistance. Collision logic and edict handling were fine-tuned for Sven Co-op.*

SemiclipML is a Metamod plugin for Sven Co-op that provides advanced player-to-player collision management. Players pass through each other in tight spaces while keeping tactical solid collision for boosting and landing — and with fire-through enabled, shots and projectiles pass through semiclipped teammates instead of hitting them.

## Features
* **Smart Collision Logic**: Automatically toggles collision based on player velocity, height difference, and proximity.
* **Fire-Through** (`smc_firethrough`): hitscan shots and physical projectiles (bolts, contact/timed grenades) pass through players you are currently semiclipped with. No more team-wiping a friend who ran across your grenade launcher on a 1 HP server. Projectiles phase through the body and continue flying; against players outside the semiclip state everything hits as normal.
* **Fall Damage Protection**: Prevents accidental fall damage deaths by disabling collision during high-speed vertical movement.
* **Stacking Support**: Players can stand on each other's heads (boosting) while crouching or standing still.
* **Dynamic Transparency**: Fades player models as they approach each other. Custom render modes set by maps/scripts are left untouched. Pass-through always mirrors transparency: if they're faded for you, your shots pass through them.
* **Multi-Mode Operation**: Standard radius mode and a "platform" mode for precise head-stacking.
* **Admin Diagnostics** (`smc_status`): server console command dumping per-client solid/group/render state and the plugin's tracking flags.

## Configuration (CVars)
* `smc_enabled` (0/1): Toggle the plugin. Default: 1. Takes effect immediately; a status line is printed to the server console on every switch.
* `smc_firethrough` (0/1): Fire-through (see above). **Experimental — off by default.** Enable consciously and read the grey-zone section below first.
* `smc_mode` (0/1): Collision mode. 0 = Radius (Standard), 1 = Platform (Precise head-stacking). Default: 0.
* `smc_dist` (units): Maximum distance for semiclip activation. Default: 64.
* `smc_trans_dist` (units): Distance at which transparency fading begins. Default: 120.
* `smc_alpha` (0-255): Minimum transparency level when close to another player. Default: 120.

All cvars are read live through the engine every frame — console changes apply instantly, no map reload needed. (Sven's engine keeps registered cvars in its own storage; reading a locally registered `cvar_t` returns a frozen default. This plugin reads by name once per frame instead. If you develop Metamod plugins for Sven Co-op: you have to do this too.)

## Installation (server admins)
1. Grab `SemiclipML.dll` from [Releases](../../releases) (Windows).
2. Copy it to your Metamod plugins folder.
3. Add the plugin to your Metamod `plugins.ini` file.
4. Verify with `meta list` — you should see `SemiclipML v2.5`.

## Building from Source
Third-party headers (Metamod-P + patched HLSDK) come in as a git submodule, so clone recursively:

```
git clone --recursive https://github.com/ej-mentol/SemiclipML.git
cd SemiclipML
compile_win.bat
```

If you already cloned without `--recursive`, run `git submodule update --init --recursive` (the build script also attempts this automatically). Requirements: Visual Studio 2017–2026 with the "Desktop development with C++" workload (x86 tools), CMake 3.10+. The script performs a clean build every run and offers to deploy the DLL to your server (the chosen path is remembered in `deploy_path.local.txt`).

## Technical Overview
* **Movement**: `PM_Move` pre-hook filters the physents list; `pev->solid` is flipped for the duration of each player move and restored in the post-hook, so server-side traces agree with movement physics. A runtime invariant self-heals and logs an error if a restore ever fails to run.
* **Hitscan fire-through**: the same `pev->solid` flip, widened to the whole player command (`PlayerPreThink` pre → `PlayerPostThink` post), so weapon traces fired in PostThink also see semiclipped players as non-solid.
* **Projectile fire-through**: the game dll's touch dispatch is superceded for a projectile touching a player who is semiclipped with its owner (no damage, no detonation, no stick), and the projectile is phased past the body along its pre-impact flight vector (cached per frame, since the engine reflects velocity around the touch callback) with a deferred velocity restore on the next frame.
* **One decision function**: visibility fading, movement, hitscan and projectile pass-through all consult the same pair logic, so "transparent to you" always equals "non-solid to you".
* **No engine internals**: everything runs on the public Metamod DLL API. No signatures, no symbol resolution, no detours — Sven Co-op updates that strip symbols or rework engine internals do not affect this plugin by construction.

## Tested and rejected (save yourself a week)
* **`pev->groupinfo` + `SetGroupMask`**: the classic GoldSrc group-masking mechanism used by CS 1.6 team-semiclip modules is non-functional in current Sven Co-op — verified with confirmed activation on both group ops, zero effect on movement or traces. A Sven team member has also warned it is entangled with the netcode (missing-entity quirks) and "not guaranteed long-term".
* **`iuser4` non-collide flag**: officially documented (both players with `iuser4 > 0` should not collide), but in practice showed no pass-through for movement, hitscan or projectiles — only erratic push-apart behavior when overlapping. Values persist in entvars (verified by readback), the effect just isn't there.
* Fun fact discovered along the way: RPG rockets fly through players natively.

## Known Limitations
* **Client prediction**: clients don't know about the plugin, so under high latency there can be minor jitter during semiclip transitions. This is inherent to any server-side solution (the engine's own overlap handling has a documented one-frame bump as well).
* **Water Occlusion**: transparency is disabled underwater to prevent engine rendering glitches.

### Untested (grey zone)
Suspected edge cases that have not been reproduced in testing yet. If you hit one, an `smc_status` dump from the moment it happens is the most useful report:
* **Pusher gibs**: GoldSrc does not do recursive pushing, so a train or door shoving one player into a space occupied by another may resolve by gibbing someone by edict index. This is an engine property that affects any non-solid-player scheme; semiclip may make the overlap more likely. Suspected, not yet observed. If you run maps with tight movers, test them first.
* **Player invisible until restart**: observed once on an experimental build; the suspected cause was removed in 2.5 and it has not reappeared since. If a player or bot turns invisible, run `smc_status`, save the output, and report — do not restart first.

## Changelog
* **2.5** — Fire-through: hitscan and projectiles pass through semiclipped teammates (`smc_firethrough`). Live cvar reads via per-frame engine snapshot — console changes now actually apply (previously all cvars were frozen at defaults on Sven). Enable/disable status lines in the server console. `smc_status` diagnostics command. Removed experimental groupinfo/iuser4 probes (see "Tested and rejected").
* **2.4** — Correctness release: registered the missing `PM_Move` post-hook (fixes permanent `SOLID_NOT` leak), unambiguous solid-state tracking, fixed bounds aliasing in cap mode, `AddToFullPack` respects the game dll's original return value, transparency no longer stomps custom render modes, dropped engine-functions table registration (ABI trap that crashed the server), thirdparty moved to a pinned submodule.
* **2.3** — Initial public version.

---
Third-party headers are provided by the [Metamod-P](https://github.com/Bots-United/metamod-p) submodule (pinned to `v1.21p109`), which bundles the patched HLSDK.
