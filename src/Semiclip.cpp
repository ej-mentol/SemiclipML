#include "Config.h"
#include "Semiclip.h"
#include <array>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace Semiclip {

namespace {

constexpr int MAX_TRACKED_CLIENTS = 32;
constexpr int TRACE_IGNORE_MONSTERS = 1; // util.h: ignore_monsters

inline bool IsPlayer(const edict_t *e) {
  return e && (e->v.flags & (FL_CLIENT | FL_FAKECLIENT)) != 0;
}

inline bool IsLive(const edict_t *e) {
  return e && !FNullEnt(e) && !e->free;
}

inline bool IsCorpseLike(int moveType, int deadFlag) {
  return moveType == MOVETYPE_TOSS || deadFlag != DEAD_NO;
}

inline bool IsDeadPlayerEnt(edict_t *e) {
  if (!e || e->v.movetype != MOVETYPE_TOSS || e->v.solid == SOLID_NOT) return false;
  const char *c = STRING(e->v.classname);
  return c && std::strcmp(c, "deadplayer") == 0;
}

inline edict_t *EdictFromIndex(int index) {
  if (index < 1 || index > MAX_TRACKED_CLIENTS || !g_engfuncs.pfnPEntityOfEntIndex) {
    return nullptr;
  }
  edict_t *ent = g_engfuncs.pfnPEntityOfEntIndex(index);
  return IsLive(ent) ? ent : nullptr;
}

inline int GetHostIndex(edict_t *host) {
  if (!IsLive(host)) return 0;
  const int idx = ENTINDEX(host);
  if (idx < 1 || idx > MAX_TRACKED_CLIENTS) return 0;
  return idx;
}

inline int GetEdictButtons(int hostIdx) {
  edict_t *host = EdictFromIndex(hostIdx);
  return host ? host->v.button : 0;
}

bool BoxesOverlap(const entvars_t &a, const entvars_t &b, float margin = 0.0f) {
  return !(a.origin.x + a.maxs.x + margin < b.origin.x + b.mins.x ||
           b.origin.x + b.maxs.x + margin < a.origin.x + a.mins.x ||
           a.origin.y + a.maxs.y + margin < b.origin.y + b.mins.y ||
           b.origin.y + b.maxs.y + margin < a.origin.y + a.mins.y ||
           a.origin.z + a.maxs.z + margin < b.origin.z + b.mins.z ||
           b.origin.z + b.maxs.z + margin < a.origin.z + a.mins.z);
}

// Unified solid-state stash for hook windows (PM_Move, StartFrame, Trace)
struct SolidStash {
  std::array<int, MAX_TRACKED_CLIENTS + 1> solid{};
  std::array<bool, MAX_TRACKED_CLIENTS + 1> held{};

  bool Flip(edict_t *e, int i) {
    if (i < 1 || i > MAX_TRACKED_CLIENTS || held[i] || !e) {
      return false;
    }
    held[i] = true;
    solid[i] = e->v.solid;
    e->v.solid = SOLID_NOT;
    return true;
  }

  void Restore() {
    for (int i = 1; i <= MAX_TRACKED_CLIENTS; i++) {
      if (!held[i]) continue;
      edict_t *e = EdictFromIndex(i);
      if (e && !IsCorpseLike(e->v.movetype, e->v.deadflag)) {
        e->v.solid = solid[i];
      }
      held[i] = false;
      solid[i] = 0;
    }
  }

  bool AnyHeld() const {
    for (int i = 1; i <= MAX_TRACKED_CLIENTS; i++) {
      if (held[i]) return true;
    }
    return false;
  }
};

SolidStash g_pmStash;
SolidStash g_frameStash;
SolidStash g_traceStash;

std::array<int, MAX_TRACKED_CLIENTS + 1> g_capTargetByHost{};

bool g_weaponPhase = false;
edict_t *g_phaseEnt = nullptr;
edict_t *g_traceSkip = nullptr;

constexpr size_t MAX_MELEE_TOKENS = 16;
constexpr size_t MAX_MELEE_TOKEN_LEN = 64;

struct LiveCVarValues {
  bool enabled = true;
  float dist = Config::DEF_DIST;
  float trans = Config::DEF_TRANS_DIST;
  float alpha = Config::DEF_ALPHA;
  int mode = 0;
  int firethrough = 0;
  float meleeRange = Config::DEF_MELEE_RANGE;
  char meleeRaw[256] = "";
  char meleeTokens[MAX_MELEE_TOKENS][MAX_MELEE_TOKEN_LEN]{};
  size_t meleeTokenCount = 0;
};

LiveCVarValues g_cvv;
float g_cvvLastRefresh = -1.0f;

void TokenizeMelee(const char *str) {
  g_cvv.meleeTokenCount = 0;
  if (!str || !str[0]) return;
  const char *p = str;
  while (*p && g_cvv.meleeTokenCount < MAX_MELEE_TOKENS) {
    while (*p == ',' || *p == ' ') p++;
    const char *e = p;
    while (*e && *e != ',') e++;
    size_t len = static_cast<size_t>(e - p);
    while (len > 0 && p[len - 1] == ' ') len--;
    if (len > 0 && len < MAX_MELEE_TOKEN_LEN) {
      char *dst = g_cvv.meleeTokens[g_cvv.meleeTokenCount++];
      for (size_t i = 0; i < len; i++) {
        dst[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(p[i])));
      }
      dst[len] = '\0';
    }
    p = e;
  }
}

