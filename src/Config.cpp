#include "Config.h"

// Flags: FCVAR_EXTDLL is required for the engine to expose these as console commands from a DLL
#define SMC_CVAR_FLAGS (FCVAR_EXTDLL | FCVAR_SERVER)

cvar_t cv_enabled = {"smc_enabled", "1", SMC_CVAR_FLAGS, 1.0f, nullptr};
cvar_t cv_dist = {"smc_dist", "64", SMC_CVAR_FLAGS, 64.0f, nullptr};
cvar_t cv_trans = {"smc_trans_dist", "120", SMC_CVAR_FLAGS, 120.0f, nullptr};
cvar_t cv_alpha = {"smc_alpha", "120", SMC_CVAR_FLAGS, 120.0f, nullptr};
cvar_t cv_mode = {"smc_mode", "0", SMC_CVAR_FLAGS, 0.0f, nullptr};

void Config::RegisterCVars() {
    if (!g_engfuncs.pfnCVarRegister) {
        return;
    }

    g_engfuncs.pfnCVarRegister(&cv_enabled);
    g_engfuncs.pfnCVarRegister(&cv_dist);
    g_engfuncs.pfnCVarRegister(&cv_trans);
    g_engfuncs.pfnCVarRegister(&cv_alpha);
    g_engfuncs.pfnCVarRegister(&cv_mode);
}
