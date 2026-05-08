// Pose.hpp — Agent 2: Source Position Interpolation & Transform
//
// Computes per-source spatial positions for each audio block by interpolating
// LUSID scene keyframes and applying layout-aware transforms. The spatializer
// (Phase 4) consumes these positions to compute per-speaker gains.
//
// RESPONSIBILITIES:
// 1. Analyze the speaker layout at load time (radius, elevation bounds, 2D).
// 2. For each audio block, compute every source's position at block center time.
// 3. Interpolate between keyframes using SLERP (spherical linear interpolation).
// 4. Sanitize elevations for the current layout (clamp, rescale, or flatten).
// 5. Apply the DBAP coordinate transform (our y-forward → AlloLib's convention).
// 6. Output a flat vector of SourcePose structs ready for the spatializer.
//
// ─────────────────────────────────────────────────────────────────────────────
// THREADING MODEL (Phase 8 — Threading and Safety)
// ─────────────────────────────────────────────────────────────────────────────
//
//  MAIN thread:
//    - Calls loadScene() (setup, before start()). Fully populates all member
//      data. After loadScene() returns, all fields are read-only except those
//      explicitly noted below.
//    - Never calls computePositions().
//
//  AUDIO thread:
//    - Calls computePositions() once per audio block (from processBlock()).
//    - Calls getPoses() immediately after to read the updated positions.
//    - EXCLUSIVELY owns mPoses and mLastGoodDir during playback.
//      No other thread touches these after loadScene().
//
//  LOADER thread:
//    - Does NOT interact with Pose at all.
//
//  READ-ONLY after loadScene() (safe to read from any thread without sync):
//    mSources, mSourceOrder, mLayoutRadius, mLayoutMinElRad,
//    mLayoutMaxElRad, mLayoutIs2D, mState
//
//  LIVE ATOMIC (read by audio thread per-block, written by OSC listener):
//    mConfig.elevationMode — loaded once at the top of computePositions() via
//      relaxed atomic load. Stale-by-one-block is acceptable: elevation mode
//      switches are not sample-accurate operations. No other mConfig fields
//      are read by Pose during playback — the rest of mConfig is read-only
//      after loadScene() from Pose's perspective.
//
//  AUDIO-THREAD-OWNED (must not be read or written from any other thread
//  while audio is streaming):
//    mPoses        — written by computePositions(), read by getPoses()
//    mLastGoodDir  — cache of per-source last-good direction, updated lazily
//                    by safeDirForSource(); a std::map allocation can occur
//                    the first time a new source is inserted, but after the
//                    first audio block all insertions are already done.
//
// ─────────────────────────────────────────────────────────────────────────────
//
// PROVENANCE:
// - SLERP interpolation: adapted from SpatialRenderer::slerpDir()
// - Direction interpolation: adapted from SpatialRenderer::interpolateDirRaw()
// - Degenerate fallback: adapted from SpatialRenderer::safeDirForSource()
// - Elevation sanitization: adapted from SpatialRenderer::sanitizeDirForLayout()
// - DBAP coordinate transform: adapted from SpatialRenderer::directionToDBAPPosition()
// - Layout analysis: adapted from SpatialRenderer constructor
//
// REAL-TIME SAFETY:
// - computePositions() is called once per audio block at the start of
//   processBlock(). It uses only pre-allocated data structures and never
//   allocates, locks, or does I/O.
// - All per-source data (keyframes, last-good directions) is loaded once
//   at scene load time and never modified during playback.
//   (Exception: mLastGoodDir can insert new entries during the FIRST block
//   per source — this is a one-time std::map allocation that happens before
//   steady-state audio is reached and is acceptable.)

#pragma once

#ifdef _MSC_VER
#  define _USE_MATH_DEFINES
#endif

