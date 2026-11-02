#pragma once
#include <extdll.h>
#include <pm_defs.h>
#include <entity_state.h>

namespace Semiclip {
  void OnPM_Move(struct playermove_s *ppmove, int server);
  void OnPM_Move_Post(struct playermove_s *ppmove, int server);
  int OnAddToFullPack(struct entity_state_s *state, int e, edict_t *ent,
                      edict_t *host, int hostflags, int player,
                      unsigned char *pSet);
  void OnPlayerPreThink(edict_t *pEntity);
  void OnPlayerPostThink(edict_t *pEntity);
  void OnPlayerPostThink_Post(edict_t *pEntity);
  void OnTouch(edict_t *pentTouched, edict_t *pentOther);
  void OnBlocked(edict_t *pentBlocked, edict_t *pentOther);
  int OnShouldCollide(edict_t *pentTouched, edict_t *pentOther);
  void OnClientDisconnect(edict_t *client);
  void OnServerDeactivate();
  void ResetCapTracking();
  void RestoreAllSolidStates();
  void OnStartFrame();
  void OnTraceLine(const float *v1, const float *v2, int fNoMonsters,
                   edict_t *pentToSkip, TraceResult *ptr);
  void OnTraceHull(const float *v1, const float *v2, int fNoMonsters, int hull,
                   edict_t *pentToSkip, TraceResult *ptr);
  void OnTraceLine_Post(const float *v1, const float *v2, int fNoMonsters,
                        edict_t *pentToSkip, TraceResult *ptr);
  void OnTraceHull_Post(const float *v1, const float *v2, int fNoMonsters,
                         int hull, edict_t *pentToSkip, TraceResult *ptr);
  void CmdStatus();
} // namespace Semiclip