bool MatchesMeleeList(const char *model) {
  if (!model || !model[0]) return false;
  char low[128];
  size_t n = 0;
  for (; n < sizeof(low) - 1 && model[n]; n++) {
    low[n] = static_cast<char>(std::tolower(static_cast<unsigned char>(model[n])));
  }
  low[n] = '\0';
  for (size_t i = 0; i < g_cvv.meleeTokenCount; i++) {
    if (std::strstr(low, g_cvv.meleeTokens[i])) return true;
  }
  return false;
}

bool IsMeleeByName(edict_t *ent) {
  if (!ent) return false;
  return MatchesMeleeList(STRING(ent->v.viewmodel)) ||
         MatchesMeleeList(STRING(ent->v.weaponmodel));
}

bool IsOnPusher(edict_t *e) {
  if (!e || !gpGlobals) return false;
  edict_t *g = e->v.groundentity;
  return g && !FNullEnt(g) && !g->free && ENTINDEX(g) > gpGlobals->maxClients &&
         g->v.movetype == MOVETYPE_PUSH;
}

float ReadCVarByName(const char *name, float fallback) {
  if (g_engfuncs.pfnCVarGetFloat) {
    return g_engfuncs.pfnCVarGetFloat(name);
  }
  return fallback;
}

void RefreshCVars() {
  if (!gpGlobals || gpGlobals->time == g_cvvLastRefresh) {
    return;
  }
  g_cvvLastRefresh = gpGlobals->time;

  g_cvv.enabled = ReadCVarByName("smc_enabled", 1.0f) > 0.0f;
  g_cvv.dist = ReadCVarByName("smc_dist", Config::DEF_DIST);
  g_cvv.trans = ReadCVarByName("smc_trans_dist", Config::DEF_TRANS_DIST);
  if (g_cvv.trans <= 0.0f) g_cvv.trans = 1.0f;
  g_cvv.alpha = ReadCVarByName("smc_alpha", Config::DEF_ALPHA);
  if (g_cvv.alpha < 0.0f) g_cvv.alpha = 0.0f;
  if (g_cvv.alpha > 255.0f) g_cvv.alpha = 255.0f;
  g_cvv.mode = static_cast<int>(ReadCVarByName("smc_mode", 0.0f));
  g_cvv.firethrough = static_cast<int>(ReadCVarByName("smc_firethrough", 0.0f));
  g_cvv.meleeRange = ReadCVarByName("smc_melee_range", Config::DEF_MELEE_RANGE);
  if (g_cvv.meleeRange <= 0.0f) g_cvv.meleeRange = Config::DEF_MELEE_RANGE;

  if (g_engfuncs.pfnCVarGetString) {
    const char *m = g_engfuncs.pfnCVarGetString("smc_melee");
    if (m && std::strcmp(m, g_cvv.meleeRaw) != 0) {
      std::strncpy(g_cvv.meleeRaw, m, sizeof(g_cvv.meleeRaw) - 1);
      g_cvv.meleeRaw[sizeof(g_cvv.meleeRaw) - 1] = '\0';
      TokenizeMelee(g_cvv.meleeRaw);
    }
  }
}

// Flat array sized for Sven Co-op's max entity limit (8192)
constexpr int MAX_ENTITIES = 8192;
std::array<Vector, MAX_ENTITIES> g_projPendingVel{};
std::array<bool, MAX_ENTITIES> g_projPendingActive{};
std::array<Vector, MAX_ENTITIES> g_projVelCache{};
std::array<bool, MAX_ENTITIES> g_projVelActive{};
float g_projVelCacheTime = -1.0f;

inline bool IsPhysicsProjectileMovetype(int mt) {
  return mt == MOVETYPE_TOSS || mt == MOVETYPE_BOUNCE || mt == MOVETYPE_FLY ||
         mt == MOVETYPE_FLYMISSILE || mt == MOVETYPE_BOUNCEMISSILE;
}

