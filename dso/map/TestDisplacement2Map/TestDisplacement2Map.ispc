// Copyright 2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

/// @file "TestDisplacement2Map.ispc"

#include "attributes.isph"

#include <moonray/rendering/shading/ispc/MapApi.isph>

typedef uniform intptr_t (*uniform DisplaceFn) (    const uniform Displacement *    uniform displacement,
                                                    uniform ShadingTLState *        uniform tls,
                                                    uniform int                     numState,
                                                    const varying State *           const uniform state,
                                                    varying Vec3f *                 uniform out);

struct TestDisplacement2Map {
    uniform intptr_t mDisplaceFunc;
    uniform Displacement* uniform mDisplacement;
};

ISPC_UTIL_EXPORT_UNIFORM_STRUCT_TO_HEADER(TestDisplacement2Map);

//
// Displacement maps don't have the usual eval infrastructure so
// do it outselves
//
inline varying Vec3f
evalDisplacementBinding(const uniform Map*    uniform map,
                     uniform ShadingTLState * uniform tls,
                     const varying State & state,
                     const uniform AttributeKey *   uniform key)
{
    // get the cached function pointer
    // needs to be cast to properly match the ispc interface
    const uniform TestDisplacement2Map* uniform me = MAP_GET_ISPC_CPTR(TestDisplacement2Map, map);
    DisplaceFn displaceFunc = (DisplaceFn) me->mDisplaceFunc;

    // early out if there is no function pointer
    if (displaceFunc == NULL) {
        return Vec3f_ctor(0.0, 0.0, 0.0);
    }

    varying Vec3f result;
    displaceFunc(me->mDisplacement, tls, 1, &state, &result);
    return result;
}

static Color
sample(const uniform Map *            uniform  map,
             uniform ShadingTLState * uniform  tls,
       const varying State &                   state)
{
    const uniform TestDisplacement2Map* uniform me = MAP_GET_ISPC_CPTR(TestDisplacement2Map, map);

    varying Vec3f inValue;
    inValue = evalDisplacementBinding(map, tls, state, (const uniform AttributeKey * uniform)inAttr);
    return Color_ctor(inValue.x, inValue.y, inValue.z);
}

DEFINE_MAP_SHADER(TestDisplacement2Map, sample)
