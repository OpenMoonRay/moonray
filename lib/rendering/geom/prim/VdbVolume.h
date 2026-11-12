// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

///
/// @file VdbVolume.h
///

#pragma once

#include <moonray/rendering/geom/VdbVolume.h>
#include <moonray/rendering/geom/prim/BufferDesc.h>
#include <moonray/rendering/geom/prim/GeomTLState.h>
#include <moonray/rendering/geom/prim/GridSampler.h>
#include <moonray/rendering/geom/prim/NamedPrimitive.h>

#include <moonray/rendering/shading/Intersection.h>
#include <moonray/rendering/shading/PrimitiveAttribute.h>
#include <moonray/rendering/shading/State.h>
#include <moonray/rendering/geom/MotionBlurParams.h>
#include <moonray/rendering/mcrt_common/Ray.h>
#include <moonray/rendering/mcrt_common/ThreadLocalState.h>
#include <moonray/rendering/mcrt_common/Util.h>

#include <scene_rdl2/common/math/Mat4.h>
#include <scene_rdl2/scene/rdl2/Geometry.h>
#include <scene_rdl2/scene/rdl2/Layer.h>
#include <scene_rdl2/scene/rdl2/Material.h>
#include <scene_rdl2/scene/rdl2/VisibilityFlags.h>
#include <scene_rdl2/scene/rdl2/VolumeShader.h>

#include <openvdb/tools/Interpolation.h>
#include <openvdb/tools/RayIntersector.h>
#include <openvdb/tools/VelocityFields.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <new>