void RefreshProjVelCache() {
  if (!gpGlobals || !g_engfuncs.pfnPEntityOfEntIndex) {
    return;
  }
  if (gpGlobals->time == g_projVelCacheTime) {
    return;
  }
  g_projVelCacheTime = gpGlobals->time;

  if (g_cvv.firethrough <= 0) {
    g_projPendingActive.fill(false);
    g_projVelActive.fill(false);
    return;
  }

  if (gpGlobals->maxEntities > MAX_ENTITIES) {
    static bool warned = false;
    if (!warned) {
      warned = true;
      if (g_engfuncs.pfnServerPrint) {
        g_engfuncs.pfnServerPrint("[SMC] WARNING: server maxEntities exceeds MAX_ENTITIES (8192)\n");
      }
    }
  }

  const int maxEnt = (std::min)(gpGlobals->maxEntities, MAX_ENTITIES);

  // Apply deferred restores
  for (int i = 1; i < maxEnt; i++) {
    if (!g_projPendingActive[i]) continue;
    g_projPendingActive[i] = false;
    edict_t *ent = g_engfuncs.pfnPEntityOfEntIndex(i);
    if (IsLive(ent) && IsPhysicsProjectileMovetype(ent->v.movetype)) {
      ent->v.velocity = g_projPendingVel[i];
      ent->v.flags &= ~FL_ONGROUND;
    }
  }

  g_projVelActive.fill(false);
  for (int i = 1; i < maxEnt; i++) {
    edict_t *ent = g_engfuncs.pfnPEntityOfEntIndex(i);
    if (!IsLive(ent) || !IsPhysicsProjectileMovetype(ent->v.movetype)) {
      continue;
    }
    edict_t *owner = ent->v.owner;
    if (IsLive(owner) && IsPlayer(owner)) {
      g_projVelCache[i] = ent->v.velocity;
      g_projVelActive[i] = true;
    }
  }
}

int ResolvePMGroundEntityIndex(const physent_t *pes, int onground, int originalNumPhysent) {
  if (!pes || onground < 0 || onground >= originalNumPhysent) {
    return -1;
  }
  return pes[onground].info;
}

Vector GetPlayerMins(int useHull) {
  return (useHull == 1) ? Vector(-16, -16, -18) : Vector(-16, -16, -36);
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
  return (hostOrigin.z + hostMins.z) - (targetOrigin.z + targetMaxs.z);
}

bool ShouldUseCap(const Vector &hostOrigin, const Vector &hostMins,
                  const Vector &targetOrigin, const Vector &targetMaxs,
                  float hostVelocityZ, bool latched) {
  if (hostVelocityZ > Config::CAP_MAX_RISE_SPEED ||
      hostVelocityZ < Config::CAP_FAST_FALL_SPEED) {
    return false;
  }
  const float dx = hostOrigin.x - targetOrigin.x;
  const float dy = hostOrigin.y - targetOrigin.y;
  const float capRadius = GetCapRadius(latched);
  if ((dx * dx + dy * dy) > (capRadius * capRadius)) {
    return false;
  }
  const float dz = GetCapBottomOffset(hostOrigin, hostMins, targetOrigin, targetMaxs);
  return (dz >= -GetCapBelowAllowance(latched) && dz <= GetCapAboveAllowance(latched));
}

bool ShouldUseCapForEdicts(edict_t *host, edict_t *target, bool latched) {
  if (!IsLive(host) || !IsLive(target)) return false;
  return ShouldUseCap(host->v.origin, host->v.mins, target->v.origin,
                      target->v.maxs, host->v.velocity.z, latched);
}

float GetCapScore(const Vector &hostOrigin, const Vector &hostMins,
                  const Vector &targetOrigin, const Vector &targetMaxs) {
  const float dx = hostOrigin.x - targetOrigin.x;
  const float dy = hostOrigin.y - targetOrigin.y;
  const float dz = std::abs(GetCapBottomOffset(hostOrigin, hostMins, targetOrigin, targetMaxs));
  return (dx * dx + dy * dy) + dz * 8.0f;
}

