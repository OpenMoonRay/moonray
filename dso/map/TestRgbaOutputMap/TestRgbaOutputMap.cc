// Copyright 2026 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

/// @file TestRgbaOutputMap.cc

#include "attributes.cc"
#include "TestRgbaOutputMap_ispc_stubs.h"

#include <moonray/rendering/shading/MapApi.h>
using namespace scene_rdl2::math;
using namespace scene_rdl2::rdl2;

RDL2_DSO_CLASS_BEGIN(TestRgbaOutputMap, Map)
public:
    TestRgbaOutputMap(SceneClass const &sceneClass, std::string const &name);
    void update();

private:
    static void sample(const Map *self, moonray::shading::TLState *tls,
                       const moonray::shading::State &state, Rgba *sample);
    ispc::TestRgbaOutputMap mIspc;

RDL2_DSO_CLASS_END(TestRgbaOutputMap)

TestRgbaOutputMap::TestRgbaOutputMap(const SceneClass& sceneClass,
        const std::string& name) :
    Parent(sceneClass, name)
{
    mSampleFuncRgba = TestRgbaOutputMap::sample;
    mSampleFuncvRgba = (SampleFuncvRgba) ispc::TestRgbaOutputMap_getSampleFunc();
    mOutputType = TYPE_RGBA;
}

void
TestRgbaOutputMap::update()
{
}

void
TestRgbaOutputMap::sample(const Map* self, moonray::shading::TLState *tls,
                 const moonray::shading::State& state, Rgba* sample)
{
    const TestRgbaOutputMap* me = static_cast<const TestRgbaOutputMap*>(self);

    Color inValue;
    Rgba outValue;
    inValue = evalColor(me, inAttr, tls, state);
    outValue = Rgba(inValue.r, inValue.g, inValue.b, 0.0f);
    *sample = outValue;
}

