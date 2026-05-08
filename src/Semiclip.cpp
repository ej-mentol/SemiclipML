#include "Semiclip.h"
#include "Config.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <limits>

namespace Semiclip {

namespace {

constexpr int MAX_TRACKED_CLIENTS = 32;
std::array<int, MAX_TRACKED_CLIENTS + 1> g_capTargetByHost{};

float GetCVarFloat(const char *name, float fallback) {
  if (g_engfuncs.pfnCVarGetFloat) {
    return g_engfuncs.pfnCVarGetFloat(name);
  }

  return fallback;
}

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

  const physent_t &ground = ppmove->physents[ppmove->onground];
  if (ground.info > 0) {
    return ground.info;
  }

  return ppmove->onground;
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

void ApplyCapBounds(physent_t *pe, float radius) {
  pe->mins.x = -radius;
  pe->mins.y = -radius;
  pe->maxs.x = radius;
  pe->maxs.y = radius;
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

} // namespace

bool ShouldBeSolidLogic(
    const Vector &hostOrigin, const Vector &hostVelocity, int hostButtons,
    int hostOnGroundIdx, const Vector &targetOrigin, int targetMoveType,
    int targetDeadFlag, int targetIndex,
    int hostWaterLevel = 0
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
  float smc_dist = cv_dist.value;

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

  if (GetCVarFloat("smc_enabled", cv_enabled.value) <= 0.0f)
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
  int mode = (int)GetCVarFloat("smc_mode", cv_mode.value);
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
        if (mode == 1) {
            if (i == selectedPhysentIdx) {
              ApplyCapBounds(pe, GetCapRadius(selectedLatched));
              nextCapTarget = selectedTargetIdx;
            } else {
              keep = false;
            }
        } 
        else {
            if (!ShouldBeSolidLogic(
                    ppmove->origin, ppmove->velocity, effectiveButtons,
                    resolvedGroundIdx, pe->origin, pe->movetype, 0, targetIdx,
                    ppmove->waterlevel)) {
              keep = false;
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
  if (GetCVarFloat("smc_enabled", cv_enabled.value) <= 0.0f || !gpGlobals)
    return 1;
  if (!state || !ent || !host)
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

  int mode = static_cast<int>(GetCVarFloat("smc_mode", cv_mode.value));
  bool shouldBeSolid = true;
  const int hostIdx = GetHostIndex(host);
  const int targetIdx = ENTINDEX(ent);

  if (mode == 1 && is_player) {
      const bool latched =
          (hostIdx != 0 && g_capTargetByHost[hostIdx] == targetIdx);
      shouldBeSolid = latched && ShouldUseCapForEdicts(host, ent, true);
  }
  else {
      int hostOnGroundIdx = -1;
      if (host->v.groundentity) {
        edict_t *ground = host->v.groundentity;
        if (!FNullEnt(ground) && !ground->free) {
          hostOnGroundIdx = ENTINDEX(ground);
        }
      }

      shouldBeSolid = ShouldBeSolidLogic(
          host->v.origin, host->v.velocity,
          host->v.button, 
          hostOnGroundIdx, ent->v.origin, ent->v.movetype, ent->v.deadflag,
          targetIdx,
          host->v.waterlevel);
  }

  if (!shouldBeSolid) {
    state->solid = SOLID_NOT;
  }

  if (is_player && ent->v.waterlevel == 0) {
    float dx = state->origin.x - host->v.origin.x;
    float dy = state->origin.y - host->v.origin.y;
    float d2 = dx * dx + dy * dy;
    float transDist = cv_trans.value;

    if (d2 < (transDist * transDist) && transDist > 0.01f) {
      float dist = std::sqrt(d2);
      float minAlpha = std::clamp(cv_alpha.value, 0.0f, 255.0f);
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

  ResetCapTracking();
}

void ResetCapTracking() {
  g_capTargetByHost.fill(0);
}

} // namespace Semiclip
