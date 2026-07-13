// Copyright 2024-2026 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

/// @file TestMat4fOutputMap.cc

#include "attributes.cc"
#include "TestMat4fOutputMap_ispc_stubs.h"

#include <moonray/rendering/shading/MapApi.h>
using namespace scene_rdl2::math;
using namespace scene_rdl2::rdl2;

RDL2_DSO_CLASS_BEGIN(TestMat4fOutputMap, Map)
public:
    TestMat4fOutputMap(SceneClass const &sceneClass, std::string const &name);
    void update();

private:
    static void sample(const Map *self, moonray::shading::TLState *tls,
                       const moonray::shading::State &state, Mat4f *sample);
    ispc::TestMat4fOutputMap mIspc;

RDL2_DSO_CLASS_END(TestMat4fOutputMap)

TestMat4fOutputMap::TestMat4fOutputMap(const SceneClass& sceneClass,
        const std::string& name) :
    Parent(sceneClass, name)
{
    mSampleFuncMat4f = TestMat4fOutputMap::sample;
    mSampleFuncvMat4f = (SampleFuncvMat4f) ispc::TestMat4fOutputMap_getSampleFunc();
    mOutputType = TYPE_MAT4F;
}

void
TestMat4fOutputMap::update()
{
}

void
TestMat4fOutputMap::sample(const Map* self, moonray::shading::TLState *tls,
                 const moonray::shading::State& state, Mat4f* sample)
{
    const TestMat4fOutputMap* me = static_cast<const TestMat4fOutputMap*>(self);

    Color inValue;
    Mat4f outValue;
    inValue = evalColor(me, inAttr, tls, state);
    outValue = Mat4f(inValue.r, 0.0f,      0.0f,      0.0f,
                     0.0f,      inValue.g, 0.0f,      0.0f,
                     0.0f,      0.0f,      inValue.b, 0.0f,
                     0.0f,      0.0f,      0.0f,      1.0f);
    *sample = outValue;
}

