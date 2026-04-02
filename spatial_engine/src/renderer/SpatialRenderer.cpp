#include "SpatialRenderer.hpp"
#include <cmath>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <algorithm>
#include <numeric>
#include <filesystem>

namespace fs = std::filesystem;

// Local helper: remap and clamp a scalar from [inMin,inMax] to [outMin,outMax].
// Protects against degenerate input ranges and clamps the normalized t to [0,1].
static inline float remapClamped(float x, float inMin, float inMax, float outMin, float outMax) {
    float denom = inMax - inMin;
    if (std::abs(denom) < 1e-12f) return outMin; // avoid div0
    float t = (x - inMin) / denom;
    t = std::clamp(t, 0.0f, 1.0f);
    return outMin + t * (outMax - outMin);
}

// Helper function to get panner type name as string
std::string SpatialRenderer::pannerTypeName(PannerType type) {
    switch (type) {
        case PannerType::DBAP: return "DBAP";
        case PannerType::LBAP: return "LBAP";
        default: return "Unknown";
    }
}

float dbap_sub_compensation = 0.95f; // LFE/subwoofer compensation factor for DBAP. TEMPORARY - UPDATE IN THE FUTURE BASED ON FOCUS SETTING
// ^ update to not be a global variable later 

SpatialRenderer::SpatialRenderer(const SpeakerLayoutData &layout,
                                 const SpatialData &spatial,
                                 const std::map<std::string, MonoWavData> &sources)
    : mLayout(layout), mSpatial(spatial), mSources(sources),
      mSpeakers(), mDBAP(nullptr), mLBAP(nullptr)
{
    // CRITICAL FIX 1: AlloLib's al::Speaker expects angles in DEGREES not radians
    // The AlloSphere layout JSON stores angles in radians but al::Speaker internally
    // converts to radians using toRad() which assumes degree input
    // Without this conversion you get speaker positions at completely wrong angles
    // like -77.7 radians instead of -77.7 degrees which is way outside valid range
    // This caused VBAP to fail silently and produce zero output
    //
    // CRITICAL FIX 2: AlloSphere hardware uses non-consecutive channel numbers 1-60 with gaps
    // but spatializers need consecutive 0-based indices for AudioIOData buffer access
    // We use array index i as the channel and ignore the original deviceChannel numbers
    // The output WAV will have consecutive channels 0-N which can be remapped later
    // if you need the original hardware channel routing
    // Old approach tried to preserve deviceChannel which caused out-of-bounds crashes
    // because AudioIOData only allocates channels 0 to numSpeakers-1
    
    // Collect subwoofer channels from layout
    mSubwooferChannels.clear();
    for (const auto& sub : layout.subwoofers) {
        mSubwooferChannels.push_back(sub.deviceChannel);
    }

    // Collect speaker distances for layout radius computation
    std::vector<float> speakerDistances;
    
    for (size_t i = 0; i < layout.speakers.size(); i++) {
        const auto &spk = layout.speakers[i];
        mSpeakers.emplace_back(al::Speaker(
            i,                                    // consecutive 0-based channel index
            spk.azimuth * 180.0f / M_PI,          // radians to degrees
            spk.elevation * 180.0f / M_PI,        // radians to degrees
            0,                                    // group id
            spk.radius                            // distance from center
        ));
        speakerDistances.push_back(spk.radius);
    }
    
    // Compute layout radius as median of speaker distances
    // This is used to convert directions to positions for DBAP
    if (!speakerDistances.empty()) {
        std::sort(speakerDistances.begin(), speakerDistances.end());
        size_t mid = speakerDistances.size() / 2;
        if (speakerDistances.size() % 2 == 0) {
            mLayoutRadius = (speakerDistances[mid - 1] + speakerDistances[mid]) / 2.0f;
        } else {
            mLayoutRadius = speakerDistances[mid];
        }
    }
    
    // Compute layout elevation bounds from speaker positions (in radians)
    // This determines the range of elevations the layout can reproduce
    mLayoutMinElRad =  1e9f;
    mLayoutMaxElRad = -1e9f;
    for (const auto& spk : layout.speakers) {
        mLayoutMinElRad = std::min(mLayoutMinElRad, spk.elevation);
        mLayoutMaxElRad = std::max(mLayoutMaxElRad, spk.elevation);
    }
    mLayoutElSpanRad = mLayoutMaxElRad - mLayoutMinElRad;
    
    // Detect "effectively 2D" layouts (elevation span < 3 degrees)
    const float twoDThreshRad = 3.0f * float(M_PI) / 180.0f;
    mLayoutIs2D = (mLayoutElSpanRad < twoDThreshRad);
    
    std::cout << "Layout: " << layout.subwoofers.size() << " subwoofers, " << layout.speakers.size() << " speakers, radius: " 
              << std::fixed << std::setprecision(2) << mLayoutRadius << "m\n";
    std::cout << "Layout elevation range: [" 
              << (mLayoutMinElRad * 180.0f / M_PI) << "°, " 
              << (mLayoutMaxElRad * 180.0f / M_PI) << "°] "
              << "(span: " << (mLayoutElSpanRad * 180.0f / M_PI) << "°)\n";
    if (mLayoutIs2D) {
        std::cout << "Layout detected as 2D (elevation span < 3°) - will flatten directions\n";
    }
    
    // Precompute speaker unit directions for nearest-speaker fallback
    // Used when a spatializer produces zero output for a direction (coverage gaps)
    mSpeakerDirs.resize(layout.speakers.size());
    for (size_t i = 0; i < layout.speakers.size(); i++) {
        const auto& spk = layout.speakers[i];
        float cosEl = std::cos(spk.elevation);
        mSpeakerDirs[i] = al::Vec3f(
            std::sin(spk.azimuth) * cosEl,
            std::cos(spk.azimuth) * cosEl,
            std::sin(spk.elevation)
        ).normalize();
    }

    
    
    // Create spatializers using unique_ptr (LBAP has broken copy semantics in AlloLib)
    
    // VBAP removed from offline engine - requires 3D speaker arrangements for triangulation
    // Use DBAP (default) or LBAP for 2D layouts
    std::cout << "VBAP removed from offline engine (requires 3D layouts)\n";
    
    // DBAP - distance-based amplitude panning
    // Note: DBAP doesn't need compile() - it uses distance-based calculations
    mDBAP = std::make_unique<al::Dbap>(mSpeakers);
    std::cout << "DBAP initialized with " << mSpeakers.size() << " speakers\n";
    
    // LBAP - layer-based panning for multi-ring layouts
    mLBAP = std::make_unique<al::Lbap>(mSpeakers);
    mLBAP->compile();
    std::cout << "LBAP initialized with " << mSpeakers.size() << " speakers\n";
}

// Reset per-render state (call at start of each render)
void SpatialRenderer::resetPerRenderState() {
    mLastGoodDir.clear();
    mWarnedDegenerate.clear();
    mFallbackCount.clear();
    
    // Reset direction sanitization diagnostics
    mDirDiag = DirDiag();
    
    // Reset panner diagnostics
    mPannerDiag = PannerDiag();
}