#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "al/math/al_Vec.hpp"       // al::Vec3f
#include "JSONLoader.hpp"            // Keyframe, SpatialData
#include "LayoutLoader.hpp"          // SpeakerLayoutData, SpeakerData
#include "RealtimeTypes.hpp"         // RealtimeConfig, ElevationMode

// ─────────────────────────────────────────────────────────────────────────────
// SourcePose — Per-source position snapshot for one audio block
// ─────────────────────────────────────────────────────────────────────────────
// Computed by Pose, consumed by the Spatializer (Phase 4).

struct SourcePose {
    std::string name;                // Source key (e.g., "1.1", "LFE")
    al::Vec3f   position;            // DBAP position at block center (coord-transformed)
    al::Vec3f   positionStart;       // DBAP position at block start  (Fix 2 — fast-mover)
    al::Vec3f   positionEnd;         // DBAP position at block end    (Fix 2 — fast-mover)
    bool        isLFE     = false;   // True → route to subwoofer, skip DBAP
    bool        isValid   = true;    // False → source had no usable position
};


// ─────────────────────────────────────────────────────────────────────────────
// Pose — Source position manager for the real-time engine
// ─────────────────────────────────────────────────────────────────────────────

class Pose {
public:

    Pose(RealtimeConfig& config, EngineState& state)
        : mConfig(config), mState(state) {}

    // ── Load scene and layout ────────────────────────────────────────────
    // Must be called BEFORE the audio stream starts.
    // Stores keyframes and analyzes the speaker layout for elevation bounds.

    bool loadScene(const SpatialData& scene, const SpeakerLayoutData& layout) {

        // ── Store keyframes per source ───────────────────────────────────
        mSources = scene.sources;
        std::cout << "[Pose] Loaded keyframes for " << mSources.size()
                  << " sources." << std::endl;

        // ── Analyze speaker layout ───────────────────────────────────────
        // Compute layout radius (median of speaker distances)
        std::vector<float> distances;
        distances.reserve(layout.speakers.size());
        for (const auto& spk : layout.speakers) {
            distances.push_back(spk.radius);
        }

        if (!distances.empty()) {
            std::sort(distances.begin(), distances.end());
            size_t mid = distances.size() / 2;
            if (distances.size() % 2 == 0) {
                mLayoutRadius = (distances[mid - 1] + distances[mid]) / 2.0f;
            } else {
                mLayoutRadius = distances[mid];
            }
        }

        // Compute elevation bounds (radians) from speaker positions
        mLayoutMinElRad =  1e9f;
        mLayoutMaxElRad = -1e9f;
        for (const auto& spk : layout.speakers) {
            mLayoutMinElRad = std::min(mLayoutMinElRad, spk.elevation);
            mLayoutMaxElRad = std::max(mLayoutMaxElRad, spk.elevation);
        }
        float elSpan = mLayoutMaxElRad - mLayoutMinElRad;

        // Detect "effectively 2D" layouts (elevation span < 3 degrees)
        const float twoDThreshRad = 3.0f * static_cast<float>(M_PI) / 180.0f;
        mLayoutIs2D = (elSpan < twoDThreshRad);

        std::cout << "[Pose] Layout: " << layout.speakers.size() << " speakers"
                  << ", radius: " << mLayoutRadius << "m"
                  << ", elevation: ["
                  << (mLayoutMinElRad * 180.0f / static_cast<float>(M_PI)) << "°, "
                  << (mLayoutMaxElRad * 180.0f / static_cast<float>(M_PI)) << "°]"
                  << (mLayoutIs2D ? " (2D)" : " (3D)") << std::endl;

        // ── Pre-allocate the pose output vector ──────────────────────────
        // One entry per source, in a stable order. The spatializer will
        // iterate this vector on the audio thread.
        mPoses.clear();
        mPoses.reserve(mSources.size());
        mSourceOrder.clear();
        mSourceOrder.reserve(mSources.size());

        for (const auto& [name, kfs] : mSources) {
            SourcePose pose;
            pose.name  = name;
            pose.isLFE = (name == "LFE");
            mPoses.push_back(pose);
            mSourceOrder.push_back(name);
        }

        // Pre-allocate fallback direction cache
        mLastGoodDir.clear();

        mState.numSpeakers.store(static_cast<int>(layout.speakers.size()),
                                 std::memory_order_relaxed);

        return true;
    }

