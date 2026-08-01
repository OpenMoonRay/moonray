// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "LightFilter.isph"
#include <moonray/rendering/pbr/core/Util.isph>
#include <moonray/rendering/shading/ispc/Shading.isph>
#include <scene_rdl2/common/math/ispc/ReferenceFrame.isph>
#include <scene_rdl2/common/platform/IspcUtil.isph>

//----------------------------------------------------------------------------

ISPC_UTIL_EXPORT_UNIFORM_STRUCT_TO_HEADER(CookieLightFilter_v2);

// UV coordinates are nudged this far off the [0,1] boundary before a texture
// lookup so the bilinear/CDF sampler never indexes one past the edge texel,
// which would return a NaN.  Applied both at the latlong poles and at the
// planar projection edges.
static const uniform float sUvEdgeMin = 0.0005f;
static const uniform float sUvEdgeMax = 1.0f - sUvEdgeMin;

// Map a projector-local direction to y-up latlong UVs.
// offset is tangent-plane blur jitter; applying it before the latlong mapping
// keeps blur angularly isotropic and pole-safe.
// NOTE: Keep this in sync with
// CookieLightFilter_v2.cc (cookieLatlongUv).
inline Vec2f
cookieLatlongUv(const varying Vec3f localP, const varying Vec2f blurOffset)
{
    // Guard degenerate directions so we never emit NaN UVs.
    float len = length(localP);
    Vec3f dir = (len > 0.f && isfinite(len)) ?
        localP * (1.0f / len) : Vec3f_ctor(0.0f, 0.0f, -1.0f);

    if (blurOffset.x != 0.f || blurOffset.y != 0.f) {
        varying ReferenceFrame frame = ReferenceFrame_ctor(dir);
        Vec3f perturbed = dir + frame.mX * blurOffset.x + frame.mY * blurOffset.y;
        dir = normalize(perturbed);
    }

    float theta = atan2(sqrt(dir.x*dir.x + dir.z*dir.z), dir.y);
    float phi = atan2(dir.z, dir.x);

    float u = phi * sOneOverTwoPi;
    float v = 1.0f - theta * sOneOverPi;

    // Wrap phi into [0, 1); atan2 returns [-pi, pi] so u starts in [-0.5, 0.5].
    if (u < 0.0f) {
        u += 1.0f;
    }

    if (!isfinite(u) || !isfinite(v)) {
        u = 0.0f;
        v = 0.5f;
    }

    // Keep lookups off the exact boundary (most importantly v at the poles).
    u = clamp(u, 0.0f, sUvEdgeMax);
    v = clamp(v, sUvEdgeMin, sUvEdgeMax);

    Vec2f uv = {u, v};
    return uv;
}

export uniform uint32_t
CookieLightFilter_v2_hudValidation(uniform bool verbose)
{
    COOKIE_LIGHT_FILTER_V2_VALIDATION;
}

//----------------------------------------------------------------------------

varying bool
CookieLightFilter_v2_canIlluminate(const uniform LightFilter * uniform lif,
                                   const varying CanIlluminateData &data)
{
    const uniform CookieLightFilter_v2 * uniform lf = (const uniform CookieLightFilter_v2 * uniform)lif;

    if (lf->mProjectorType == SPHERICAL_LATLONG) {
        // Spherical mode covers all directions; nothing is culled.
        return true;
    }

    Vec3f projectorPos = lerp(lf->mProjectorPos[0], lf->mProjectorPos[1], data.time);
    Vec3f projectorDir = lerp(lf->mProjectorDir[0], lf->mProjectorDir[1], data.time);
    Vec3f shadingVec = data.shadingPointPosition - projectorPos;

    if (dot(shadingVec, projectorDir) < 0.f) {
        // The shading point is behind the projector, we don't need to evaluate the
        // map shader.
        switch (lf->mOutsideProjection) {
        case BLACK:
            return false;
        case WHITE:
            return true;
        default:
            return false;
        }
    }
    return true;
}

