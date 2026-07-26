#pragma once
#include <extdll.h>
#include <pm_defs.h>
#include <entity_state.h>

namespace Semiclip {
    void OnPM_Move(struct playermove_s *ppmove, int server);
    void OnPM_Move_Post(struct playermove_s *ppmove, int server);
    int OnAddToFullPack(struct entity_state_s *state, int e, edict_t *ent, edict_t *host, int hostflags, int player, unsigned char *pSet);
    void OnClientDisconnect(edict_t *client);
    void OnServerDeactivate();
    void ResetCapTracking();
    // 2.4: restores any pev->solid values flipped during OnPM_Move.
    // Called from OnPM_Move_Post every move, and from Meta_Detach as a safety net.
    void RestoreTrackedSolidStates();
}