    // ── Compute positions for all sources at a given time ────────────────
    // Called once at the start of each audio block from processBlock().
    // blockCenterTimeSec = playback time at the center of the current block.
    //
    // Updates the internal mPoses vector in-place. The spatializer reads
    // from getPoses() after this call.
    //
    // THREADING: AUDIO THREAD ONLY. Must not be called from any other thread.
    //   - Writes mPoses[i].position and mPoses[i].isValid.
    //   - May lazily insert into mLastGoodDir on the first pass per source.
    //     After the first complete block, all map keys exist and no further
    //     allocation occurs.
    //
    // REAL-TIME SAFE: no allocation (after first block), no I/O, no locks.

    // Fix 2 — signature extended to carry block start and end times so that
    // positionStart / positionEnd can be computed for fast-mover sub-stepping.
    // blockCenterTimeSec is derived internally as the midpoint.
    //
    // Ordering contract (important for mLastGoodDir correctness):
    //   1. Center position is computed FIRST via the normal mutating path
    //      (safeDirForSource writes mLastGoodDir when it sees a valid direction).
    //   2. positionStart and positionEnd are computed via computePositionAtTimeReadOnly(),
    //      which reads mLastGoodDir but never writes it.
    // This guarantees that mLastGoodDir reflects the center-time direction and is
    // not overwritten by start/end evaluations that are only ~5 ms away.
    void computePositions(double blockStartTimeSec, double blockEndTimeSec) {
        const double blockCenterTimeSec = (blockStartTimeSec + blockEndTimeSec) * 0.5;

        // Read elevation mode once per block (relaxed — stale-by-one-block is fine).
        ElevationMode elMode = static_cast<ElevationMode>(
            mConfig.elevationMode.load(std::memory_order_relaxed));

        for (size_t i = 0; i < mSourceOrder.size(); ++i) {
            const std::string& name = mSourceOrder[i];
            SourcePose& pose = mPoses[i];

            // LFE doesn't need a spatial position — it goes straight to subs
            if (pose.isLFE) {
                pose.position = pose.positionStart = pose.positionEnd =
                    al::Vec3f(0.0f, 0.0f, 0.0f);
                pose.isValid = true;
                continue;
            }

            // Look up this source's keyframes
            auto it = mSources.find(name);
            if (it == mSources.end() || it->second.empty()) {
                pose.isValid = false;
                continue;
            }

            // ── Center position (mutating path — updates mLastGoodDir) ───────
            // Step 1: Interpolate raw direction from keyframes (SLERP)
            al::Vec3f rawDir = interpolateDirRaw(it->second, blockCenterTimeSec);
            // Step 2: Validate and apply fallback if degenerate (writes mLastGoodDir)
            al::Vec3f safeDir = safeDirForSource(name, it->second,
                                                  rawDir, blockCenterTimeSec);
            // Step 3: Sanitize elevation for speaker layout
            al::Vec3f sanitized = sanitizeDirForLayout(safeDir, elMode);
            // Step 4: Convert to DBAP position (coord transform + radius)
            pose.position = directionToDBAPPosition(sanitized);
            pose.isValid = true;

            // ── Start / End positions (read-only path — mLastGoodDir untouched) ─
            // Uses mLastGoodDir (set just above) as the fallback but never writes it.
            pose.positionStart = computePositionAtTimeReadOnly(
                name, it->second, blockStartTimeSec, elMode);
            pose.positionEnd   = computePositionAtTimeReadOnly(
                name, it->second, blockEndTimeSec, elMode);
        }
    }

