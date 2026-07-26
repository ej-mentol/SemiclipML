#pragma once
#include <extdll.h>
#include <meta_api.h>

extern cvar_t cv_enabled;
extern cvar_t cv_dist;
extern cvar_t cv_trans;
extern cvar_t cv_alpha;
extern cvar_t cv_mode; // 0 = Radius (Standard), 1 = Platform (Hat)
extern cvar_t cv_firethrough; // fire-through: think-window solid flip + projectile touch phase-through

// Live cvar views: engine-side structs resolved via CVarGetPointer after
// registration. Console changes land in the ENGINE's cvar storage; on Sven
// the locally registered struct's .value stays frozen at its default, so all
// runtime reads must go through these pointers.
extern cvar_t *pv_enabled;
extern cvar_t *pv_dist;
extern cvar_t *pv_trans;
extern cvar_t *pv_alpha;
extern cvar_t *pv_mode;
extern cvar_t *pv_firethrough;

namespace Config {
// Definitive Semiclip Constants
constexpr float FLOAT_CROUCH = 39.0f; // Minimum height for any stacking
constexpr float SAFE_HEIGHT = 46.0f;  // Safe landing height
constexpr float CENTER_BOX =
    28.0f; // Box check radius (Reduced to 28.0 per user request)
constexpr float OVERLAP_Z = 45.0f;  // Vertical overlap protection
constexpr float OVERLAP_XY = 38.0f; // Horizontal overlap protection
constexpr float JUMP_THRESHOLD = 0.0f;   // Any upward velocity = jumping
constexpr float FALL_THRESHOLD = -10.0f; // Stable falling velocity

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
