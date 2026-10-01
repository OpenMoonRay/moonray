// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "TestRdlGeometry.h"

#include <moonray/rendering/geom/Curves.h>
#include <moonray/rendering/geom/MotionBlurParams.h>
#include <moonray/rendering/geom/Points.h>
#include <moonray/rendering/geom/PolygonMesh.h>
#include <moonray/rendering/geom/PrimitiveVisitor.h>
#include <moonray/rendering/geom/Procedural.h>
#include <moonray/rendering/geom/ProceduralContext.h>
#include <moonray/rendering/geom/SubdivisionMesh.h>
#include <moonray/rendering/geom/prim/Curves.h>
#include <moonray/rendering/geom/prim/Points.h>
#include <moonray/rendering/geom/prim/PolyMesh.h>
#include <moonray/rendering/geom/prim/PrimitivePrivateAccess.h>
#include <moonray/rendering/geom/prim/SubdMesh.h>
#include <moonray/rendering/shading/AttributeKey.h>

#include <scene_rdl2/common/except/exceptions.h>
#include <scene_rdl2/common/math/Xform.h>
#include <scene_rdl2/scene/rdl2/Attribute.h>
#include <scene_rdl2/scene/rdl2/Geometry.h>
#include <scene_rdl2/scene/rdl2/Layer.h>
#include <scene_rdl2/scene/rdl2/LightSet.h>
#include <scene_rdl2/scene/rdl2/Material.h>
#include <scene_rdl2/scene/rdl2/SceneClass.h>
#include <scene_rdl2/scene/rdl2/SceneContext.h>
#include <scene_rdl2/scene/rdl2/UserData.h>

#include <set>
#include <string>
#include <vector>

namespace moonray {
namespace geom {
namespace unittest {

namespace {

using scene_rdl2::rdl2::Geometry;
using scene_rdl2::rdl2::SceneContext;
using scene_rdl2::rdl2::SceneObjectVector;
using scene_rdl2::rdl2::UserData;

// values of RdlGeometry's "geo_type"
constexpr int GEO_TYPE_MESH = 0;
constexpr int GEO_TYPE_CURVES = 1;
constexpr int GEO_TYPE_POINTS = 2;

const std::string sPrimvarName("test_primvar");

// Minimal GenerateContext, so the test doesn't need a GeometryManager
class TestGenerateContext : public GenerateContext
{
public:
    TestGenerateContext(const scene_rdl2::rdl2::Layer* rdlLayer,
                        const scene_rdl2::rdl2::Geometry* rdlGeometry,
                        bool motionBlur)
        : mRdlLayer(rdlLayer)
        , mRdlGeometry(rdlGeometry)
        , mMotionBlurParams(motionBlur ? MotionBlurParams({0.f, 1.f}, 0.f, 1.f, true, 24.f)
                                       : MotionBlurParams({0.f}, 0.f, 0.f, false, 24.f))
    {}

    const scene_rdl2::rdl2::Layer* getRdlLayer() const override { return mRdlLayer; }
    const scene_rdl2::rdl2::Geometry* getRdlGeometry() const override { return mRdlGeometry; }
    int getCurrentFrame() const override { return 0; }
    int getThreads() const override { return 1; }
    const MotionBlurParams& getMotionBlurParams() const override { return mMotionBlurParams; }
    const std::vector<float>& getMotionSteps() const override { return mMotionBlurParams.getMotionSteps(); }
    float getShutterOpen() const override { return mMotionBlurParams.getShutterOpen(); }
    float getShutterClose() const override { return mMotionBlurParams.getShutterClose(); }
    bool isMotionBlurOn() const override { return mMotionBlurParams.isMotionBlurOn(); }
    void getMotionBlurDelta(float& shutterOpenDelta, float& shutterCloseDelta) const override
    {
        mMotionBlurParams.getMotionBlurDelta(shutterOpenDelta, shutterCloseDelta);
    }
    const shading::AttributeKeySet& getRequestedAttributes() const override { return mRequestedAttributes; }
    bool requestAttribute(const shading::AttributeKey&) const override { return false; }

private:
    const scene_rdl2::rdl2::Layer* mRdlLayer;
    const scene_rdl2::rdl2::Geometry* mRdlGeometry;
    MotionBlurParams mMotionBlurParams;
    shading::AttributeKeySet mRequestedAttributes;
};

// Finds the single primitive a Rdl*Geometry procedural generates
class PrimitiveFinder : public PrimitiveVisitor
{
public:
    void visitCurves(Curves& p) override { found(p, "Curves"); }
    void visitPoints(Points& p) override { found(p, "Points"); }
    void visitPolygonMesh(PolygonMesh& p) override { found(p, "PolygonMesh"); }
    void visitSubdivisionMesh(SubdivisionMesh& p) override { found(p, "SubdivisionMesh"); }

