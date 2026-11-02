#pragma once
#include <extdll.h>
#include <meta_api.h>

extern cvar_t cv_enabled;
extern cvar_t cv_dist;
extern cvar_t cv_trans;
extern cvar_t cv_alpha;
extern cvar_t cv_mode; // 0 = Radius (Standard), 1 = Platform (Hat)
extern cvar_t cv_melee;
extern cvar_t cv_melee_range;
extern cvar_t cv_firethrough;

namespace Config {
// Default CVar values
constexpr float DEF_DIST = 64.0f;
constexpr float DEF_TRANS_DIST = 120.0f;
constexpr float DEF_ALPHA = 120.0f;
constexpr float DEF_MELEE_RANGE = 96.0f;
constexpr const char *DEF_MELEE_LIST = "crowbar,wrench,medkit,barnacle,knife,grapple,pipe";

// Sven Co-op 5.16+ expands physents from 600 (HLSDK MAX_PHYSENTS) to 1024
constexpr int MAX_PHYSENTS_SVEN = 1024;

// Definitive Semiclip Constants
constexpr float FLOAT_CROUCH = 39.0f; // Minimum height for any stacking
constexpr float SAFE_HEIGHT = 46.0f;  // Safe landing height
constexpr float CENTER_BOX = 20.0f;   // Box check radius
constexpr float OVERLAP_Z = 45.0f;    // Vertical overlap protection
constexpr float OVERLAP_XY = 38.0f;   // Horizontal overlap protection
constexpr float JUMP_THRESHOLD = 0.0f; // Any upward velocity = jumping

// Mode 1 "cap" tuning. The acquire thresholds are intentionally tighter than
// the hold thresholds to reduce state flapping and edge pushes.
constexpr float CAP_PLATFORM_THICKNESS = 4.0f;
constexpr float CAP_ACQUIRE_RADIUS = 12.0f;
constexpr float CAP_HOLD_RADIUS = 18.0f;
constexpr float CAP_ACQUIRE_ABOVE = 12.0f;
constexpr float CAP_HOLD_ABOVE = 18.0f;
constexpr float CAP_ACQUIRE_BELOW = 2.0f;
constexpr float CAP_HOLD_BELOW = 6.0f;
constexpr float CAP_MAX_RISE_SPEED = 30.0f;
constexpr float CAP_FAST_FALL_SPEED = -600.0f;

void RegisterCVars();
} // namespace Config
