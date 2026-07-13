// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "LocalMotionBlur.h"

#include <iomanip>

using namespace moonray::shading;
using namespace moonray::geom;
using namespace scene_rdl2::rdl2;
using namespace scene_rdl2::math;

namespace moonray {
namespace local_motion_blur {

LocalMotionBlur::LocalMotionBlur(const GenerateContext& generateContext,
                                 const std::vector<XformSamples>& regionXforms,
                                 const PrimitiveAttributeTable& pointsAttributes,
                                 const bool useLocalCameraMotionBlur,
                                 const float strengthMult,
                                 const float radiusMult) :
    mRdlGeometry(generateContext.getRdlGeometry()),
    mFps(24.0f),
    mUseLocalCameraMotionBlur(useLocalCameraMotionBlur)
{
    // Get this geometry's node_xform unblended values
    const Mat4d m0 = mRdlGeometry->get(Node::sNodeXformKey, TIMESTEP_BEGIN);
    const Mat4d m1 = mRdlGeometry->get(Node::sNodeXformKey, TIMESTEP_END);
    const Xform3f x0(m0.vx.x, m0.vx.y, m0.vx.z,
                     m0.vy.x, m0.vy.y, m0.vy.z,
                     m0.vz.x, m0.vz.y, m0.vz.z,
                     m0.vw.x, m0.vw.y, m0.vw.z);
    const Xform3f x1(m1.vx.x, m1.vx.y, m1.vx.z,
                     m1.vy.x, m1.vy.y, m1.vy.z,
                     m1.vz.x, m1.vz.y, m1.vz.z,
                     m1.vw.x, m1.vw.y, m1.vw.z);
    mNodeXform = { x0, x1 };

    const SceneVariables& sv = mRdlGeometry->getSceneClass().getSceneContext()->getSceneVariables();
    mFps = sv.get(SceneVariables::sFpsKey);

    mCameraXform = { Xform3f(scene_rdl2::math::one) };
    mCameraRelXform = Xform3f(scene_rdl2::math::one);
    if (useLocalCameraMotionBlur) {
        // Get the inverse of the scene camera's node_xform
        const SceneObject* cameraObject = sv.getCamera();
        const Camera* camera = (cameraObject) ?  cameraObject->asA<Camera>() : nullptr;
        if (camera) {
            const Mat4d m0 = camera->get(Node::sNodeXformKey, TIMESTEP_BEGIN).inverse();
            const Mat4d m1 = camera->get(Node::sNodeXformKey, TIMESTEP_END).inverse();
            const Xform3f x0(m0.vx.x, m0.vx.y, m0.vx.z,
                             m0.vy.x, m0.vy.y, m0.vy.z,
                             m0.vz.x, m0.vz.y, m0.vz.z,
                             m0.vw.x, m0.vw.y, m0.vw.z);
            const Xform3f x1(m1.vx.x, m1.vx.y, m1.vx.z,
                             m1.vy.x, m1.vy.y, m1.vy.z,
                             m1.vz.x, m1.vz.y, m1.vz.z,
                             m1.vw.x, m1.vw.y, m1.vw.z);
            mCameraXform = {x0, x1};

            // Relative camera transform: maps a world-space point so that
            // its appearance in the TIMESTEP_BEGIN camera matches its
            // original appearance in the TIMESTEP_END camera.
            // In row-vector convention: transformPoint(rel, P) = P * cam1_inv * cam0.
            // Xform3f A*B applies A first then B, so we need:
            //   A = xfm1 (= cam1_inv),  B = xfm0.inverse() (= cam0)
            mCameraRelXform = mCameraXform[1] * mCameraXform[0].inverse();
        }
    }

    for (size_t i = 0; i < regionXforms.size(); ++i) {
        float radius = 1.0f;
        float inner_radius = 1.0f;
        float multiplier = 1.0f;
        for (const auto& kv : pointsAttributes) {
            moonray::shading::AttributeKey key = kv.first;
            const PrimitiveAttributeBase& attr = *kv.second[0];
            if (key.getType() == scene_rdl2::rdl2::TYPE_FLOAT &&
                (attr.getRate() == RATE_VERTEX || attr.getRate() == RATE_VARYING)) {

                if (strcmp(key.getName(), "radius") == 0) {
                    radius = attr.as<float>()[i];
                } else if (strcmp(key.getName(), "inner_radius") == 0) {
                    inner_radius = attr.as<float>()[i];
                } else if (strcmp(key.getName(), "multiplier") == 0) {
                    multiplier = attr.as<float>()[i];
                }
            }
        }

        mRegions.emplace_back(regionXforms[i][0],
                              radius * max(0.0f, radiusMult),
                              inner_radius * max(0.0f, radiusMult),
                              1.0f + (multiplier - 1.0f) * clamp(strengthMult));
    }
}

float
LocalMotionBlur::getMultiplier(const Vec3f& P,
                               const XformSamples& parent2root) const
{
    // If there are 2 motion steps use the second one
    float mbMult = 1.0f;
    for (size_t r = 0; r < mRegions.size(); ++r) {
        const Vec3f localP = transformPoint(mRegions[r].mXform.inverse(), P);
        const float distSqr = lengthSqr(localP);
        const float r2 = mRegions[r].mRadius * mRegions[r].mRadius;
        float ir2 = mRegions[r].mInnerRadius * mRegions[r].mInnerRadius;
        if (ir2 > r2) ir2 = r2 - sEpsilon;
        const float t = 1.0f - clamp((distSqr - ir2) / (r2 - ir2));
        mbMult = min(mbMult, lerp(1.0f,  mRegions[r].mMultiplier, t));
    }
    return mbMult;
}

template <> void
LocalMotionBlur::apply(const MotionBlurType mbType,
                       const XformSamples& parent2root,
                       VertexBuffer<Vec3fa, InterleavedTraits>& vertices,
                       PrimitiveAttributeTable& primitiveAttributeTable) const
{
    const size_t numTimeSteps = vertices.get_time_steps();
    VertexBuffer<Vec3f, InterleavedTraits> vertices3f(vertices.size(), numTimeSteps);
    std::vector<float> widths0(vertices.size(), 0.f);
    std::vector<float> widths1(vertices.size(), 0.f);
    for (size_t i = 0; i < vertices.size(); ++i) {
        vertices3f(i, 0) = Vec3f(vertices(i, 0).x, vertices(i, 0).y, vertices(i, 0).z);
        widths0[i] = vertices(i, 0).w;
        if (numTimeSteps > 1) {
            vertices3f(i, 1) = Vec3f(vertices(i, 1).x, vertices(i, 1).y, vertices(i, 1).z);
            widths1[i] = vertices(i, 1).w;
        }
    }

    apply(mbType,
          parent2root,
          vertices3f,
          primitiveAttributeTable);

    // compute the curve radius scaling that is applied to the w component
    XformSamples local2world = concatenate(parent2root, mNodeXform);
    float radiusScale0 = (length(local2world[0].l.vx) + length(local2world[0].l.vy) + length(local2world[0].l.vz)) / 3.f;
    float radiusScale1 = (length(local2world[1].l.vx) + length(local2world[1].l.vy) + length(local2world[1].l.vz)) / 3.f;

    for (size_t i = 0; i < vertices.size(); ++i) {
        vertices(i, 0) = Vec3fa(vertices3f(i, 0).x,
                                vertices3f(i, 0).y,
                                vertices3f(i, 0).z,
                                vertices(i, 0).w * radiusScale0);
        if (numTimeSteps > 1) {
            vertices(i, 1) = Vec3fa(vertices3f(i, 1).x,
                                    vertices3f(i, 1).y,
                                    vertices3f(i, 1).z,
                                    vertices(i, 1).w * radiusScale1);
        }

    }
}

template <typename AttributeType> void
LocalMotionBlur::apply(const MotionBlurType mbType,
                       const XformSamples& parent2root,
                       VertexBuffer<AttributeType, InterleavedTraits>& vertices,
                       PrimitiveAttributeTable& primitiveAttributeTable) const
{
    std::vector<float> localMbMask(vertices.size());

    const size_t numTimeSteps = vertices.get_time_steps();

    // Vertices are transformed into world space using the object's
    // node_xform parameter and the local transforms in parent2root.
    XformSamples local2world = concatenate(parent2root, mNodeXform);

    // Expand a single-timestep vertex buffer to two steps by duplicating step 0
    // into step 1, so the renderer always sees 2 motion samples.
    const auto expandVertexBuffer = [&vertices, numTimeSteps]() {
        if (numTimeSteps < 2) {
            VertexBuffer<AttributeType, InterleavedTraits> expanded(vertices.size(), 2);
            for (size_t i = 0; i < vertices.size(); ++i) {
                expanded(i, 0) = vertices(i, 0);
                expanded(i, 1) = vertices(i, 0);
            }
            vertices = std::move(expanded);
        }
    };

    switch (mbType) {

    case MotionBlurType::STATIC_DUPLICATE:
    case MotionBlurType::FRAME_DELTA:
    {
        expandVertexBuffer();
        for (size_t i = 0; i < vertices.size(); ++i) {
            const AttributeType orig0 = vertices(i, 0);
            const AttributeType orig1 = vertices(i, 1);
            vertices(i, 0) = transformPoint(local2world[0], orig0);
            vertices(i, 1) = transformPoint(local2world[1], orig1);

            const Vec3f& p0 = vertices(i, 0);
            const Vec3f& p1 = vertices(i, 1);
            const float mbMult = getMultiplier(p1, parent2root);
            localMbMask[i] = 1.0f - mbMult;
            if (!isOne(mbMult)) {
                // Translate the first motion step position towards the second
                // motion step position(p1) and add the camera correction
                // to counteract camera motion blur from ray generation.
                const Vec3f p0_reduced = p1 + (p0 - p1) * mbMult;
                const Vec3f cameraCorrection = transformPoint(mCameraRelXform, p0_reduced) - p0_reduced;
                vertices(i, 0) = p0_reduced + cameraCorrection * (1.0f - mbMult);
            }
        }
        break;
    }

    case MotionBlurType::VELOCITY:
    {
        if (!primitiveAttributeTable.hasAttribute(StandardAttributes::sVelocity)) {
            mRdlGeometry->error("Missing velocity attribute for local motion blur");
            return;
        }
        PrimitiveAttribute<Vec3f>& velocityAttr =
            primitiveAttributeTable.getAttribute(StandardAttributes::sVelocity);
        if (velocityAttr.getRate() != AttributeRate::RATE_VERTEX &&
            velocityAttr.getRate() != AttributeRate::RATE_VARYING) {
            mRdlGeometry->error("Velocity must be defined at vertex or varying rate for local motion blur");
            return;
        }

        for (size_t i = 0; i < vertices.size(); ++i) {
            vertices(i, 0) = transformPoint(local2world[0], vertices(i, 0));

            const float mbMult = getMultiplier(vertices(i, 0), parent2root);
            localMbMask[i] = 1.0f - mbMult;
            if (!isOne(mbMult)) {
                const Vec3f cameraVelCorrection =
                    (transformPoint(mCameraRelXform, vertices(i, 0)) - vertices(i, 0)) * mFps;
                velocityAttr[i] = velocityAttr[i] * mbMult -
                                  cameraVelCorrection * (1.0f - mbMult);
            }
        }

        break;
    }

    case MotionBlurType::ACCELERATION:
    {
        if (!primitiveAttributeTable.hasAttribute(StandardAttributes::sVelocity)) {
            mRdlGeometry->error("Missing velocity attribute for local motion blur");
            return;
        }
        PrimitiveAttribute<Vec3f>& velocityAttr =
            primitiveAttributeTable.getAttribute(StandardAttributes::sVelocity);
        if (velocityAttr.getRate() != AttributeRate::RATE_VERTEX &&
            velocityAttr.getRate() != AttributeRate::RATE_VARYING) {
            mRdlGeometry->error("Velocity must be defined at vertex or varying rate for local motion blur");
            return;
        }

        if (!primitiveAttributeTable.hasAttribute(TypedAttributeKey<Vec3f>("accel"))) {
            mRdlGeometry->error("Missing 'accel' acceleration attribute for local motion blur");
            return;
        }
        PrimitiveAttribute<Vec3f>& accelAttr =
            primitiveAttributeTable.getAttribute(TypedAttributeKey<Vec3f>("accel"));
        if (accelAttr.getRate() != AttributeRate::RATE_VERTEX &&
            accelAttr.getRate() != AttributeRate::RATE_VARYING) {
            mRdlGeometry->error("Acceleration must be defined at vertex or varying rate for local motion blur");
            return;
        }

        for (size_t i = 0; i < vertices.size(); ++i) {
            vertices(i, 0) = transformPoint(local2world[0], vertices(i, 0));

            const float mbMult = getMultiplier(vertices(i, 0), parent2root);
            localMbMask[i] = 1.0f - mbMult;
            if (!isOne(mbMult)) {
                const Vec3f cameraVelCorrection =
                    (transformPoint(mCameraRelXform, vertices(i, 0)) - vertices(i, 0)) * mFps;
                velocityAttr[i] = velocityAttr[i] * mbMult -
                                  cameraVelCorrection * (1.0f - mbMult);

                accelAttr[i] = accelAttr[i] * mbMult -
                               cameraVelCorrection * (1.0f - mbMult);
            }
        }
        break;
    }

    case scene_rdl2::rdl2::MotionBlurType::HERMITE:
    {
        if (!primitiveAttributeTable.hasAttribute(StandardAttributes::sVelocity)) {
            mRdlGeometry->error("Missing velocity attribute for local motion blur");
            return;
        }
        PrimitiveAttribute<Vec3f>& velocity0Attr =
            primitiveAttributeTable.getAttribute(StandardAttributes::sVelocity, 0);
        if (velocity0Attr.getRate() != AttributeRate::RATE_VERTEX &&
            velocity0Attr.getRate() != AttributeRate::RATE_VARYING) {
            mRdlGeometry->error("Velocity must be defined at vertex or varying rate for local motion blur");
            return;
        }
        expandVertexBuffer();

        // Ensure velocity always has 2 time samples to match the (now 2-step)
        // vertex buffer.  The sample count may be 1 even when numTimeSteps >= 2
        // (e.g. a geometry that exported positions at two times but only one
        // velocity sample), so we gate on the actual velocity sample count, not
        // on numTimeSteps.
        if (primitiveAttributeTable.getTimeSampleCount(StandardAttributes::sVelocity) < 2) {
            std::vector<Vec3f> velCopy(velocity0Attr.begin(), velocity0Attr.end());
            primitiveAttributeTable.find(StandardAttributes::sVelocity)->second.emplace_back(
                new PrimitiveAttribute<Vec3f>(velocity0Attr.getRate(), std::move(velCopy)));
        }
        PrimitiveAttribute<Vec3f>& velocity1Attr =
            primitiveAttributeTable.getAttribute(StandardAttributes::sVelocity, 1);

        for (size_t i = 0; i < vertices.size(); ++i) {
            const AttributeType orig0 = vertices(i, 0);
            const AttributeType orig1 = vertices(i, 1);
            vertices(i, 0) = transformPoint(local2world[0], orig0);
            vertices(i, 1) = transformPoint(local2world[1], orig1);

            const Vec3f& p0 = vertices(i, 0);
            const Vec3f& p1 = vertices(i, 1);
            const float mbMult = getMultiplier(p1, parent2root);
            localMbMask[i] = 1.0f - mbMult;
            if (!isOne(mbMult)) {

                // Compute per-sample velocity corrections since the
                // rotational component is position-dependent.
                const Vec3f cameraVelCorrection0 =
                    (transformPoint(mCameraRelXform, p0) - p0) * mFps;
                velocity0Attr[i] = velocity0Attr[i] * mbMult -
                                   cameraVelCorrection0 * (1.0f - mbMult);

                const Vec3f cameraVelCorrection1 =
                    (transformPoint(mCameraRelXform, p1) - p1) * mFps;
                velocity1Attr[i] = velocity1Attr[i] * mbMult -
                                   cameraVelCorrection1 * (1.0f - mbMult);

                // Translate the first motion step position towards the second
                // motion step position(p1) and add the camera correction
                // to counteract camera motion blur from ray generation.
                const Vec3f p0_reduced = p1 + (p0 - p1) * mbMult;
                const Vec3f cameraCorrection = transformPoint(mCameraRelXform, p0_reduced) - p0_reduced;
                vertices(i, 0) = p0_reduced + cameraCorrection * (1.0f - mbMult);
            }
        }
        break;
    }

    }

    primitiveAttributeTable.addAttribute(TypedAttributeKey<float>("local_motion_blur"),
                                         RATE_VERTEX,
                                         std::move(localMbMask));
}

template void LocalMotionBlur::apply(const MotionBlurType mbType,
                                     const XformSamples& parent2root,
                                     VertexBuffer<Vec3f, InterleavedTraits>& vertices,
                                     PrimitiveAttributeTable& primitiveAttributeTable) const;

template void LocalMotionBlur::apply(const MotionBlurType mbType,
                                     const XformSamples& parent2root,
                                     VertexBuffer<Vec3fa, InterleavedTraits>& vertices,
                                     PrimitiveAttributeTable& primitiveAttributeTable) const;

std::unique_ptr<LocalMotionBlur>
createFromPointLists(
    const geom::GenerateContext& generateContext,
    const std::vector<Vec3f>& pointList,
    const std::vector<scene_rdl2::math::Vec4f>& orientList,
    const std::vector<Vec3f>& scaleList,
    const std::vector<float>& radiusList,
    const std::vector<float>& innerRadiusList,
    const std::vector<float>& multiplierList,
    float strengthMult,
    float radiusMult,
    XformSamples& parent2render)
{
    const Geometry* rdlGeometry = generateContext.getRdlGeometry();
    const size_t numPoints = pointList.size();

    const bool hasOrients     = (orientList.size()      == numPoints);
    const bool hasScales      = (scaleList.size()       == numPoints);
    const bool hasRadii       = (radiusList.size()      == numPoints);
    const bool hasInnerRadii  = (innerRadiusList.size() == numPoints);
    const bool hasMultipliers = (multiplierList.size()  == numPoints);

    if (!hasOrients && !orientList.empty()) {
        rdlGeometry->warn("local_motion_blur_orient_list size (", orientList.size(),
                          ") does not match point count (", numPoints, "), ignoring");
    }
    if (!hasScales && !scaleList.empty()) {
        rdlGeometry->warn("local_motion_blur_scale_list size (", scaleList.size(),
                          ") does not match point count (", numPoints, "), ignoring");
    }
    if (!hasRadii && !radiusList.empty()) {
        rdlGeometry->warn("local_motion_blur_radius_list size (", radiusList.size(),
                          ") does not match point count (", numPoints, "), ignoring");
    }
    if (!hasInnerRadii && !innerRadiusList.empty()) {
        rdlGeometry->warn("local_motion_blur_inner_radius_list size (", innerRadiusList.size(),
                          ") does not match point count (", numPoints, "), ignoring");
    }
    if (!hasMultipliers && !multiplierList.empty()) {
        rdlGeometry->warn("local_motion_blur_multiplier_list size (", multiplierList.size(),
                          ") does not match point count (", numPoints, "), ignoring");
    }

    std::vector<XformSamples> regionXforms;
    PrimitiveAttributeTable lmbAttributeTable;
    std::vector<float> radii, innerRadii, multipliers;
    regionXforms.reserve(numPoints);
    radii.reserve(numPoints);
    innerRadii.reserve(numPoints);
    multipliers.reserve(numPoints);

    for (size_t i = 0; i < numPoints; ++i) {
        Vec3f scale(one);
        if (hasScales) scale = scaleList[i];

        Quaternion3f orient(one);
        if (hasOrients) {
            const auto& q = orientList[i];
            // Vec4f stores (i, j, k, r) in (x, y, z, w) to match Alembic orient convention
            orient = normalize(Quaternion3f(q.w, q.x, q.y, q.z));
        }
        const Xform3f xf(Mat3f::scale(scale) * Mat3f(orient), pointList[i]);
        regionXforms.push_back({xf});

        radii.push_back(      hasRadii       ? radiusList[i]      : 1.0f);
        innerRadii.push_back( hasInnerRadii  ? innerRadiusList[i] : 1.0f);
        multipliers.push_back(hasMultipliers ? multiplierList[i]  : 1.0f);
    }

    if (!regionXforms.empty()) {
        lmbAttributeTable.addAttribute(TypedAttributeKey<float>("radius"),
                                       RATE_VERTEX, std::move(radii));
        lmbAttributeTable.addAttribute(TypedAttributeKey<float>("inner_radius"),
                                       RATE_VERTEX, std::move(innerRadii));
        lmbAttributeTable.addAttribute(TypedAttributeKey<float>("multiplier"),
                                       RATE_VERTEX, std::move(multipliers));
        return std::make_unique<LocalMotionBlur>(
            generateContext,
            regionXforms,
            lmbAttributeTable,
            rdlGeometry->getUseLocalCameraMotionBlur(),
            strengthMult,
            radiusMult);
    } else {
        // use_local_motion_blur is enabled but no regions were provided.
        // The renderer skips node_xform when LMB is active, so apply it here.
        const Mat4d mat0 = rdlGeometry->get(Node::sNodeXformKey, TIMESTEP_BEGIN);
        const Mat4d mat1 = rdlGeometry->get(Node::sNodeXformKey, TIMESTEP_END);
        const Xform3f x0(mat0.vx.x, mat0.vx.y, mat0.vx.z,
                         mat0.vy.x, mat0.vy.y, mat0.vy.z,
                         mat0.vz.x, mat0.vz.y, mat0.vz.z,
                         mat0.vw.x, mat0.vw.y, mat0.vw.z);
        const Xform3f x1(mat1.vx.x, mat1.vx.y, mat1.vx.z,
                         mat1.vy.x, mat1.vy.y, mat1.vy.z,
                         mat1.vz.x, mat1.vz.y, mat1.vz.z,
                         mat1.vw.x, mat1.vw.y, mat1.vw.z);
        const XformSamples nodeXform = {x0, x1};
        parent2render = concatenate(nodeXform, parent2render);
        return nullptr;
    }
}

} // end local_motion_blur
} // end namespace moonray
