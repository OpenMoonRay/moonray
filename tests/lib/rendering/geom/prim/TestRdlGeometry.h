// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cppunit/extensions/HelperMacros.h>

#include <memory>
#include <string>

namespace scene_rdl2 {
namespace rdl2 {
class LightSet;
class Layer;
class Material;
class SceneContext;
}
}

namespace moonray {
namespace geom {
namespace unittest {

// Tests the RdlGeometry procedural (dso/geometry/RdlGeometry) against the
// RdlMeshGeometry, RdlCurveGeometry and RdlPointGeometry procedurals it
// mirrors, and the release of its input data.
class TestRdlGeometry : public CppUnit::TestFixture
{
public:
    void setUp() override;
    void tearDown() override;

    CPPUNIT_TEST_SUITE(TestRdlGeometry);
    CPPUNIT_TEST(testPolygonMeshMatchesRdlMesh);
    CPPUNIT_TEST(testSubdMeshMatchesRdlMesh);
    CPPUNIT_TEST(testCurvesMatchRdlCurve);
    CPPUNIT_TEST(testPointsMatchRdlPoint);
    CPPUNIT_TEST(testMeshMotionStepsMatchRdlMesh);
    CPPUNIT_TEST(testCurvesMotionStepsMatchRdlCurve);
    CPPUNIT_TEST(testPointsMotionStepsMatchRdlPoint);
    CPPUNIT_TEST(testPerMotionStepKeysAreIgnored);
    CPPUNIT_TEST(testScalarData);
    CPPUNIT_TEST(testMeshAttributeKeysAreNotPrimitiveAttributes);
    CPPUNIT_TEST(testCurvesAttributeKeysAreNotPrimitiveAttributes);
    CPPUNIT_TEST(testPointsAttributeKeysAreNotPrimitiveAttributes);
    CPPUNIT_TEST(testReleaseInputData);
    CPPUNIT_TEST(testReleasedGeometryCannotBeUpdated);
    CPPUNIT_TEST(testReleasedGeometryCannotBeRegenerated);
    CPPUNIT_TEST(testSharedReleasedUserDataCannotBeRegenerated);
    CPPUNIT_TEST_SUITE_END();

    void testPolygonMeshMatchesRdlMesh();
    void testSubdMeshMatchesRdlMesh();
    void testCurvesMatchRdlCurve();
    void testPointsMatchRdlPoint();
    void testMeshMotionStepsMatchRdlMesh();
    void testCurvesMotionStepsMatchRdlCurve();
    void testPointsMotionStepsMatchRdlPoint();
    void testPerMotionStepKeysAreIgnored();
    void testScalarData();
    void testMeshAttributeKeysAreNotPrimitiveAttributes();
    void testCurvesAttributeKeysAreNotPrimitiveAttributes();
    void testPointsAttributeKeysAreNotPrimitiveAttributes();
    void testReleaseInputData();
    void testReleasedGeometryCannotBeUpdated();
    void testReleasedGeometryCannotBeRegenerated();
    void testSharedReleasedUserDataCannotBeRegenerated();

private:
    void testAttributeKeysAreNotPrimitiveAttributes(const std::string& referenceClassName, int geoType);

    std::unique_ptr<scene_rdl2::rdl2::SceneContext> mContext;
    scene_rdl2::rdl2::Layer* mLayer;
    scene_rdl2::rdl2::Material* mMaterial;
    scene_rdl2::rdl2::LightSet* mLightSet;
};

} // namespace unittest
} // namespace geom
} // namespace moonray