    // ── Accessors ────────────────────────────────────────────────────────

    /// Get the computed poses. Call after computePositions().
    const std::vector<SourcePose>& getPoses() const { return mPoses; }

    /// Number of sources with poses.
    size_t numSources() const { return mPoses.size(); }


private:

    // ═════════════════════════════════════════════════════════════════════
    // INTERPOLATION — Adapted from SpatialRenderer.cpp
    // ═════════════════════════════════════════════════════════════════════

    // ── Safe normalize: returns front direction if input is degenerate ───
    static al::Vec3f safeNormalize(const al::Vec3f& v) {
        float mag = v.mag();
        if (mag < 1e-6f || !std::isfinite(mag)) {
            return al::Vec3f(0.0f, 1.0f, 0.0f);  // fallback: front
        }
        return v / mag;
    }

    // ── Check if all components are finite ───────────────────────────────
    static bool finite3(const al::Vec3f& v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }

    // ── SLERP: Spherical linear interpolation between two unit vectors ──
    // Adapted from SpatialRenderer::slerpDir()
    static al::Vec3f slerpDir(const al::Vec3f& a, const al::Vec3f& b, float t) {
        t = std::max(0.0f, std::min(1.0f, t));

        float dot = a.dot(b);
        dot = std::max(-1.0f, std::min(1.0f, dot));

        // Very close → linear interpolation
        if (dot > 0.9995f) {
            return safeNormalize(a + t * (b - a));
        }

        // Nearly opposite → rotate around perpendicular axis
        if (dot < -0.9995f) {
            al::Vec3f perp = (std::abs(a.x) < 0.9f)
                ? al::Vec3f(1, 0, 0) : al::Vec3f(0, 1, 0);
            perp = (a.cross(perp)).normalize();
            float theta = static_cast<float>(M_PI) * t;
            return a * std::cos(theta) + perp * std::sin(theta);
        }

        // Standard SLERP
        float theta    = std::acos(dot);
        float sinTheta = std::sin(theta);
        float wa = std::sin((1.0f - t) * theta) / sinTheta;
        float wb = std::sin(t * theta) / sinTheta;
        return a * wa + b * wb;
    }

    // ── Raw keyframe interpolation (may return degenerate vectors) ───────
    // Adapted from SpatialRenderer::interpolateDirRaw()
    al::Vec3f interpolateDirRaw(const std::vector<Keyframe>& kfs, double t) const {
        if (kfs.empty()) return al::Vec3f(0.0f, 0.0f, 0.0f);

        if (kfs.size() == 1) {
            return safeNormalize(al::Vec3f(kfs[0].x, kfs[0].y, kfs[0].z));
        }

        // Clamp to first/last keyframe
        if (t <= kfs.front().time) {
            return safeNormalize(al::Vec3f(kfs.front().x, kfs.front().y, kfs.front().z));
        }
        if (t >= kfs.back().time) {
            return safeNormalize(al::Vec3f(kfs.back().x, kfs.back().y, kfs.back().z));
        }

        // Find the keyframe segment containing time t
        const Keyframe* k1 = &kfs[0];
        const Keyframe* k2 = &kfs[1];
        for (size_t i = 0; i < kfs.size() - 1; ++i) {
            if (t >= kfs[i].time && t <= kfs[i + 1].time) {
                k1 = &kfs[i];
                k2 = &kfs[i + 1];
                break;
            }
        }

        // Handle degenerate time segment
        double dt = k2->time - k1->time;
        if (dt <= 1e-9) {
            return safeNormalize(al::Vec3f(k2->x, k2->y, k2->z));
        }

        // Compute interpolation parameter and SLERP
        double u = std::clamp((t - k1->time) / dt, 0.0, 1.0);
        al::Vec3f a = safeNormalize(al::Vec3f(k1->x, k1->y, k1->z));
        al::Vec3f b = safeNormalize(al::Vec3f(k2->x, k2->y, k2->z));
        return slerpDir(a, b, static_cast<float>(u));
    }

