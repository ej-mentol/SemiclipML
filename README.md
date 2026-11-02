# SemiclipML

Metamod plugin for Sven Co-op. Lets players walk through each other and stops them from getting stuck in each other, in corpses, or on moving platforms. Optionally lets shots and projectiles pass through players.

**Status**
* Windows: tested on a LAN server with bots and a second client. Not tested at 32 players.
* Linux (`SemiclipML.so`): builds, not run on a live server yet.
* Developed with AI assistance, tuned by trial and error on Sven Co-op. Expect rough edges.

## What it does

* **Player vs player collision.** Removes players from each other's movement physics depending on distance, height difference, velocity and ducking. Standing on someone's head still works (stacking).
* **Corpses.** A dead player entity does not collide with a living player it overlaps, so nobody gets stuck in a body.
* **Pushers (func_plat, func_train, func_door).** Overlapping players are made non-solid for the pusher's physics step and `Blocked` is suppressed for them, so two players sharing a platform don't crush each other. A single player is not protected: if they block a pusher, the game handles it as usual.
* **Fall damage guard.** Collision is off during fast falls.
* **Transparency.** Fades player models by distance. Custom render modes set by maps/scripts are left alone. Disabled underwater.
* **Fire-through (optional, off by default).** See below.

## Install

1. Copy `SemiclipML.dll` (Windows) or `SemiclipML.so` (Linux) to your Metamod plugins folder.
2. Add it to `plugins.ini`.
3. Check with `meta list`.

## CVars

All cvars are read by name once per frame, so console changes apply immediately. (On Sven, a locally registered `cvar_t` keeps its default value; reading by name avoids that.)

| CVar | Default | Description |
|---|---|---|
| `smc_enabled` | 1 | Master switch. |
| `smc_mode` | 0 | 0 = radius, 1 = platform (head-stacking cap only). Mode 1 is less tested. |
| `smc_dist` | 64 | Max distance for semiclip. |
| `smc_trans_dist` | 120 | Distance where fading starts. |
| `smc_alpha` | 120 | Minimum alpha when close. |
| `smc_firethrough` | 0 | 1 = shots and projectiles pass through players. Experimental. |
| `smc_melee_range` | 96 | Traces from a player up to this length count as melee (stay solid). |
| `smc_melee` | `crowbar,wrench,medkit,barnacle,knife,grapple,pipe` | Substrings of viewmodel/weaponmodel that are always treated as melee. Used only when `smc_firethrough 1`. |

## Fire-through

With `smc_firethrough 0`, all weapons interact with players normally. Every trace the game dll runs from a living player collides with other players, so melee (crowbar etc.) hits regardless of overlap.

With `smc_firethrough 1`:
* A trace from a player longer than `smc_melee_range` ignores all other players, at any distance. Short traces (melee) still hit.
* Projectiles owned by a player pass through any other player, and continue along their flight path.
* There is no team check and no distance limit.

Limits of this approach:
* Melee vs ranged is guessed from trace length. A melee weapon with a long trace (or a ranged one with a short trace) is misclassified; the `smc_melee` list and `smc_melee_range` are the workarounds.
* Grapple-type weapons have long traces, so they also pass through players.
* Reading weapon ammo/clip state is not possible from a Metamod plugin (it lives in AngelScript objects), so it is not used.

## Build

```
git clone --recursive https://github.com/ej-mentol/SemiclipML.git
cd SemiclipML
```

Third-party headers (Metamod-P + HLSDK) come from the submodule. Without git history, `thirdparty/metamod-p` must be filled manually.

**Windows:** Visual Studio with C++ x86 tools, CMake 3.13+, `compile_win.bat`. Must be a 32-bit build.

**Linux:** `apt install build-essential gcc-multilib g++-multilib cmake`, then `./compile_linux.sh`. The script fails if the result is not 32-bit, warns about unexpected exported symbols or a dynamic libstdc++ dependency, and prints the highest glibc version required. If that version is newer than the server's, build in an older distribution (container).

## Implementation notes

* `PM_Move` pre-hook filters the physents list and flips `pev->solid` for the move; the post-hook restores it. If a restore was missed, it is repeated at the start of the next move.
* `ShouldCollide` (NewDLL API) decides trace collisions between players. It never overrides engine-internal checks for a pusher/overlap pair, except for traces started by the game dll from a living player.
* `StartFrame` (post) flips overlapping players to `SOLID_NOT` before entity physics; the window closes in the next `PlayerPreThink`.
* `pfnTraceLine` / `pfnTraceHull` are hooked (pre and post). Only those two fields of the engine table are assigned; the table is not copied wholesale (copying the whole table caused an ABI crash on older Metamod builds).
* Projectile touch: the game dll's `Touch` is superseded, and the projectile is moved past the body along its pre-impact velocity; velocity is re-applied next frame.
* Uses only the public Metamod API. No signatures, no detours.

## Known issues

* **Jumping into a player at head height can push you sideways.** This is part of the engine's player physics; the other known semiclip implementation has the same behavior. Not investigated further.
* **Client prediction.** Clients don't know about the plugin; expect minor jitter at high latency during transitions.
* **Pusher gibs.** A mover shoving one player into a space occupied by another can gib someone. Engine behavior, not observed here, but semiclip may make the overlap more likely. Test maps with tight movers.
* **Invisible player.** Seen once on an experimental build; suspected cause was removed, not reproduced since. If it happens, report it with the map, the weapons used and the cvar values.

### Not verified

* Linux runtime.
* 32 players under load.
* Hooking `pfnTraceLine`/`pfnTraceHull` on different Metamod builds.
* Melee (crowbar, scripted weapons) hitting a player standing next to another player. A fix is in place but not confirmed in-game.

## Tried and rejected

* `pev->groupinfo` + `SetGroupMask`: no effect on movement or traces in current Sven Co-op.
* `iuser4` non-collide flag: no pass-through; only erratic push-apart.
* Making the pass-through decision follow the transparency decision for shots: replaced by the trace-length rule above, because the distance cutoff made shots pass or hit depending on exact distance.

---
Third-party headers: [Metamod-P](https://github.com/Bots-United/metamod-p) submodule (pinned commit), which bundles the patched HLSDK.
