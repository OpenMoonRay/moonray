// Copyright 2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

/// @file TestDisplacement2Map.cc

#include "attributes.cc"
#include "TestDisplacement2Map_ispc_stubs.h"

#include <moonray/rendering/shading/MapApi.h>
using namespace scene_rdl2::math;
using namespace scene_rdl2::rdl2;

RDL2_DSO_CLASS_BEGIN(TestDisplacement2Map, Map)
public:
    TestDisplacement2Map(SceneClass const &sceneClass, std::string const &name);
    void update();

private:
    static void sample(const Map *self, moonray::shading::TLState *tls,
                       const moonray::shading::State &state, Color *sample);
    ispc::TestDisplacement2Map mIspc;

RDL2_DSO_CLASS_END(TestDisplacement2Map)

TestDisplacement2Map::TestDisplacement2Map(const SceneClass& sceneClass,
        const std::string& name) :
    Parent(sceneClass, name)
{
    mSampleFunc = TestDisplacement2Map::sample;
    mSampleFuncv = (SampleFuncv) ispc::TestDisplacement2Map_getSampleFunc();
}

void
TestDisplacement2Map::update()
{
    const SceneObject* displacement= getBinding(inAttr);
    if (displacement != nullptr) {
        Displacement* displace = (Displacement*)displacement;
        mIspc.mDisplaceFunc = (intptr_t) displace->mDisplaceFuncv;
        mIspc.mDisplacement = displace;
    } else {
        mIspc.mDisplaceFunc = (intptr_t)nullptr;
        mIspc.mDisplacement = nullptr;
    }
}

Vec3f
evalDisplaceRaw(const Shader* const obj,
                 const AttributeKey<Vec3f>& valueKey,
                 moonray::shading::TLState *tls,
                 const moonray::shading::State& state)
{
    Vec3f result = obj->get(valueKey);
    const SceneObject* mapObj = obj->getBinding(valueKey);
    if (mapObj != nullptr) {
        const Displacement* map = mapObj->asA<Displacement>();
        if (map != nullptr) {
            map->displace(tls, state, &result);

            return result;
        }
    }
    return result;
}


void
TestDisplacement2Map::sample(const Map* self, moonray::shading::TLState *tls,
                 const moonray::shading::State& state, Color* sample)
{
    const TestDisplacement2Map* me = static_cast<const TestDisplacement2Map*>(self);

    Vec3f result;
    result = evalDisplaceRaw(me, inAttr, tls, state);
    *sample = Color(result.x, result.y, result.z);
}