namespace moonray {
namespace geom {

namespace internal {

namespace {

template<typename GridType>
inline std::unique_ptr<EmissionDistribution>
computeEmissionDistributionImpl(const scene_rdl2::rdl2::Geometry* rdlGeometry,
                                const GridType& emissionGrid,
                                const scene_rdl2::math::Mat4f (&renderToPrim)[2],
                                const std::vector<float>& values,
                                const scene_rdl2::rdl2::VolumeShader* volumeShader,
                                float emissionSampleRate)
{
    shading::TLState *tls = mcrt_common::getFrameUpdateTLS()->mShadingTls.get();

    // figure out the bounding box first
    openvdb::CoordBBox bbox;
    size_t valueIndex = 0;
    for (auto it = emissionGrid.cbeginValueOn(); it; ++it) {
        const float value = values[valueIndex];
        ++valueIndex;
        if (value <= scene_rdl2::math::sEpsilon || !scene_rdl2::math::isfinite(value)) {
            continue;
        }

        if (it.isVoxelValue()) {
            bbox.expand(it.getCoord());
        } else {
            openvdb::CoordBBox bound;
            it.getBoundingBox(bound);
            bbox.expand(bound);
        }
    }

    // No finite positive emission was found, so there is no meaningful importance
    // distribution to build from this grid.
    if (bbox.empty()) {
        return nullptr;
    }

    openvdb::Coord pMin = bbox.min();
    openvdb::Coord dim = bbox.dim();

    if (dim[0] <= 0 || dim[1] <= 0 || dim[2] <= 0) {
        rdlGeometry->warn("Emission grid has invalid bounds for importance sampling; "
                          "falling back to coarse bounding-box sampling.");
        return nullptr;
    }

    // TODO: fix the dual usage of VdbVolume's mPrimToRender matrices, then the inversions done here won't be needed
    scene_rdl2::math::Mat4f primToRender[2];
    primToRender[0] = renderToPrim[0].inverse();
    primToRender[1] = renderToPrim[1].inverse();
    openvdb::math::MapBase::ConstPtr map = emissionGrid.transform().baseMap();
    openvdb::math::Mat4d M = map->getAffineMap()->getMat4().asPointer();

    scene_rdl2::math::Mat4f indexToPrim(M[0][0], M[0][1], M[0][2], M[0][3],
                                        M[1][0], M[1][1], M[1][2], M[1][3],
                                        M[2][0], M[2][1], M[2][2], M[2][3],
                                        M[3][0], M[3][1], M[3][2], M[3][3]);

    scene_rdl2::math::Mat4f indexToRender[2] = {indexToPrim * primToRender[0],
                                                indexToPrim * primToRender[1]};

    // We must pick a single representative voxel volume - interpolate halfway between the 2 matrices
    float voxelVolume = scene_rdl2::math::lerp(indexToRender[0], indexToRender[1], 0.5f).det();

    // Apply emission_sample_rate to scale down the histogram resolution.
    // This trades accuracy for reduced memory usage and faster initialization.
    scene_rdl2::math::Vec3i res(
        std::max(1, static_cast<int>(dim[0] * emissionSampleRate)),
        std::max(1, static_cast<int>(dim[1] * emissionSampleRate)),
        std::max(1, static_cast<int>(dim[2] * emissionSampleRate)));

    // Scale factor: each histogram bin covers (dim[i]/res[i]) voxels along each axis.
    const float sx = (float)dim[0] / res[0];
    const float sy = (float)dim[1] / res[1];
    const float sz = (float)dim[2] / res[2];

    // invUnitVolume is the inverse of one histogram bin's volume in render space.
    // A bin covers sx*sy*sz voxels, so its volume = voxelVolume * sx*sy*sz.
    float invUnitVolume = 1.0f / (voxelVolume * sx * sy * sz);

    // This is really subtle. The vdb point sampler transforms the input
    // world coordinates to index space and uses "round" to look up the
    // voxel value so we need to offset the sample by 0.5
    float tx = pMin[0] - 0.5f;
    float ty = pMin[1] - 0.5f;
    float tz = pMin[2] - 0.5f;

    scene_rdl2::math::Mat4f distToIndex(sx  , 0.0f, 0.0f, 0.0f,
                                        0.0f, sy  , 0.0f, 0.0f,
                                        0.0f, 0.0f, sz  , 0.0f,
                                        tx  , ty  , tz  , 1.0f);

    scene_rdl2::math::Mat4f distToRender[2] = {distToIndex * indexToRender[0],
                                               distToIndex * indexToRender[1]};

    const size_t rx = static_cast<size_t>(res[0]);
    const size_t ry = static_cast<size_t>(res[1]);
    const size_t rz = static_cast<size_t>(res[2]);

    auto checkedMultiply = [](size_t a, size_t b, size_t& result) -> bool
        {
            if (a != 0 && b > std::numeric_limits<size_t>::max() / a) {
                return false;
            }
            result = a * b;
            return true;
        };

    // Use checked size_t arithmetic to avoid overflow for large sparse grids
    // (e.g. a 3333x1312x4633 bounding box with few active voxels).
    size_t denseResolution = 0;
    if (!checkedMultiply(rx, ry, denseResolution) ||
        !checkedMultiply(denseResolution, rz, denseResolution)) {
        rdlGeometry->warn("Emission grid histogram is too large to build; ",
                          "please reduce \"emission_sample_rate\".");
        return nullptr;
    }

    // If the histogram is still too large even after applying emission_sample_rate, warn.
    if (denseResolution > (size_t)std::numeric_limits<int>::max()) {
        // The cube root of 2 ^ 31 is about 1290.
        float factor = 3.f * 1290.f / (res[0] + res[1] + res[2]);

        // Round to nearest 2 decimals.
        float suggestedSampleRate = std::round(factor * emissionSampleRate * 100.0f) / 100.0f;
        suggestedSampleRate = scene_rdl2::math::clamp(suggestedSampleRate, 0.0f, 1.0f);

        rdlGeometry->warn("Emission grid histogram is too large to build. "
                          "Please set \"emission_sample_rate\" to ", suggestedSampleRate,
                          " or lower to reduce memory usage; lower values may increase noise.");
        return nullptr;
    }

    auto evalVolumeShader = [&](const openvdb::Coord& coord)->float
    {
        // eval volume shader at this position
        openvdb::Vec3d pd = emissionGrid.indexToWorld(coord);
        const Vec3f p(pd.x(), pd.y(), pd.z());
        shading::Intersection isect;
        // TODO: this code relates to map shaders, so when we support xform mblur for map shaders,
        // use time-based interpolation here. It's ok to leave it at t=0 for now.
        isect.setP(transformPoint(primToRender[0], p));
        return scene_rdl2::math::luminance(volumeShader->emission(tls,
                                                                  shading::State(&isect),
                                                                  scene_rdl2::math::Color(1.0f)));
    };

    std::vector<float> histogram;
    try {
        histogram.assign(denseResolution, 0.0f);
    } catch (const std::bad_alloc&) {
        rdlGeometry->warn("Failed to allocate emission histogram; "
                          "falling back to coarse bounding-box sampling.");
        return nullptr;
    }

    auto getHistogramIndex = [&](const openvdb::Coord& ijk)->size_t
        {
            // Map the voxel's offset inside the emission bbox into the reduced histogram grid.
            // Each axis is scaled independently, so multiple VDB voxels can collapse into one bin
            // when emissionSampleRate < 1.
            const size_t xOffset = static_cast<size_t>(ijk[0] - pMin[0]);
            const size_t yOffset = static_cast<size_t>(ijk[1] - pMin[1]);
            const size_t zOffset = static_cast<size_t>(ijk[2] - pMin[2]);

            const size_t bx = scene_rdl2::math::min(rx - 1, xOffset * rx / static_cast<size_t>(dim[0]));
            const size_t by = scene_rdl2::math::min(ry - 1, yOffset * ry / static_cast<size_t>(dim[1]));
            const size_t bz = scene_rdl2::math::min(rz - 1, zOffset * rz / static_cast<size_t>(dim[2]));

            const size_t voxelsPerSlice = rx * ry;
            return bz * voxelsPerSlice + by * rx + bx;
        };

    valueIndex = 0;
    for (auto it = emissionGrid.cbeginValueOn(); it.test(); ++it) {
        float value = values[valueIndex];
        ++valueIndex;
        if (value <= scene_rdl2::math::sEpsilon || !scene_rdl2::math::isfinite(value)) {
            continue;
        }

        if (it.isVoxelValue()) {
            const openvdb::Coord& coord = it.getCoord();
            value *= evalVolumeShader(coord);

            // set histogram value
            histogram[getHistogramIndex(coord)] += value;
        } else {
            openvdb::CoordBBox bound;
            it.getBoundingBox(bound);
            for (openvdb::CoordBBox::Iterator<true> ijk(bound); ijk; ++ijk) {
                const openvdb::Coord& coord = *ijk;
                value *= evalVolumeShader(coord);
                histogram[getHistogramIndex(coord)] += value;
            }
        }
    }

    try {
        return std::make_unique<DenseEmissionDistribution>(res,
                                                           distToRender,
                                                           invUnitVolume,
                                                           histogram);
    } catch (const std::bad_alloc&) {
        rdlGeometry->warn("Failed to build emission importance distribution; "
                          "falling back to coarse bounding-box sampling.");
        return nullptr;
    }
}

} // end anonymous namespace

MNRY_STATIC_ASSERT(int(geom::VdbVolume::Interpolation::POINT) == int(geom::internal::Interpolation::POINT));
MNRY_STATIC_ASSERT(int(geom::VdbVolume::Interpolation::BOX) == int(geom::internal::Interpolation::BOX));
MNRY_STATIC_ASSERT(int(geom::VdbVolume::Interpolation::QUADRATIC) == int(geom::internal::Interpolation::QUADRATIC));

class VolumeRayState;
class DDAIntersector;

// temporary attribute container before initializing VdbVolume
struct VdbVolumeData
{
    VdbVolumeData(const std::string& filePath,
                  const std::string& densityGridName,
                  const std::string& emissionGridName,
                  const std::string& velocityGridName,
                  const MotionBlurParams& motionBlurParams,
                  shading::PrimitiveAttributeTable&& primitiveAttributeTable) :
        mFilePath(filePath),
        mDensityGridName(densityGridName),
        mEmissionGridName(emissionGridName),
        mVelocityGridName(velocityGridName),
        mIsMotionBlurOn(motionBlurParams.isMotionBlurOn()),
        mShutterOpen(motionBlurParams.getShutterOpen()),
        mShutterClose(motionBlurParams.getShutterClose()),
        mVelocityScale(0.0f),
        mVelocitySampleRate(0.0f),
        mEmissionSampleRate(0.0f),
        mPrimitiveAttributeTable(std::move(primitiveAttributeTable))
    {}

