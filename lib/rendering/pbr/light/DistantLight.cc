// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


//----------------------------------------------------------------------------
//
// The physical interpretation of a distant light is a spherical cap on the sphere at infinity.
//
// The solid angle of the cap is 2pi * (1-cosThetaMax), where thetaMax is the angular radius. Care must be taken
// when thetaMax is small and cosThetaMax approaches 1, since the expression 1-cosThetaMax suffers from catastrophic
// cancellation. To combat this we use the trig identity
//
//      1-cos(t) = 2(sin(t))^2.
//
// Similarly, the function sampleLocalSphericalCapUniform2() (see shading/util.h) for generating sample directions
// replaces the standard calculation
//
//      sin(theta) = sqrt(1 - (cos(theta))^2)
//
// with the more numerically robust
// 
//      sin(theta) = sqrt(versine(theta) * (2 - versine(theta))
//
// There is still one potential accuracy issue in the way thresholding is performed in DistantLight::intersect().
// By comparing the dot product to cosThetaMax, we lose considerable accuracy when both values are close to 1.
// But it doesn't appear to cause any trouble so has been left as-is.
//
// The definition of 'normalised' for distant lights is that when a distant light of uniform radiance (1,1,1)
// and any angular extent is placed directly overhead a Lambertian surface of reflectance (1,1,1), the resulting
// outgoing radiance will be (1,1,1). This requires applying an angle-dependent normalisation factor which can be
// derived directly from the rendering equation, and is found to be
//
//      1/sin(min(thetaMax,pi/2))^2
//
// where thetaMax has been clamped to a right-angle since if the distant light covers more than a hemisphere only
// the upper hemisphere contributes light to the surface. See DistantLight::update() for the implementation.


#include "DistantLight.h"

#include <moonray/rendering/pbr/core/RayState.h>
#include <moonray/rendering/pbr/light/DistantLight_ispc_stubs.h>
#include <moonray/rendering/shading/Util.h>

#include <scene_rdl2/scene/rdl2/rdl2.h>
#include <moonray/common/mcrt_macros/moonray_static_check.h>

using namespace scene_rdl2;
using namespace scene_rdl2::math;

