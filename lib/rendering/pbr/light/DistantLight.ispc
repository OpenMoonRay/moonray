// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#include "Light.isph"

#include <moonray/rendering/pbr/core/RayState.isph>
#include <moonray/rendering/shading/ispc/Util.isph>
#include <scene_rdl2/common/platform/IspcUtil.isph>


//----------------------------------------------------------------------------

ISPC_UTIL_EXPORT_UNIFORM_STRUCT_TO_HEADER(DistantLight);

export uniform uint32_t
DistantLight_hudValidation(uniform bool verbose)
{
    DISTANT_LIGHT_VALIDATION;
}


//----------------------------------------------------------------------------
// For details of localToUv() and uvToLocal() see notes in DistantLight.cc

static Vec2f
DistantLight_localToUv(const uniform DistantLight * uniform light,
                       const varying Vec3f &dir)
{
    MNRY_ASSERT(isNormalized(dir));

    const varying float oneMinusZ = max(1.0f - dir.z, 1.0e-20f);    // protect against negative arg to sqrt and div by zero
    const varying float scale = -light->mLocalToUvConst / sqrt(oneMinusZ);

    const varying float u = clamp(dir.x * scale + 0.5f, 0.0f, 1.0f);
    const varying float v = clamp(dir.y * scale + 0.5f, 0.0f, 1.0f);

    return Vec2f_ctor(u, v);
}

static Vec3f
DistantLight_uvToLocal(const uniform DistantLight * uniform light,
                       const varying Vec2f &uv)
{
    const varying float u1 = uv.x - 0.5f;
    const varying float v1 = uv.y - 0.5f;

    const varying float z = -1.0f + light->mUvToLocalConst * (u1*u1 + v1*v1);
    const varying float scale = -sqrt(light->mUvToLocalConst * (1.0f - z));

    const varying float x = u1 * scale;
    const varying float y = v1 * scale;

    return Vec3f_ctor(x, y, z);
}

static Vec3f
DistantLight_localToGlobal(const uniform DistantLight * uniform light,
                           const varying Vec3f &v, varying float time)
{
    if (!light->mMb) return localToGlobal(light->mFrame, v);

    varying Mat3f m = Mat3f_ctor(slerp(light->mOrientation[0], light->mOrientation[1], time));
    return v * m;
}

static Vec3f
DistantLight_globalToLocal(const uniform DistantLight * uniform light,
                           const varying Vec3f &v, varying float time)
{
    if (!light->mMb) return globalToLocal(light->mFrame, v);

    varying Mat3f m = transpose(Mat3f_ctor(slerp(light->mOrientation[0], light->mOrientation[1], time)));
    return v * m;
}

static varying Xform3f
DistantLight_globalToLocalXform(const uniform DistantLight * uniform light, 
                                varying float time, bool needed = true)
{
    if (!needed) {
        Xform3f result;
        return result;
    }

    if (!light->mMb) {
        return Xform3f_ctor(transpose(Mat3f_ctor(light->mOrientation[0])),
                            Vec3f_ctor(0.f));
    }

    // construct a new frame
    return Xform3f_ctor(
        transpose(Mat3f_ctor(slerp(light->mOrientation[0], 
                                   light->mOrientation[1], time))),
        Vec3f_ctor(0.f));
}

//----------------------------------------------------------------------------

varying bool
DistantLight_canIlluminate(const uniform Light * uniform li,
        const varying Vec3f &p, const varying Vec3f &cullingNormal, varying float time,
        varying float radius, const uniform LightFilterList * uniform lightFilterList,
        const varying PathVertex * uniform pv)
{
    const uniform LocalParamLight * uniform lpl = (const uniform LocalParamLight * uniform)li;
    const uniform DistantLight * uniform light = (const uniform DistantLight * uniform)li;

    MNRY_ASSERT(li->mOn);

    // Don't illuminate as a regular light if referenced by a portal
    if (li->mHasPortal) return false;

    // The sense of this comparison takes into account the 180-degree rotation built into
    // DistantLight's localToRender matrix.
    if (isValidCullingNormal(cullingNormal) && dot(cullingNormal, Light_getDirection(li, time)) > light->mCullThreshold) {
        return false;
    }

    if (lightFilterList) {
        CanIlluminateData cid;
        cid.pv = pv;
        cid.lightPosition = Light_getPosition(li, time);
        cid.lightRadius = sMaxValue;
        cid.shadingPointPosition = p;
        cid.lightRender2LocalXform = DistantLight_globalToLocalXform(
                light, time, LightFilterList_getNeedsLightXform(lightFilterList));
        cid.shadingPointRadius = radius;
        cid.time = time;
        return LightFilterList_canIlluminate(lightFilterList, cid);
    }

    return true;
}

varying bool
DistantLight_intersect(const uniform Light * uniform li,
        const varying Vec3f &p, const varying Vec3f &cullingNormal,
        const varying Vec3f &wi, varying float time,
        varying float maxDistance, varying LightIntersection &isect)
{
    const uniform DistantLight * uniform light = (const uniform DistantLight * uniform)li;

    // DistantLight's localToRender matrix has a built-in 180-degree rotation for consistency with DiskLight.
    // This has the effect of negating any dot product with the light's z-axis, so we must negate cos thetaMax here.
    if (dot(wi, Light_getDirection(li, time)) > -light->mCosThetaMax) {
        return false;
    }

    if (sDistantLightDistance > maxDistance) {
        return false;
    }

    isect.N = neg(wi);
    isect.distance = sDistantLightDistance;
    isect.uv = light->mDistribution ? DistantLight_localToUv(light, DistantLight_globalToLocal(light, wi, time))
                                    : Vec2f_ctor(0.0f);

    return true;
}

