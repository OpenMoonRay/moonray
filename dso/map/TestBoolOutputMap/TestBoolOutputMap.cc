// Copyright 2024-2026 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

/// @file TestBoolOutputMap.cc

#include "attributes.cc"
#include "TestBoolOutputMap_ispc_stubs.h"

#include <moonray/rendering/shading/MapApi.h>
using namespace scene_rdl2::math;
using namespace scene_rdl2::rdl2;

RDL2_DSO_CLASS_BEGIN(TestBoolOutputMap, Map)
public:
    TestBoolOutputMap(SceneClass const &sceneClass, std::string const &name);
    void update();

private:
    static void sample(const Map *self, moonray::shading::TLState *tls,
                       const moonray::shading::State &state, Bool *sample);
    ispc::TestBoolOutputMap mIspc;

RDL2_DSO_CLASS_END(TestBoolOutputMap)

TestBoolOutputMap::TestBoolOutputMap(const SceneClass& sceneClass,
        const std::string& name) :
    Parent(sceneClass, name)
{
    mSampleFuncBool = TestBoolOutputMap::sample;
    mSampleFuncvBool= (SampleFuncvBool) ispc::TestBoolOutputMap_getSampleFunc();
    mOutputType = TYPE_BOOL;
}

void
TestBoolOutputMap::update()
{
}

void
TestBoolOutputMap::sample(const Map* self, moonray::shading::TLState *tls,
                 const moonray::shading::State& state, Bool* sample)
{
    const TestBoolOutputMap* me = static_cast<const TestBoolOutputMap*>(self);

    Color inValue;
    Bool outValue;
    inValue = evalColor(me, inAttr, tls, state);
    outValue = ((inValue.r+inValue.b+inValue.g) > 1.5);
    *sample = outValue;
}

