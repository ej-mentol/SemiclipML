#include "Config.h"
#include "Semiclip.h"
#include <extdll.h>
#include <h_export.h>
#include <meta_api.h>
#include <cstring>

// Global vars
meta_globals_t *gpMetaGlobals;
gamedll_funcs_t *gpGamedllFuncs;
mutil_funcs_t *gpMetaUtilFuncs;
enginefuncs_t g_engfuncs;
globalvars_t *gpGlobals;

#if defined(_WIN32) && defined(_M_IX86)
#pragma comment(linker, "/EXPORT:GiveFnptrsToDll=_GiveFnptrsToDll@8")
#endif

// Plugin info
plugin_info_t Plugin_info = {
    META_INTERFACE_VERSION, // ifvers
    "SemiclipML",           // name
    "2.5",                  // version
    "2026/07/26",           // date
    "mentol",                 // author
    "",                     // url
    "SMC",                  // logtag
    PT_CHANGELEVEL,         // (when) loadable
    PT_CHANGELEVEL,         // (when) unloadable
};

DLL_FUNCTIONS g_dll_hooks;
static DLL_FUNCTIONS g_dll_hooks_post;

// Initialization
void PluginInit() {
  std::memset(&g_dll_hooks, 0, sizeof(g_dll_hooks));
  std::memset(&g_dll_hooks_post, 0, sizeof(g_dll_hooks_post));

  g_dll_hooks.pfnPM_Move = Semiclip::OnPM_Move;
  g_dll_hooks.pfnPlayerPreThink = Semiclip::OnPlayerPreThink;
  g_dll_hooks.pfnTouch = Semiclip::OnTouch;
  g_dll_hooks.pfnClientDisconnect = Semiclip::OnClientDisconnect;
  g_dll_hooks.pfnServerDeactivate = Semiclip::OnServerDeactivate;
  // 2.4: post-hook restores pev->solid values flipped in OnPM_Move.
  // Without this registration the SOLID_NOT flips leak permanently.
  g_dll_hooks_post.pfnPM_Move = Semiclip::OnPM_Move_Post;
  g_dll_hooks_post.pfnAddToFullPack = Semiclip::OnAddToFullPack;
  g_dll_hooks_post.pfnPlayerPostThink = Semiclip::OnPlayerPostThink_Post;

  Config::RegisterCVars();

  if (g_engfuncs.pfnAddServerCommand) {
    g_engfuncs.pfnAddServerCommand("smc_status", Semiclip::CmdStatus);
  }
}

// Metamod Callbacks
C_DLLEXPORT int GetEntityAPI2(DLL_FUNCTIONS *pFunctionTable,
                              int *interfaceVersion) {
  if (!pFunctionTable || !interfaceVersion) {
    return FALSE;
  }

  if (*interfaceVersion != INTERFACE_VERSION) {
    *interfaceVersion = INTERFACE_VERSION;
    return FALSE;
  }

  std::memcpy(pFunctionTable, &g_dll_hooks, sizeof(DLL_FUNCTIONS));
  return TRUE;
}

C_DLLEXPORT int GetEntityAPI2_Post(DLL_FUNCTIONS *pFunctionTable,
                                   int *interfaceVersion) {
  if (!pFunctionTable || !interfaceVersion) {
    return FALSE;
  }

  if (*interfaceVersion != INTERFACE_VERSION) {
    *interfaceVersion = INTERFACE_VERSION;
    return FALSE;
  }

  std::memcpy(pFunctionTable, &g_dll_hooks_post, sizeof(DLL_FUNCTIONS));
  return TRUE;
}

#ifdef WIN32
extern "C" C_DLLEXPORT void WINAPI GiveFnptrsToDll(enginefuncs_t *pengfuncsFromEngine,
                                       globalvars_t *pGlobals) {
#else
extern "C" C_DLLEXPORT void GiveFnptrsToDll(enginefuncs_t *pengfuncsFromEngine,
                                globalvars_t *pGlobals) {
#endif
  if (pengfuncsFromEngine) {
    std::memcpy(&g_engfuncs, pengfuncsFromEngine, sizeof(enginefuncs_t));
  }
  gpGlobals = pGlobals;
}

static META_FUNCTIONS gMetaFunctionTable = {
    NULL,               // pfnGetEntityAPI
    NULL,               // pfnGetEntityAPI_Post
    GetEntityAPI2,      // pfnGetEntityAPI2
    GetEntityAPI2_Post, // pfnGetEntityAPI2_Post
    NULL,               // pfnGetNewDLLFunctions
    NULL,               // pfnGetNewDLLFunctions_Post
    // 2.4: deliberately NO GetEngineFunctions / _Post. This plugin hooks no
    // engine functions, and copying sizeof(enginefuncs_t) into metamod's
    // buffer is an ABI trap: enginefuncs_t grew in metamod-p p105
    // (pfnPEntityOfEntIndexAllEntities), so a plugin built with new headers
    // overflows the table of an older metamod binary and crashes on load.
    NULL,               // pfnGetEngineFunctions
    NULL,               // pfnGetEngineFunctions_Post
};

C_DLLEXPORT int Meta_Query(char *interfaceVersion, plugin_info_t **plinfo,
                           mutil_funcs_t *pMetaUtilFuncs) {
  if (!interfaceVersion || !plinfo || !pMetaUtilFuncs) {
    return FALSE;
  }

  *plinfo = &Plugin_info;
  gpMetaUtilFuncs = pMetaUtilFuncs;
  return TRUE;
}

C_DLLEXPORT int Meta_Attach(PLUG_LOADTIME now, META_FUNCTIONS *pFunctionTable,
                            meta_globals_t *pMGlobals,
                            gamedll_funcs_t *pGamedllFuncs) {
  if (!pFunctionTable || !pMGlobals) {
    return FALSE;
  }

  gpMetaGlobals = pMGlobals;
  gpGamedllFuncs = pGamedllFuncs;
  
  std::memcpy(pFunctionTable, &gMetaFunctionTable, sizeof(META_FUNCTIONS));

  PluginInit();

  return TRUE;
}

C_DLLEXPORT int Meta_Detach(PLUG_LOADTIME now, PL_UNLOAD_REASON reason) {
  // 2.4: if we unload between a pre/post pair, put pev->solid back first.
  Semiclip::RestoreTrackedSolidStates();
  Semiclip::RestoreThinkWindowStates();
  Semiclip::ResetCapTracking();
  gpGamedllFuncs = nullptr;
  gpMetaGlobals = nullptr;
  gpMetaUtilFuncs = nullptr;
  return TRUE;
}