    std::string mFilePath;
    std::string mDensityGridName;
    std::string mEmissionGridName;
    std::string mVelocityGridName;
    bool mIsMotionBlurOn;
    float mShutterOpen;
    float mShutterClose;
    float mVelocityScale;
    float mVelocitySampleRate;
    float mEmissionSampleRate;
    scene_rdl2::math::Mat4f mXform;
    scene_rdl2::math::Mat4f mWorldToRender;
    shading::PrimitiveAttributeTable mPrimitiveAttributeTable;
};

class VdbVolume : public NamedPrimitive
{
public:
    VdbVolume(const std::string& vdbFilePath,
              const std::string& densityGridName,
              const std::string& emissionGridName,
              const std::string& velocityGridName,
              const MotionBlurParams& motionBlurParams,
              LayerAssignmentId&& layerAssignmentId,
              shading::PrimitiveAttributeTable&& primitiveAttributeTable);

    ~VdbVolume();

    virtual size_t getMemory() const override;

    // VdbVolume use velocity motion blur instead of loading two sets
    // of voxel grids for linear interpolation motion blur
    virtual size_t getMotionSamplesCount() const override
    {
        return 1;
    }

    virtual bool canIntersect() const override
    {
        return false;
    }

    virtual void tessellate(const TessellationParams& tessellationParams,
                            TessellationStats& stats) override;