// Initialize the selected spatializer for this render
void SpatialRenderer::initializeSpatializer(const RenderConfig& config) {
    mActivePannerType = config.pannerType;
    
    switch (config.pannerType) {
        case PannerType::DBAP:
            mActiveSpatializer = mDBAP.get();
            mDBAP->setFocus(config.dbapFocus);
            break;
        case PannerType::LBAP:
            mActiveSpatializer = mLBAP.get();
            mLBAP->setDispersionThreshold(config.lbapDispersion);
            break;
        default:
            std::cerr << "Unknown panner type, defaulting to DBAP\n";
            mActiveSpatializer = mDBAP.get();
            mActivePannerType = PannerType::DBAP;
            break;
    }
}

// Print spatializer info at start of render
void SpatialRenderer::printSpatializerInfo(const RenderConfig& config) {
    std::cout << "Spatializer: " << pannerTypeName(config.pannerType);
    
    switch (config.pannerType) {
        case PannerType::DBAP:
            std::cout << " (focus=" << config.dbapFocus << ")";
            // Print coordinate transform warning once per render
            std::cout << "\n  NOTE: DBAP uses coordinate transform (x,y,z)->(x,z,-y) for AlloLib compatibility";
            break;
        case PannerType::LBAP:
            std::cout << " (dispersion=" << config.lbapDispersion << ")";
            break;
    }
    std::cout << "\n";
}

// Safe normalize: returns fallback if input is degenerate
al::Vec3f SpatialRenderer::safeNormalize(const al::Vec3f& v) {
    float mag = v.mag();
    if (mag < 1e-6f || !std::isfinite(mag)) {
        return al::Vec3f(0.0f, 1.0f, 0.0f);  // fallback: front
    }
    return v / mag;
}

// Convert direction to DBAP position
// DBAP needs a position in 3D space, not just a direction
// We use direction * layoutRadius to place the source at the speaker ring distance
//
// COORDINATE TRANSFORM NOTE:
// AlloLib's DBAP internally does: Vec3d relpos = Vec3d(pos.x, -pos.z, pos.y)
// Our system uses: y-forward, x-right, z-up
// To compensate, we transform: (x, y, z) -> (x, z, -y)
// This makes our y-forward map to DBAP's expected z-forward
//
// DEV NOTE (2026-01-27): This is marked as "FIXME test DBAP" in AlloLib source.
// If AlloLib changes this in future versions, we'll need to update or remove
// this transform. Consider adding a version check or config option.
//
// FUTURE POSSIBILITY: Could add per-source distance from keyframes to use
// instead of fixed layoutRadius. Would require parsing distance from JSON.
al::Vec3f SpatialRenderer::directionToDBAPPosition(const al::Vec3f& dir) {
    // Scale direction by layout radius to get position
    al::Vec3f pos = dir * mLayoutRadius;
    
    // Apply coordinate transform for AlloLib DBAP
    // Our: (x=right, y=forward, z=up)
    // DBAP internal swap: (x, -z, y) means it expects (x=right, z=backward, y=up)
    // So we send: (our_x, our_z, -our_y) which after DBAP's swap becomes:
    // (our_x, our_y, our_z) - back to our original!
    return al::Vec3f(pos.x, pos.z, -pos.y);
}

// Sanitize direction to fit within speaker layout's representable range
// This prevents sources from becoming inaudible due to out-of-range directions

 //updating to compensate for low speakers 
al::Vec3f SpatialRenderer::sanitizeDirForLayout(const al::Vec3f& v, ElevationMode mode) {
    al::Vec3f d = safeNormalize(v);
    float mag = d.mag();
    
    // Handle degenerate input
    if (!std::isfinite(mag) || mag < 1e-6f) {
        mDirDiag.invalidDir++;
        return al::Vec3f(0.0f, 1.0f, 0.0f);  // fallback: front
    }
    
    // 2D layout: flatten elevation to z=0
    if (mLayoutIs2D) {
        if (std::abs(d.z) > 1e-6f) {
            mDirDiag.flattened2D++;
        }
        d.z = 0.0f;
        return safeNormalize(d);
    }
    
    // 3D layout: convert to spherical coordinates (y-forward convention)
    // az: azimuth angle in horizontal plane, measured from +y axis
    // el: elevation angle from horizontal plane
    float az = std::atan2(d.x, d.y);
    float el = std::asin(std::clamp(d.z, -1.0f, 1.0f));
    
    float el2 = el;

    // Apply elevation mapping according to the selected mode. All angles are
    // in radians. We intentionally keep the mapping logic localized here so
    // changes remain minimal and easy to audit.
    switch (mode) {
        case ElevationMode::Clamp: {
            // Hard clip elevation to layout bounds
            float clamped = std::clamp(el, mLayoutMinElRad, mLayoutMaxElRad);
            if (clamped != el) {
                mDirDiag.clampedEl++;
            }
            el2 = clamped;
            break;
        }
        case ElevationMode::RescaleAtmosUp: {
            // Source assumed in [0, +pi/2] (ear -> top). Map that range into
            // the layout's [mLayoutMinElRad, mLayoutMaxElRad]. Inputs below 0
            // or above +pi/2 are clamped by remapClamped.
            float mapped = remapClamped(el, 0.0f, float(M_PI)/2.0f, mLayoutMinElRad, mLayoutMaxElRad);
            if (std::abs(mapped - el) > 1e-5f) {
                mDirDiag.rescaledAtmosUp++;
            }
            el2 = mapped;
            break;
        }
        case ElevationMode::RescaleFullSphere: {
            // Source assumed in [-pi/2, +pi/2]. Map full sphere into layout range.
            float mapped = remapClamped(el, -float(M_PI)/2.0f, float(M_PI)/2.0f, mLayoutMinElRad, mLayoutMaxElRad);
            if (std::abs(mapped - el) > 1e-5f) {
                mDirDiag.rescaledFullSphere++;
            }
            el2 = mapped;
            break;
        }
        default: {
            // Safe fallback: clamp
            float clamped = std::clamp(el, mLayoutMinElRad, mLayoutMaxElRad);
            if (clamped != el) mDirDiag.clampedEl++;
            el2 = clamped;
            break;
        }
    }
    
    // Convert back to Cartesian coordinates
    float c = std::cos(el2);
    al::Vec3f out(std::sin(az) * c, std::cos(az) * c, std::sin(el2));
    return safeNormalize(out);
}

