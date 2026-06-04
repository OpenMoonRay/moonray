// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once
#ifndef LOCALMOTIONBLUR_H
#define LOCALMOTIONBLUR_H

#include <moonray/rendering/shading/PrimitiveAttribute.h>
#include <moonray/rendering/shading/Xform.h>
#include <moonray/rendering/geom/Api.h>
#include <moonray/rendering/geom/ProceduralContext.h>

#include <scene_rdl2/common/math/Xform.h>
#include <scene_rdl2/common/math/Vec3.h>
#include <scene_rdl2/common/math/Vec4.h>
#include <scene_rdl2/scene/rdl2/rdl2.h>

#include <memory>
#include <vector>

#pragma once

namespace moonray {
namespace local_motion_blur {

// Attributes in the points file that will be used
// to contruct the regions
static moonray::shading::AttributeKeySet sLocalMotionBlurAttributes = {
    moonray::shading::TypedAttributeKey<float>("radius"),
    moonray::shading::TypedAttributeKey<float>("inner_radius"),
    moonray::shading::TypedAttributeKey<float>("multiplier")
};

struct MbRegion
{
    MbRegion(scene_rdl2::math::Xform3f xform,
             float radius,
             float innerRadius,
             float multiplier) :
        mXform(xform),
        mRadius(radius),
        mInnerRadius(innerRadius),
        mMultiplier(multiplier) {}

    scene_rdl2::math::Xform3f mXform;
    float mRadius;
    float mInnerRadius;
    float mMultiplier;
};

// This class creates spherical regions which locally modulate motion blur.
// The regions are defined by a list of transforms and parameters.
class LocalMotionBlur
{
public:
    LocalMotionBlur(const moonray::geom::GenerateContext& generateContext,
                    const std::vector<moonray::shading::XformSamples>& regionXforms,
                    const moonray::shading::PrimitiveAttributeTable& pointsAttributes,
                    const bool useLocalCameraMotionBlur=false,
                    const float strengthMult=1.0f,
                    const float radiusMult=1.0f);

    template <typename AttributeType> void
    apply(const scene_rdl2::rdl2::MotionBlurType mbType,
          const moonray::shading::XformSamples& parent2root,
          moonray::geom::VertexBuffer<AttributeType, moonray::geom::InterleavedTraits>& vertices,
          moonray::shading::PrimitiveAttributeTable& primitiveAttributeTable) const;

    // Convenience overload: derives MotionBlurType from the sample counts that
    // were actually used to build the vertex buffer, promoting STATIC to
    // STATIC_DUPLICATE so local motion blur can always expand to two time steps.
    template <typename AttributeType> void
    apply(int numPosSamples, int numVelSamples, int numAccSamples,
          const moonray::shading::XformSamples& parent2root,
          moonray::geom::VertexBuffer<AttributeType, moonray::geom::InterleavedTraits>& vertices,
          moonray::shading::PrimitiveAttributeTable& primitiveAttributeTable) const
    {
        scene_rdl2::rdl2::MotionBlurType mbType;
        if      (numPosSamples == 2 && numVelSamples == 2) mbType = scene_rdl2::rdl2::MotionBlurType::HERMITE;
        else if (numPosSamples == 2)                       mbType = scene_rdl2::rdl2::MotionBlurType::FRAME_DELTA;
        else if (numVelSamples > 0 && numAccSamples > 0)   mbType = scene_rdl2::rdl2::MotionBlurType::ACCELERATION;
        else if (numVelSamples > 0)                        mbType = scene_rdl2::rdl2::MotionBlurType::VELOCITY;
        else                                               mbType = scene_rdl2::rdl2::MotionBlurType::STATIC_DUPLICATE;
        apply(mbType, parent2root, vertices, primitiveAttributeTable);
    }

    virtual ~LocalMotionBlur() = default;

private:
    float getMultiplier(const scene_rdl2::math::Vec3f& P,
                        const moonray::shading::XformSamples& parent2root) const;

    std::vector<MbRegion> mRegions;
    const scene_rdl2::rdl2::Geometry* mRdlGeometry;
    moonray::shading::XformSamples mNodeXform;
    moonray::shading::XformSamples mCameraXform;
    scene_rdl2::math::Xform3f mCameraRelXform;
    float mShutterInterval;
    float mFps;
    bool mUseLocalCameraMotionBlur;
};


// Factory helper: builds a LocalMotionBlur from per-point RDL2 attribute lists.
// Handles validation, xform construction, and the node_xform fallback when no
// regions are specified (use_local_motion_blur enabled but point list is empty).
// When the point list is empty, parent2render is updated to include the geometry
// node_xform (which the renderer skips when LMB is active), and nullptr is returned.
std::unique_ptr<LocalMotionBlur> createFromPointLists(
    const moonray::geom::GenerateContext& generateContext,
    const std::vector<scene_rdl2::math::Vec3f>& pointList,
    const std::vector<scene_rdl2::math::Vec4f>& orientList,
    const std::vector<scene_rdl2::math::Vec3f>& scaleList,
    const std::vector<float>& radiusList,
    const std::vector<float>& innerRadiusList,
    const std::vector<float>& multiplierList,
    float strengthMult,
    float radiusMult,
    moonray::shading::XformSamples& parent2render);

} // namespace local_motion_blur
} // namespace moonray 

#endif // LOCALMOTIONBLUR_H
