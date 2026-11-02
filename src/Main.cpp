#include "Config.h"
#include "Semiclip.h"
#include <extdll.h>
#include <h_export.h>
#include <meta_api.h>
#include <engine_api.h>
#include <cstring>

// Global vars
meta_globals_t *gpMetaGlobals = nullptr;
gamedll_funcs_t *gpGamedllFuncs = nullptr;
mutil_funcs_t *gpMetaUtilFuncs = nullptr;
enginefuncs_t g_engfuncs;
globalvars_t *gpGlobals = nullptr;

#if defined(_WIN32) && defined(_M_IX86)
#pragma comment(linker, "/EXPORT:GiveFnptrsToDll=_GiveFnptrsToDll@8")
#endif

// Plugin info
plugin_info_t Plugin_info = {
    META_INTERFACE_VERSION,           // ifvers
    const_cast<char *>("SemiclipML"), // name
    const_cast<char *>("2.8"),        // version
    const_cast<char *>("2026/11/02"), // date
    const_cast<char *>("Necr"),       // author
    const_cast<char *>(""),           // url
    const_cast<char *>("SMC"),        // logtag
    PT_CHANGELEVEL,                   // (when) loadable
    PT_NEVER,                         // (when) unloadable
};

static DLL_FUNCTIONS gFunctionTable;
static DLL_FUNCTIONS gFunctionTable_Post;
static NEW_DLL_FUNCTIONS gNewDLLFunctionTable;

static void PluginInit() {
  std::memset(&gFunctionTable, 0, sizeof(gFunctionTable));
  std::memset(&gFunctionTable_Post, 0, sizeof(gFunctionTable_Post));

  gFunctionTable.pfnPM_Move = Semiclip::OnPM_Move;
  gFunctionTable.pfnPlayerPreThink = Semiclip::OnPlayerPreThink;
  gFunctionTable.pfnTouch = Semiclip::OnTouch;
  gFunctionTable.pfnBlocked = Semiclip::OnBlocked;
  gFunctionTable.pfnPlayerPostThink = Semiclip::OnPlayerPostThink;
  gFunctionTable.pfnClientDisconnect = Semiclip::OnClientDisconnect;
  gFunctionTable.pfnServerDeactivate = Semiclip::OnServerDeactivate;

  gFunctionTable_Post.pfnPM_Move = Semiclip::OnPM_Move_Post;
  gFunctionTable_Post.pfnAddToFullPack = Semiclip::OnAddToFullPack;
  gFunctionTable_Post.pfnPlayerPostThink = Semiclip::OnPlayerPostThink_Post;
  gFunctionTable_Post.pfnStartFrame = Semiclip::OnStartFrame;

  Config::RegisterCVars();

  if (g_engfuncs.pfnAddServerCommand) {
    g_engfuncs.pfnAddServerCommand(const_cast<char *>("smc_status"), Semiclip::CmdStatus);
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
  std::memcpy(pFunctionTable, &gFunctionTable, sizeof(DLL_FUNCTIONS));
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
  std::memcpy(pFunctionTable, &gFunctionTable_Post, sizeof(DLL_FUNCTIONS));
  return TRUE;
}

C_DLLEXPORT int GetEngineFunctions(enginefuncs_t *t, int *ver) {
  if (!t || !ver) return FALSE;
  if (*ver != ENGINE_INTERFACE_VERSION) {
    *ver = ENGINE_INTERFACE_VERSION;
    return FALSE;
  }
  t->pfnTraceLine = Semiclip::OnTraceLine;
  t->pfnTraceHull = Semiclip::OnTraceHull;
  return TRUE;
}

C_DLLEXPORT int GetEngineFunctions_Post(enginefuncs_t *t, int *ver) {
  if (!t || !ver) return FALSE;
  if (*ver != ENGINE_INTERFACE_VERSION) {
    *ver = ENGINE_INTERFACE_VERSION;
    return FALSE;
  }
  t->pfnTraceLine = Semiclip::OnTraceLine_Post;
  t->pfnTraceHull = Semiclip::OnTraceHull_Post;
  return TRUE;
}

C_DLLEXPORT int GetNewDLLFunctions(NEW_DLL_FUNCTIONS *t, int *ver) {
  if (!t || !ver) {
    return FALSE;
  }
  if (*ver != NEW_DLL_FUNCTIONS_VERSION) {
    *ver = NEW_DLL_FUNCTIONS_VERSION;
    return FALSE;
  }
  std::memset(&gNewDLLFunctionTable, 0, sizeof(gNewDLLFunctionTable));
  gNewDLLFunctionTable.pfnShouldCollide = Semiclip::OnShouldCollide;
  std::memcpy(t, &gNewDLLFunctionTable, sizeof(NEW_DLL_FUNCTIONS));
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
    NULL,                    // pfnGetEntityAPI
    NULL,                    // pfnGetEntityAPI_Post
    GetEntityAPI2,           // pfnGetEntityAPI2
    GetEntityAPI2_Post,      // pfnGetEntityAPI2_Post
    GetNewDLLFunctions,      // pfnGetNewDLLFunctions
    NULL,                    // pfnGetNewDLLFunctions_Post
    GetEngineFunctions,      // pfnGetEngineFunctions
    GetEngineFunctions_Post, // pfnGetEngineFunctions_Post
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

C_DLLEXPORT int Meta_Attach(PLUG_LOADTIME /*now*/, META_FUNCTIONS *pFunctionTable,
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

C_DLLEXPORT int Meta_Detach(PLUG_LOADTIME /*now*/, PL_UNLOAD_REASON /*reason*/) {
  Semiclip::RestoreAllSolidStates();
  Semiclip::ResetCapTracking();
  gpGamedllFuncs = nullptr;
  gpMetaGlobals = nullptr;
  gpMetaUtilFuncs = nullptr;
  return TRUE;
}