// Print direction sanitization summary at end of render
void SpatialRenderer::printSanitizationSummary() {
    std::cout << "\nDirection Sanitization Summary:\n";
    std::cout << "  Layout type: " << (mLayoutIs2D ? "2D" : "3D") << "\n";
    std::cout << "  Elevation range: [" 
              << std::fixed << std::setprecision(1) 
              << (mLayoutMinElRad * 180.0f / M_PI) << "°, " 
              << (mLayoutMaxElRad * 180.0f / M_PI) << "°]\n";
    
    if (mLayoutIs2D) {
        std::cout << "  Flattened to plane: " << mDirDiag.flattened2D << " directions\n";
    } else {
        std::cout << "  Clamped elevations: " << mDirDiag.clampedEl << "\n";
        std::cout << "  Rescaled (AtmosUp): " << mDirDiag.rescaledAtmosUp << "\n";
        std::cout << "  Rescaled (FullSphere): " << mDirDiag.rescaledFullSphere << "\n";
    }
    std::cout << "  Invalid/fallback directions: " << mDirDiag.invalidDir << "\n";
}

// Find nearest speaker direction for fallback
// Returns direction 90% toward the nearest speaker to stay inside the hull
al::Vec3f SpatialRenderer::nearestSpeakerDir(const al::Vec3f& dir) {
    if (mSpeakerDirs.empty()) {
        return al::Vec3f(0.0f, 1.0f, 0.0f);  // fallback: front
    }
    
    float maxDot = -2.0f;
    size_t bestIdx = 0;
    
    for (size_t i = 0; i < mSpeakerDirs.size(); i++) {
        float d = dir.dot(mSpeakerDirs[i]);
        if (d > maxDot) {
            maxDot = d;
            bestIdx = i;
        }
    }
    
    // Return direction 90% toward the speaker to stay inside hull
    // This ensures VBAP can still find a valid triplet
    al::Vec3f spkDir = mSpeakerDirs[bestIdx];
    al::Vec3f blended = dir * 0.1f + spkDir * 0.9f;
    return safeNormalize(blended);
}

// Print panner diagnostics summary (zero blocks, retargets, substeps)
void SpatialRenderer::printPannerDiagSummary() {
    std::cout << "\n" << pannerTypeName(mActivePannerType) << " Robustness Summary:\n";
    
    if (mPannerDiag.totalZeroBlocks == 0 && mPannerDiag.totalRetargets == 0 && mPannerDiag.totalSubsteps == 0) {
        std::cout << "  All blocks rendered normally (no panner failures or fast motion detected)\n";
        return;
    }
    
    std::cout << "  Total zero-output blocks detected: " << mPannerDiag.totalZeroBlocks << "\n";
    std::cout << "  Total retargets to nearest speaker: " << mPannerDiag.totalRetargets << "\n";
    std::cout << "  Total sub-stepped blocks (fast motion): " << mPannerDiag.totalSubsteps << "\n";
    
    // Show top offenders for zero blocks
    if (!mPannerDiag.zeroBlocks.empty()) {
        std::vector<std::pair<std::string, uint64_t>> sorted(
            mPannerDiag.zeroBlocks.begin(), mPannerDiag.zeroBlocks.end());
        std::sort(sorted.begin(), sorted.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });
        
        std::cout << "  Zero-block sources (top 5):\n";
        int shown = 0;
        for (const auto& [name, count] : sorted) {
            if (shown++ >= 5) break;
            std::cout << "    " << name << ": " << count << " blocks\n";
        }
    }
    
    // Show top offenders for substeps
    if (!mPannerDiag.substeppedBlocks.empty()) {
        std::vector<std::pair<std::string, uint64_t>> sorted(
            mPannerDiag.substeppedBlocks.begin(), mPannerDiag.substeppedBlocks.end());
        std::sort(sorted.begin(), sorted.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });
        
        std::cout << "  Fast-mover sources (top 5):\n";
        int shown = 0;
        for (const auto& [name, count] : sorted) {
            if (shown++ >= 5) break;
            std::cout << "    " << name << ": " << count << " sub-stepped blocks\n";
        }
    }
}

// Spherical linear interpolation between two unit vectors
// Returns smooth interpolation on the unit sphere from a (t=0) to b (t=1)
al::Vec3f SpatialRenderer::slerpDir(const al::Vec3f& a, const al::Vec3f& b, float t) {
    // Clamp t to [0,1]
    t = std::max(0.0f, std::min(1.0f, t));
    
    // Compute cosine of angle between directions
    float dot = a.dot(b);
    
    // Clamp to handle numerical errors
    dot = std::max(-1.0f, std::min(1.0f, dot));
    
    // If vectors are very close, use linear interpolation to avoid division by zero
    if (dot > 0.9995f) {
        al::Vec3f result = a + t * (b - a);
        return safeNormalize(result);
    }
    
    // If vectors are nearly opposite, pick a perpendicular axis
    if (dot < -0.9995f) {
        // Find a perpendicular vector
        al::Vec3f perp = (std::abs(a.x) < 0.9f) ? al::Vec3f(1,0,0) : al::Vec3f(0,1,0);
        perp = (a.cross(perp)).normalize();
        // Rotate around perpendicular axis
        float theta = M_PI * t;
        return a * std::cos(theta) + perp * std::sin(theta);
    }
    
    // Standard SLERP formula
    float theta = std::acos(dot);
    float sinTheta = std::sin(theta);
    
    float wa = std::sin((1.0f - t) * theta) / sinTheta;
    float wb = std::sin(t * theta) / sinTheta;
    
    return a * wa + b * wb;
}

// Main safe direction getter - wraps interpolation with fallback logic
al::Vec3f SpatialRenderer::safeDirForSource(const std::string& name, 
                                             const std::vector<Keyframe>& kfs, 
                                             double t) {
    // Get raw interpolated direction
    al::Vec3f v = interpolateDirRaw(kfs, t);
    float m2 = v.magSqr();
    
    // Check for degenerate direction
    if (!finite3(v) || !std::isfinite(m2) || m2 < 1e-8f) {
        // Increment fallback counter
        mFallbackCount[name]++;
        
        // Warn once per source with detailed reason
        if (mWarnedDegenerate.find(name) == mWarnedDegenerate.end()) {
            std::cerr << "Warning: degenerate direction for source '" << name 
                      << "' at t=" << t << "s";
            
            // Log the specific reason
            if (!finite3(v)) {
                std::cerr << " (reason: NaN/Inf in direction)";
            } else if (m2 < 1e-8f) {
                std::cerr << " (reason: near-zero magnitude " << std::sqrt(m2) << ")";
            }
            
            if (!kfs.empty()) {
                std::cerr << " [keyframes: " << kfs.size() 
                          << ", range: " << kfs.front().time << "s to " << kfs.back().time << "s]";
            }
            std::cerr << "\n";
            mWarnedDegenerate.insert(name);
        }
        
        // Try last-good direction for this source
        auto it = mLastGoodDir.find(name);
        if (it != mLastGoodDir.end()) {
            return it->second;
        }
        
        // No last-good exists: use nearest keyframe direction instead of global front
        // This provides a more sensible fallback that's related to the source's actual data
        if (!kfs.empty()) {
            al::Vec3f fallbackDir;
            if (t <= kfs.front().time) {
                // Before first keyframe: use first keyframe
                fallbackDir = safeNormalize(al::Vec3f(kfs.front().x, kfs.front().y, kfs.front().z));
            } else if (t >= kfs.back().time) {
                // After last keyframe: use last keyframe
                fallbackDir = safeNormalize(al::Vec3f(kfs.back().x, kfs.back().y, kfs.back().z));
            } else {
                // Within keyframe range: find nearest keyframe by time
                double minDist = std::abs(t - kfs[0].time);
                size_t nearestIdx = 0;
                for (size_t i = 1; i < kfs.size(); i++) {
                    double dist = std::abs(t - kfs[i].time);
                    if (dist < minDist) {
                        minDist = dist;
                        nearestIdx = i;
                    }
                }
                fallbackDir = safeNormalize(al::Vec3f(kfs[nearestIdx].x, kfs[nearestIdx].y, kfs[nearestIdx].z));
            }
            
            // Store as last-good so future fallbacks use this
            mLastGoodDir[name] = fallbackDir;
            return fallbackDir;
        }
        
        // Absolute last resort: global front direction (only if no keyframes at all)
        return al::Vec3f(0.0f, 1.0f, 0.0f);
    }
    
    // Valid direction - normalize and store as last-good
    al::Vec3f normalized = v.normalize();
    mLastGoodDir[name] = normalized;
    return normalized;
}

