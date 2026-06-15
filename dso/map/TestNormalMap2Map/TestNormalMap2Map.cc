// Copyright 2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

/// @file TestNormalMap2Map.cc

#include "attributes.cc"
#include "TestNormalMap2Map_ispc_stubs.h"

#include <moonray/rendering/shading/MapApi.h>
using namespace scene_rdl2::math;
using namespace scene_rdl2::rdl2;

RDL2_DSO_CLASS_BEGIN(TestNormalMap2Map, Map)
public:
    TestNormalMap2Map(SceneClass const &sceneClass, std::string const &name);
    void update();

private:
    static void sample(const Map *self, moonray::shading::TLState *tls,
                       const moonray::shading::State &state, Color *sample);
    ispc::TestNormalMap2Map mIspc;

RDL2_DSO_CLASS_END(TestNormalMap2Map)

TestNormalMap2Map::TestNormalMap2Map(const SceneClass& sceneClass,
        const std::string& name) :
    Parent(sceneClass, name)
{
    mSampleFunc = TestNormalMap2Map::sample;
    mSampleFuncv = (SampleFuncv) ispc::TestNormalMap2Map_getSampleFunc();
}

void
TestNormalMap2Map::update()
{
}

Vec3f
evalNormalMapRaw(const Shader* const obj,
                 const AttributeKey<Vec3f>& valueKey,
                 moonray::shading::TLState *tls,
                 const moonray::shading::State& state)
{
    Vec3f result = obj->get(valueKey);
    const SceneObject* mapObj = obj->getBinding(valueKey);
    if (mapObj != nullptr) {
        const NormalMap* map = mapObj->asA<NormalMap>();
        if (map != nullptr) {
            map->sampleNormal(tls, state, &result);

            return result;
        }
    }
    return result;
}


void
TestNormalMap2Map::sample(const Map* self, moonray::shading::TLState *tls,
                 const moonray::shading::State& state, Color* sample)
{
    const TestNormalMap2Map* me = static_cast<const TestNormalMap2Map*>(self);

    Vec3f result;
    result = evalNormalMapRaw(me, inAttr, tls, state);
    *sample = Color(result.x, result.y, result.z);
}