    virtual void transformPrimitive(const scene_rdl2::math::Mat4f& primToRender);

    void setTransform(const shading::XformSamples& xforms,
                      float shutterOpenDelta,
                      float shutterCloseDelta)
    {
        if (mIsMotionBlurOn) {
            MNRY_ASSERT(xforms.size() == 2);
            mPrimToRender[0] = lerp(scene_rdl2::math::Mat4f(xforms[0]),
                                    scene_rdl2::math::Mat4f(xforms[1]),
                                    shutterOpenDelta);

            mPrimToRender[1] = lerp(scene_rdl2::math::Mat4f(xforms[0]),
                                    scene_rdl2::math::Mat4f(xforms[1]),
                                    shutterCloseDelta);

            mRenderToPrim[0] = mPrimToRender[0].inverse();
            mRenderToPrim[1] = mPrimToRender[1].inverse();
        } else {
            // Set both time samples equal
            mPrimToRender[0] = scene_rdl2::math::Mat4f(xforms[0]);
            mPrimToRender[1] = scene_rdl2::math::Mat4f(xforms[0]);
            mRenderToPrim[0] = mPrimToRender[0].inverse();
            mRenderToPrim[1] = mRenderToPrim[0];
        }
    }

    virtual int getIntersectionAssignmentId(int primID) const override;

    void getTessellatedMesh(BufferDesc * vertexBufferDesc,
                            BufferDesc& indexBufferDesc,
                            size_t& vertexCount,
                            size_t& faceCount,
                            size_t& timeSteps) const;

    virtual void postIntersect(mcrt_common::ThreadLocalState& tls,
                               const scene_rdl2::rdl2::Layer* pRdlLayer,
                               const mcrt_common::Ray& ray,
                               shading::Intersection& intersection) const override;

    virtual BBox3f computeAABB() const override;
    virtual BBox3f computeAABBAtTimeStep(int timeStep) const override;

    virtual std::unique_ptr<EmissionDistribution>
    computeEmissionDistribution(const scene_rdl2::rdl2::VolumeShader* volumeShader) const override;

    virtual const scene_rdl2::rdl2::Material* getIntersectionMaterial(const scene_rdl2::rdl2::Layer* pRdlLayer,
                                                                      const mcrt_common::Ray &ray) const override;

    virtual PrimitiveType getType() const override
    {
        return VDB_VOLUME;
    }

    virtual void initVolumeSampleInfo(VolumeSampleInfo* info,
                                      const Vec3f& rayOrg,
                                      const Vec3f& rayDir,
                                      const float time,
                                      const scene_rdl2::rdl2::VolumeShader* volumeShader,
                                      int volumeId) const override;

    // This sample position is used for sampling the vdb grids. It must be in the vdb grids' space.
    virtual scene_rdl2::math::Vec3f evalVolumeSamplePosition(mcrt_common::ThreadLocalState* tls,
                                                             uint32_t volumeId,
                                                             const Vec3f& pSample,
                                                             float time) const override;