// Raw interpolation - may return invalid vectors (caller must validate)
al::Vec3f SpatialRenderer::interpolateDirRaw(const std::vector<Keyframe> &kfs, double t) {
    // Returns interpolated direction from keyframes using SLERP.
    // This avoids the near-zero vector problem that linear Cartesian interpolation has
    // when keyframes are far apart on the sphere (chord through origin).
    // Does NOT validate output - use safeDirForSource() for safe access.
    
    // Empty keyframes - return zero vector (will trigger fallback)
    if (kfs.empty()) {
        return al::Vec3f(0.0f, 0.0f, 0.0f);
    }
    
    // Single keyframe - return normalized direction
    if (kfs.size() == 1) {
        return safeNormalize(al::Vec3f(kfs[0].x, kfs[0].y, kfs[0].z));
    }
    
    // Clamp to first keyframe if before all keyframes
    if (t <= kfs.front().time) {
        return safeNormalize(al::Vec3f(kfs.front().x, kfs.front().y, kfs.front().z));
    }
    
    // Clamp to last keyframe if after all keyframes
    if (t >= kfs.back().time) {
        return safeNormalize(al::Vec3f(kfs.back().x, kfs.back().y, kfs.back().z));
    }
    
    // Find the keyframe segment containing time t
    const Keyframe *k1 = &kfs[0];
    const Keyframe *k2 = &kfs[1];
    for (size_t i = 0; i < kfs.size() - 1; i++) {
        if (t >= kfs[i].time && t <= kfs[i+1].time) {
            k1 = &kfs[i];
            k2 = &kfs[i+1];
            break;
        }
    }
    
    // Handle degenerate time segments (dt <= 0)
    double dt = k2->time - k1->time;
    if (dt <= 1e-9) {
        return safeNormalize(al::Vec3f(k2->x, k2->y, k2->z));
    }
    
    // Compute interpolation parameter
    double u = (t - k1->time) / dt;
    u = std::max(0.0, std::min(1.0, u));  // clamp u to [0,1] for safety
    
    // SLERP interpolation: normalize endpoints first, then interpolate on sphere
    // This prevents near-zero vectors when keyframes are far apart
    al::Vec3f a = safeNormalize(al::Vec3f(k1->x, k1->y, k1->z));
    al::Vec3f b = safeNormalize(al::Vec3f(k2->x, k2->y, k2->z));
    
    return slerpDir(a, b, (float)u);
}

// Print end-of-render fallback summary
void SpatialRenderer::printFallbackSummary(int totalBlocks) {
    if (mFallbackCount.empty()) {
        std::cout << "  Direction fallbacks: none (all sources had valid directions)\n";
        return;
    }
    
    // Sort sources by fallback count (descending)
    std::vector<std::pair<std::string, int>> sorted(mFallbackCount.begin(), mFallbackCount.end());
    std::sort(sorted.begin(), sorted.end(), 
              [](const auto& a, const auto& b) { return a.second > b.second; });
    
    std::cout << "  Direction fallbacks by source:\n";
    int totalFallbacks = 0;
    for (const auto& [name, count] : sorted) {
        float pct = 100.0f * count / totalBlocks;
        std::cout << "    " << name << ": " << count << " blocks (" 
                  << std::fixed << std::setprecision(1) << pct << "%)\n";
        totalFallbacks += count;
        
        // Copy to stats
        mLastStats.sourceFallbackCount[name] = count;
    }
    mLastStats.totalFallbackBlocks = totalFallbacks;
}

// Legacy heuristic time unit detection - now just a safety net
// Primary time unit handling is done in JSONLoader via explicit "timeUnit" field
void SpatialRenderer::normalizeKeyframeTimes(double durationSec, size_t totalSamples, int sr) {
    for (auto &[name, kfs] : mSpatial.sources) {
        if (kfs.empty()) continue;
        
        double maxTime = 0.0;
        for (const auto &kf : kfs) {
            maxTime = std::max(maxTime, kf.time);
        }
        
        // Safety net: detect obvious mismatches that JSONLoader might have missed
        // This should rarely trigger if JSON has correct "timeUnit" field
        if (maxTime > durationSec * 10.0 && maxTime <= (double)totalSamples * 1.1) {
            std::cerr << "  WARNING: Source '" << name << "' times look like samples, not seconds!\n";
            std::cerr << "    maxTime=" << maxTime << " vs durationSec=" << durationSec << "\n";
            std::cerr << "    Add \"timeUnit\": \"samples\" to your JSON to fix this properly.\n";
            std::cerr << "    Auto-converting for now...\n";
            for (auto &kf : kfs) {
                kf.time /= (double)sr;
            }
        }
    }
}

// Compute statistics on rendered output
void SpatialRenderer::computeRenderStats(const MultiWavData &output) {
    mLastStats = RenderStats();
    mLastStats.numChannels = output.channels;
    mLastStats.totalSamples = output.samples.empty() ? 0 : output.samples[0].size();
    mLastStats.durationSec = (double)mLastStats.totalSamples / output.sampleRate;
    mLastStats.numSources = mSpatial.sources.size();
    
    mLastStats.channelRMS.resize(output.channels, 0.0f);
    mLastStats.channelPeak.resize(output.channels, 0.0f);
    mLastStats.channelNaNCount.resize(output.channels, 0);
    mLastStats.channelInfCount.resize(output.channels, 0);
    
    for (int ch = 0; ch < output.channels; ch++) {
        const auto &samples = output.samples[ch];
        double sumSq = 0.0;
        float peak = 0.0f;
        int nanCount = 0, infCount = 0;
        
        for (float s : samples) {
            if (std::isnan(s)) {
                nanCount++;
            } else if (std::isinf(s)) {
                infCount++;
            } else {
                sumSq += (double)s * s;
                peak = std::max(peak, std::abs(s));
            }
        }
        
        double rms = std::sqrt(sumSq / samples.size());
        // Convert to dBFS (0 dB = full scale = 1.0)
        float rmsDB = (rms > 1e-10) ? 20.0f * std::log10((float)rms) : -120.0f;
        
        mLastStats.channelRMS[ch] = rmsDB;
        mLastStats.channelPeak[ch] = peak;
        mLastStats.channelNaNCount[ch] = nanCount;
        mLastStats.channelInfCount[ch] = infCount;
    }
}

