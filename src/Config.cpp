#include "Config.h"

// Flags: FCVAR_EXTDLL is required for the engine to expose these as console commands from a DLL
#define SMC_CVAR_FLAGS (FCVAR_EXTDLL | FCVAR_SERVER)

#define SMC_CVAR(n, s, v) {const_cast<char *>(n), const_cast<char *>(s), SMC_CVAR_FLAGS, v, nullptr}

cvar_t cv_enabled = SMC_CVAR("smc_enabled", "1", 1.0f);
cvar_t cv_dist = SMC_CVAR("smc_dist", "64", Config::DEF_DIST);
cvar_t cv_trans = SMC_CVAR("smc_trans_dist", "120", Config::DEF_TRANS_DIST);
cvar_t cv_alpha = SMC_CVAR("smc_alpha", "120", Config::DEF_ALPHA);
cvar_t cv_mode = SMC_CVAR("smc_mode", "0", 0.0f);
cvar_t cv_firethrough = SMC_CVAR("smc_firethrough", "0", 0.0f);
cvar_t cv_melee_range = SMC_CVAR("smc_melee_range", "96", Config::DEF_MELEE_RANGE);
cvar_t cv_melee = SMC_CVAR("smc_melee", Config::DEF_MELEE_LIST, 0.0f);

void Config::RegisterCVars() {
  if (!g_engfuncs.pfnCVarRegister) {
    return;
  }

  g_engfuncs.pfnCVarRegister(&cv_enabled);
  g_engfuncs.pfnCVarRegister(&cv_dist);
  g_engfuncs.pfnCVarRegister(&cv_trans);
  g_engfuncs.pfnCVarRegister(&cv_alpha);
  g_engfuncs.pfnCVarRegister(&cv_mode);
  g_engfuncs.pfnCVarRegister(&cv_firethrough);
  g_engfuncs.pfnCVarRegister(&cv_melee);
  g_engfuncs.pfnCVarRegister(&cv_melee_range);
}
