#pragma once
#include <extdll.h>
#include <pm_defs.h>
#include <entity_state.h>

namespace Semiclip {
    void OnPM_Move(struct playermove_s *ppmove, int server);
    void OnPM_Move_Post(struct playermove_s *ppmove, int server);
    int OnAddToFullPack(struct entity_state_s *state, int e, edict_t *ent, edict_t *host, int hostflags, int player, unsigned char *pSet);
    // 2.5 (smc_firethrough): widen the pev->solid flip window from PM_Move
    // to the whole player command (PreThink..PostThink) so weapon-fire traces
    // in PostThink also see semiclipped players as non-solid.
    void OnPlayerPreThink(edict_t *pEntity);
    void OnPlayerPostThink_Post(edict_t *pEntity);
    // 2.5 (smc_firethrough): block game-dll touch (damage/detonation/stick)
    // between a projectile and a player who is semiclipped with its owner.
    void OnTouch(edict_t *pentTouched, edict_t *pentOther);
    void RestoreThinkWindowStates();
    // Server console command "smc_status": per-client dump of every pev field
    // that can make a player invisible or non-solid, plus our tracking flags.
    void CmdStatus();
    void OnClientDisconnect(edict_t *client);
    void OnServerDeactivate();
    void ResetCapTracking();
    // 2.4: restores any pev->solid values flipped during OnPM_Move.
    // Called from OnPM_Move_Post every move, and from Meta_Detach as a safety net.
    void RestoreTrackedSolidStates();
}