// Default render (uses default config)
MultiWavData SpatialRenderer::render() {
    RenderConfig defaultConfig;
    return render(defaultConfig);
}

// Main render function with configuration options
MultiWavData SpatialRenderer::render(const RenderConfig &config) {
    int sr = mSpatial.sampleRate;
    int numSpeakers = mLayout.speakers.size();
    
    // Initialize the selected spatializer
    initializeSpatializer(config);
    
    // Apply force2D mode if requested via config
    bool originalIs2D = mLayoutIs2D;
    if (config.force2D && !mLayoutIs2D) {
        mLayoutIs2D = true;
        std::cout << "FORCE_2D: Treating layout as 2D (all elevations will be flattened)\n";
    }

    size_t totalSamples = 0;
    for (auto &[name, wav] : mSources) {
        totalSamples = std::max(totalSamples, wav.samples.size());
    }
    
    // Use LUSID scene duration if available, otherwise calculate from WAV files
    double durationSec;
    if (mSpatial.duration > 0.0) {
        durationSec = mSpatial.duration;
        // Update totalSamples to match LUSID duration
        totalSamples = (size_t)(durationSec * sr);
        std::cout << "Using LUSID scene duration: " << std::fixed << std::setprecision(2)
                  << durationSec << " seconds (" << totalSamples << " samples)\n";
    } else {
        durationSec = (double)totalSamples / sr;
        std::cout << "Using WAV file duration: " << std::fixed << std::setprecision(2)
                  << durationSec << " seconds (" << totalSamples << " samples)\n";
    }
    
    // Reset per-render tracking state
    resetPerRenderState();
    
    // Detect and fix keyframe time units (samples vs seconds)
    normalizeKeyframeTimes(durationSec, totalSamples, sr);

    // Determine render range based on config
    size_t startSample = 0;
    size_t endSample = totalSamples;
    
    if (config.t0 >= 0.0) {
        startSample = (size_t)(config.t0 * sr);
        startSample = std::min(startSample, totalSamples);
    }
    if (config.t1 >= 0.0) {
        endSample = (size_t)(config.t1 * sr);
        endSample = std::min(endSample, totalSamples);
    }
    
    size_t renderSamples = (endSample > startSample) ? (endSample - startSample) : 0;

    std::cout << "Rendering " << renderSamples << " samples (" 
              << (double)renderSamples / sr << " sec) to " 
              << numSpeakers << " speakers from " << mSources.size() << " sources\n";
    
    // Print spatializer info
    printSpatializerInfo(config);
    
    std::cout << "  Master gain: " << config.masterGain << "\n";
    std::cout << "  Render resolution: " << config.renderResolution << " (block size: " << config.blockSize << ")\n";
    // Print a human-readable elevation mode string
    std::string emodeStr;
    switch (config.elevationMode) {
        case ElevationMode::Clamp: emodeStr = "clamp"; break;
        case ElevationMode::RescaleAtmosUp: emodeStr = "rescale_atmos_up"; break;
        case ElevationMode::RescaleFullSphere: emodeStr = "rescale_full_sphere"; break;
        default: emodeStr = "unknown"; break;
    }
    std::cout << "  Elevation mode: " << emodeStr << "\n";
    
    if (!config.soloSource.empty()) {
        std::cout << "  SOLO MODE: Only rendering source '" << config.soloSource << "'\n";
    }
    if (config.t0 >= 0.0 || config.t1 >= 0.0) {
        std::cout << "  TIME WINDOW: " << (config.t0 >= 0.0 ? config.t0 : 0.0) 
                  << "s to " << (config.t1 >= 0.0 ? config.t1 : durationSec) << "s\n";
    }
    
    // Per-source diagnostics: check for silent sources and missing spatial data
    std::cout << "\n  Source diagnostics:\n";
    int silentSources = 0;
    int missingSpatial = 0;
    int missingAudio = 0;
    
    for (const auto& [name, kfs] : mSpatial.sources) {
        auto srcIt = mSources.find(name);
        if (srcIt == mSources.end()) {
            std::cerr << "    WARNING: Source '" << name << "' has spatial data but no audio file!\n";
            missingAudio++;
            continue;
        }
        
        // Compute input RMS for this source
        const MonoWavData& src = srcIt->second;
        double sumSq = 0.0;
        size_t count = 0;
        for (size_t i = startSample; i < endSample && i < src.samples.size(); i++) {
            sumSq += (double)src.samples[i] * src.samples[i];
            count++;
        }
        double rms = (count > 0) ? std::sqrt(sumSq / count) : 0.0;
        float rmsDB = (rms > 1e-10) ? 20.0f * std::log10((float)rms) : -120.0f;
        
        if (rmsDB < -60.0f) {
            std::cerr << "    WARNING: Source '" << name << "' is near-silent (RMS: " 
                      << std::fixed << std::setprecision(1) << rmsDB << " dBFS)\n";
            silentSources++;
        }
        
        if (kfs.empty()) {
            std::cerr << "    WARNING: Source '" << name << "' has no keyframes!\n";
            missingSpatial++;
        }
    }
    
    // Check for audio files without spatial data
    for (const auto& [name, wav] : mSources) {
        if (mSpatial.sources.find(name) == mSpatial.sources.end()) {
            std::cerr << "    WARNING: Audio file '" << name << "' has no spatial data (won't be rendered)!\n";
        }
    }
    
    if (silentSources == 0 && missingSpatial == 0 && missingAudio == 0) {
        std::cout << "    All sources OK\n";
    }
    std::cout << "\n";
    
    // output uses consecutive channels 0 to numSpeakers-1
    //return here if there are indexing issues with channel output 
    MultiWavData out;
    out.sampleRate = sr;
    int maxChannel = numSpeakers - 1;
    for (int subCh : mSubwooferChannels) {
        if (subCh > maxChannel) maxChannel = subCh;
    }
    //logic to accommodate subwoofer channels that may be beyond speaker count / placed out of order 
    out.channels = maxChannel + 1;
    out.samples.resize(out.channels);
    for (auto &c : out.samples) c.resize(renderSamples, 0.0f);

    // Dispatch to appropriate render resolution
    if (config.renderResolution == "block") {
        renderPerBlock(out, config, startSample, endSample);
    } else if (config.renderResolution == "sample") {
        std::cerr << "  ERROR: 'sample' mode is DISABLED (VBAP removed). Use 'block' mode instead.\n";
        std::cerr << "         For per-sample accuracy, use 'block' mode with --block_size 1.\n";
        return {};  // Return empty result
    } else if (config.renderResolution == "smooth") {
        std::cerr << "  ERROR: 'smooth' mode is DISABLED (VBAP removed). Use 'block' mode instead.\n";
        std::cerr << "         For smooth interpolation, use 'block' mode with small --block_size.\n";
        return {};  // Return empty result
    } else {
        std::cerr << "  ERROR: Unknown render resolution '" << config.renderResolution << "', using 'block'\n";
        renderPerBlock(out, config, startSample, endSample);
    }
    
    // Calculate total blocks for fallback summary
    int totalBlocks = (renderSamples + config.blockSize - 1) / config.blockSize;
    
    // Compute and store render statistics
    computeRenderStats(out);
    
    // Log summary statistics
    std::cout << "\nRender Statistics:\n";
    int silentChannels = 0;
    int clippingChannels = 0;
    int nanChannels = 0;
    float overallPeak = 0.0f;
    
    for (int ch = 0; ch < numSpeakers; ch++) {
        if (mLastStats.channelRMS[ch] < -85.0f) silentChannels++;
        if (mLastStats.channelPeak[ch] > 1.0f) clippingChannels++;
        if (mLastStats.channelNaNCount[ch] > 0) nanChannels++;
        overallPeak = std::max(overallPeak, mLastStats.channelPeak[ch]);
    }
    
    std::cout << "  Overall peak: " << overallPeak << " (" 
              << 20.0f * std::log10(std::max(overallPeak, 1e-10f)) << " dBFS)\n";
    std::cout << "  Near-silent channels (< -85 dBFS): " << silentChannels << "/" << numSpeakers << "\n";
    std::cout << "  Clipping channels (peak > 1.0): " << clippingChannels << "\n";
    std::cout << "  Channels with NaN: " << nanChannels << "\n";
    
    // Print fallback summary (shows which sources had degenerate directions)
    printFallbackSummary(totalBlocks);
    
    // Print direction sanitization summary
    printSanitizationSummary();
    
    // Print panner robustness summary (zero blocks, retargets, substeps)
    printPannerDiagSummary();
    
    // Write statistics to JSON if diagnostics enabled
    if (config.debugDiagnostics) {
        fs::create_directories(config.debugOutputDir);
        std::ofstream statsFile(config.debugOutputDir + "/render_stats.json");
        statsFile << "{\n";
        statsFile << "  \"spatializer\": \"" << pannerTypeName(config.pannerType) << "\",\n";
        statsFile << "  \"totalSamples\": " << mLastStats.totalSamples << ",\n";
        statsFile << "  \"durationSec\": " << mLastStats.durationSec << ",\n";
        statsFile << "  \"numChannels\": " << mLastStats.numChannels << ",\n";
        statsFile << "  \"numSources\": " << mLastStats.numSources << ",\n";
        statsFile << "  \"renderResolution\": \"" << config.renderResolution << "\",\n";
        statsFile << "  \"blockSize\": " << config.blockSize << ",\n";
        statsFile << "  \"overallPeak\": " << overallPeak << ",\n";
        statsFile << "  \"silentChannels\": " << silentChannels << ",\n";
        statsFile << "  \"clippingChannels\": " << clippingChannels << ",\n";
        statsFile << "  \"nanChannels\": " << nanChannels << ",\n";
        statsFile << "  \"masterGain\": " << config.masterGain << ",\n";
        if (config.pannerType == PannerType::DBAP) {
            statsFile << "  \"dbapFocus\": " << config.dbapFocus << ",\n";
        }
        if (config.pannerType == PannerType::LBAP) {
            statsFile << "  \"lbapDispersion\": " << config.lbapDispersion << ",\n";
        }
        statsFile << "  \"channelRMS\": [";
        for (int i = 0; i < numSpeakers; i++) {
            statsFile << mLastStats.channelRMS[i];
            if (i < numSpeakers - 1) statsFile << ", ";
        }
        statsFile << "],\n";
        statsFile << "  \"channelPeak\": [";
        for (int i = 0; i < numSpeakers; i++) {
            statsFile << mLastStats.channelPeak[i];
            if (i < numSpeakers - 1) statsFile << ", ";
        }
        statsFile << "]\n";
        statsFile << "}\n";
        statsFile.close();
        std::cout << "  Debug stats written to " << config.debugOutputDir << "/\n";
    }
    
    // Restore original layout 2D setting if it was overridden
    mLayoutIs2D = originalIs2D;
    
    std::cout << "\n";
    return out;
}