    Primitive* mPrimitive = nullptr;
    std::string mType;
    int mCount = 0;

private:
    void found(Primitive& p, const std::string& type)
    {
        mPrimitive = &p;
        mType = type;
        ++mCount;
    }
};

PrimitiveFinder
findPrimitive(Geometry* geometry)
{
    PrimitiveFinder finder;
    CPPUNIT_ASSERT(geometry->getProcedural());
    geometry->getProcedural()->forEachPrimitive(finder, /*parallel=*/false);
    return finder;
}

// The names of the primitive attributes of a primitive, before tessellation
std::set<std::string>
getPrimitiveAttributeNames(Primitive& primitive)
{
    const internal::Primitive* impl = internal::PrimitivePrivateAccess::getPrimitiveImpl(&primitive);
    const shading::PrimitiveAttributeTable* table = nullptr;
    if (auto polyMesh = dynamic_cast<const internal::PolyMesh*>(impl)) {
        table = polyMesh->getPrimitiveAttributeTable();
    } else if (auto subdMesh = dynamic_cast<const internal::SubdMesh*>(impl)) {
        table = &subdMesh->getPrimitiveAttributeTable();
    } else if (auto curves = dynamic_cast<const internal::Curves*>(impl)) {
        table = curves->getPrimitiveAttributeTable();
    } else if (auto points = dynamic_cast<const internal::Points*>(impl)) {
        table = points->getPrimitiveAttributeTable();
    }
    CPPUNIT_ASSERT(table);

    std::set<std::string> names;
    for (const auto& kv : *table) {
        names.insert(kv.first.getName());
    }
    return names;
}

template <typename F>
UserData*
createUserData(SceneContext& ctx, const std::string& name, F setData)
{
    UserData* userData = ctx.createSceneObject("UserData", name)->asA<UserData>();
    userData->beginUpdate();
    setData(userData);
    userData->endUpdate();
    return userData;
}

Geometry*
createRdlGeometry(SceneContext& ctx, const std::string& name, int geoType, const SceneObjectVector& data)
{
    Geometry* geometry = ctx.createSceneObject("RdlGeometry", name)->asA<Geometry>();
    geometry->beginUpdate();
    geometry->set("geo_type", geoType);
    geometry->set("data", data);
    geometry->endUpdate();
    return geometry;
}

// Assigns the geometry and generates it, exactly as
// GeometryManager::loadGeometries() does
void
generate(scene_rdl2::rdl2::Layer* layer,
         scene_rdl2::rdl2::Material* material,
         scene_rdl2::rdl2::LightSet* lightSet,
         Geometry* geometry,
         bool motionBlur = false)
{
    geometry->applyUpdates();

    layer->beginUpdate();
    layer->assign(geometry, "", material, lightSet);
    layer->endUpdate();
    layer->applyUpdates();

    if (!geometry->getProcedural()) {
        geometry->loadProcedural();
    }
    TestGenerateContext generateContext(layer, geometry, motionBlur);
    geometry->getProcedural()->clear();
    geometry->getProcedural()->generate(generateContext, {scene_rdl2::math::Xform3f(scene_rdl2::math::one)});
}

// An n x n grid of quads: n*n vertices, (n-1)*(n-1) faces
struct GridMesh
{
    explicit GridMesh(int n)
    {
        for (int y = 0; y < n; ++y) {
            for (int x = 0; x < n; ++x) {
                mPositions.emplace_back(float(x), float(y), 0.0f);
                mColors.emplace_back(float(x) / n, float(y) / n, 0.0f);
            }
        }
        for (int y = 0; y < n - 1; ++y) {
            for (int x = 0; x < n - 1; ++x) {
                mFaceVertexCount.push_back(4);
                mIndices.push_back(y * n + x);
                mIndices.push_back(y * n + x + 1);
                mIndices.push_back((y + 1) * n + x + 1);
                mIndices.push_back((y + 1) * n + x);
                for (int i = 0; i < 4; ++i) {
                    mUVs.emplace_back(float(x) / n, float(y) / n);
                }
            }
        }
    }

    scene_rdl2::rdl2::IntVector mFaceVertexCount;
    scene_rdl2::rdl2::IntVector mIndices;
    scene_rdl2::rdl2::Vec3fVector mPositions;
    scene_rdl2::rdl2::Vec3fVector mColors; // per vertex
    scene_rdl2::rdl2::Vec2fVector mUVs;    // per face-vertex
};

// Linear curves, each with the same number of vertices
struct LinearCurves
{
    LinearCurves(int curveCount, int vertsPerCurve)
        : mCurvesVertexCount(curveCount, vertsPerCurve)
    {
        for (int c = 0; c < curveCount; ++c) {
            for (int v = 0; v < vertsPerCurve; ++v) {
                mPositions.emplace_back(float(v), float(c), 0.0f);
                mRadii.push_back(0.1f + 0.01f * v);
            }
            mColors.emplace_back(float(c), 0.0f, 0.0f);
        }
    }

    scene_rdl2::rdl2::IntVector mCurvesVertexCount;
    scene_rdl2::rdl2::Vec3fVector mPositions;
    scene_rdl2::rdl2::FloatVector mRadii;  // per vertex
    scene_rdl2::rdl2::Vec3fVector mColors; // per curve
};

struct PointCloud
{
    explicit PointCloud(int count)
    {
        for (int i = 0; i < count; ++i) {
            mPositions.emplace_back(float(i), 0.0f, 0.0f);
            mRadii.push_back(0.1f + 0.001f * i);
            mColors.emplace_back(float(i) / count, 0.0f, 0.0f);
        }
    }