    // ── Safe direction with fallback logic ───────────────────────────────
    // Adapted from SpatialRenderer::safeDirForSource()
    al::Vec3f safeDirForSource(const std::string& name,
                                const std::vector<Keyframe>& kfs,
                                const al::Vec3f& rawDir,
                                double t) {
        float m2 = rawDir.magSqr();

        // Valid direction → normalize and store as last-good
        if (finite3(rawDir) && std::isfinite(m2) && m2 >= 1e-8f) {
            al::Vec3f normalized = rawDir.normalized();
            mLastGoodDir[name] = normalized;
            return normalized;
        }

        // Degenerate → try last-good direction
        auto it = mLastGoodDir.find(name);
        if (it != mLastGoodDir.end()) {
            return it->second;
        }

        // No last-good → use nearest keyframe direction
        if (!kfs.empty()) {
            al::Vec3f fallback;
            if (t <= kfs.front().time) {
                fallback = safeNormalize(al::Vec3f(kfs.front().x, kfs.front().y, kfs.front().z));
            } else if (t >= kfs.back().time) {
                fallback = safeNormalize(al::Vec3f(kfs.back().x, kfs.back().y, kfs.back().z));
            } else {
                // Find nearest keyframe by time
                double minDist = std::abs(t - kfs[0].time);
                size_t nearestIdx = 0;
                for (size_t i = 1; i < kfs.size(); ++i) {
                    double dist = std::abs(t - kfs[i].time);
                    if (dist < minDist) {
                        minDist = dist;
                        nearestIdx = i;
                    }
                }
                fallback = safeNormalize(al::Vec3f(kfs[nearestIdx].x,
                                                    kfs[nearestIdx].y,
                                                    kfs[nearestIdx].z));
            }
            mLastGoodDir[name] = fallback;
            return fallback;
        }

        // Absolute last resort: front direction
        return al::Vec3f(0.0f, 1.0f, 0.0f);
    }


    // ═════════════════════════════════════════════════════════════════════
    // LAYOUT TRANSFORMS — Adapted from SpatialRenderer.cpp
    // ═════════════════════════════════════════════════════════════════════

    // ── Remap and clamp a scalar from one range to another ───────────────
    static float remapClamped(float x, float inMin, float inMax,
                               float outMin, float outMax) {
        float denom = inMax - inMin;
        if (std::abs(denom) < 1e-12f) return outMin;
        float t = std::clamp((x - inMin) / denom, 0.0f, 1.0f);
        return outMin + t * (outMax - outMin);
    }

    // ── Sanitize direction for speaker layout's elevation range ──────────
    // Adapted from SpatialRenderer::sanitizeDirForLayout()
    al::Vec3f sanitizeDirForLayout(const al::Vec3f& v, ElevationMode mode) const {
        al::Vec3f d = safeNormalize(v);
        float mag = d.mag();

        if (!std::isfinite(mag) || mag < 1e-6f) {
            return al::Vec3f(0.0f, 1.0f, 0.0f);  // fallback: front
        }

        // 2D layout: flatten to horizontal plane
        if (mLayoutIs2D) {
            d.z = 0.0f;
            return safeNormalize(d);
        }

        // 3D layout: convert to spherical, apply elevation mapping
        float az = std::atan2(d.x, d.y);  // azimuth from +y (forward)
        float el = std::asin(std::clamp(d.z, -1.0f, 1.0f));

        float el2 = el;
        switch (mode) {
            case ElevationMode::Clamp:
                el2 = std::clamp(el, mLayoutMinElRad, mLayoutMaxElRad);
                break;

            case ElevationMode::RescaleAtmosUp:
                // Content in [0, +π/2] → map to layout range
                el2 = remapClamped(el, 0.0f, static_cast<float>(M_PI) / 2.0f,
                                    mLayoutMinElRad, mLayoutMaxElRad);
                break;

            case ElevationMode::RescaleFullSphere:
                // Content in [-π/2, +π/2] → map to layout range
                el2 = remapClamped(el, -static_cast<float>(M_PI) / 2.0f,
                                    static_cast<float>(M_PI) / 2.0f,
                                    mLayoutMinElRad, mLayoutMaxElRad);
                break;
        }

        // Convert back to Cartesian
        float c = std::cos(el2);
        al::Vec3f out(std::sin(az) * c, std::cos(az) * c, std::sin(el2));
        return safeNormalize(out);
    }