float SmoothStep(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

int ComputeFadeAlpha(float dist, float transDist, float minAlpha) {
  if (dist >= transDist) return 255;
  if (dist <= 0.0f) return static_cast<int>(minAlpha);
  const float ratio = dist / transDist;
  const float easedRatio = SmoothStep(ratio);
  return static_cast<int>(minAlpha + (255.0f - minAlpha) * easedRatio);
}

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

void ResetCapTargetForIndex(int hostIdx) {
  if (hostIdx >= 1 && hostIdx <= MAX_TRACKED_CLIENTS) {
    g_capTargetByHost[hostIdx] = 0;
  }
}

bool ShouldBeSolidLogic(
    const Vector &hostOrigin, const Vector &hostVelocity, int hostButtons,
    int hostOnGroundIdx, const Vector &targetOrigin, int targetMoveType,
    int targetDeadFlag, int targetIndex, int hostWaterLevel
) {
  if (IsCorpseLike(targetMoveType, targetDeadFlag)) return false;
  if (hostVelocity.z < Config::CAP_FAST_FALL_SPEED) return false;

  const float dx = hostOrigin.x - targetOrigin.x;
  const float dy = hostOrigin.y - targetOrigin.y;
  const float d2 = dx * dx + dy * dy;
  const float smcDist = g_cvv.dist;

  if (d2 >= (smcDist * smcDist)) return true;

  const float fDiff = hostOrigin.z - targetOrigin.z;

  if (hostOnGroundIdx == targetIndex) {
    return fDiff >= Config::FLOAT_CROUCH;
  }

  const bool criticalOverlap = std::abs(fDiff) < Config::OVERLAP_Z &&
                               d2 < (Config::OVERLAP_XY * Config::OVERLAP_XY);
  if (criticalOverlap) return false;

  if (std::abs(dx) > Config::CENTER_BOX || std::abs(dy) > Config::CENTER_BOX) {
    return false;
  }

  if (hostOnGroundIdx == -1 && hostWaterLevel == 0 && hostVelocity.z > 5.0f) {
    return false;
  }

  if (hostOnGroundIdx > gpGlobals->maxClients) {
    return false;
  }

  if (fDiff >= Config::FLOAT_CROUCH) {
    if (hostVelocity.z > Config::JUMP_THRESHOLD) return false;
    if ((hostButtons & IN_DUCK) != 0) return true;
    if (fDiff >= Config::SAFE_HEIGHT && hostVelocity.z <= 10.0f) return true;
  }

  return false;
}

bool PairShouldBeSolid(edict_t *host, edict_t *ent, int mode) {
  if (!IsLive(host) || !IsLive(ent)) return true;

  const int hostIdx = GetHostIndex(host);
  const int targetIdx = ENTINDEX(ent);
  const bool isTargetPlayer = IsPlayer(ent);

  if (mode == 1 && isTargetPlayer) {
    const bool latched = (hostIdx != 0 && g_capTargetByHost[hostIdx] == targetIdx);
    return latched && ShouldUseCapForEdicts(host, ent, true);
  }

  int hostOnGroundIdx = -1;
  if (host->v.groundentity && IsLive(host->v.groundentity)) {
    hostOnGroundIdx = ENTINDEX(host->v.groundentity);
  }

  return ShouldBeSolidLogic(host->v.origin, host->v.velocity, host->v.button,
                            hostOnGroundIdx, ent->v.origin, ent->v.movetype,
                            ent->v.deadflag, targetIdx, host->v.waterlevel);
}

void TracePre(const float *v1, const float *v2, int fNoMonsters, edict_t *pentToSkip) {
  if (gpMetaGlobals) gpMetaGlobals->mres = MRES_IGNORED;
  g_traceSkip = nullptr;
  if (!pentToSkip || !gpGlobals || !v1 || !v2 || !IsPlayer(pentToSkip)) return;
  RefreshCVars();
  if (!g_cvv.enabled || IsCorpseLike(pentToSkip->v.movetype, pentToSkip->v.deadflag)) return;

  const float tdx = v2[0] - v1[0], tdy = v2[1] - v1[1], tdz = v2[2] - v1[2];
  const float len2 = tdx * tdx + tdy * tdy + tdz * tdz;
  if (len2 < 1.0f) return; // Zero-length traces are position tests (unstuck checks)
  g_traceSkip = pentToSkip;

  if ((fNoMonsters & TRACE_IGNORE_MONSTERS) || g_cvv.firethrough <= 0) return;

  const float r = g_cvv.meleeRange;
  if (len2 <= r * r || IsMeleeByName(pentToSkip)) return;

  const int n = (std::min)(gpGlobals->maxClients, MAX_TRACKED_CLIENTS);
  for (int i = 1; i <= n; i++) {
    edict_t *t = EdictFromIndex(i);
    if (!t || t == pentToSkip || t->v.solid == SOLID_NOT || !IsPlayer(t)) continue;
    g_traceStash.Flip(t, i);
  }
}

} // namespace

void ResetCapTracking() {
  g_capTargetByHost.fill(0);
}

void RestoreAllSolidStates() {
  g_pmStash.Restore();
  g_frameStash.Restore();
  g_traceStash.Restore();
}