// renderPerBlock: Direction computed at block center (reduces stepping artifacts)
// 
// ROBUSTNESS FEATURES (added 2026-01-27):
// 1. Input energy detection - skip expensive checks for silent blocks
// 2. Zero-block detection - detect when panner produces ~silence despite input
// 3. Nearest-speaker fallback - retarget to nearest speaker when panner fails
// 4. Fast-mover sub-stepping - subdivide blocks when direction changes too fast
//
// DEV NOTE: Zero-block detection runs for all panners for consistency.
// Consider optimizing for DBAP/LBAP in future - they shouldn't produce zero blocks.
//
void SpatialRenderer::renderPerBlock(MultiWavData &out, const RenderConfig &config,
                                      size_t startSample, size_t endSample) {
    int sr = mSpatial.sampleRate;
    int numSpeakers = mLayout.speakers.size();
    int bufferSize = config.blockSize;
    size_t renderSamples = endSample - startSample;
    
    // Initialize AudioIOData for panner (main accumulator)
    al::AudioIOData audioIO;
    audioIO.framesPerBuffer(bufferSize);
    audioIO.framesPerSecond(sr);
    audioIO.channelsIn(0);
    audioIO.channelsOut(numSpeakers);
    
    // Temp buffer for panner failure detection and retargeting
    // DEV NOTE: Could use lazy allocation for DBAP/LBAP since they shouldn't need fallback
    al::AudioIOData audioTemp;
    audioTemp.framesPerBuffer(bufferSize);
    audioTemp.framesPerSecond(sr);
    audioTemp.channelsIn(0);
    audioTemp.channelsOut(numSpeakers);
    
    std::vector<float> sourceBuffer(bufferSize, 0.0f);
    
    int blocksProcessed = 0;
    for (size_t blockStart = startSample; blockStart < endSample; blockStart += bufferSize) {
        size_t blockEnd = std::min(endSample, blockStart + bufferSize);
        size_t blockLen = blockEnd - blockStart;
        size_t outBlockStart = blockStart - startSample;
        
        if (blocksProcessed % 1000 == 0) {
            std::cout << "  Block " << blocksProcessed << " (" 
                      << (int)(100.0 * (blockStart - startSample) / renderSamples) << "%)\n" << std::flush;
        }
        blocksProcessed++;
        
        audioIO.zeroOut();
        
        for (auto &[name, kfs] : mSpatial.sources) {
            if (!config.soloSource.empty() && name != config.soloSource) continue;
            
            auto srcIt = mSources.find(name);
            if (srcIt == mSources.end()) continue;
            const MonoWavData &src = srcIt->second;
            
            // Fill source buffer
            std::fill(sourceBuffer.begin(), sourceBuffer.end(), 0.0f);
            for (size_t i = 0; i < blockLen; i++) {
                size_t globalIdx = blockStart + i;
                sourceBuffer[i] = (globalIdx < src.samples.size()) ? src.samples[globalIdx] : 0.0f;
            }
            
            // Compute input energy - skip expensive checks for silent blocks
            float inAbsSum = 0.0f;
            for (size_t i = 0; i < blockLen; i++) {
                inAbsSum += std::abs(sourceBuffer[i]);
            }
            float inputThreshold = kInputEnergyThreshold * blockLen;
            bool hasInputEnergy = (inAbsSum >= inputThreshold);
            
            // Skip silent sources entirely (no panning needed)
            if (!hasInputEnergy) {
                continue;
            }
            // Special handling for LFE channel / SUB (no spatialization) 
            if (name == "LFE") {
                // Example: assume mSubwooferChannels is a std::vector<int> of sub channel indices
                float subGain = (config.masterGain * dbap_sub_compensation) / mSubwooferChannels.size(); // Could customize LFE gain if desired
                for (size_t i = 0; i < blockLen; ++i) {
                    float sample = sourceBuffer[i];
                    for (int subCh : mSubwooferChannels) {
                        out.samples[subCh][outBlockStart + i] += sample * subGain;
                    }
                }
                continue; // Skip spatialization for LFE
            }

            
            // Measure angular delta for fast-mover detection
            // Sample directions at 25% and 75% through the block
            double t0 = (double)(blockStart + blockLen / 4) / (double)sr;
            double t1 = (double)(blockStart + 3 * blockLen / 4) / (double)sr;
            
            al::Vec3f rawDir0 = safeDirForSource(name, kfs, t0);
            al::Vec3f rawDir1 = safeDirForSource(name, kfs, t1);
            al::Vec3f dir0 = sanitizeDirForLayout(rawDir0, config.elevationMode);
            al::Vec3f dir1 = sanitizeDirForLayout(rawDir1, config.elevationMode);
            
            float dotVal = std::clamp(dir0.dot(dir1), -1.0f, 1.0f);
            float angleDelta = std::acos(dotVal);
            
            bool isFastMover = (angleDelta > kFastMoverAngleRad);
            
            // Sub-step rendering for fast movers
            if (isFastMover) {
                mPannerDiag.substeppedBlocks[name]++;
                mPannerDiag.totalSubsteps++;
                
                // Render in smaller chunks with direction computed per chunk
                for (size_t off = 0; off < blockLen; off += kSubStepHop) {
                    size_t len = std::min((size_t)kSubStepHop, blockLen - off);
                    double tSub = (double)(blockStart + off + len / 2) / (double)sr;
                    
                    al::Vec3f rawDirSub = safeDirForSource(name, kfs, tSub);
                    al::Vec3f dirSub = sanitizeDirForLayout(rawDirSub, config.elevationMode);
                    
                    // Convert direction to position for DBAP, use direction for VBAP/LBAP
                    al::Vec3f posOrDir = (mActivePannerType == PannerType::DBAP) 
                        ? directionToDBAPPosition(dirSub) : dirSub;
                    
                    // Render sub-chunk into temp buffer for zero-detection
                    audioTemp.zeroOut();
                    audioTemp.frame(0);
                    mActiveSpatializer->renderBuffer(audioTemp, posOrDir, sourceBuffer.data() + off, len);
                    
                    // Check for panner failure on this sub-chunk
                    float outAbsSum = 0.0f;
                    audioTemp.frame(0);
                    for (size_t i = 0; i < len; i++) {
                        for (int ch = 0; ch < numSpeakers; ch++) {
                            outAbsSum += std::abs(audioTemp.out(ch, i));
                        }
                    }
                    
                    // Compute sub-chunk input energy
                    float subInAbsSum = 0.0f;
                    for (size_t i = 0; i < len; i++) {
                        subInAbsSum += std::abs(sourceBuffer[off + i]);
                    }
                    float subThreshold = kInputEnergyThreshold * len;
                    
                    // If panner failed, retarget to nearest speaker
                    if (subInAbsSum >= subThreshold && outAbsSum < kPannerZeroThreshold * len * numSpeakers) {
                        mPannerDiag.zeroBlocks[name]++;
                        mPannerDiag.totalZeroBlocks++;
                        
                        al::Vec3f dirFallback = nearestSpeakerDir(dirSub);
                        al::Vec3f posOrDirFallback = (mActivePannerType == PannerType::DBAP)
                            ? directionToDBAPPosition(dirFallback) : dirFallback;
                        
                        audioTemp.zeroOut();
                        audioTemp.frame(0);
                        mActiveSpatializer->renderBuffer(audioTemp, posOrDirFallback, sourceBuffer.data() + off, len);
                        
                        mPannerDiag.retargetBlocks[name]++;
                        mPannerDiag.totalRetargets++;
                    }
                    
                    // Accumulate sub-chunk into main buffer
                    audioTemp.frame(0);
                    audioIO.frame(0);
                    for (size_t i = 0; i < len; i++) {
                        for (int ch = 0; ch < numSpeakers; ch++) {
                            float current = audioIO.out(ch, off + i);
                            float addition = audioTemp.out(ch, i);
                            audioIO.out(ch, off + i) = current + addition;
                        }
                    }
                }
            } else {
                // Normal path: single direction for entire block
                double timeSec = (double)(blockStart + blockLen / 2) / (double)sr;
                al::Vec3f rawDir = safeDirForSource(name, kfs, timeSec);
                al::Vec3f dir = sanitizeDirForLayout(rawDir, config.elevationMode);
                
                // Convert direction to position for DBAP
                al::Vec3f posOrDir = (mActivePannerType == PannerType::DBAP)
                    ? directionToDBAPPosition(dir) : dir;
                
                // Render into temp buffer to detect panner failure
                audioTemp.zeroOut();
                audioTemp.frame(0);
                mActiveSpatializer->renderBuffer(audioTemp, posOrDir, sourceBuffer.data(), blockLen);
                
                // Measure output energy
                float outAbsSum = 0.0f;
                audioTemp.frame(0);
                for (size_t i = 0; i < blockLen; i++) {
                    for (int ch = 0; ch < numSpeakers; ch++) {
                        outAbsSum += std::abs(audioTemp.out(ch, i));
                    }
                }
                
                // If panner produced ~silence despite input, retarget
                if (outAbsSum < kPannerZeroThreshold * blockLen * numSpeakers) {
                    mPannerDiag.zeroBlocks[name]++;
                    mPannerDiag.totalZeroBlocks++;
                    
                    al::Vec3f dirFallback = nearestSpeakerDir(dir);
                    al::Vec3f posOrDirFallback = (mActivePannerType == PannerType::DBAP)
                        ? directionToDBAPPosition(dirFallback) : dirFallback;
                    
                    audioTemp.zeroOut();
                    audioTemp.frame(0);
                    mActiveSpatializer->renderBuffer(audioTemp, posOrDirFallback, sourceBuffer.data(), blockLen);
                    
                    mPannerDiag.retargetBlocks[name]++;
                    mPannerDiag.totalRetargets++;
                }
                
                // Accumulate into main buffer
                audioTemp.frame(0);
                audioIO.frame(0);
                for (size_t i = 0; i < blockLen; i++) {
                    for (int ch = 0; ch < numSpeakers; ch++) {
                        float current = audioIO.out(ch, i);
                        float addition = audioTemp.out(ch, i);
                        audioIO.out(ch, i) = current + addition;
                    }
                }
            }
        }
        
        // Copy output with gain
        audioIO.frame(0);
        for (size_t i = 0; i < blockLen; i++) {
            for (int ch = 0; ch < numSpeakers; ch++) {
                float sample = audioIO.out(ch, i);
                if (!std::isfinite(sample)) sample = 0.0f;
                out.samples[ch][outBlockStart + i] = sample * config.masterGain;
            }
        }


    }
}

