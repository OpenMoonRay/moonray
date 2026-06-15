// Copyright 2024-2026 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

/// @file TestVec4fOutputMap.cc

#include "attributes.cc"
#include "TestVec4fOutputMap_ispc_stubs.h"

#include <moonray/rendering/shading/MapApi.h>
using namespace scene_rdl2::math;
using namespace scene_rdl2::rdl2;

RDL2_DSO_CLASS_BEGIN(TestVec4fOutputMap, Map)
public:
    TestVec4fOutputMap(SceneClass const &sceneClass, std::string const &name);
    void update();

private:
    static void sample(const Map *self, moonray::shading::TLState *tls,
                       const moonray::shading::State &state, Vec4f *sample);
    ispc::TestVec4fOutputMap mIspc;

RDL2_DSO_CLASS_END(TestVec4fOutputMap)

TestVec4fOutputMap::TestVec4fOutputMap(const SceneClass& sceneClass,
        const std::string& name) :
    Parent(sceneClass, name)
{
    mSampleFuncVec4f = TestVec4fOutputMap::sample;
    mSampleFuncvVec4f = (SampleFuncvVec4f) ispc::TestVec4fOutputMap_getSampleFunc();
    mOutputType = TYPE_VEC4F;
}

void
TestVec4fOutputMap::update()
{
}

void
TestVec4fOutputMap::sample(const Map* self, moonray::shading::TLState *tls,
                 const moonray::shading::State& state, Vec4f* sample)
{
    const TestVec4fOutputMap* me = static_cast<const TestVec4fOutputMap*>(self);

    Color inValue;
    Vec4f outValue;
    inValue = evalColor(me, inAttr, tls, state);
    outValue = Vec4f(inValue.r, inValue.g, inValue.b, 0.0f);
    *sample = outValue;
}