void OnPM_Move(struct playermove_s *ppmove, int /*server*/) {
  if (gpMetaGlobals) gpMetaGlobals->mres = MRES_IGNORED;
  if (!ppmove || !gpGlobals) return;

  RefreshCVars();

  if (g_pmStash.AnyHeld()) {
    g_pmStash.Restore();
    static bool warnedSolidLeak = false;
    if (!warnedSolidLeak) {
      warnedSolidLeak = true;
      if (gpMetaUtilFuncs) {
        LOG_ERROR(PLID,
                  "solid tracking not empty at PM_Move start; "
                  "PM_Move post-hook did not run. Self-healed.");
      }
    }
  }

  static int lastAnnouncedEnabled = -1;
  const bool enabledNow = g_cvv.enabled;
  if (lastAnnouncedEnabled != static_cast<int>(enabledNow)) {
    lastAnnouncedEnabled = static_cast<int>(enabledNow);
    if (g_engfuncs.pfnServerPrint) {
      g_engfuncs.pfnServerPrint(enabledNow ? "[SMC] enabled\n" : "[SMC] disabled\n");
    }
  }

  if (!enabledNow) return;

  const int hostIdx = ppmove->player_index + 1;
  if (hostIdx < 1 || hostIdx > MAX_TRACKED_CLIENTS) return;

  // Protect against corrupted numphysent; Sven Co-op 5.16+ expands buffer to 1024
  if (ppmove->numphysent < 0 || ppmove->numphysent > Config::MAX_PHYSENTS_SVEN) {
    return;
  }

  const int originalNumPhysent = ppmove->numphysent;
  physent_t *const pes = ppmove->physents;
  int numphysent = 0;
  const int mode = g_cvv.mode;
  const int effectiveButtons = GetEdictButtons(hostIdx);
  const int resolvedGroundIdx = ResolvePMGroundEntityIndex(pes, ppmove->onground, originalNumPhysent);

  const Vector hostMins = GetPlayerMins(ppmove->usehull);
  const int previousCapTarget = g_capTargetByHost[hostIdx];
  int nextCapTarget = 0;
  int selectedTargetIdx = 0;
  int selectedPhysentIdx = -1;
  bool selectedLatched = false;
  float selectedScore = (std::numeric_limits<float>::max)();

  if (mode == 1) {
    for (int i = 0; i < originalNumPhysent; i++) {
      physent_t *pe = &pes[i];
      if (pe->player == 0) continue;

      const int targetIdx = pe->info;
      if (targetIdx < 1 || targetIdx > gpGlobals->maxClients || targetIdx == hostIdx) {
        continue;
      }

      edict_t *targetEnt = EdictFromIndex(targetIdx);
      if (targetEnt && IsCorpseLike(targetEnt->v.movetype, targetEnt->v.deadflag)) {
        continue;
      }

      const bool latched = (previousCapTarget == targetIdx);
      if (!ShouldUseCap(ppmove->origin, hostMins, pe->origin, pe->maxs,
                        ppmove->velocity.z, latched)) {
        continue;
      }

      const float score = GetCapScore(ppmove->origin, hostMins, pe->origin, pe->maxs);
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

  for (int i = 0; i < originalNumPhysent; i++) {
    physent_t *pe = &pes[i];
    bool keep = true;

    if (pe->player != 0) {
      const int targetIdx = pe->info;
      if (targetIdx >= 1 && targetIdx <= gpGlobals->maxClients && targetIdx != hostIdx) {
        edict_t *targetEnt = EdictFromIndex(targetIdx);
        const int targetDeadFlag = targetEnt ? targetEnt->v.deadflag : DEAD_NO;
        const int targetMoveType = targetEnt ? targetEnt->v.movetype : pe->movetype;

        if (mode == 1) {
          if (i == selectedPhysentIdx) {
            const Vector origMins = pe->mins;
            const Vector origMaxs = pe->maxs;
            ApplyCapBounds(pe, GetCapRadius(selectedLatched), selectedLatched,
                           origMins, origMaxs);
            nextCapTarget = selectedTargetIdx;
          } else {
            keep = false;
          }
        } else {
          if (!ShouldBeSolidLogic(
                  ppmove->origin, ppmove->velocity, effectiveButtons,
                  resolvedGroundIdx, pe->origin, targetMoveType, targetDeadFlag,
                  targetIdx, ppmove->waterlevel)) {
            keep = false;
            if (targetEnt) {
              g_pmStash.Flip(targetEnt, targetIdx);
            }
          }
        }
      }
    } else if (pe->movetype == MOVETYPE_TOSS) {
      keep = false;
    }

    if (keep) {
      if (numphysent != i) {
        pes[numphysent] = pes[i];
      }
      numphysent++;
    }
  }

  g_capTargetByHost[hostIdx] = nextCapTarget;
  ppmove->numphysent = numphysent;

  // Clear pruned tail elements to prevent stale physent reads by the engine
  for (int i = numphysent; i < originalNumPhysent; i++) {
    pes[i] = {};
  }
}

int OnAddToFullPack(struct entity_state_s *state, int /*e*/, edict_t *ent,
                    edict_t *host, int /*hostflags*/, int player,
                    unsigned char * /*pSet*/) {
  if (gpMetaGlobals) gpMetaGlobals->mres = MRES_IGNORED;
  RefreshCVars();
  if (!g_cvv.enabled || !gpGlobals || !state || !ent || !host) return 1;

  if (gpMetaGlobals && META_RESULT_ORIG_RET(int) == 0) return 1;

  const bool isPlayer = player || IsPlayer(ent);
  const bool isCorpse = IsCorpseLike(ent->v.movetype, ent->v.deadflag);
  if (!isPlayer && !isCorpse) return 1;

  const int hostIdx = GetHostIndex(host);
  const int targetIdx = ENTINDEX(ent);
  if (hostIdx != 0 && hostIdx == targetIdx) return 1;

  const int mode = g_cvv.mode;
  const bool shouldBeSolid = PairShouldBeSolid(host, ent, mode);

  if (!shouldBeSolid) {
    state->solid = SOLID_NOT;
  }

  const bool isDead = ent->v.deadflag != DEAD_NO || ent->v.health <= 0.0f ||
                      ent->v.iuser1 != 0;
  if (isPlayer && !isDead && ent->v.waterlevel == 0 &&
      ent->v.rendermode == kRenderNormal) {
    const float dx = state->origin.x - host->v.origin.x;
    const float dy = state->origin.y - host->v.origin.y;
    const float d2 = dx * dx + dy * dy;
    const float transDist = g_cvv.trans;

    if (d2 < (transDist * transDist) && transDist > 0.01f) {
      const float dist = std::sqrt(d2);
      const float minAlpha = g_cvv.alpha;
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

void OnPlayerPreThink(edict_t *pEntity) {
  if (gpMetaGlobals) gpMetaGlobals->mres = MRES_IGNORED;
  if (!pEntity || !gpGlobals) return;

  g_frameStash.Restore();
  RefreshCVars();
  RefreshProjVelCache();
}

void OnPlayerPostThink(edict_t *pEntity) {
  if (gpMetaGlobals) gpMetaGlobals->mres = MRES_IGNORED;
  if (pEntity && IsMeleeByName(pEntity)) {
    g_frameStash.Restore();
  }
  g_weaponPhase = true;
  g_phaseEnt = pEntity;
}

void OnPlayerPostThink_Post(edict_t * /*pEntity*/) {
  if (gpMetaGlobals) gpMetaGlobals->mres = MRES_IGNORED;
  g_weaponPhase = false;
  g_phaseEnt = nullptr;
  g_traceStash.Restore();
}

void OnTouch(edict_t *pentTouched, edict_t *pentOther) {
  if (gpMetaGlobals) gpMetaGlobals->mres = MRES_IGNORED;
  if (!pentTouched || !pentOther || !gpGlobals) return;
  RefreshCVars();
  if (!g_cvv.enabled || g_cvv.firethrough <= 0) return;

  edict_t *proj = nullptr;
  edict_t *playerEnt = nullptr;

  if (IsPlayer(pentTouched) && IsPhysicsProjectileMovetype(pentOther->v.movetype)) {
    playerEnt = pentTouched;
    proj = pentOther;
  } else if (IsPlayer(pentOther) && IsPhysicsProjectileMovetype(pentTouched->v.movetype)) {
    playerEnt = pentOther;
    proj = pentTouched;
  } else {
    return;
  }

  edict_t *owner = proj->v.owner;
  if (!IsLive(owner) || !IsPlayer(owner) || owner == playerEnt) {
    return;
  }

  if (gpMetaGlobals) {
    gpMetaGlobals->mres = MRES_SUPERCEDE;

    Vector vel = proj->v.velocity;
    const int projIdx = ENTINDEX(proj);
    if (projIdx > 0 && projIdx < MAX_ENTITIES && g_projVelActive[projIdx]) {
      vel = g_projVelCache[projIdx];
    }

    const float speed2 = vel.x * vel.x + vel.y * vel.y + vel.z * vel.z;
    if (speed2 > 1.0f && g_engfuncs.pfnTraceLine && g_engfuncs.pfnSetOrigin) {
      const float invLen = 1.0f / std::sqrt(speed2);
      const Vector dir(vel.x * invLen, vel.y * invLen, vel.z * invLen);
      constexpr float NUDGE_MAX = 72.0f;

      const Vector start = proj->v.origin;
      const Vector end = start + dir * NUDGE_MAX;
      TraceResult tr{};
      g_engfuncs.pfnTraceLine(start, end, TRACE_IGNORE_MONSTERS, proj, &tr);

      const float dist = NUDGE_MAX * tr.flFraction - 4.0f;
      if (dist > 8.0f) {
        const Vector dest = start + dir * dist;
        g_engfuncs.pfnSetOrigin(proj, dest);
        proj->v.velocity = vel;
        proj->v.groundentity = nullptr;
        if (projIdx > 0 && projIdx < MAX_ENTITIES) {
          g_projPendingVel[projIdx] = vel;
          g_projPendingActive[projIdx] = true;
          g_projVelCache[projIdx] = vel;
          g_projVelActive[projIdx] = true;
        }
      }
    }
  }
}

void OnBlocked(edict_t *pentBlocked, edict_t *pentOther) {
  if (gpMetaGlobals) gpMetaGlobals->mres = MRES_IGNORED;
  if (!pentBlocked || !pentOther || !gpGlobals) return;
  RefreshCVars();
  if (!g_cvv.enabled) return;

  if (!IsPlayer(pentOther)) return;
  const int idx = ENTINDEX(pentOther);

  if (idx >= 1 && idx <= MAX_TRACKED_CLIENTS && g_frameStash.held[idx]) {
    if (gpMetaGlobals) gpMetaGlobals->mres = MRES_SUPERCEDE;
    return;
  }

  if (!IsCorpseLike(pentOther->v.movetype, pentOther->v.deadflag)) {
    return;
  }

  const int n = (std::min)(gpGlobals->maxClients, MAX_TRACKED_CLIENTS);
  for (int i = 1; i <= n; i++) {
    edict_t *other = EdictFromIndex(i);
    if (!other || other == pentOther || !IsPlayer(other)) continue;
    if (IsCorpseLike(other->v.movetype, other->v.deadflag)) continue;
    if (BoxesOverlap(pentOther->v, other->v, -0.001f)) {
      if (gpMetaGlobals) gpMetaGlobals->mres = MRES_SUPERCEDE;
      return;
    }
  }
}

int OnShouldCollide(edict_t *pentTouched, edict_t *pentOther) {
  if (gpMetaGlobals) gpMetaGlobals->mres = MRES_IGNORED;
  if (!pentTouched || !pentOther || !gpGlobals) return 1;
  RefreshCVars();
  if (!g_cvv.enabled) return 1;

  edict_t *const a = pentTouched;
  edict_t *const b = pentOther;

  const bool pa = IsPlayer(a);
  const bool pb = IsPlayer(b);

  if (pa != pb) {
    edict_t *pl = pa ? a : b;
    edict_t *o = pa ? b : a;
    if (IsDeadPlayerEnt(o) && BoxesOverlap(pl->v, o->v, 4.0f)) {
      if (gpMetaGlobals) gpMetaGlobals->mres = MRES_SUPERCEDE;
      return 0;
    }
    return 1;
  }
  if (!pa || g_cvv.mode == 1) return 1;

  if (g_traceSkip && (a == g_traceSkip || b == g_traceSkip)) return 1;
  if (g_weaponPhase && g_phaseEnt && (a == g_phaseEnt || b == g_phaseEnt)) return 1;

  const int mode = g_cvv.mode;
  if (PairShouldBeSolid(a, b, mode) || PairShouldBeSolid(b, a, mode)) return 1;

  if (IsOnPusher(a) || IsOnPusher(b) || BoxesOverlap(a->v, b->v, 0.0f)) {
    if (gpMetaGlobals) gpMetaGlobals->mres = MRES_SUPERCEDE;
    return 0;
  }

  return 1;
}

void OnClientDisconnect(edict_t *client) {
  if (gpMetaGlobals) gpMetaGlobals->mres = MRES_IGNORED;
  if (g_phaseEnt == client) {
    g_phaseEnt = nullptr;
    g_weaponPhase = false;
  }
  ResetCapTargetForIndex(GetHostIndex(client));
}

void OnServerDeactivate() {
  if (gpMetaGlobals) gpMetaGlobals->mres = MRES_IGNORED;
  g_projPendingActive.fill(false);
  g_projVelActive.fill(false);
  RestoreAllSolidStates();
  ResetCapTracking();
  g_weaponPhase = false;
  g_phaseEnt = nullptr;
  g_traceSkip = nullptr;
  g_cvvLastRefresh = -1.0f;
  g_projVelCacheTime = -1.0f;
}

void OnTraceLine(const float *v1, const float *v2, int fNoMonsters,
                 edict_t *pentToSkip, TraceResult * /*ptr*/) {
  TracePre(v1, v2, fNoMonsters, pentToSkip);
}

void OnTraceHull(const float *v1, const float *v2, int fNoMonsters, int /*hull*/,
                 edict_t *pentToSkip, TraceResult * /*ptr*/) {
  TracePre(v1, v2, fNoMonsters, pentToSkip);
}

void OnTraceLine_Post(const float * /*v1*/, const float * /*v2*/, int /*fNoMonsters*/,
                      edict_t * /*pentToSkip*/, TraceResult * /*ptr*/) {
  if (gpMetaGlobals) gpMetaGlobals->mres = MRES_IGNORED;
  g_traceSkip = nullptr;
  g_traceStash.Restore();
}

void OnTraceHull_Post(const float * /*v1*/, const float * /*v2*/, int /*fNoMonsters*/,
                       int /*hull*/, edict_t * /*pentToSkip*/, TraceResult * /*ptr*/) {
  if (gpMetaGlobals) gpMetaGlobals->mres = MRES_IGNORED;
  g_traceSkip = nullptr;
  g_traceStash.Restore();
}

void OnPM_Move_Post(struct playermove_s * /*ppmove*/, int /*server*/) {
  if (gpMetaGlobals) gpMetaGlobals->mres = MRES_IGNORED;
  g_pmStash.Restore();
  g_traceStash.Restore();
}

void OnStartFrame() {
  if (gpMetaGlobals) gpMetaGlobals->mres = MRES_IGNORED;
  g_frameStash.Restore();

  if (!gpGlobals) return;
  RefreshCVars();
  if (!g_cvv.enabled) return;

  constexpr float MARGIN = 4.0f;
  const int n = (std::min)(gpGlobals->maxClients, MAX_TRACKED_CLIENTS);

  edict_t *ents[MAX_TRACKED_CLIENTS + 1] = {};
  bool flip[MAX_TRACKED_CLIENTS + 1] = {};

  for (int i = 1; i <= n; i++) {
    edict_t *e = EdictFromIndex(i);
    if (e && e->v.solid != SOLID_NOT) {
      ents[i] = e;
    }
  }

  for (int i = 1; i <= n; i++) {
    if (!ents[i]) continue;
    for (int j = i + 1; j <= n; j++) {
      if (!ents[j]) continue;
      if (!BoxesOverlap(ents[i]->v, ents[j]->v, MARGIN)) continue;

      const int mode = g_cvv.mode;
      if (PairShouldBeSolid(ents[i], ents[j], mode) ||
          PairShouldBeSolid(ents[j], ents[i], mode)) {
        continue;
      }

      // Pusher crush protection: only needed if either player is on a moving pusher.
      // Keeps players solid elsewhere to prevent breaking monster physics (barnacles) and unstuck checks.
      if (!IsOnPusher(ents[i]) && !IsOnPusher(ents[j])) {
        continue;
      }
      flip[i] = flip[j] = true;
    }
  }

  for (int i = 1; i <= n; i++) {
    if (flip[i]) {
      g_frameStash.Flip(ents[i], i);
    }
  }
}

void CmdStatus() {
  if (!gpGlobals || !g_engfuncs.pfnServerPrint) return;

  g_engfuncs.pfnServerPrint("[SMC] idx name             solid grp iuser4 effects rmode ramt mtype dead trkPM trkFrm trkTr\n");
  const int maxClients = (std::min)(gpGlobals->maxClients, MAX_TRACKED_CLIENTS);
  for (int i = 1; i <= maxClients; i++) {
    edict_t *ent = EdictFromIndex(i);
    char line[256];
    if (!ent) {
      std::snprintf(line, sizeof(line), "[SMC] %3d <empty>\n", i);
    } else {
      const char *name = STRING(ent->v.netname);
      std::snprintf(
          line, sizeof(line),
          "[SMC] %3d %-16.16s %5d %3d %6d %7d %5d %4d %5d %4d %5d  %6d  %5d\n",
          i, (name && name[0]) ? name : "-", ent->v.solid,
          ent->v.groupinfo, ent->v.iuser4, static_cast<int>(ent->v.effects),
          ent->v.rendermode, static_cast<int>(ent->v.renderamt), ent->v.movetype,
          ent->v.deadflag, static_cast<int>(g_pmStash.held[i]),
          static_cast<int>(g_frameStash.held[i]),
          static_cast<int>(g_traceStash.held[i]));
    }
    g_engfuncs.pfnServerPrint(line);
  }
}

} // namespace Semiclip
