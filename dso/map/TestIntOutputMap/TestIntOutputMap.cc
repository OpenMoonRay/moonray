// Copyright 2024-2026 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

/// @file TestIntOutputMap.cc

#include "attributes.cc"
#include "TestIntOutputMap_ispc_stubs.h"

#include <moonray/rendering/shading/MapApi.h>
using namespace scene_rdl2::math;
using namespace scene_rdl2::rdl2;

RDL2_DSO_CLASS_BEGIN(TestIntOutputMap, Map)
public:
    TestIntOutputMap(SceneClass const &sceneClass, std::string const &name);
    void update();

private:
    static void sample(const Map *self, moonray::shading::TLState *tls,
                       const moonray::shading::State &state, Int *sample);
    ispc::TestIntOutputMap mIspc;

RDL2_DSO_CLASS_END(TestIntOutputMap)

TestIntOutputMap::TestIntOutputMap(const SceneClass& sceneClass,
        const std::string& name) :
    Parent(sceneClass, name)
{
    mSampleFuncInt = TestIntOutputMap::sample;
    mSampleFuncvInt= (SampleFuncvInt) ispc::TestIntOutputMap_getSampleFunc();
    mOutputType = TYPE_INT;
}

void
TestIntOutputMap::update()
{
    Color inValue = get(inAttr);
    int red = (inValue.r*255.0);
    if (red == 173) {
        setFataled(true);
    } else {
        setFataled(false);
    }
}

void
TestIntOutputMap::sample(const Map* self, moonray::shading::TLState *tls,
                 const moonray::shading::State& state, Int* sample)
{
    const TestIntOutputMap* me = static_cast<const TestIntOutputMap*>(self);

    Color inValue;
    Int outValue;
    inValue = evalColor(me, inAttr, tls, state);
    outValue = int(inValue.r*255) | (int(inValue.g*255)<<8) | (int(inValue.b*255) << 16);
    *sample = outValue;
}