    // ── Convert direction to DBAP position ───────────────────────────────
    // Adapted from SpatialRenderer::directionToDBAPPosition()
    //
    // DBAP needs a 3D position, not just a direction. We scale by the
    // layout radius to place the source at the speaker ring distance.
    //
    // COORDINATE TRANSFORM:
    //   AlloLib DBAP internally does: Vec3d relpos = Vec3d(pos.x, -pos.z, pos.y)
    //   Our system: y-forward, x-right, z-up
    //   To compensate: (x, y, z) → (x, z, -y)
    al::Vec3f directionToDBAPPosition(const al::Vec3f& dir) const {
        al::Vec3f pos = dir * mLayoutRadius;
        return al::Vec3f(pos.x, pos.z, -pos.y);
    }


    // ── Fix 2: Read-only position helper ─────────────────────────────────
    // Computes a DBAP position at time t using the full pipeline, but NEVER
    // writes to mLastGoodDir. Used for positionStart / positionEnd so that
    // only the center-time evaluation mutates the last-good-direction cache.
    //
    // Fallback priority (same as safeDirForSource, read-only version):
    //   1. rawDir is valid → normalize → sanitize → convert
    //   2. rawDir is degenerate → read mLastGoodDir (const find, no insert)
    //   3. Not in cache → use safeNormalize(rawDir) or front direction
    //
    // THREADING: audio thread only (reads mLastGoodDir which is audio-thread-owned).
    // This method is const so the compiler enforces no writes to member state.
    al::Vec3f computePositionAtTimeReadOnly(const std::string& name,
                                             const std::vector<Keyframe>& kfs,
                                             double t,
                                             ElevationMode elMode) const {
        al::Vec3f rawDir = interpolateDirRaw(kfs, t);
        al::Vec3f dir;
        float m2 = rawDir.magSqr();
        if (finite3(rawDir) && std::isfinite(m2) && m2 >= 1e-8f) {
            dir = rawDir.normalized();
        } else {
            // Read-only fallback: use whatever mLastGoodDir has (set by center pass)
            auto it = mLastGoodDir.find(name);
            if (it != mLastGoodDir.end()) {
                dir = it->second;
            } else {
                dir = al::Vec3f(0.0f, 1.0f, 0.0f);  // absolute last resort: front
            }
        }
        return directionToDBAPPosition(sanitizeDirForLayout(dir, elMode));
    }

    // ── Member data ──────────────────────────────────────────────────────

    RealtimeConfig& mConfig;
    EngineState&    mState;

    // Scene keyframes per source (set once at load time, read-only during playback)
    std::map<std::string, std::vector<Keyframe>> mSources;

    // Source order and pre-allocated pose output
    std::vector<std::string> mSourceOrder;
    std::vector<SourcePose>  mPoses;

    // Layout parameters (computed once at load time)
    float mLayoutRadius     = 5.0f;  // Median speaker distance
    float mLayoutMinElRad   = 0.0f;  // Minimum speaker elevation (radians)
    float mLayoutMaxElRad   = 0.0f;  // Maximum speaker elevation (radians)
    bool  mLayoutIs2D       = false;  // True if layout is effectively 2D

    // Direction fallback cache (per-source last-good direction)
    std::map<std::string, al::Vec3f> mLastGoodDir;
};