// renderSmooth: Direction interpolated within each block using SLERP
// Note: This mode is DEPRECATED. Only VBAP gains interpolation is implemented.
void SpatialRenderer::renderSmooth(MultiWavData &out, const RenderConfig &config,
                                    size_t startSample, size_t endSample) {
    int sr = mSpatial.sampleRate;
    int numSpeakers = mLayout.speakers.size();
    int bufferSize = config.blockSize;
    size_t renderSamples = endSample - startSample;
    
    // For smooth mode, we need per-sample gain interpolation
    // We compute VBAP gains at block start and end, then interpolate
    // NOTE: This only works properly with VBAP, other panners use block mode internally
    std::vector<float> gainsStart(numSpeakers);
    std::vector<float> gainsEnd(numSpeakers);
    std::vector<float> gainsInterp(numSpeakers);
    
    int blocksProcessed = 0;
    for (size_t blockStart = startSample; blockStart < endSample; blockStart += bufferSize) {
        size_t blockEnd = std::min(endSample, blockStart + bufferSize);
        size_t blockLen = blockEnd - blockStart;
        size_t outBlockStart = blockStart - startSample;
        
        if (blocksProcessed % 1000 == 0) {
            std::cout << "  Block " << blocksProcessed << " (" 
                      << (int)(100.0 * (blockStart - startSample) / renderSamples) << "%)\n" << std::flush;
        }
        blocksProcessed++;
        
        // For each source, compute interpolated gains across the block
        for (auto &[name, kfs] : mSpatial.sources) {
            if (!config.soloSource.empty() && name != config.soloSource) continue;
            
            auto srcIt = mSources.find(name);
            if (srcIt == mSources.end()) continue;
            const MonoWavData &src = srcIt->second;
            
            // Get directions at block boundaries
            double timeStart = (double)blockStart / (double)sr;
            double timeEnd = (double)blockEnd / (double)sr;
            
            al::Vec3f rawDirStart = safeDirForSource(name, kfs, timeStart);
            al::Vec3f rawDirEnd = safeDirForSource(name, kfs, timeEnd);
            
            // Sanitize directions to fit within speaker layout's representable range
            al::Vec3f dirStart = sanitizeDirForLayout(rawDirStart, config.elevationMode);
            al::Vec3f dirEnd = sanitizeDirForLayout(rawDirEnd, config.elevationMode);
            
            // Compute gains at both ends (VBAP removed - smooth mode disabled)
            // computeVBAPGains(dirStart, gainsStart);
            // computeVBAPGains(dirEnd, gainsEnd);
            // Smooth mode is disabled, so this code should not be reached
            assert(false && "Smooth mode should be disabled");
            
            // Process each sample with interpolated gains
            for (size_t i = 0; i < blockLen; i++) {
                size_t globalIdx = blockStart + i;
                float inputSample = (globalIdx < src.samples.size()) ? src.samples[globalIdx] : 0.0f;
                
                // Interpolate gains within block
                float t = (blockLen > 1) ? (float)i / (float)(blockLen - 1) : 0.0f;
                for (int ch = 0; ch < numSpeakers; ch++) {
                    gainsInterp[ch] = gainsStart[ch] * (1.0f - t) + gainsEnd[ch] * t;
                }
                
                // Accumulate into output
                for (int ch = 0; ch < numSpeakers; ch++) {
                    float sample = inputSample * gainsInterp[ch] * config.masterGain;
                    if (!std::isfinite(sample)) sample = 0.0f;
                    out.samples[ch][outBlockStart + i] += sample;
                }
            }
        }
    }
}