varying float
DistantLight_getMipLevel(const uniform Light * uniform li, varying float rayDirFootprint)
{
    const uniform DistantLight * uniform light = (const uniform DistantLight * uniform)li;
    return rayDirFootprint - light->mLog2TexelAngle;
}


varying bool
DistantLight_sample(const uniform Light * uniform li,
        const varying Vec3f &p, const varying Vec3f &cullingNormal, varying float time, const varying Vec3f &r,
        varying Vec3f &wi, varying LightIntersection &isect, varying float rayDirFootprint)
{
    const uniform DistantLight * uniform light = (const uniform DistantLight * uniform)li;

    MNRY_ASSERT(li->mOn);

    if (light->mDistribution) {
        float mipLevel = DistantLight_getMipLevel(li, rayDirFootprint);
        ImageDistribution_sample(light->mDistribution, r.x, r.y, mipLevel, &isect.uv, nullptr, light->mTextureFilter);
        float U = isect.uv.x * 2.0f - 1.0f;
        float V = isect.uv.y * 2.0f - 1.0f;
        if (U*U + V*V >= 1.0f) {
            return false;
        }
        Vec3f wiLocal = DistantLight_uvToLocal(light, isect.uv);
        wi = DistantLight_localToGlobal(light, wiLocal, time);
    } else {
        // The cap sampling utiility function generates a position on a cap centered on the frame's positive z-axis.
        // DistantLight is also defined to lie on the positive z-axis of its local frame; however its localToRender
        // matrix has a built-in 180-degree rotation, so we negate the generated cap direction to produce the local
        // wi vector.
        Vec3f wiLocal = neg(sampleLocalSphericalCapUniform2(r.x, r.y, light->mVersineThetaMax));
        wi = DistantLight_localToGlobal(light, wiLocal, time);
        isect.uv = Vec2f_ctor(0.0f);
    }

    if (isValidCullingNormal(cullingNormal)  &&  dot(cullingNormal, wi) < sEpsilon) {
        return false;
    }

    isect.N = neg(wi);
    isect.distance = sDistantLightDistance;

    return true;
}


varying Color
DistantLight_eval(const uniform Light * uniform li, uniform ShadingTLState * uniform tls,
        const varying Vec3f &wi, const varying Vec3f &p, const varying LightFilterRandomValues& filterR,
        varying float time, const varying LightIntersection &isect, uniform bool fromCamera,
        const uniform LightFilterList * uniform lightFilterList, const varying PathVertex * uniform pv,
        varying float rayDirFootprint, varying float * uniform visibility, varying float * uniform pdf)
{
    const uniform DistantLight * uniform light = (const uniform DistantLight * uniform)li;
    const uniform LocalParamLight * uniform lpl = (const uniform LocalParamLight * uniform) li;

    MNRY_ASSERT(light->mOn);

    float mipLevel = DistantLight_getMipLevel(li, rayDirFootprint);

    Color radiance = light->mRadiance;
    if (light->mDistribution) {
        radiance = radiance * ImageDistribution_eval(light->mDistribution, isect.uv.x, isect.uv.y, mipLevel,
                                                     light->mTextureFilter);
    }

    if (lightFilterList) {
        EvalData evalData;
        evalData.tls = tls;
        evalData.isect = &isect;
        evalData.lightPosition = Light_getPosition(li, time);
        evalData.lightDirection = Light_getDirection(li, time);
        evalData.pv = pv;
        evalData.shadingPointPosition = p;
        evalData.randVar = filterR;
        evalData.time = time;
        evalData.lightRender2LocalXform = DistantLight_globalToLocalXform(
            light, time, LightFilterList_getNeedsLightXform(lightFilterList));
        evalData.wi = wi;

        LightFilterList_eval(lightFilterList, evalData, visibility, &radiance);
    }

    if (pdf) {
        if (light->mDistribution) {
            *pdf = ImageDistribution_pdf(light->mDistribution, isect.uv.x, isect.uv.y, mipLevel, light->mTextureFilter)
                    * light->mJacobian;
        } else {
            *pdf = light->mInvArea;
        }
    }

    return radiance;
}

// A value of -1 for both indicates that this type of light (unbounded) 
// should always be sampled (i.e. not included in the light sampling BVH)
inline uniform float DistantLight_getThetaO(const uniform Light * uniform li) { return -1.f; }
inline uniform float DistantLight_getThetaE(const uniform Light * uniform li) { return -1.f; }

//----------------------------------------------------------------------------

export void
DistantLight_init(uniform DistantLight * uniform light, uniform bool uniformSampling)
{
    MNRY_ASSERT(!uniformSampling);

    Light_init((uniform Light * uniform)light,
               LIGHT_TYPE_DISTANT,
               DistantLight_canIlluminate,
               DistantLight_intersect,
               DistantLight_sample,
               DistantLight_eval,
               DistantLight_getThetaO,
               DistantLight_getThetaE);
}


//----------------------------------------------------------------------------

