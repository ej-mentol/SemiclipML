#include "Semiclip.h"
#include "Config.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <unordered_map>

namespace Semiclip {

bool ShouldBeSolidLogic(const Vector &hostOrigin, const Vector &hostVelocity,
                        int hostButtons, int hostOnGroundIdx,
                        const Vector &targetOrigin, int targetMoveType,
                        int targetDeadFlag, int targetIndex,
                        int hostWaterLevel);

namespace {

constexpr int MAX_TRACKED_CLIENTS = 32;
std::array<int, MAX_TRACKED_CLIENTS + 1> g_capTargetByHost{};

// 2.4: separate "tracked" flag instead of using 0 as a sentinel value.
// SOLID_NOT == 0, so a stored value of 0 is ambiguous with "not tracked".
std::array<int, MAX_TRACKED_CLIENTS + 1> g_originalSolidState{};
std::array<bool, MAX_TRACKED_CLIENTS + 1> g_solidTracked{};

// Think-window (smc_firethrough) tracking: separate from the PM_Move-window
// arrays above ON PURPOSE. Windows nest (PreThink -> PM_Move -> PostThink):
// the move-window restore in OnPM_Move_Post must not close the outer think
// window, so each window restores only its own records.
std::array<int, MAX_TRACKED_CLIENTS + 1> g_thinkOriginalSolid{};
std::array<bool, MAX_TRACKED_CLIENTS + 1> g_thinkSolidTracked{};

// Pre-impact velocity cache for player-owned projectiles (smc_firethrough).
// For BOUNCE-class movetypes the engine reflects velocity BEFORE dispatching
// Touch, so at hook time proj->v.velocity may already point back at the
// shooter; phasing along it flings the projectile backwards at angles that
// depend on the impact normal. Snapshot velocities once per frame, before
// entity physics runs, and phase along the cached (true flight) direction.
std::unordered_map<int, Vector> g_projVelCache;
float g_projVelCacheTime = -1.0f;

// Deferred velocity restore: the engine applies ClipVelocity to the
// projectile AFTER our touch hook returns, wiping whatever velocity we set
// inside it. So the hook only schedules the restore here, and the per-frame
// sweep applies it at the START of the next frame - before physics, where
// nothing overwrites it anymore.
std::unordered_map<int, Vector> g_projPendingRestore;

// 2.4: one-shot warning flag for the tracking-leak invariant.
bool g_warnedSolidLeak = false;

edict_t *EdictFromIndex(int index) {
  if (index < 1 || index > MAX_TRACKED_CLIENTS ||
      !g_engfuncs.pfnPEntityOfEntIndex) {
    return nullptr;
  }

  edict_t *edict = g_engfuncs.pfnPEntityOfEntIndex(index);
  if (!edict || FNullEnt(edict) || edict->free) {
    return nullptr;
  }

  return edict;
}

int GetEdictButtons(int hostIdx) {
  edict_t *host = EdictFromIndex(hostIdx);
  return host ? host->v.button : 0;
}

int GetEffectiveButtons(int hostIdx, int fallbackButtons) {
  return fallbackButtons | GetEdictButtons(hostIdx);
}

int ResolvePMGroundEntityIndex(playermove_s *ppmove, int originalNumPhysent) {
  if (!ppmove || ppmove->onground < 0 || ppmove->onground >= originalNumPhysent) {
    return -1;
  }

  return ppmove->physents[ppmove->onground].info;
}

bool IsCorpseLike(int moveType, int deadFlag) {
  return moveType == MOVETYPE_TOSS || deadFlag != DEAD_NO;
}

Vector GetPlayerMins(int useHull) {
  if (useHull == 1) {
    return Vector(-16, -16, -18);
  }

  return Vector(-16, -16, -36);
}

float GetCapRadius(bool latched) {
  return latched ? Config::CAP_HOLD_RADIUS : Config::CAP_ACQUIRE_RADIUS;
}

float GetCapAboveAllowance(bool latched) {
  return latched ? Config::CAP_HOLD_ABOVE : Config::CAP_ACQUIRE_ABOVE;
}

float GetCapBelowAllowance(bool latched) {
  return latched ? Config::CAP_HOLD_BELOW : Config::CAP_ACQUIRE_BELOW;
}

float GetCapBottomOffset(const Vector &hostOrigin, const Vector &hostMins,
                         const Vector &targetOrigin, const Vector &targetMaxs) {
  const float hostBottom = hostOrigin.z + hostMins.z;
  const float targetTop = targetOrigin.z + targetMaxs.z;
  return hostBottom - targetTop;
}

bool ShouldUseCap(const Vector &hostOrigin, const Vector &hostMins,
                  const Vector &targetOrigin, const Vector &targetMaxs,
                  float hostVerticalVelocity, bool latched) {
  if (hostVerticalVelocity > Config::CAP_MAX_RISE_SPEED ||
      hostVerticalVelocity < Config::CAP_FAST_FALL_SPEED) {
    return false;
  }

  const float dx = hostOrigin.x - targetOrigin.x;
  const float dy = hostOrigin.y - targetOrigin.y;
  const float capRadius = GetCapRadius(latched);
  const float d2 = dx * dx + dy * dy;

  if (d2 > (capRadius * capRadius)) {
    return false;
  }

  const float dz =
      GetCapBottomOffset(hostOrigin, hostMins, targetOrigin, targetMaxs);

  if (dz < -GetCapBelowAllowance(latched)) {
    return false;
  }

  if (dz > GetCapAboveAllowance(latched)) {
    return false;
  }

  return true;
}

bool ShouldUseCapForEdicts(edict_t *host, edict_t *target, bool latched) {
  if (!host || !target || FNullEnt(host) || FNullEnt(target) || host->free ||
      target->free) {
    return false;
  }

  return ShouldUseCap(host->v.origin, host->v.mins, target->v.origin,
                      target->v.maxs, host->v.velocity.z, latched);
}

float GetCapScore(const Vector &hostOrigin, const Vector &hostMins,
                  const Vector &targetOrigin, const Vector &targetMaxs) {
  const float dx = hostOrigin.x - targetOrigin.x;
  const float dy = hostOrigin.y - targetOrigin.y;
  const float d2 = dx * dx + dy * dy;
  const float dz = std::abs(
      GetCapBottomOffset(hostOrigin, hostMins, targetOrigin, targetMaxs));

  return d2 + dz * 8.0f;
}

float SmoothStep(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

int ComputeFadeAlpha(float dist, float transDist, float minAlpha) {
  const float ratio = dist / transDist;
  const float easedRatio = SmoothStep(ratio);
  return static_cast<int>(minAlpha + (255.0f - minAlpha) * easedRatio);
}

// 2.4: origMins/origMaxs MUST be copies, not references into *pe —
// this function mutates pe->mins/pe->maxs, so passing pe's own fields
// by reference would alias the values being read. See call site.
void ApplyCapBounds(physent_t *pe, float radius, bool latched,
                    const Vector &origMins, const Vector &origMaxs) {
  if (!latched) {
    pe->mins.x = (std::min)(origMins.x, -radius);
    pe->mins.y = (std::min)(origMins.y, -radius);
    pe->maxs.x = (std::max)(origMaxs.x, radius);
    pe->maxs.y = (std::max)(origMaxs.y, radius);
  } else {
    pe->mins.x = -radius;
    pe->mins.y = -radius;
    pe->maxs.x = radius;
    pe->maxs.y = radius;
  }
  pe->mins.z = pe->maxs.z - Config::CAP_PLATFORM_THICKNESS;
}

int GetHostIndex(edict_t *host) {
  if (!host || FNullEnt(host) || host->free) {
    return 0;
  }

  const int hostIdx = ENTINDEX(host);
  if (hostIdx < 1 || hostIdx > MAX_TRACKED_CLIENTS) {
    return 0;
  }

  return hostIdx;
}

void ResetCapTargetForIndex(int index) {
  if (index < 1 || index > MAX_TRACKED_CLIENTS) {
    return;
  }

  g_capTargetByHost[index] = 0;
}

bool AnySolidTracked() {
  for (int i = 1; i <= MAX_TRACKED_CLIENTS; i++) {
    if (g_solidTracked[i]) {
      return true;
    }
  }
  return false;
}

bool IsPhysicsProjectileMovetype(int mt) {
  return mt == MOVETYPE_TOSS || mt == MOVETYPE_BOUNCE || mt == MOVETYPE_FLY ||
         mt == MOVETYPE_FLYMISSILE || mt == MOVETYPE_BOUNCEMISSILE;
}


// -1 forces a status line on the first move after plugin load.
int g_lastAnnouncedEnabled = -1;

// --- Per-frame cvar snapshot -----------------------------------------------
// Sven's reworked cvar system: console writes land in engine storage that
// neither our registered structs NOR the CVarGetPointer result reflect
// (verified live: FCVAR_SERVER change broadcast fires, reads stay stale).
// pfnCVarGetFloat resolves by name through the same path the console uses,
// so it cannot diverge. To keep the O(n) name lookup out of the per-entity
// hot path, all six values are snapshotted at most once per server frame.
struct LiveCVarValues {
  float enabled = 1.0f;
  float dist = 64.0f;
  float trans = 120.0f;
  float alpha = 120.0f;
  float mode = 0.0f;
  float firethrough = 0.0f;
};

LiveCVarValues g_cvv;
float g_cvvLastRefresh = -1.0f;

float ReadCVarByName(const char *name, cvar_t *fallback) {
  if (g_engfuncs.pfnCVarGetFloat) {
    return g_engfuncs.pfnCVarGetFloat(name);
  }
  return fallback ? fallback->value : 0.0f;
}

void RefreshCVars() {
  if (!gpGlobals || gpGlobals->time == g_cvvLastRefresh) {
    return;
  }
  g_cvvLastRefresh = gpGlobals->time;

  g_cvv.enabled = ReadCVarByName("smc_enabled", pv_enabled);
  g_cvv.dist = ReadCVarByName("smc_dist", pv_dist);
  g_cvv.trans = ReadCVarByName("smc_trans_dist", pv_trans);
  g_cvv.alpha = ReadCVarByName("smc_alpha", pv_alpha);
  g_cvv.mode = ReadCVarByName("smc_mode", pv_mode);
  g_cvv.firethrough = ReadCVarByName("smc_firethrough", pv_firethrough);
}



// Keep late joiners covered AND reassert the global mask every move — the
// game dll may reset group state around its own traces, which would silently
// disarm a one-shot SetGroupMask call.


void RefreshProjVelCache() {
  if (!gpGlobals || g_cvv.firethrough <= 0.0f ||
      gpGlobals->time == g_projVelCacheTime ||
      !g_engfuncs.pfnPEntityOfEntIndex) {
    return;
  }
  g_projVelCacheTime = gpGlobals->time;

  // Apply deferred restores scheduled by superceded touches last frame.
  for (const auto &pending : g_projPendingRestore) {
    edict_t *ent = g_engfuncs.pfnPEntityOfEntIndex(pending.first);
    if (ent && !FNullEnt(ent) && !ent->free &&
        IsPhysicsProjectileMovetype(ent->v.movetype)) {
      ent->v.velocity = pending.second;
      ent->v.flags &= ~FL_ONGROUND; // the engine may have parked it
      ent->v.groundentity = nullptr;
    }
  }
  g_projPendingRestore.clear();

  g_projVelCache.clear();

  const int maxEnts = gpGlobals->maxEntities;
  for (int i = gpGlobals->maxClients + 1; i < maxEnts; i++) {
    edict_t *ent = g_engfuncs.pfnPEntityOfEntIndex(i);
    if (!ent || FNullEnt(ent) || ent->free ||
        !IsPhysicsProjectileMovetype(ent->v.movetype)) {
      continue;
    }
    edict_t *owner = ent->v.owner;
    if (owner && !FNullEnt(owner) && !owner->free &&
        (owner->v.flags & (FL_CLIENT | FL_FAKECLIENT)) != 0) {
      g_projVelCache[i] = ent->v.velocity;
    }
  }
}

// Edict-level pair decision shared by AddToFullPack, the think window and the
// touch filter: should `ent` be solid from `host`'s point of view right now?
bool PairShouldBeSolid(edict_t *host, edict_t *ent, int mode) {
  if (!host || !ent || FNullEnt(host) || FNullEnt(ent) || host->free ||
      ent->free) {
    return true;
  }

  const int hostIdx = GetHostIndex(host);
  const int targetIdx = ENTINDEX(ent);
  const bool is_player = (ent->v.flags & (FL_CLIENT | FL_FAKECLIENT)) != 0;

  if (mode == 1 && is_player) {
    const bool latched =
        (hostIdx != 0 && g_capTargetByHost[hostIdx] == targetIdx);
    return latched && ShouldUseCapForEdicts(host, ent, true);
  }

  int hostOnGroundIdx = -1;
  if (host->v.groundentity) {
    edict_t *ground = host->v.groundentity;
    if (!FNullEnt(ground) && !ground->free) {
      hostOnGroundIdx = ENTINDEX(ground);
    }
  }

  return ShouldBeSolidLogic(host->v.origin, host->v.velocity, host->v.button,
                            hostOnGroundIdx, ent->v.origin, ent->v.movetype,
                            ent->v.deadflag, targetIdx, host->v.waterlevel);
}

} // namespace

bool ShouldBeSolidLogic(
    const Vector &hostOrigin, const Vector &hostVelocity, int hostButtons,
    int hostOnGroundIdx, const Vector &targetOrigin, int targetMoveType,
    int targetDeadFlag, int targetIndex,
    int hostWaterLevel
) {
  // === CHECK 1: CORPSE FILTER ===
  if (IsCorpseLike(targetMoveType, targetDeadFlag)) {
    return false;
  }

  // === CHECK 1.5: FALL DAMAGE PROTECTION ===
  if (hostVelocity.z < Config::CAP_FAST_FALL_SPEED) {
    return false;
  }

  // === CHECK 2: DISTANCE CUTOFF ===
  float dx = hostOrigin.x - targetOrigin.x;
  float dy = hostOrigin.y - targetOrigin.y;
  float d2 = dx * dx + dy * dy;
  float smc_dist = g_cvv.dist;

  if (d2 >= (smc_dist * smc_dist)) {
    return true;
  }

  // === CHECK 3: OVERLAP PROTECTION (3D) ===
  float fDiff = hostOrigin.z - targetOrigin.z;

  bool critical_overlap = std::abs(fDiff) < Config::OVERLAP_Z &&
                          d2 < (Config::OVERLAP_XY * Config::OVERLAP_XY);

  if (critical_overlap) {
    return false;
  }

  // === CHECK 4: ENGINE AUTHORITY ===
  if (hostOnGroundIdx == targetIndex) {
    if (fDiff < Config::FLOAT_CROUCH) {
      return false;
    }
    return true;
  }

  // === CHECK 5: BOX CHECK ===
  if (std::abs(dx) > Config::CENTER_BOX || std::abs(dy) > Config::CENTER_BOX) {
    return false;
  }

  // === CHECK 5.5: LADDER DETECTION ===
  if (hostOnGroundIdx == -1 && hostWaterLevel == 0) {
    if (hostVelocity.z > 5.0f) {
      return false;
    }
  }

  // === CHECK 6: MOVING PLATFORM FIX ===
  if (hostOnGroundIdx > gpGlobals->maxClients) {
    return false;
  }

  // === CHECK 7: HEIGHT + VELOCITY LOGIC ===
  if (fDiff >= Config::FLOAT_CROUCH) {
    if (hostVelocity.z > Config::JUMP_THRESHOLD) {
      return false;
    }

    if ((hostButtons & IN_DUCK) != 0) {
      return true;
    }

    if (fDiff >= Config::SAFE_HEIGHT && hostVelocity.z <= 10.0f) {
      return true;
    }
  }

  return false;
}

void OnPM_Move(struct playermove_s *ppmove, int server) {
  if (gpMetaGlobals) {
    gpMetaGlobals->mres = MRES_IGNORED;
  }
  if (!ppmove || !gpGlobals)
    return;

  RefreshCVars();

  // 2.4 INVARIANT: tracking must be empty at the start of every PM_Move.
  // If it isn't, the post-hook restore did not run for the previous move
  // (hook not registered / another plugin superseded us). Self-heal and
  // shout once so the failure mode is visible instead of silent.
  if (AnySolidTracked()) {
    RestoreTrackedSolidStates();
    if (!g_warnedSolidLeak) {
      g_warnedSolidLeak = true;
      if (gpMetaUtilFuncs) {
        LOG_ERROR(PLID,
                  "solid tracking not empty at PM_Move start; "
                  "PM_Move post-hook did not run. Self-healed, investigate.");
      }
    }
  }

  // Honest status: announce enable/disable transitions in the server console
  // so the admin knows the toggle took effect immediately (no reload needed).
  const bool enabledNow = g_cvv.enabled > 0.0f;
  if (g_lastAnnouncedEnabled != (int)enabledNow) {
    g_lastAnnouncedEnabled = (int)enabledNow;
    if (g_engfuncs.pfnServerPrint) {
      g_engfuncs.pfnServerPrint(
          enabledNow
              ? "[SMC] smc_enabled 1: semiclip checks and transparency ACTIVE\n"
              : "[SMC] smc_enabled 0: ALL semiclip checks and transparency "
                "OFF - effective immediately, no map reload needed\n");
    }
  }

  if (!enabledNow)
    return;

  const int hostIdx = ppmove->player_index + 1;
  if (hostIdx < 1 || hostIdx > MAX_TRACKED_CLIENTS) {
    return;
  }

  // Safety: Protect against corrupted numphysent
  constexpr int SMC_MAX_PHYSENTS = 600;
  if (ppmove->numphysent < 0 || ppmove->numphysent > SMC_MAX_PHYSENTS) {
    ppmove->numphysent = 0;
    return;
  }

  const int original_numphysent = ppmove->numphysent;
  int numphysent = 0;
  int mode = (int)g_cvv.mode;
  const int effectiveButtons = GetEffectiveButtons(hostIdx, ppmove->cmd.buttons);
  const int resolvedGroundIdx =
      ResolvePMGroundEntityIndex(ppmove, original_numphysent);

  Vector hostMins = GetPlayerMins(ppmove->usehull);
  const int previousCapTarget = g_capTargetByHost[hostIdx];
  int nextCapTarget = 0;
  int selectedTargetIdx = 0;
  int selectedPhysentIdx = -1;
  bool selectedLatched = false;
  float selectedScore = (std::numeric_limits<float>::max)();

  if (mode == 1) {
    for (int i = 0; i < original_numphysent; i++) {
      physent_t *pe = &ppmove->physents[i];
      if (pe->player == 0) {
        continue;
      }

      const int targetIdx = pe->info;
      if (targetIdx < 1 || targetIdx > gpGlobals->maxClients ||
          targetIdx == hostIdx) {
        continue;
      }

      edict_t *targetEnt = EdictFromIndex(targetIdx);
      if (targetEnt && IsCorpseLike(targetEnt->v.movetype, targetEnt->v.deadflag)) {
        continue;
      }

      const bool latched = previousCapTarget == targetIdx;
      if (!ShouldUseCap(ppmove->origin, hostMins, pe->origin, pe->maxs,
                        ppmove->velocity.z, latched)) {
        continue;
      }

      const float score =
          GetCapScore(ppmove->origin, hostMins, pe->origin, pe->maxs);
      const bool betterCandidate =
          selectedPhysentIdx == -1 ||
          (latched && !selectedLatched) ||
          (latched == selectedLatched && score < selectedScore);

      if (betterCandidate) {
        selectedTargetIdx = targetIdx;
        selectedPhysentIdx = i;
        selectedLatched = latched;
        selectedScore = score;
      }
    }
  }

  for (int i = 0; i < original_numphysent; i++) {
    physent_t *pe = &ppmove->physents[i];
    bool keep = true;

    if (pe->player != 0) {
      int targetIdx = pe->info;

      if (targetIdx >= 1 && targetIdx <= gpGlobals->maxClients && targetIdx != hostIdx) {
        edict_t *targetEnt = EdictFromIndex(targetIdx);
        int targetDeadFlag = targetEnt ? targetEnt->v.deadflag : DEAD_NO;
        int targetMoveType = targetEnt ? targetEnt->v.movetype : pe->movetype;

        if (mode == 1) {
            if (i == selectedPhysentIdx) {
              // 2.4: take copies BEFORE ApplyCapBounds mutates pe->mins/maxs.
              const Vector origMins = pe->mins;
              const Vector origMaxs = pe->maxs;
              ApplyCapBounds(pe, GetCapRadius(selectedLatched), selectedLatched,
                             origMins, origMaxs);
              nextCapTarget = selectedTargetIdx;
            } else {
              keep = false;
            }
        } 
        else {
            if (!ShouldBeSolidLogic(
                    ppmove->origin, ppmove->velocity, effectiveButtons,
                    resolvedGroundIdx, pe->origin, targetMoveType, targetDeadFlag, targetIdx,
                    ppmove->waterlevel)) {
              keep = false;

              // Flip pev->solid synchronously for radius mode so server-side
              // traces (lag comp, unstuck) agree with movement. Restored in
              // OnPM_Move_Post.
              if (targetEnt && !g_solidTracked[targetIdx]) {
                g_solidTracked[targetIdx] = true;
                g_originalSolidState[targetIdx] = targetEnt->v.solid;
                targetEnt->v.solid = SOLID_NOT;
              }
            }
        }
      }
    }
    else if (pe->movetype == MOVETYPE_TOSS) {
      keep = false;
    }

    if (keep) {
      if (numphysent != i) {
        ppmove->physents[numphysent] = ppmove->physents[i];
      }
      numphysent++;
    }
  }

  g_capTargetByHost[hostIdx] = nextCapTarget;
  ppmove->numphysent = numphysent;
}

int OnAddToFullPack(struct entity_state_s *state, int e, edict_t *ent,
                    edict_t *host, int hostflags, int player,
                    unsigned char *pSet) {
  if (gpMetaGlobals) {
    gpMetaGlobals->mres = MRES_IGNORED;
  }
  RefreshCVars();
  if (g_cvv.enabled <= 0.0f || !gpGlobals)
    return 1;
  if (!state || !ent || !host)
    return 1;

  // 2.4: this is a POST hook. If the game dll decided not to add this
  // entity to the pack, state is not going to the client — leave it alone.
  if (gpMetaGlobals && META_RESULT_ORIG_RET(int) == 0)
    return 1;

  if (FNullEnt(ent) || FNullEnt(host))
    return 1;
  if (ent->free || host->free)
    return 1;

  if (ent == host)
    return 1;

  bool is_player = (ent->v.flags & (FL_CLIENT | FL_FAKECLIENT)) != 0;
  bool is_corpse = IsCorpseLike(ent->v.movetype, ent->v.deadflag);

  if (!is_player && !is_corpse)
    return 1;

  int mode = static_cast<int>(g_cvv.mode);
  bool shouldBeSolid = true;

  if (is_player || is_corpse) {
      shouldBeSolid = PairShouldBeSolid(host, ent, mode);
  }

  if (!shouldBeSolid) {
    state->solid = SOLID_NOT;
  }

  // 2.4: don't stomp custom render states set by the map / game
  // (invisibility, glow shells, scripted effects).
  if (is_player && ent->v.waterlevel == 0 &&
      ent->v.rendermode == kRenderNormal) {
    float dx = state->origin.x - host->v.origin.x;
    float dy = state->origin.y - host->v.origin.y;
    float d2 = dx * dx + dy * dy;
    float transDist = g_cvv.trans;

    if (d2 < (transDist * transDist) && transDist > 0.01f) {
      float dist = std::sqrt(d2);
      float minAlpha = std::clamp(g_cvv.alpha, 0.0f, 255.0f);
      const float solidMinAlpha = (minAlpha > 235.0f) ? minAlpha : 235.0f;
      const int alpha = shouldBeSolid
                            ? ComputeFadeAlpha(dist, transDist, solidMinAlpha)
                            : ComputeFadeAlpha(dist, transDist, minAlpha);

      state->rendermode = kRenderTransAlpha;
      state->renderamt = std::clamp(alpha, 0, 255);
      state->renderfx = kRenderFxNone;
    }
  }

  return 1;
}


void RestoreThinkWindowStates() {
  for (int i = 1; i <= MAX_TRACKED_CLIENTS; i++) {
    if (!g_thinkSolidTracked[i]) {
      continue;
    }

    edict_t *ent = EdictFromIndex(i);
    if (ent) {
      ent->v.solid = g_thinkOriginalSolid[i];
    }

    g_thinkSolidTracked[i] = false;
    g_thinkOriginalSolid[i] = 0;
  }
}

void OnPlayerPreThink(edict_t *pEntity) {
  if (gpMetaGlobals) {
    gpMetaGlobals->mres = MRES_IGNORED;
  }
  if (!pEntity || !gpGlobals) {
    return;
  }

  RefreshCVars();
  RefreshProjVelCache();

  // Invariant: the previous player's think window must be closed by now.
  // Self-heal like the PM_Move invariant does.
  bool leftover = false;
  for (int i = 1; i <= MAX_TRACKED_CLIENTS; i++) {
    if (g_thinkSolidTracked[i]) { leftover = true; break; }
  }
  if (leftover) {
    RestoreThinkWindowStates();
  }

  if (g_cvv.enabled <= 0.0f || g_cvv.firethrough <= 0.0f) {
    return;
  }

  const int hostIdx = GetHostIndex(pEntity);
  if (hostIdx == 0) {
    return;
  }

  const int mode = (int)g_cvv.mode;
  const int maxClients =
      (gpGlobals->maxClients < MAX_TRACKED_CLIENTS) ? gpGlobals->maxClients
                                                    : MAX_TRACKED_CLIENTS;
  for (int i = 1; i <= maxClients; i++) {
    if (i == hostIdx) {
      continue;
    }

    edict_t *target = EdictFromIndex(i);
    if (!target) {
      continue;
    }

    if (!PairShouldBeSolid(pEntity, target, mode) && !g_thinkSolidTracked[i]) {
      g_thinkSolidTracked[i] = true;
      g_thinkOriginalSolid[i] = target->v.solid;
      target->v.solid = SOLID_NOT;
    }
  }
}

void OnPlayerPostThink_Post(edict_t *pEntity) {
  if (gpMetaGlobals) {
    gpMetaGlobals->mres = MRES_IGNORED;
  }

  // Weapon fire (ItemPostFrame) has run inside the game dll's PostThink by
  // the time this post hook executes - close the window.
  RestoreThinkWindowStates();
}

void OnTouch(edict_t *pentTouched, edict_t *pentOther) {
  if (gpMetaGlobals) {
    gpMetaGlobals->mres = MRES_IGNORED;
  }
  if (!pentTouched || !pentOther || !gpGlobals) {
    return;
  }

  RefreshCVars();
  if (g_cvv.enabled <= 0.0f || g_cvv.firethrough <= 0.0f) {
    return;
  }

  // Identify (projectile, player) in either argument order.
  edict_t *proj = nullptr;
  edict_t *playerEnt = nullptr;
  if (IsPhysicsProjectileMovetype(pentTouched->v.movetype) &&
      (pentOther->v.flags & (FL_CLIENT | FL_FAKECLIENT)) != 0) {
    proj = pentTouched;
    playerEnt = pentOther;
  } else if (IsPhysicsProjectileMovetype(pentOther->v.movetype) &&
             (pentTouched->v.flags & (FL_CLIENT | FL_FAKECLIENT)) != 0) {
    proj = pentOther;
    playerEnt = pentTouched;
  } else {
    return;
  }

  edict_t *owner = proj->v.owner;
  if (!owner || FNullEnt(owner) || owner->free || owner == playerEnt ||
      (owner->v.flags & (FL_CLIENT | FL_FAKECLIENT)) == 0) {
    return;
  }

  // Owner and touched player are currently semiclipped with each other:
  // suppress the game dll's touch (no damage, no detonation, no stick).
  if (!PairShouldBeSolid(owner, playerEnt, (int)g_cvv.mode) && gpMetaGlobals) {
    gpMetaGlobals->mres = MRES_SUPERCEDE;

    // The engine has already stopped the projectile physically; its default
    // response deflects it (bolts spring back off bodies). Phase it through
    // instead: advance the origin past the body along the flight direction,
    // keeping velocity. A world-only trace caps the nudge so we never
    // teleport into a wall right behind the player.
    Vector vel = proj->v.velocity;
    const int projIdx = ENTINDEX(proj);
    auto cached = g_projVelCache.find(projIdx);
    if (cached != g_projVelCache.end()) {
      vel = cached->second; // true pre-impact flight vector
    }
    const float speed2 = vel.x * vel.x + vel.y * vel.y + vel.z * vel.z;
    if (speed2 > 1.0f && g_engfuncs.pfnTraceLine && g_engfuncs.pfnSetOrigin) {
      const float invLen = 1.0f / std::sqrt(speed2);
      const Vector dir(vel.x * invLen, vel.y * invLen, vel.z * invLen);
      constexpr float NUDGE_MAX = 72.0f;

      Vector start = proj->v.origin;
      Vector end = start + dir * NUDGE_MAX;
      TraceResult tr{};
      g_engfuncs.pfnTraceLine(start, end, 1 /* ignore monsters/players */,
                              proj, &tr);

      const float dist = NUDGE_MAX * tr.flFraction - 4.0f;
      if (dist > 8.0f) {
        Vector dest = start + dir * dist;
        g_engfuncs.pfnSetOrigin(proj, dest);
        proj->v.velocity = vel;               // may be wiped post-touch...
        g_projPendingRestore[projIdx] = vel;  // ...so reassert next frame
        g_projVelCache[projIdx] = vel;
      }
    }
  }
}

void CmdStatus() {
  if (!gpGlobals || !g_engfuncs.pfnServerPrint) {
    return;
  }

  g_engfuncs.pfnServerPrint(
      "[SMC] idx name             solid grp iuser4 effects rmode ramt mtype "
      "dead trkM trkT\n");

  const int maxClients =
      (gpGlobals->maxClients < MAX_TRACKED_CLIENTS) ? gpGlobals->maxClients
                                                    : MAX_TRACKED_CLIENTS;
  for (int i = 1; i <= maxClients; i++) {
    edict_t *ent = EdictFromIndex(i);
    char line[224];
    if (!ent) {
      snprintf(line, sizeof(line), "[SMC] %3d <empty>\n", i);
    } else {
      const char *name = STRING(ent->v.netname);
      snprintf(line, sizeof(line),
               "[SMC] %3d %-16.16s %5d %3d %6d %7d %5d %4d %5d %4d %4d %4d\n",
               i, (name && name[0]) ? name : "-", ent->v.solid,
               ent->v.groupinfo, ent->v.iuser4, (int)ent->v.effects,
               ent->v.rendermode, (int)ent->v.renderamt, ent->v.movetype,
               ent->v.deadflag, (int)g_solidTracked[i],
               (int)g_thinkSolidTracked[i]);
    }
    g_engfuncs.pfnServerPrint(line);
  }

  char tail[160];
  snprintf(tail, sizeof(tail),
           "[SMC] expected for a live visible player: solid=3 grp=0 iuser4=0 "
           "effects w/o 128(EF_NODRAW) rmode>=0 trk=0/0\n");
  g_engfuncs.pfnServerPrint(tail);
}

void OnClientDisconnect(edict_t *client) {
  if (gpMetaGlobals) {
    gpMetaGlobals->mres = MRES_IGNORED;
  }

  ResetCapTargetForIndex(GetHostIndex(client));
}

void OnServerDeactivate() {
  if (gpMetaGlobals) {
    gpMetaGlobals->mres = MRES_IGNORED;
  }

  g_projVelCache.clear();
  g_projPendingRestore.clear();
  RestoreTrackedSolidStates();
  RestoreThinkWindowStates();
  ResetCapTracking();
}

void ResetCapTracking() {
  g_capTargetByHost.fill(0);
}

void RestoreTrackedSolidStates() {
  for (int i = 1; i <= MAX_TRACKED_CLIENTS; i++) {
    if (!g_solidTracked[i]) {
      continue;
    }

    edict_t *ent = EdictFromIndex(i);
    if (ent) {
      ent->v.solid = g_originalSolidState[i];
    }

    g_solidTracked[i] = false;
    g_originalSolidState[i] = 0;
  }
}

void OnPM_Move_Post(struct playermove_s *ppmove, int server) {
  if (gpMetaGlobals) {
    gpMetaGlobals->mres = MRES_IGNORED;
  }

  // Restore pev->solid values flipped during OnPM_Move for this move.
  RestoreTrackedSolidStates();
}

} // namespace Semiclip
