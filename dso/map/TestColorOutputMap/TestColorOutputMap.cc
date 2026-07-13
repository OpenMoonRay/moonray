// Copyright 2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

/// @file TestColorOutputMap.cc

#include "attributes.cc"
#include "TestColorOutputMap_ispc_stubs.h"

#include <moonray/rendering/shading/MapApi.h>
using namespace scene_rdl2::math;
using namespace scene_rdl2::rdl2;

RDL2_DSO_CLASS_BEGIN(TestColorOutputMap, Map)
public:
    TestColorOutputMap(SceneClass const &sceneClass, std::string const &name);
    void update();

private:
    static void sample(const Map *self, moonray::shading::TLState *tls,
                       const moonray::shading::State &state, Color *sample);
    ispc::TestColorOutputMap mIspc;

RDL2_DSO_CLASS_END(TestColorOutputMap)

TestColorOutputMap::TestColorOutputMap(const SceneClass& sceneClass,
        const std::string& name) :
    Parent(sceneClass, name)
{
    mSampleFunc = TestColorOutputMap::sample;
    mSampleFuncv = (SampleFuncv) ispc::TestColorOutputMap_getSampleFunc();
}

void
TestColorOutputMap::update()
{
}

void
TestColorOutputMap::sample(const Map* self, moonray::shading::TLState *tls,
                 const moonray::shading::State& state, Color* sample)
{
    const TestColorOutputMap* me = static_cast<const TestColorOutputMap*>(self);

    *sample = evalColor(me, inAttr, tls, state);
}

