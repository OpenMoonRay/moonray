// Copyright 2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

/// @file "TestNormalMap2Map.ispc"

#include "attributes.isph"

#include <moonray/rendering/shading/ispc/MapApi.isph>

/// normal map sample function pointer.  matches rdl2::SampleNormalFuncv
/*
typedef void (* uniform SampleNormalPtr)(const uniform NormalMap * uniform normalMap,
                                               uniform ShadingTLState * uniform tls,
                                         const varying State *          uniform state,
                                               varying Vec3f *          uniform normal);
*/

struct TestNormalMap2Map {
};
ISPC_UTIL_EXPORT_UNIFORM_STRUCT_TO_HEADER(TestNormalMap2Map);

//
// The following function is a hack because of the weird need to evaluate
// a NormalMap within a Map shader. This is not a normal thing that a shader
// should be doing and is just done to provide testing infrastructure.
// Doing this makes this shader more likely to break with future
// infrastructure changes. Do no do this in your shader.
//

/// @brief evaluate a map binding
/// @return false if no map binding, true if binding exists and
/// was evaluated.  results returned in color.
inline varying Vec3f
evalNormalMapBinding(const uniform Map*    uniform map,
                     uniform ShadingTLState * uniform tls,
                     const varying State & state,
                     const uniform AttributeKey *   uniform key)
{
    uniform rdl2Ctx ctx = getRdl2Ctx((PTR8) map);

    // within the bindings, get the pointer to the thing map/key is
    // bound to.
    int64 * uniform ptrToNormalMapObjPtr = (int64 * uniform) (ctx.bindings + key->mIndex);

    // deref to get the map object pointer
    int64 * uniform normalMapObjPtr = (int64 * uniform) *ptrToNormalMapObjPtr;

    Vec3f normal;

    // if null, no binding
    if (normalMapObjPtr != NULL) {
        // we have a binding, find the sample function and evaluate it
        int64 * uniform ptr =
            (int64 * uniform) *(normalMapObjPtr + (NORMALMAP_SAMPLENORMALFUNCV_OFFSET / 8));

        // if the function pointer is null, no binding
        if (ptr != NULL) {

            // all good, call the sample function.
            SampleNormalPtr sampleFn = (SampleNormalPtr) ptr;
            const uniform int64 start = getInclusiveTime(normalMapObjPtr, tls);
            sampleFn((const NormalMap * uniform) normalMapObjPtr, tls, &state, &normal);
            const uniform int64 end = getInclusiveTime(normalMapObjPtr, tls);
            subtractTimeInChild(map, tls, start, end);

            return normal;
        }
    }
    return Vec3f_ctor(0.0, 0.0, 0.0);
}

static Color
sample(const uniform Map *            uniform  map,
             uniform ShadingTLState * uniform  tls,
       const varying State &                   state)
{
    const uniform TestNormalMap2Map* uniform me = MAP_GET_ISPC_CPTR(TestNormalMap2Map, map);

    varying Vec3f inValue;
    inValue = evalNormalMapBinding(map, tls, state, (const uniform AttributeKey * uniform)inAttr);
    return Color_ctor(inValue.x, inValue.y, inValue.z);
}

DEFINE_MAP_SHADER(TestNormalMap2Map, sample)