void
CookieLightFilter_v2_eval(const uniform LightFilter * uniform lif,
                          const varying EvalData& data,
                          varying Color * uniform filterValue)
{
    const uniform CookieLightFilter_v2 * uniform lf = (const uniform CookieLightFilter_v2 * uniform)lif;

    Vec3f projectorPos = lerp(lf->mProjectorPos[0], lf->mProjectorPos[1], data.time);
    Vec3f projectorDir = lerp(lf->mProjectorDir[0], lf->mProjectorDir[1], data.time);
    Vec3f shadingVec = data.shadingPointPosition - projectorPos;
    float distanceToProjector = length(shadingVec);

    const uniform bool spherical = (lf->mProjectorType == SPHERICAL_LATLONG);

    if (!spherical && dot(shadingVec, projectorDir) < 0.f) {
        // The shading point is behind the projector, we don't need to evaluate the
        // map shader.
        switch (lf->mOutsideProjection) {
        case BLACK:
            *filterValue = Color_ctor(0.f);
            break;
        case WHITE:
            *filterValue = Color_ctor(1.f);
            break;
        default:
            *filterValue = Color_ctor(0.f);
            break;
        }
        return;
    }

    Vec3f screenP = Vec3f_ctor(0.f, 0.f, 1.f);
    Vec3f sphericalLocalP = Vec3f_ctor(0.f, 0.f, 1.f);
    if (spherical) {
        // Keep the shading point in the projector's local space; the latlong UV
        // (with blur jitter) is computed below once the filter radius is known.
        // In spherical mode mProjectorR2S has an identity screen projection, so it
        // maps render space straight to projector-local space.
        Vec3f localP0 = transformPoint(lf->mProjectorR2S[0], data.shadingPointPosition);
        Vec3f localP1 = transformPoint(lf->mProjectorR2S[1], data.shadingPointPosition);
        sphericalLocalP = lerp(localP0, localP1, data.time);
    } else {
        // We need the screen space position of the render space point.
        Vec4f spp = {data.shadingPointPosition.x,
                     data.shadingPointPosition.y,
                     data.shadingPointPosition.z,
                     1.f};
        Vec3f screenP0 = transformH(lf->mProjectorR2S[0], spp);
        Vec3f screenP1 = transformH(lf->mProjectorR2S[1], spp);
        screenP = lerp(screenP0, screenP1, data.time);

        // Right now we are in screen space where (0, 0) is the center of the projection.
        // However, for maps, the center is (0.5, 0.5) and we want the map centered
        // in the center of the projection.  So we need to add an 0.5 offset.
        screenP.x += 0.5f;
        screenP.y += 0.5f;
    }

    float filterRadius = 0.f; // aka blur value
    if (distanceToProjector <= lf->mBlurNearDistance) {
        filterRadius = lf->mBlurNearValue;
    } else if (distanceToProjector <= lf->mBlurMidpoint) {
        float dt = lf->mBlurMidpoint - lf->mBlurNearDistance;
        if (dt > 0.f) {
            float t = (distanceToProjector - lf->mBlurNearDistance) / dt;
            filterRadius = lerp(lf->mBlurNearValue, lf->mBlurMidValue, t);
        } else {
            filterRadius = lf->mBlurMidValue;
        }
    } else if (distanceToProjector <= lf->mBlurFarDistance) {
        float dt = lf->mBlurFarDistance - lf->mBlurMidpoint;
        if (dt > 0.f) {
            float t = (distanceToProjector - lf->mBlurMidpoint) / dt;
            filterRadius = lerp(lf->mBlurMidValue, lf->mBlurFarValue, t);
        } else {
            filterRadius = lf->mBlurFarValue;
        }
    } else {
        filterRadius = lf->mBlurFarValue;
    }

    // Box filter, kept here for debugging.
    // Vec2f offset = {(r.x - 0.5f) * filterRadius, (r.y - 0.5f) * filterRadius};

    Vec2f offset;

    switch (lf->mBlurType) {
    case GAUSSIAN:
        // Quadratic bspline "gaussian" filter, which is the same filter the regular camera uses.
        offset.x = quadraticBSplineWarp(data.randVar.r2.x) * filterRadius;
        offset.y = quadraticBSplineWarp(data.randVar.r2.y) * filterRadius;
        break;
    case CIRCULAR:
        offset.x = data.randVar.r2.x;
        offset.y = data.randVar.r2.y;
        toUnitDisk(offset.x, offset.y);
        offset.x *= filterRadius;
        offset.y *= filterRadius;
        break;
    default:
        MNRY_ASSERT(false);
    }

    Vec2f st;

    if (spherical) {
        // Apply the blur by perturbing the sampling direction on the sphere
        // (see cookieLatlongUv), which is angularly isotropic and pole-safe.
        st = cookieLatlongUv(sphericalLocalP, offset);
    } else {
        // Planar case just adds offset directly and checks bounds.
        st.x = screenP.x + offset.x;
        st.y = screenP.y + offset.y;

        if (!isfinite(st.x) || !isfinite(st.y) || 
            st.x < 0.f || st.x > 1.f ||
            st.y < 0.f || st.y > 1.f) {
            switch (lf->mOutsideProjection) {
                case BLACK:
                    *filterValue = Color_ctor(0.f);
                    break;
                case WHITE:
                    *filterValue = Color_ctor(1.f);
                    break;
                default:
                    *filterValue = Color_ctor(0.f);
                    break;
            }
            return;
        }

        st.x = clamp(st.x, sUvEdgeMin, sUvEdgeMax);
        st.y = clamp(st.y, sUvEdgeMin, sUvEdgeMax);
    }

    // Lookup the map value using the displaced screen space position. When the
    // texture is missing, mDistribution is null and we fall back to the
    // untextured color (white when unspecified, or the fatal color on a failed
    // load), matching the untextured-fallback logic used by the lights.
    if (lf->mDistribution != NULL) {
        *filterValue = ImageDistribution_eval(lf->mDistribution,
                                              st.x, st.y, 0,
                                              TEXTURE_FILTER_BILINEAR);
    } else {
        *filterValue = lf->mTextureFallbackColor;
    }

    filterValue->r = 1.f - lf->mDensity + filterValue->r * lf->mDensity;
    filterValue->g = 1.f - lf->mDensity + filterValue->g * lf->mDensity;
    filterValue->b = 1.f - lf->mDensity + filterValue->b * lf->mDensity;

    if (lf->mInvert) {
        *filterValue = 1.f - *filterValue;
    }
}

//----------------------------------------------------------------------------

export void
CookieLightFilter_v2_init(uniform CookieLightFilter_v2 * uniform lightfilter)
{
    LightFilter_init((uniform LightFilter * uniform)lightfilter, 
            CookieLightFilter_v2_canIlluminate, CookieLightFilter_v2_eval);
}

//----------------------------------------------------------------------------

