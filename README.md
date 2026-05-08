# SemiclipML (Slop Edition)

**Warning:** This is an experimental version. Not recommended for production use on public servers. The build targets Windows. 

You can still execute the stuck command (L) and walk through obstacles. Fixing that wasn't part of the original task.

*Note: This project was developed with AI assistance. Collision logic and edict handling were fine-tuned for Sven Co-op. Below are some bold claims that aren't particularly helpful :D*

SemiclipML is a Metamod plugin for Sven Co-op that provides advanced player-to-player collision management. It allows players to pass through each other in tight spaces while maintaining tactical solid collision for boosting and landing.

## Features
* **Smart Collision Logic**: Automatically toggles collision based on player velocity, height difference, and proximity.
* **Fall Damage Protection**: Prevents accidental fall damage deaths by disabling collision during high-speed vertical movement.
* **Stacking Support**: Allows players to stand on each other's heads (boosting) while crouching or when standing still.
* **Dynamic Transparency**: Fades player models as they approach each other to improve visibility in crowded areas.
* **Multi-Mode Operation**: Includes a standard radius-based mode and a specialized "platform" mode for precise head-stacking.

## Configuration (CVars)
* `smc_enabled` (0/1): Toggle the plugin. Default: 1.
* `smc_mode` (0/1): Collision mode. 0 = Radius (Standard), 1 = Platform (Precise head-stacking). Default: 0.
* `smc_dist` (units): Maximum distance for semiclip activation. Default: 64.
* `smc_trans_dist` (units): Distance at which transparency fading begins. Default: 120.
* `smc_alpha` (0-255): Minimum transparency level when close to another player. Default: 120.

## Installation
1. Copy `SemiclipML.dll` (Windows) to your Metamod plugins folder.
2. Add the plugin to your Metamod `plugins.ini` file.

## Technical Overview
* **Logic Split**: Processing is divided between `PM_Move` (for smooth client-side prediction) and `AddToFullPack` (for server-side physics state and visuals).
* **Entity Detection**: Uses `MOVETYPE_TOSS` and `MOVETYPE_FLY` checks to distinguish between active players and corpses/physics objects.
* **Sven Co-op Compatibility**: Specifically skips `deadflag` entities in `AddToFullPack` to ensure transparency doesn't interfere with the game's revival system.
* **Prediction Stability**: Optimized to prevent "sliding" and "teleporting" issues common in standard semiclip implementations.

## Known Limitations
* **Engine Prediction**: Extreme network latency may cause minor visual jitter during semiclip transitions.
* **Water Occlusion**: Transparency is disabled underwater to prevent engine rendering glitches where players might pop through the water surface.

---
This project uses headers from [Metamod-P](https://github.com/jkivilin/metamod-p/releases).