    scene_rdl2::rdl2::Vec3fVector mPositions;
    scene_rdl2::rdl2::FloatVector mRadii;
    scene_rdl2::rdl2::Vec3fVector mColors;
};

// The UserData for an RdlGeometry equivalent to the given mesh, followed by
// a primitive attribute
SceneObjectVector
createMeshData(SceneContext& ctx, const std::string& prefix, const GridMesh& mesh)
{
    return {
        createUserData(ctx, prefix + "_fvc", [&](UserData* ud) {
            ud->setIntData("face_vertex_count", mesh.mFaceVertexCount); }),
        createUserData(ctx, prefix + "_indices", [&](UserData* ud) {
            ud->setIntData("vertices_by_index", mesh.mIndices); }),
        createUserData(ctx, prefix + "_positions", [&](UserData* ud) {
            ud->setVec3fData("vertex_list", mesh.mPositions); }),
        createUserData(ctx, prefix + "_uvs", [&](UserData* ud) {
            ud->setVec2fData("uv_list", mesh.mUVs); }),
        createUserData(ctx, prefix + "_primvar", [&](UserData* ud) {
            ud->setVec3fData(sPrimvarName, mesh.mColors); }),
    };
}

} // anonymous namespace

void
TestRdlGeometry::setUp()
{
    // Normally done by the RenderContext. Without it the standard keys (e.g.
    // velocity) alias whichever key happens to be registered first.
    shading::StandardAttributes::init();

    mContext.reset(new SceneContext);
    mContext->setDsoPath(mContext->getDsoPath() + ":.");
    mLayer = mContext->createSceneObject("Layer", "/seq/shot/layer")->asA<scene_rdl2::rdl2::Layer>();
    mMaterial = mContext->createSceneObject("TestMaterial", "mtl")->asA<scene_rdl2::rdl2::Material>();
    mLightSet = mContext->createSceneObject("LightSet", "lgt")->asA<scene_rdl2::rdl2::LightSet>();
}

void
TestRdlGeometry::tearDown()
{
    mContext.reset();
}

namespace {

void
checkMeshMatchesRdlMesh(SceneContext& ctx,
                        scene_rdl2::rdl2::Layer* layer,
                        scene_rdl2::rdl2::Material* material,
                        scene_rdl2::rdl2::LightSet* lightSet,
                        bool isSubd)
{
    const GridMesh mesh(20);

    // the reference RdlMeshGeometry
    UserData* primvar = createUserData(ctx, "rdlMeshPrimvar", [&](UserData* ud) {
        ud->setVec3fData(sPrimvarName, mesh.mColors); });
    Geometry* rdlMesh = ctx.createSceneObject("RdlMeshGeometry", "rdlMesh")->asA<Geometry>();
    rdlMesh->beginUpdate();
    rdlMesh->set("face_vertex_count", mesh.mFaceVertexCount);
    rdlMesh->set("vertices_by_index", mesh.mIndices);
    rdlMesh->set("vertex_list_0", mesh.mPositions);
    rdlMesh->set("uv_list", mesh.mUVs);
    rdlMesh->set("is_subd", isSubd);
    rdlMesh->set("primitive_attributes", SceneObjectVector{primvar});
    rdlMesh->endUpdate();
    generate(layer, material, lightSet, rdlMesh);

    SceneObjectVector data = createMeshData(ctx, "rdlGeometry", mesh);
    data.push_back(createUserData(ctx, "rdlGeometryIsSubd", [&](UserData* ud) {
        ud->setBoolData("is_subd", {isSubd}); }));
    Geometry* rdlGeometry = createRdlGeometry(ctx, "rdlGeometry", GEO_TYPE_MESH, data);
    generate(layer, material, lightSet, rdlGeometry);

    PrimitiveFinder expected = findPrimitive(rdlMesh);
    PrimitiveFinder actual = findPrimitive(rdlGeometry);
    CPPUNIT_ASSERT_EQUAL(1, expected.mCount);
    CPPUNIT_ASSERT_EQUAL(1, actual.mCount);
    CPPUNIT_ASSERT_EQUAL(std::string(isSubd ? "SubdivisionMesh" : "PolygonMesh"), actual.mType);
    CPPUNIT_ASSERT_EQUAL(expected.mType, actual.mType);
    CPPUNIT_ASSERT_EQUAL(rdlMesh->getProcedural()->getMemory(), rdlGeometry->getProcedural()->getMemory());
    if (!isSubd) {
        CPPUNIT_ASSERT_EQUAL(static_cast<PolygonMesh*>(expected.mPrimitive)->getFaceCount(),
                             static_cast<PolygonMesh*>(actual.mPrimitive)->getFaceCount());
        CPPUNIT_ASSERT_EQUAL(static_cast<PolygonMesh*>(expected.mPrimitive)->getVertexCount(),
                             static_cast<PolygonMesh*>(actual.mPrimitive)->getVertexCount());
    }
    CPPUNIT_ASSERT(getPrimitiveAttributeNames(*expected.mPrimitive) ==
                   getPrimitiveAttributeNames(*actual.mPrimitive));
    CPPUNIT_ASSERT(getPrimitiveAttributeNames(*actual.mPrimitive).count(sPrimvarName));
}

} // anonymous namespace

void
TestRdlGeometry::testPolygonMeshMatchesRdlMesh()
{
    checkMeshMatchesRdlMesh(*mContext, mLayer, mMaterial, mLightSet, /*isSubd=*/false);
}

void
TestRdlGeometry::testSubdMeshMatchesRdlMesh()
{
    checkMeshMatchesRdlMesh(*mContext, mLayer, mMaterial, mLightSet, /*isSubd=*/true);
}

void
TestRdlGeometry::testCurvesMatchRdlCurve()
{
    const LinearCurves curves(8, 6);

    UserData* primvar = createUserData(*mContext, "rdlCurvePrimvar", [&](UserData* ud) {
        ud->setVec3fData(sPrimvarName, curves.mColors); });
    Geometry* rdlCurve = mContext->createSceneObject("RdlCurveGeometry", "rdlCurve")->asA<Geometry>();
    rdlCurve->beginUpdate();
    rdlCurve->set("curves_vertex_count", curves.mCurvesVertexCount);
    rdlCurve->set("vertex_list_0", curves.mPositions);
    rdlCurve->set("radius_list", curves.mRadii);
    rdlCurve->set("curve_type", 0); // linear
    rdlCurve->set("primitive_attributes", SceneObjectVector{primvar});
    rdlCurve->endUpdate();
    generate(mLayer, mMaterial, mLightSet, rdlCurve);

    const SceneObjectVector data = {
        createUserData(*mContext, "counts", [&](UserData* ud) {
            ud->setIntData("curves_vertex_count", curves.mCurvesVertexCount); }),
        createUserData(*mContext, "positions", [&](UserData* ud) {
            ud->setVec3fData("vertex_list", curves.mPositions); }),
        createUserData(*mContext, "radii", [&](UserData* ud) {
            ud->setFloatData("radius_list", curves.mRadii); }),
        createUserData(*mContext, "curveType", [&](UserData* ud) {
            ud->setIntData("curve_type", {0}); }),
        createUserData(*mContext, "primvar", [&](UserData* ud) {
            ud->setVec3fData(sPrimvarName, curves.mColors); }),
    };
    Geometry* rdlGeometry = createRdlGeometry(*mContext, "rdlGeometry", GEO_TYPE_CURVES, data);
    generate(mLayer, mMaterial, mLightSet, rdlGeometry);

    PrimitiveFinder expected = findPrimitive(rdlCurve);
    PrimitiveFinder actual = findPrimitive(rdlGeometry);
    CPPUNIT_ASSERT_EQUAL(1, actual.mCount);
    CPPUNIT_ASSERT_EQUAL(std::string("Curves"), actual.mType);
    CPPUNIT_ASSERT_EQUAL(rdlCurve->getProcedural()->getMemory(), rdlGeometry->getProcedural()->getMemory());
    CPPUNIT_ASSERT(getPrimitiveAttributeNames(*expected.mPrimitive) ==
                   getPrimitiveAttributeNames(*actual.mPrimitive));
    CPPUNIT_ASSERT(getPrimitiveAttributeNames(*actual.mPrimitive).count(sPrimvarName));
}

void
TestRdlGeometry::testPointsMatchRdlPoint()
{
    const PointCloud points(100);

    UserData* primvar = createUserData(*mContext, "rdlPointPrimvar", [&](UserData* ud) {
        ud->setVec3fData(sPrimvarName, points.mColors); });
    Geometry* rdlPoint = mContext->createSceneObject("RdlPointGeometry", "rdlPoint")->asA<Geometry>();
    rdlPoint->beginUpdate();
    rdlPoint->set("vertex_list_0", points.mPositions);
    rdlPoint->set("radius_list", points.mRadii);
    rdlPoint->set("primitive_attributes", SceneObjectVector{primvar});
    rdlPoint->endUpdate();
    generate(mLayer, mMaterial, mLightSet, rdlPoint);

    const SceneObjectVector data = {
        createUserData(*mContext, "positions", [&](UserData* ud) {
            ud->setVec3fData("vertex_list", points.mPositions); }),
        createUserData(*mContext, "radii", [&](UserData* ud) {
            ud->setFloatData("radius_list", points.mRadii); }),
        createUserData(*mContext, "primvar", [&](UserData* ud) {
            ud->setVec3fData(sPrimvarName, points.mColors); }),
    };
    Geometry* rdlGeometry = createRdlGeometry(*mContext, "rdlGeometry", GEO_TYPE_POINTS, data);
    generate(mLayer, mMaterial, mLightSet, rdlGeometry);

    PrimitiveFinder expected = findPrimitive(rdlPoint);
    PrimitiveFinder actual = findPrimitive(rdlGeometry);
    CPPUNIT_ASSERT_EQUAL(1, actual.mCount);
    CPPUNIT_ASSERT_EQUAL(std::string("Points"), actual.mType);
    CPPUNIT_ASSERT_EQUAL(rdlPoint->getProcedural()->getMemory(), rdlGeometry->getProcedural()->getMemory());
    CPPUNIT_ASSERT(getPrimitiveAttributeNames(*expected.mPrimitive) ==
                   getPrimitiveAttributeNames(*actual.mPrimitive));
    CPPUNIT_ASSERT(getPrimitiveAttributeNames(*actual.mPrimitive).count(sPrimvarName));
}

namespace {

// Offsets every position, for a second motion step or velocities
scene_rdl2::rdl2::Vec3fVector
offset(const scene_rdl2::rdl2::Vec3fVector& positions, const scene_rdl2::math::Vec3f& delta)
{
    scene_rdl2::rdl2::Vec3fVector result;
    for (const auto& p : positions) {
        result.push_back(p + delta);
    }
    return result;
}

// Checks that RdlGeometry built the same motion blurred primitive as the
// equivalent Rdl*Geometry
void
checkMotionBlurMatches(Geometry* rdlReference, Geometry* rdlGeometry)
{
    PrimitiveFinder expected = findPrimitive(rdlReference);
    PrimitiveFinder actual = findPrimitive(rdlGeometry);
    CPPUNIT_ASSERT_EQUAL(1, expected.mCount);
    CPPUNIT_ASSERT_EQUAL(1, actual.mCount);
    CPPUNIT_ASSERT_EQUAL(expected.mType, actual.mType);
    CPPUNIT_ASSERT(expected.mPrimitive->getMotionSamplesCount() > 1);
    CPPUNIT_ASSERT_EQUAL(expected.mPrimitive->getMotionSamplesCount(), actual.mPrimitive->getMotionSamplesCount());
    CPPUNIT_ASSERT_EQUAL(rdlReference->getProcedural()->getMemory(), rdlGeometry->getProcedural()->getMemory());
    CPPUNIT_ASSERT(getPrimitiveAttributeNames(*expected.mPrimitive) ==
                   getPrimitiveAttributeNames(*actual.mPrimitive));
}

} // anonymous namespace

void
TestRdlGeometry::testMeshMotionStepsMatchRdlMesh()
{
    // hermite motion blur uses both motion steps of the positions and the velocities
    const GridMesh mesh(10);
    const scene_rdl2::rdl2::Vec3fVector positions1 = offset(mesh.mPositions, {0.1f, 0.f, 0.f});
    const scene_rdl2::rdl2::Vec3fVector velocities0(mesh.mPositions.size(), scene_rdl2::math::Vec3f(1.f, 0.f, 0.f));
    const scene_rdl2::rdl2::Vec3fVector velocities1(mesh.mPositions.size(), scene_rdl2::math::Vec3f(1.f, 0.5f, 0.f));
    const int hermite = static_cast<int>(scene_rdl2::rdl2::MotionBlurType::HERMITE);

    Geometry* rdlMesh = mContext->createSceneObject("RdlMeshGeometry", "rdlMesh")->asA<Geometry>();
    rdlMesh->beginUpdate();
    rdlMesh->set("face_vertex_count", mesh.mFaceVertexCount);
    rdlMesh->set("vertices_by_index", mesh.mIndices);
    rdlMesh->set("vertex_list_0", mesh.mPositions);
    rdlMesh->set("vertex_list_1", positions1);
    rdlMesh->set("velocity_list_0", velocities0);
    rdlMesh->set("velocity_list_1", velocities1);
    rdlMesh->set("motion_blur_type", hermite);
    rdlMesh->set("is_subd", false);
    rdlMesh->endUpdate();
    generate(mLayer, mMaterial, mLightSet, rdlMesh, /*motionBlur=*/true);

    const SceneObjectVector data = {
        createUserData(*mContext, "fvc", [&](UserData* ud) {
            ud->setIntData("face_vertex_count", mesh.mFaceVertexCount); }),
        createUserData(*mContext, "indices", [&](UserData* ud) {
            ud->setIntData("vertices_by_index", mesh.mIndices); }),
        createUserData(*mContext, "positions", [&](UserData* ud) {
            ud->setVec3fData("vertex_list", mesh.mPositions, positions1); }),
        createUserData(*mContext, "velocities", [&](UserData* ud) {
            ud->setVec3fData("velocity_list", velocities0, velocities1); }),
        createUserData(*mContext, "motionBlurType", [&](UserData* ud) {
            ud->setIntData("motion_blur_type", {hermite}); }),
        createUserData(*mContext, "isSubd", [](UserData* ud) {
            ud->setBoolData("is_subd", {false}); }),
    };
    Geometry* rdlGeometry = createRdlGeometry(*mContext, "rdlGeometry", GEO_TYPE_MESH, data);
    generate(mLayer, mMaterial, mLightSet, rdlGeometry, /*motionBlur=*/true);

    checkMotionBlurMatches(rdlMesh, rdlGeometry);
    CPPUNIT_ASSERT(getPrimitiveAttributeNames(*findPrimitive(rdlGeometry).mPrimitive).count("velocity"));
}

void
TestRdlGeometry::testCurvesMotionStepsMatchRdlCurve()
{
    // acceleration motion blur uses only the first motion step of the velocities
    const LinearCurves curves(4, 5);
    const scene_rdl2::rdl2::Vec3fVector velocities0(curves.mPositions.size(), scene_rdl2::math::Vec3f(0.f, 1.f, 0.f));
    const scene_rdl2::rdl2::Vec3fVector accelerations(curves.mPositions.size(), scene_rdl2::math::Vec3f(0.f, 0.f, 2.f));
    const int acceleration = static_cast<int>(scene_rdl2::rdl2::MotionBlurType::ACCELERATION);

    Geometry* rdlCurve = mContext->createSceneObject("RdlCurveGeometry", "rdlCurve")->asA<Geometry>();
    rdlCurve->beginUpdate();
    rdlCurve->set("curves_vertex_count", curves.mCurvesVertexCount);
    rdlCurve->set("vertex_list_0", curves.mPositions);
    rdlCurve->set("velocity_list_0", velocities0);
    rdlCurve->set("accleration_list", accelerations);
    rdlCurve->set("motion_blur_type", acceleration);
    rdlCurve->set("curve_type", 0);
    rdlCurve->endUpdate();
    generate(mLayer, mMaterial, mLightSet, rdlCurve, /*motionBlur=*/true);

    const SceneObjectVector data = {
        createUserData(*mContext, "counts", [&](UserData* ud) {
            ud->setIntData("curves_vertex_count", curves.mCurvesVertexCount); }),
        createUserData(*mContext, "positions", [&](UserData* ud) {
            ud->setVec3fData("vertex_list", curves.mPositions); }),
        createUserData(*mContext, "velocities", [&](UserData* ud) {
            ud->setVec3fData("velocity_list", velocities0); }),
        createUserData(*mContext, "accelerations", [&](UserData* ud) {
            ud->setVec3fData("accleration_list", accelerations); }),
        createUserData(*mContext, "motionBlurType", [&](UserData* ud) {
            ud->setIntData("motion_blur_type", {acceleration}); }),
        createUserData(*mContext, "curveType", [](UserData* ud) {
            ud->setIntData("curve_type", {0}); }),
    };
    Geometry* rdlGeometry = createRdlGeometry(*mContext, "rdlGeometry", GEO_TYPE_CURVES, data);
    generate(mLayer, mMaterial, mLightSet, rdlGeometry, /*motionBlur=*/true);

    checkMotionBlurMatches(rdlCurve, rdlGeometry);
}

void
TestRdlGeometry::testPointsMotionStepsMatchRdlPoint()
{
    // frame delta motion blur uses both motion steps of the positions
    const PointCloud points(50);
    const scene_rdl2::rdl2::Vec3fVector positions1 = offset(points.mPositions, {0.f, 0.2f, 0.f});
    const int frameDelta = static_cast<int>(scene_rdl2::rdl2::MotionBlurType::FRAME_DELTA);

    Geometry* rdlPoint = mContext->createSceneObject("RdlPointGeometry", "rdlPoint")->asA<Geometry>();
    rdlPoint->beginUpdate();
    rdlPoint->set("vertex_list_0", points.mPositions);
    rdlPoint->set("vertex_list_1", positions1);
    rdlPoint->set("motion_blur_type", frameDelta);
    rdlPoint->endUpdate();
    generate(mLayer, mMaterial, mLightSet, rdlPoint, /*motionBlur=*/true);

    const SceneObjectVector data = {
        createUserData(*mContext, "positions", [&](UserData* ud) {
            ud->setVec3fData("vertex_list", points.mPositions, positions1); }),
        createUserData(*mContext, "motionBlurType", [&](UserData* ud) {
            ud->setIntData("motion_blur_type", {frameDelta}); }),
    };
    Geometry* rdlGeometry = createRdlGeometry(*mContext, "rdlGeometry", GEO_TYPE_POINTS, data);
    generate(mLayer, mMaterial, mLightSet, rdlGeometry, /*motionBlur=*/true);

    checkMotionBlurMatches(rdlPoint, rdlGeometry);
}

void
TestRdlGeometry::testPerMotionStepKeysAreIgnored()
{
    const GridMesh mesh(5);
    const int frameDelta = static_cast<int>(scene_rdl2::rdl2::MotionBlurType::FRAME_DELTA);

    // "vertex_list_0" doesn't supply the positions, so there's no mesh
    SceneObjectVector data = {
        createUserData(*mContext, "fvc", [&](UserData* ud) {
            ud->setIntData("face_vertex_count", mesh.mFaceVertexCount); }),
        createUserData(*mContext, "indices", [&](UserData* ud) {
            ud->setIntData("vertices_by_index", mesh.mIndices); }),
        createUserData(*mContext, "positions0", [&](UserData* ud) {
            ud->setVec3fData("vertex_list_0", mesh.mPositions); }),
        createUserData(*mContext, "isSubd", [](UserData* ud) {
            ud->setBoolData("is_subd", {false}); }),
    };
    Geometry* noPositions = createRdlGeometry(*mContext, "noPositions", GEO_TYPE_MESH, data);
    generate(mLayer, mMaterial, mLightSet, noPositions, /*motionBlur=*/true);
    CPPUNIT_ASSERT_EQUAL(0, findPrimitive(noPositions).mCount);

    // "vertex_list_1" doesn't supply a second motion step, so the mesh is
    // static, just like a mesh with no second motion step at all
    data[2] = createUserData(*mContext, "positions", [&](UserData* ud) {
        ud->setVec3fData("vertex_list", mesh.mPositions); });
    data.push_back(createUserData(*mContext, "motionBlurType", [&](UserData* ud) {
        ud->setIntData("motion_blur_type", {frameDelta}); }));
    Geometry* staticMesh = createRdlGeometry(*mContext, "staticMesh", GEO_TYPE_MESH, data);
    generate(mLayer, mMaterial, mLightSet, staticMesh, /*motionBlur=*/true);

    data.push_back(createUserData(*mContext, "positions1", [&](UserData* ud) {
        ud->setVec3fData("vertex_list_1", offset(mesh.mPositions, {0.1f, 0.f, 0.f})); }));
    Geometry* perStepMesh = createRdlGeometry(*mContext, "perStepMesh", GEO_TYPE_MESH, data);
    generate(mLayer, mMaterial, mLightSet, perStepMesh, /*motionBlur=*/true);

    PrimitiveFinder expected = findPrimitive(staticMesh);
    PrimitiveFinder actual = findPrimitive(perStepMesh);
    CPPUNIT_ASSERT_EQUAL(1, actual.mCount);
    CPPUNIT_ASSERT_EQUAL(expected.mPrimitive->getMotionSamplesCount(), actual.mPrimitive->getMotionSamplesCount());
    CPPUNIT_ASSERT_EQUAL(staticMesh->getProcedural()->getMemory(), perStepMesh->getProcedural()->getMemory());
    CPPUNIT_ASSERT(!getPrimitiveAttributeNames(*actual.mPrimitive).count("vertex_list_1"));
}

void
TestRdlGeometry::testScalarData()
{
    const GridMesh mesh(5);

    // "is_subd" defaults to true, like RdlMeshGeometry
    Geometry* byDefault = createRdlGeometry(*mContext, "byDefault", GEO_TYPE_MESH,
        createMeshData(*mContext, "byDefault", mesh));
    generate(mLayer, mMaterial, mLightSet, byDefault);
    CPPUNIT_ASSERT_EQUAL(std::string("SubdivisionMesh"), findPrimitive(byDefault).mType);

    // single-valued attributes use the first value
    SceneObjectVector data = createMeshData(*mContext, "boolData", mesh);
    data.push_back(createUserData(*mContext, "boolIsSubd", [](UserData* ud) {
        ud->setBoolData("is_subd", {false}); }));
    Geometry* boolData = createRdlGeometry(*mContext, "boolData", GEO_TYPE_MESH, data);
    generate(mLayer, mMaterial, mLightSet, boolData);
    CPPUNIT_ASSERT_EQUAL(std::string("PolygonMesh"), findPrimitive(boolData).mType);

    // values must be the type of the attribute, as they would have to be on
    // RdlMeshGeometry: an int "is_subd" is an error, so nothing is generated
    data = createMeshData(*mContext, "intData", mesh);
    data.push_back(createUserData(*mContext, "intIsSubd", [](UserData* ud) {
        ud->setIntData("is_subd", {0}); }));
    Geometry* intData = createRdlGeometry(*mContext, "intData", GEO_TYPE_MESH, data);
    generate(mLayer, mMaterial, mLightSet, intData);
    CPPUNIT_ASSERT_EQUAL(0, findPrimitive(intData).mCount);

    // as is an int "mesh_resolution", which is a Float attribute
    data = createMeshData(*mContext, "intFloat", mesh);
    data.push_back(createUserData(*mContext, "intMeshResolution", [](UserData* ud) {
        ud->setIntData("mesh_resolution", {4}); }));
    Geometry* intFloat = createRdlGeometry(*mContext, "intFloat", GEO_TYPE_MESH, data);
    generate(mLayer, mMaterial, mLightSet, intFloat);
    CPPUNIT_ASSERT_EQUAL(0, findPrimitive(intFloat).mCount);

    // as are quaternions packed into floats for the Vec4fVector
    // "local_motion_blur_orient_list"
    data = createMeshData(*mContext, "floatOrient", mesh);
    data.push_back(createUserData(*mContext, "floatOrientList", [](UserData* ud) {
        ud->setFloatData("local_motion_blur_orient_list", {0.f, 0.f, 0.f, 1.f}); }));
    Geometry* floatOrient = createRdlGeometry(*mContext, "floatOrient", GEO_TYPE_MESH, data);
    generate(mLayer, mMaterial, mLightSet, floatOrient);
    CPPUNIT_ASSERT_EQUAL(0, findPrimitive(floatOrient).mCount);

    // primitive attributes don't support vec4f, so other vec4f keys are ignored
    data = createMeshData(*mContext, "vec4fPrimvar", mesh);
    data.push_back(createUserData(*mContext, "vec4fPrimvarData", [](UserData* ud) {
        ud->setVec4fData("vec4f_primvar", {scene_rdl2::math::Vec4f(0.f, 0.f, 0.f, 1.f)}); }));
    Geometry* vec4fPrimvar = createRdlGeometry(*mContext, "vec4fPrimvar", GEO_TYPE_MESH, data);
    generate(mLayer, mMaterial, mLightSet, vec4fPrimvar);
    const PrimitiveFinder vec4fPrimvarPrimitive = findPrimitive(vec4fPrimvar);
    CPPUNIT_ASSERT_EQUAL(1, vec4fPrimvarPrimitive.mCount);
    CPPUNIT_ASSERT(!getPrimitiveAttributeNames(*vec4fPrimvarPrimitive.mPrimitive).count("vec4f_primvar"));
    CPPUNIT_ASSERT(getPrimitiveAttributeNames(*vec4fPrimvarPrimitive.mPrimitive).count(sPrimvarName));

    // aliases work too
    data = {
        createUserData(*mContext, "aliasFvc", [&](UserData* ud) {
            ud->setIntData("face vertex count", mesh.mFaceVertexCount); }),
        createUserData(*mContext, "aliasIndices", [&](UserData* ud) {
            ud->setIntData("vertices_by_index", mesh.mIndices); }),
        createUserData(*mContext, "aliasPositions", [&](UserData* ud) {
            ud->setVec3fData("vertex list", mesh.mPositions); }),
        createUserData(*mContext, "aliasIsSubd", [](UserData* ud) {
            ud->setBoolData("is subd", {false}); }),
    };
    Geometry* alias = createRdlGeometry(*mContext, "alias", GEO_TYPE_MESH, data);
    generate(mLayer, mMaterial, mLightSet, alias);
    PrimitiveFinder aliasPrimitive = findPrimitive(alias);
    CPPUNIT_ASSERT_EQUAL(std::string("PolygonMesh"), aliasPrimitive.mType);
    CPPUNIT_ASSERT_EQUAL(mesh.mFaceVertexCount.size(),
                         static_cast<size_t>(static_cast<PolygonMesh*>(aliasPrimitive.mPrimitive)->getFaceCount()));
}

namespace {

// Sets values of the UserData type matching an attribute's type, as
// RdlGeometry requires. Single-valued attributes get their default value.
// Returns false for types RdlGeometry never reads.
bool
setMatchingData(UserData* ud, const scene_rdl2::rdl2::Attribute& attr, const std::string& key)
{
    using namespace scene_rdl2::rdl2;
    switch (attr.getType()) {
    case TYPE_BOOL: ud->setBoolData(key, {attr.getDefaultValue<Bool>()}); return true;
    case TYPE_INT: ud->setIntData(key, {attr.getDefaultValue<Int>()}); return true;
    case TYPE_FLOAT: ud->setFloatData(key, {attr.getDefaultValue<Float>()}); return true;
    case TYPE_INT_VECTOR: ud->setIntData(key, {0}); return true;
    case TYPE_FLOAT_VECTOR: ud->setFloatData(key, {1.f}); return true;
    case TYPE_STRING_VECTOR: ud->setStringData(key, {"part"}); return true;
    case TYPE_VEC2F_VECTOR: ud->setVec2fData(key, {Vec2f(0.f, 0.f)}); return true;
    case TYPE_VEC3F_VECTOR: ud->setVec3fData(key, {Vec3f(0.f, 0.f, 1.f)}); return true;
    case TYPE_VEC4F_VECTOR: ud->setVec4fData(key, {Vec4f(0.f, 0.f, 0.f, 1.f)}); return true;
    default: return false;
    }
}

} // anonymous namespace

void
TestRdlGeometry::testAttributeKeysAreNotPrimitiveAttributes(const std::string& referenceClassName, int geoType)
{
    // For every name and alias of every attribute of the equivalent
    // Rdl*Geometry, a key of the attribute's type is never a primitive
    // attribute, and a key of another type is an error that stops the
    // geometry from being generated. This catches RdlGeometry's key tables
    // drifting from the Rdl*Geometry declarations.
    const scene_rdl2::rdl2::SceneClass* referenceClass = mContext->createSceneClass(referenceClassName);
    const scene_rdl2::rdl2::SceneClass* rdlGeometryClass = mContext->createSceneClass("RdlGeometry");

    // Attributes RdlGeometry ignores with a warning: those every Geometry
    // has, which are set on the RdlGeometry itself, and those it replaces
    const std::set<std::string> ignoredNames = {
        "vertex_list_0", "vertex list 0", "vertex_list_1", "vertex list 1", "vertex_list_mb", "vertex list mb",
        "velocity_list_0", "velocity list 0", "velocity_list_1", "velocity list 1", "velocity_list_B",
        "velocity list B", "primitive_attributes", "primitive attributes",
    };
    auto isIgnored = [&](const std::string& name) {
        if (ignoredNames.count(name)) {
            return true;
        }
        try {
            rdlGeometryClass->getAttribute(name);
            return true;
        } catch (const scene_rdl2::except::KeyError&) {
            return false;
        }
    };

    // real data first, so the values below never replace it
    SceneObjectVector baseData;
    if (geoType == GEO_TYPE_MESH) {
        baseData = createMeshData(*mContext, "data", GridMesh(5));
        baseData.push_back(createUserData(*mContext, "isSubd", [](UserData* ud) {
            ud->setBoolData("is_subd", {false}); }));
    } else if (geoType == GEO_TYPE_CURVES) {
        const LinearCurves curves(2, 4);
        baseData = {
            createUserData(*mContext, "counts", [&](UserData* ud) {
                ud->setIntData("curves_vertex_count", curves.mCurvesVertexCount); }),
            createUserData(*mContext, "positions", [&](UserData* ud) {
                ud->setVec3fData("vertex_list", curves.mPositions); }),
            createUserData(*mContext, "curveType", [](UserData* ud) { ud->setIntData("curve_type", {0}); }),
        };
    } else {
        baseData = {
            createUserData(*mContext, "positions", [&](UserData* ud) {
                ud->setVec3fData("vertex_list", PointCloud(10).mPositions); }),
        };
    }

    int i = 0;
    for (auto it = referenceClass->beginAttributes(); it != referenceClass->endAttributes(); ++it) {
        const scene_rdl2::rdl2::Attribute& attr = **it;
        std::vector<std::string> names = attr.getAliases();
        names.push_back(attr.getName());
        for (const std::string& name : names) {
            const std::string id = std::to_string(i++);
            const bool ignored = isIgnored(name);

            // the attribute's own type, or a string for those ignored anyway
            SceneObjectVector data = baseData;
            data.push_back(createUserData(*mContext, "matchingData" + id, [&](UserData* ud) {
                if (ignored || !setMatchingData(ud, attr, name)) {
                    ud->setStringData(name, {"ignored"});
                }
            }));
            Geometry* matching = createRdlGeometry(*mContext, "matching" + id, geoType, data);
            generate(mLayer, mMaterial, mLightSet, matching);
            const PrimitiveFinder finder = findPrimitive(matching);
            if (finder.mCount != 1) {
                CPPUNIT_FAIL("\"" + name + "\" of " + referenceClassName + "'s type stopped generation");
            }
            if (getPrimitiveAttributeNames(*finder.mPrimitive).count(name)) {
                CPPUNIT_FAIL("\"" + name + "\" is an attribute of " + referenceClassName +
                             " but was added as a primitive attribute");
            }

            // another type is an error, unless the key is ignored
            if (ignored) {
                continue;
            }
            data = baseData;
            data.push_back(createUserData(*mContext, "mismatchedData" + id, [&](UserData* ud) {
                if (attr.getType() == scene_rdl2::rdl2::TYPE_STRING_VECTOR) {
                    ud->setIntData(name, {1});
                } else {
                    ud->setStringData(name, {"wrong type"});
                }
            }));
            Geometry* mismatched = createRdlGeometry(*mContext, "mismatched" + id, geoType, data);
            generate(mLayer, mMaterial, mLightSet, mismatched);
            if (findPrimitive(mismatched).mCount != 0) {
                CPPUNIT_FAIL("\"" + name + "\" of the wrong type for " + referenceClassName + " was not an error");
            }
        }
    }

    // and anything else is a primitive attribute
    SceneObjectVector data = baseData;
    data.push_back(createUserData(*mContext, "string_primvar", [](UserData* ud) {
        ud->setStringData("string_primvar", {"a primitive attribute"}); }));
    Geometry* primvar = createRdlGeometry(*mContext, "primvar", geoType, data);
    generate(mLayer, mMaterial, mLightSet, primvar);
    CPPUNIT_ASSERT(getPrimitiveAttributeNames(*findPrimitive(primvar).mPrimitive).count("string_primvar"));
}

void
TestRdlGeometry::testMeshAttributeKeysAreNotPrimitiveAttributes()
{
    testAttributeKeysAreNotPrimitiveAttributes("RdlMeshGeometry", GEO_TYPE_MESH);
}

void
TestRdlGeometry::testCurvesAttributeKeysAreNotPrimitiveAttributes()
{
    testAttributeKeysAreNotPrimitiveAttributes("RdlCurveGeometry", GEO_TYPE_CURVES);
}

void
TestRdlGeometry::testPointsAttributeKeysAreNotPrimitiveAttributes()
{
    testAttributeKeysAreNotPrimitiveAttributes("RdlPointGeometry", GEO_TYPE_POINTS);
}

void
TestRdlGeometry::testReleaseInputData()
{
    const GridMesh mesh(20);
    const SceneObjectVector data = createMeshData(*mContext, "data", mesh);
    Geometry* rdlGeometry = createRdlGeometry(*mContext, "rdlGeometry", GEO_TYPE_MESH, data);
    generate(mLayer, mMaterial, mLightSet, rdlGeometry);
    const size_t primitiveMemory = rdlGeometry->getProcedural()->getMemory();

    rdlGeometry->releaseInputData();

    CPPUNIT_ASSERT(rdlGeometry->isDataReleased());
    for (scene_rdl2::rdl2::SceneObject* sceneObject : data) {
        const UserData* userData = sceneObject->asA<UserData>();
        CPPUNIT_ASSERT(userData->isDataReleased());
        CPPUNIT_ASSERT(!userData->hasIntData());
        CPPUNIT_ASSERT(!userData->hasVec2fData());
        CPPUNIT_ASSERT(!userData->hasVec3fData());
        CPPUNIT_ASSERT_EQUAL(size_t(0), userData->getVec3fValues().capacity());
    }

    // the primitive already built from the data is unaffected
    CPPUNIT_ASSERT_EQUAL(primitiveMemory, rdlGeometry->getProcedural()->getMemory());

    // Geometry that doesn't override releaseInputData() keeps its data
    Geometry* rdlMesh = mContext->createSceneObject("RdlMeshGeometry", "rdlMesh")->asA<Geometry>();
    rdlMesh->beginUpdate();
    rdlMesh->set("vertex_list_0", mesh.mPositions);
    rdlMesh->endUpdate();
    rdlMesh->releaseInputData();
    CPPUNIT_ASSERT(!rdlMesh->isDataReleased());
    CPPUNIT_ASSERT_EQUAL(mesh.mPositions.size(), rdlMesh->get<scene_rdl2::rdl2::Vec3fVector>("vertex_list_0").size());
}

void
TestRdlGeometry::testReleasedGeometryCannotBeUpdated()
{
    const SceneObjectVector data = createMeshData(*mContext, "data", GridMesh(5));
    Geometry* rdlGeometry = createRdlGeometry(*mContext, "rdlGeometry", GEO_TYPE_MESH, data);
    generate(mLayer, mMaterial, mLightSet, rdlGeometry);
    rdlGeometry->releaseInputData();

    CPPUNIT_ASSERT_THROW(rdlGeometry->beginUpdate(), scene_rdl2::except::RuntimeError);
    for (scene_rdl2::rdl2::SceneObject* userData : data) {
        CPPUNIT_ASSERT_THROW(userData->beginUpdate(), scene_rdl2::except::RuntimeError);
    }
}

void
TestRdlGeometry::testReleasedGeometryCannotBeRegenerated()
{
    Geometry* rdlGeometry = createRdlGeometry(*mContext, "rdlGeometry", GEO_TYPE_MESH,
        createMeshData(*mContext, "data", GridMesh(5)));
    generate(mLayer, mMaterial, mLightSet, rdlGeometry);
    rdlGeometry->releaseInputData();

    CPPUNIT_ASSERT_THROW(generate(mLayer, mMaterial, mLightSet, rdlGeometry), scene_rdl2::except::RuntimeError);
}

void
TestRdlGeometry::testSharedReleasedUserDataCannotBeRegenerated()
{
    // A UserData shared by two RdlGeometry is released with either of them
    const GridMesh mesh(5);
    SceneObjectVector data = createMeshData(*mContext, "data", mesh);
    Geometry* first = createRdlGeometry(*mContext, "first", GEO_TYPE_MESH, data);
    UserData* otherPositions = createUserData(*mContext, "otherPositions", [&](UserData* ud) {
        ud->setVec3fData("vertex_list", mesh.mPositions); });
    data[2] = otherPositions;
    Geometry* second = createRdlGeometry(*mContext, "second", GEO_TYPE_MESH, data);
    generate(mLayer, mMaterial, mLightSet, first);
    generate(mLayer, mMaterial, mLightSet, second);

    first->releaseInputData();

    CPPUNIT_ASSERT(!second->isDataReleased());
    CPPUNIT_ASSERT(!otherPositions->isDataReleased());
    CPPUNIT_ASSERT_THROW(generate(mLayer, mMaterial, mLightSet, second), scene_rdl2::except::RuntimeError);
}

} // namespace unittest
} // namespace geom
} // namespace moonray
