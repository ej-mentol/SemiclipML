#include "Config.h"
#include <cstdio>

#if defined(__GNUC__)
// Old HLSDK structs (plugin_info_t, cvar_t) and engine prototypes take
// non-const char*; the string literals we pass are never written to.
// Compiler-level guard on purpose: gcc AND clang warn (-Wwrite-strings),
// MSVC does not - this is about the compiler, not the OS.
#pragma GCC diagnostic ignored "-Wwrite-strings"
#endif

// Flags: FCVAR_EXTDLL is required for the engine to expose these as console commands from a DLL
#define SMC_CVAR_FLAGS (FCVAR_EXTDLL | FCVAR_SERVER)

cvar_t cv_enabled = {"smc_enabled", "1", SMC_CVAR_FLAGS, 1.0f, nullptr};
cvar_t cv_dist = {"smc_dist", "64", SMC_CVAR_FLAGS, 64.0f, nullptr};
cvar_t cv_trans = {"smc_trans_dist", "120", SMC_CVAR_FLAGS, 120.0f, nullptr};
cvar_t cv_alpha = {"smc_alpha", "120", SMC_CVAR_FLAGS, 120.0f, nullptr};
cvar_t cv_mode = {"smc_mode", "0", SMC_CVAR_FLAGS, 0.0f, nullptr};
cvar_t cv_firethrough = {"smc_firethrough", "0", SMC_CVAR_FLAGS, 0.0f, nullptr};

// Default the live views to the local structs so reads are always safe,
// then rebind them to the engine's own cvar storage after registration.
cvar_t *pv_enabled = &cv_enabled;
cvar_t *pv_dist = &cv_dist;
cvar_t *pv_trans = &cv_trans;
cvar_t *pv_alpha = &cv_alpha;
cvar_t *pv_mode = &cv_mode;
cvar_t *pv_firethrough = &cv_firethrough;

static cvar_t *ResolveLive(cvar_t &local) {
    cvar_t *live = g_engfuncs.pfnCVarGetPointer
                       ? g_engfuncs.pfnCVarGetPointer(local.name)
                       : nullptr;

    // Classify what pfnCVarRegister actually did — its void return hides three
    // very different outcomes, and reads behave differently in each:
    //   linked-local : engine linked OUR struct; direct .value would work
    //   engine-side  : engine keeps its own storage; live pointer is REQUIRED
    //   missing      : registration failed entirely (or CVarGetPointer absent)
    if (g_engfuncs.pfnServerPrint) {
        char msg[160];
        const char *kind = !live ? "MISSING (register failed or no CVarGetPointer)"
                           : (live == &local)
                               ? "linked-local"
                               : "engine-side storage (live pointer required)";
        snprintf(msg, sizeof(msg), "[SMC] cvar %s -> %s\n", local.name, kind);
        g_engfuncs.pfnServerPrint(msg);
    }

    return live ? live : &local;
}

void Config::RegisterCVars() {
    if (!g_engfuncs.pfnCVarRegister) {
        return;
    }

    // Register exactly once per process: on a re-attach in the same server
    // process the engine already has these names, and re-registering would
    // hit the silent "already defined" early-out. Re-RESOLVING below is done
    // every attach on purpose — it rebinds our live pointers to whatever
    // storage the engine actually holds.
    static bool s_registered = false;
    if (!s_registered) {
        s_registered = true;
        g_engfuncs.pfnCVarRegister(&cv_enabled);
        g_engfuncs.pfnCVarRegister(&cv_dist);
        g_engfuncs.pfnCVarRegister(&cv_trans);
        g_engfuncs.pfnCVarRegister(&cv_alpha);
        g_engfuncs.pfnCVarRegister(&cv_mode);
        g_engfuncs.pfnCVarRegister(&cv_firethrough);
    }

    pv_enabled = ResolveLive(cv_enabled);
    pv_dist = ResolveLive(cv_dist);
    pv_trans = ResolveLive(cv_trans);
    pv_alpha = ResolveLive(cv_alpha);
    pv_mode = ResolveLive(cv_mode);
    pv_firethrough = ResolveLive(cv_firethrough);
}
