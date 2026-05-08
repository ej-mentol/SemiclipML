#pragma once
#include <extdll.h>
#include <pm_defs.h>
#include <entity_state.h>

namespace Semiclip {
    void OnPM_Move(struct playermove_s *ppmove, int server);
    int OnAddToFullPack(struct entity_state_s *state, int e, edict_t *ent, edict_t *host, int hostflags, int player, unsigned char *pSet);
    void OnClientDisconnect(edict_t *client);
    void OnServerDeactivate();
    void ResetCapTracking();
}