    // This sample position is used for sampling map shaders. It must be in render space or
    // local space if this is a shared primitive.
    virtual scene_rdl2::math::Vec3f transformVolumeSamplePosition(const Vec3f& pSample,
                                                                  float time) const override;

    virtual scene_rdl2::math::Color evalDensity(mcrt_common::ThreadLocalState* tls,
                                                uint32_t volumeId,
                                                const Vec3f& pSample,
                                                float /*rayVolumeDepth*/,
                                                const scene_rdl2::rdl2::VolumeShader* const) const override;

    virtual void evalVolumeCoefficients(mcrt_common::ThreadLocalState* tls,
                                        uint32_t volumeId,
                                        const Vec3f& pSample,
                                        scene_rdl2::math::Color* extinction,
                                        scene_rdl2::math::Color* albedo,
                                        scene_rdl2::math::Color* temperature,
                                        bool highQuality,
                                        float /*rayVolumeDepth*/,
                                        const scene_rdl2::rdl2::VolumeShader* const) const override;

    virtual scene_rdl2::math::Color evalTemperature(mcrt_common::ThreadLocalState* tls,
                                                    uint32_t volumeId,
                                                    const Vec3f& pSample) const override;

    // query all volume intersections of this VdbVolume alone the ray
    // note that tfar is stored in volumeState already
    bool queryIntersections(const Vec3f& rayOrg,
                            const Vec3f& rayDir,
                            float tNear,
                            float time,
                            int threadIdx,
                            int volumeId,
                            VolumeRayState& volumeRayState,
                            bool computeRenderSpaceDistance);

    // query whether a given position is inside active field of vdb grid
    bool isInActiveField(uint32_t threadIdx,
                         const Vec3f& p,
                         float time) const;

    void initMotionBlurBoundary(openvdb::FloatGrid::Ptr& topologyGrid,
                                openvdb::VectorGrid::Ptr velocityGrid,
                                float tShutterOpen, float tShutterClose);

    void setInterpolation(geom::VdbVolume::Interpolation interpolation)
    {
        mInterpolationMode = static_cast<geom::internal::Interpolation>(interpolation);
    }

    void setVelocityScale(float velocityScale)
    {
        MNRY_ASSERT_REQUIRE(mVdbVolumeData,
                            "velocity scale can only be set before initialization");
        mVdbVolumeData->mVelocityScale = velocityScale;
    }

    void setVelocitySampleRate(float velocitySampleRate)
    {
        MNRY_ASSERT_REQUIRE(mVdbVolumeData,
                            "velocity sample rate can only be set before initialization");
        mVdbVolumeData->mVelocitySampleRate = velocitySampleRate;
    }

    void setEmissionSampleRate(float emissionSampleRate)
    {
        MNRY_ASSERT_REQUIRE(mVdbVolumeData,
                            "emission sample rate can only be set before initialization");
        mVdbVolumeData->mEmissionSampleRate = emissionSampleRate;
    }

    bool hasUniformVoxels() const
    {
        return mHasUniformVoxels;
    }

    bool hasEmissionField() const
    {
        return mHasEmissionField;
    }

    // we'll be empty if the vdb file is missing or contains no grids
    bool isEmpty() const
    {
        return mIsEmpty;
    }


    // This transformation is only used for baking the volume shader into a grid.
    // We only return the first time sample because currently volume shaders do not
    // blur with the geometry. Blurring VolumeShaders is a TODO if requested.
    const scene_rdl2::math::Mat4f& getTransform() const
    {
        return mPrimToRender[0];
    }

protected:
    bool initializePhase1(const std::string& vdbFilePath,
                          openvdb::io::File& file,
                          const scene_rdl2::rdl2::Geometry& rdlGeometry,
                          const scene_rdl2::rdl2::Layer* layer,
                          const VolumeAssignmentTable* volumeAssignmentTable,
                          std::vector<int>& volumeIds,
                          openvdb::GridPtrVecPtr& grids,
                          openvdb::VectorGrid::Ptr& velocityGrid);

    void initializePhase2(const scene_rdl2::rdl2::Geometry& rdlGeometry,
                          const scene_rdl2::rdl2::Layer* layer,
                          const VolumeAssignmentTable* volumeAssignmentTable,
                          const std::vector<int>& volumeIds,
                          openvdb::VectorGrid::Ptr& velocityGrid);