namespace moonray {
namespace pbr {


bool                                                     DistantLight::sAttributeKeyInitialized;
scene_rdl2::rdl2::AttributeKey<scene_rdl2::rdl2::Bool>   DistantLight::sNormalizedKey;
scene_rdl2::rdl2::AttributeKey<scene_rdl2::rdl2::Float>  DistantLight::sAngularExtent;

//----------------------------------------------------------------------------

HUD_VALIDATOR(DistantLight);


// The functions localToUv() and uvToLocal() establish the mapping in both directions between the direction
// vector in the light's local space and the (u,v) coordinates on the texture. DistantLight defines the
// mapping as an equisolid angle fisheye projection, chosen because each texel subtends the same solid angle.
// For further details see FisheyeCamera::createDir() (case equisolid angle).
// 
//
// Let the unit direction vector (x,y,z) map to texture coords (u,v) with u,v in [0,1]. Here we show how to
// obtain (u,v) from (x,y,z) and vice versa.
//
// First, define scaled-and-offset texture coords (U,V) with U,V in [-1,1]:
//
//     U = 2u - 1
//     V = 2v - 1
//
// Given (U,V), let r be its radial distance from the origin:
// 
//     r^2 = U^2 + V^2
// 
// With the U- and x-axes aligned, and the V- and y-axes aligned, we require (x,y) to be a uniform scaling of (U,V).
// Let w represent this scaling factor, so we have
// 
//     x = U * w
//     y = V * w
//
// The scale factor w is a function of z, and will be different for each fisheye mapping. Our particular mapping is
// equisolid angle, which https://en.wikipedia.org/wiki/Fisheye_lens defines using
// 
//     r = 2 f sin(theta/2)
// 
// Here theta is the angle subtended between the direction (x,y,z) and the positive z-axis, i.e. cos(theta) = z,
// and f is the focal length of the lens, which is not a consideration in the present context but a corresponding
// scale factor must still be used so that the maximal angle thetaMax corresponding to the outer rim of the
// DistantLight leads to a circle of radius 1. So we can write
// 
//     r = sin(theta/2) / sin(thetaMax/2)
//  
// Using the above equations together with the condition |(x,y,z)| = 1 we can derive the following forward and
// reverse mappings:
//
//
// (x,y,z) -> (u,v)
// ================
// 
// k = sqrt(2) * sin(thetaMax/2)
// 
// w = k * sqrt(1+z)
//
// U = x / w
// V = y / w
// 
// u = (U+1)/2
// v = (V+1)/2
// 
// 
// (u,v) -> (x,y,z)
// ================
//
// k = sqrt(2) * sin(thetaMax/2)
// 
// U = 2u-1
// V = 2v-1
//
// z = 1 - k^2 (U^2 + V^2)
// 
// w = k * sqrt(1+z)
//
// x = U * w
// y = V * w
//
//
// These have been slightly modified to reduce the number of arithmetic operations and to protect against
// ill conditioned edge cases, yielding the two functions below.
// 
// Finally note that the direction vector has been negated relative to the above, in both functions, to
// account for the internal 180 degree rotation built into DistantLight's frame. This ensures consistency
// with the texturing for DiskLight.


Vec2f
DistantLight::localToUv(const Vec3f &dir) const
{
    MNRY_ASSERT(isNormalized(dir));

    const float oneMinusZ = max(1.0f - dir.z, 1.0e-20f);    // protect against negative arg to sqrt and div by zero
    const float scale = -mLocalToUvConst / scene_rdl2::math::sqrt(oneMinusZ);

    const float u = clamp(dir.x * scale + 0.5f, 0.0f, 1.0f);
    const float v = clamp(dir.y * scale + 0.5f, 0.0f, 1.0f);

    return Vec2f(u, v);
}

Vec3f
DistantLight::uvToLocal(const Vec2f &uv) const
{
    const float u1 = uv.x - 0.5f;
    const float v1 = uv.y - 0.5f;

    const float z = -1.0f + mUvToLocalConst * (u1*u1 + v1*v1);
    const float scale = -scene_rdl2::math::sqrt(mUvToLocalConst * (1.0f - z));

    const float x = u1 * scale;
    const float y = v1 * scale;

    return Vec3f(x, y, z);
}

Vec3f
DistantLight::localToGlobal(const Vec3f &v, float time) const
{
    if (!isMb()) return mFrame.localToGlobal(v);

    const Mat3f m(slerp(mOrientation[0], mOrientation[1], time));
    return v * m;
}

Vec3f
DistantLight::globalToLocal(const Vec3f &v, float time) const
{
    if (!isMb()) return mFrame.globalToLocal(v);

    const Mat3f m = Mat3f(slerp(mOrientation[0], mOrientation[1], time)).transposed();
    return v * m;
}

Xform3f
DistantLight::globalToLocalXform(float time, bool needed) const
{
    if (!needed) {
        return math::Xform3f();
    }

    if (!isMb()) {
        return Mat3f(mOrientation[0]).transposed();
    }

    return Mat3f(slerp(mOrientation[0], mOrientation[1], time)).transposed();
}

DistantLight::DistantLight(const scene_rdl2::rdl2::Light* rdlLight, bool uniformSampling) :
    Light(rdlLight)
{
    initAttributeKeys(rdlLight->getSceneClass());

    ispc::DistantLight_init(this->asIspc(), uniformSampling);
}

DistantLight::~DistantLight()
{
}

bool
DistantLight::update(const Mat4d& world2render)
{
    MNRY_ASSERT(mRdlLight);

    mOn = mRdlLight->get(scene_rdl2::rdl2::Light::sOnKey);
    if (!mOn) {
        return false;
    }

    updateVisibilityFlags();
    updatePresenceShadows();
    updateRayTermination();
    updateTextureFilter();
    updateMaxShadowDistance();
    updateMinShadowDistance();

    const Mat4d l2w0 = mRdlLight->get(scene_rdl2::rdl2::Node::sNodeXformKey, /* rayTime = */ 0.0f);
    const Mat4d l2w1 = mRdlLight->get(scene_rdl2::rdl2::Node::sNodeXformKey, /* rayTime = */ 1.0f);

    // DistantLight has an internal 180-degree rotation about the x-axis (sRotateX180),
    // to maintain consistency with DiskLight which emits in the positive local z-direction.
    const Mat4f local2render0 = Mat4f::orthonormalize(sRotateX180 * toFloat(l2w0 * world2render));
    const Mat4f local2render1 = Mat4f::orthonormalize(sRotateX180 * toFloat(l2w1 * world2render));

    const ReferenceFrame frame0(local2render0);
    const ReferenceFrame frame1(local2render1);
    mPosition[0] = mPosition[1] = zero;
    mOrientation[0] = normalize(math::Quaternion3f(frame0.getX(), frame0.getY(), frame0.getZ()));
    mOrientation[1] = normalize(math::Quaternion3f(frame1.getX(), frame1.getY(), frame1.getZ()));
    if (dot(mOrientation[0], mOrientation[1]) < 0) {
        mOrientation[1] *= -1.f;
    }
    mFrame = frame0;
    mDirection = mFrame.getZ();

    // setup mMb
    mMb = LIGHT_MB_NONE;
    const scene_rdl2::rdl2::SceneVariables &vars =
        getRdlLight()->getSceneClass().getSceneContext()->getSceneVariables();
    if (vars.get(scene_rdl2::rdl2::SceneVariables::sEnableMotionBlur) &&
        getRdlLight()->get(scene_rdl2::rdl2::Light::sMbKey) &&
        (!isEqual(frame0.getX(), frame1.getX()) ||
         !isEqual(frame0.getY(), frame1.getY()) ||
         !isEqual(frame0.getZ(), frame1.getZ())))  {
        mMb = LIGHT_MB_ROTATION;
    }

    const float angularExtentDegrees = max(mRdlLight->get(sAngularExtent), sEpsilon);
    const float angularExtentRadians = deg2rad(angularExtentDegrees);
    const float thetaMax = 0.5f * angularExtentRadians;

    // DistantLight's localToRender matrix has a built-in 180-degree rotation for consistency with DiskLight.
    // This has the effect of negating any dot product with the light's z-axis, so we must negate the cull threshold.
    mCullThreshold = -scene_rdl2::math::cos(min(thetaMax + sHalfPi, sPi));
    MNRY_ASSERT(mCullThreshold >= 0.0f);

    mCosThetaMax = scene_rdl2::math::cos(thetaMax);
    const float sinHalfThetaMax = scene_rdl2::math::sin(thetaMax * 0.5f);
    mVersineThetaMax = 2.0f * sinHalfThetaMax * sinHalfThetaMax;
    mLocalToUvConst = 0.5f * scene_rdl2::math::sqrt(0.5f) / sinHalfThetaMax;
    mUvToLocalConst = 4.0f * mVersineThetaMax;

    // We store solid angle and its inverse in the area members, since this is also useful for the unittest.
    // We use an accurately computed versine here because 1-cosine suffers from catastrophic cancellation
    // when the angle is small.
    const float solidAngle = mVersineThetaMax * sTwoPi;
    mArea = solidAngle;
    mInvArea = 1.0f / solidAngle;

    // To convert the pdf returned by sampling the texture into one which represents the sampling density
    // on the sphere, we must divide by the solid angle of the projected texture. Above, we computed the
    // solidAngle for the spherical cap - this must be scaled by 4/pi to account for the full texture, i.e by
    // the ratio of a square's area to that of its inscribed circle. (Note that this computation is made trivial
    // by the fact that all texels subtend the same solid angle, since we use an equisolid angle projection.)
    // So for the Jacobian we store the reciprocal of (4/pi) * solidAngle. This expands to
    // 1 / ((4/pi) * (versine(thetaMax) * (2pi))), which simplifies to 1 / (8 * versine(thetaMax)).
    mJacobian = 0.125f / mVersineThetaMax;

    // Compute radiance.
    mRadiance = computeLightRadiance(mRdlLight, scene_rdl2::rdl2::Light::sColorKey,
        scene_rdl2::rdl2::Light::sIntensityKey, scene_rdl2::rdl2::Light::sExposureKey);

    // Apply normalisation factor if light is normalised. The normalisation factor used here is such that
    // if a distant light of radiance (1,1,1) is directly overhead a Lambertian surface of colour (1,1,1),
    // the  resulting outgoing radiance at the surface will be (1,1,1) regardless of the light's angular
    // extent. This factor can be derived from the rendering equation.
    if (mRdlLight->get<scene_rdl2::rdl2::Bool>(sNormalizedKey)) {
        const float s = scene_rdl2::math::sin(min(thetaMax,sHalfPi));
        mRadiance *= 1.0f / (s*s);
    }

    if (isBlack(mRadiance)) {
        mOn = false;
        return false;
    }

    // Set here in case we early-out
    mLog2TexelAngle = scene_rdl2::math::neg_inf;

    if (!updateImageMap(Distribution2D::CIRCULAR)) {
        return false;
    }

    // Precompute a value for use with ray-footprint-based mip level selection based on the larger texel extent
    if (mDistribution) {
        const float texelAngleU = angularExtentRadians / (float)mDistribution->getWidth();
        const float texelAngleV = angularExtentRadians / (float)mDistribution->getHeight();
        const float texelAngle = max(texelAngleU, texelAngleV);
        mLog2TexelAngle = scene_rdl2::math::log2(texelAngle);
    }

    return true;
}

bool
DistantLight::canIlluminate(const Vec3f p, const Vec3f *n, float time, float radius,
    const LightFilterList* lightFilterList, const PathVertex* pv) const
{
    MNRY_ASSERT(mOn);

    // Don't illuminate as a regular light if referenced by a portal
    if (mHasPortal) return false;

    // The sense of this comparison takes into account the 180-degree rotation built into
    // DistantLight's localToRender matrix.
    if (n && dot(*n, getDirection(time)) > mCullThreshold) {
        return false;
    }

    if (lightFilterList) {
        return canIlluminateLightFilterList(lightFilterList,
            { getPosition(time),
              math::inf, p, globalToLocalXform(time, lightFilterList->needsLightXform()),
              radius, time, pv
            });
    }

    return true;
}

bool
DistantLight::isBounded() const
{
    return false;
}

bool
DistantLight::isDistant() const
{
    return true;
}

bool
DistantLight::isEnv() const
{
    return false;
}

bool
DistantLight::intersect(const Vec3f &p, const Vec3f *n, const Vec3f &wi, float time,
        float maxDistance, LightIntersection &isect) const
{
    // DistantLight's localToRender matrix has a built-in 180-degree rotation for consistency with DiskLight.
    // This has the effect of negating any dot product with the light's z-axis, so we must negate cos thetaMax here.
    if (dot(wi, getDirection(time)) > -mCosThetaMax) {
        return false;
    }

    if (sDistantLightDistance > maxDistance) {
        return false;
    }

    isect.N = -wi;
    isect.distance = sDistantLightDistance;
    isect.uv = mDistribution ? localToUv(globalToLocal(wi, time)) : zero;

    return true;
}

bool
DistantLight::sample(const Vec3f &p, const Vec3f *n, float time, const Vec3f& r,
                     Vec3f &wi, LightIntersection &isect, float rayDirFootprint) const
{
    MNRY_ASSERT(mOn);

    if (mDistribution) {
        const float mipLevel = getMipLevel(rayDirFootprint);
        mDistribution->sample(r[0], r[1], mipLevel, &isect.uv, nullptr, mTextureFilter);
        const float U = isect.uv.x * 2.0f - 1.0f;
        const float V = isect.uv.y * 2.0f - 1.0f;
        if (U*U + V*V >= 1.0f) {
            return false;
        }
        const Vec3f wiLocal = uvToLocal(isect.uv);
        wi = localToGlobal(wiLocal, time);
    } else {
        // The cap sampling utiility function generates a position on a cap centered on the frame's positive z-axis.
        // DistantLight is also defined to lie on the positive z-axis of its local frame; however its localToRender
        // matrix has a built-in 180-degree rotation, so we negate the generated cap direction to produce the local
        // wi vector.
        const Vec3f sampledCapDir = shading::sampleLocalSphericalCapUniform2(r[0], r[1], mVersineThetaMax);
        const Vec3f wiLocal = -sampledCapDir;
        wi = localToGlobal(wiLocal, time);
        isect.uv = zero;
    }

    if (n  &&  dot(*n, wi) < sEpsilon) {
        return false;
    }

    isect.N = -wi;
    isect.distance = sDistantLightDistance;

    return true;
}

Color
DistantLight::eval(mcrt_common::ThreadLocalState* tls, const Vec3f &wi, const Vec3f &p, const LightFilterRandomValues& filterR,
        float time, const LightIntersection &isect, bool fromCamera, const LightFilterList *lightFilterList, const PathVertex *pv,
        float rayDirFootprint, float *visibility, float *pdf) const
{
    MNRY_ASSERT(mOn);

    const float mipLevel = getMipLevel(rayDirFootprint);

    Color radiance = mRadiance;
    if (mDistribution) {
        radiance *= mDistribution->eval(isect.uv[0], isect.uv[1], mipLevel, mTextureFilter);
    }

    if (lightFilterList) {
        evalLightFilterList(lightFilterList,
                            { tls, &isect, getPosition(time),
                              getDirection(time), p,
                              filterR, time, pv,
                              globalToLocalXform(time, lightFilterList->needsLightXform()),
                              wi
                            },
                            radiance,
                            visibility);
    }


    if (pdf) {
        if (mDistribution) {
            *pdf = mDistribution->pdf(isect.uv[0], isect.uv[1], mipLevel, mTextureFilter) * mJacobian;
        } else {
            *pdf = mInvArea;
        }
    }

    return radiance;
}

Vec3f
DistantLight::getEquiAngularPivot(const Vec3f& r, float time) const
{
    MNRY_ASSERT_REQUIRE(isBounded(),
        "light with infinite distance should not use equi-angular sampling");
    return getPosition(time);
}

void
DistantLight::initAttributeKeys(const scene_rdl2::rdl2::SceneClass &sc)
{
    if (sAttributeKeyInitialized) {
        return;
    }

    MOONRAY_START_NON_THREADSAFE_STATIC_WRITE

    sAttributeKeyInitialized = true;

    sNormalizedKey = sc.getAttributeKey<scene_rdl2::rdl2::Bool>("normalized");

    sAngularExtent = sc.getAttributeKey<scene_rdl2::rdl2::Float>("angular_extent");

    MOONRAY_FINISH_NON_THREADSAFE_STATIC_WRITE
}

//----------------------------------------------------------------------------

} // namespace pbr
} // namespace moonray

