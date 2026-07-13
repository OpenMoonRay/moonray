// Copyright 2024-2026 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

/// @file TestMat3fOutputMap.cc

#include "attributes.cc"
#include "TestMat3fOutputMap_ispc_stubs.h"

#include <moonray/rendering/shading/MapApi.h>
using namespace scene_rdl2::math;
using namespace scene_rdl2::rdl2;

RDL2_DSO_CLASS_BEGIN(TestMat3fOutputMap, Map)
public:
    TestMat3fOutputMap(SceneClass const &sceneClass, std::string const &name);
    void update();

private:
    static void sample(const Map *self, moonray::shading::TLState *tls,
                       const moonray::shading::State &state, Mat3f *sample);
    ispc::TestMat3fOutputMap mIspc;

RDL2_DSO_CLASS_END(TestMat3fOutputMap)

TestMat3fOutputMap::TestMat3fOutputMap(const SceneClass& sceneClass,
        const std::string& name) :
    Parent(sceneClass, name)
{
    mSampleFuncMat3f = TestMat3fOutputMap::sample;
    mSampleFuncvMat3f = (SampleFuncvMat3f) ispc::TestMat3fOutputMap_getSampleFunc();
    mOutputType = TYPE_MAT3F;
}

void
TestMat3fOutputMap::update()
{
}

void
TestMat3fOutputMap::sample(const Map* self, moonray::shading::TLState *tls,
                 const moonray::shading::State& state, Mat3f* sample)
{
    const TestMat3fOutputMap* me = static_cast<const TestMat3fOutputMap*>(self);

    Color inValue;
    Mat3f outValue;
    inValue = evalColor(me, inAttr, tls, state);
    outValue = Mat3f(inValue.r, 0.0f, 0.0f, 0.0f, inValue.g, 0.0f, 0.0f, 0.0f, inValue.b);
    *sample = outValue;
}