// renderPerSample: Direction computed at every sample (slowest, smoothest) ### NOT UP TO DATE , DO NOT USE
void SpatialRenderer::renderPerSample(MultiWavData &out, const RenderConfig &config,
                                       size_t startSample, size_t endSample) {
    int sr = mSpatial.sampleRate;
    int numSpeakers = mLayout.speakers.size();
    size_t renderSamples = endSample - startSample;
    
    std::vector<float> gains(numSpeakers);
    
    // Progress reporting interval
    size_t reportInterval = renderSamples / 100;
    if (reportInterval < 1000) reportInterval = 1000;
    
    size_t samplesProcessed = 0;
    for (size_t sampleIdx = startSample; sampleIdx < endSample; sampleIdx++) {
        size_t outIdx = sampleIdx - startSample;
        
        if (samplesProcessed % reportInterval == 0) {
            std::cout << "  Sample " << samplesProcessed << "/" << renderSamples 
                      << " (" << (int)(100.0 * samplesProcessed / renderSamples) << "%)\n" << std::flush;
        }
        samplesProcessed++;
        
        double timeSec = (double)sampleIdx / (double)sr;
        
        for (auto &[name, kfs] : mSpatial.sources) {
            if (!config.soloSource.empty() && name != config.soloSource) continue;
            
            auto srcIt = mSources.find(name);
            if (srcIt == mSources.end()) continue;
            const MonoWavData &src = srcIt->second;
            
            float inputSample = (sampleIdx < src.samples.size()) ? src.samples[sampleIdx] : 0.0f;
            
            // Compute direction at exact sample time
            al::Vec3f rawDir = safeDirForSource(name, kfs, timeSec);
            
            // Sanitize direction to fit within speaker layout's representable range
            al::Vec3f dir = sanitizeDirForLayout(rawDir, config.elevationMode);
            
            // For per-sample mode, always use VBAP gains (most accurate per-sample)
            // computeVBAPGains(dir, gains);
            // Sample mode is disabled, so this code should not be reached
            assert(false && "Sample mode should be disabled");
            
            // Accumulate into output
            for (int ch = 0; ch < numSpeakers; ch++) {
                float sample = inputSample * gains[ch] * config.masterGain;
                if (!std::isfinite(sample)) sample = 0.0f;
                out.samples[ch][outIdx] += sample;
            }
        }
    }
}