    virtual bool initialize(const scene_rdl2::rdl2::Geometry& rdlGeometry,
                            const scene_rdl2::rdl2::Layer* layer,
                            const VolumeAssignmentTable* volumeAssignmentTable);

    bool initDensitySampler(openvdb::io::File& file,
                            openvdb::GridPtrVecPtr grids,
                            const std::string& densityGridName,
                            const scene_rdl2::rdl2::Geometry& rdlGeometry,
                            openvdb::VectorGrid::Ptr velocityGrid,
                            const std::vector<int>& volumeIds);

    bool initEmissionSampler(openvdb::io::File& file,
                             openvdb::GridPtrVecPtr grids,
                             const std::string& emissionGridName,
                             const scene_rdl2::rdl2::Geometry& rdlGeometry,
                             openvdb::VectorGrid::Ptr velocityGrid,
                             const std::vector<int>& volumeIds);

    virtual void bakeVolumeShaderDensityMap(const scene_rdl2::rdl2::VolumeShader* volumeShader,
                                            const scene_rdl2::math::Mat4f& primToRender,
                                            const MotionBlurParams& motionBlurParams,
                                            const VolumeAssignmentTable* volumeAssignmentTable,
                                            const int assignmentId) override;

    scene_rdl2::math::Color sampleBakedDensity(mcrt_common::ThreadLocalState* tls,
                                               uint32_t volumeId,
                                               const openvdb::Vec3d& p) const;

protected:
    struct LinearGridTransform
    {
        LinearGridTransform(const scene_rdl2::math::Mat4f& indexToRender):
            mIndexToRender(indexToRender),
            mRenderToIndex(indexToRender.inverse())
        {}

        void appendXform(const scene_rdl2::math::Mat4f& xform)
        {
            mIndexToRender *= xform;
            mRenderToIndex = mIndexToRender.inverse();
        }

        scene_rdl2::math::Mat4f mIndexToRender;
        scene_rdl2::math::Mat4f mRenderToIndex;
    };

    // Use this to transform render space position to vdb world space.
    // We use two time samples for transformation motion blur.
    // When the VdbVolume is a shared primitive (i.e. when instancing),
    // this member will be the indentity matrix.
    scene_rdl2::math::Mat4f mRenderToPrim[2];

    // vdb world space to render space for baked volume shader grid
    // We use two time samples for transformation motion blur.
    // If not a shared primitive (i.e when not instancing), this is just
    // the inverse of mRenderToPrim.  However, with instancing this is
    // the world2Render xform.  For motivation, think of a shared primitive
    // as existing at the world origin when running shader networks.  We
    // do something similar when evaluating displacement maps on shared
    // primitives.
    scene_rdl2::math::Mat4f mPrimToRender[2];

    // 8 vertices to form a bounding box in render space; 2 time samples for transformation motion blur
    Vec3fa mBBoxVertices[8 * 2];

    // 4 * 6 = 24 index values for a bounding box (same topology for both time samples)
    int mBBoxIndices[24];

    // this member is only valid when the grid has linear transform
    // We use two time samples for transformation motion blur.
    std::unique_ptr<LinearGridTransform> mLinearTransform[2];

    // this member is only valid when the grid uses non-uniform voxels
    std::unique_ptr<DDAIntersector> mDDAIntersector;

    // the active voxels that we should ray trace
    openvdb::FloatGrid::Ptr mTopologyGrid;
    std::vector<openvdb::FloatGrid::ConstAccessor> mTopologyAccessors;
    std::vector<openvdb::tools::VolumeRayIntersector<openvdb::FloatGrid>> mTopologyIntersectors;

    bool mHasUniformVoxels;

    VDBSampler<openvdb::FloatGrid> mDensitySampler;

    bool mHasEmissionField;
    openvdb::Vec3SGrid::Ptr mEmissionGrid;
    VDBSampler<openvdb::Vec3SGrid> mEmissionSampler;

    Interpolation mInterpolationMode;

    // empty vdb?
    bool mIsEmpty;
    std::unique_ptr<VdbVolumeData> mVdbVolumeData;

    bool mIsMotionBlurOn;
};


} // namespace internal
} // namespace geom
} // namespace moonray


