// Copyright 2025-2026 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

/// @file "TestVec4fOutputMap.ispc"

#include "attributes.isph"

#include <moonray/rendering/shading/ispc/MapApi.isph>

struct TestVec4fOutputMap {
};
ISPC_UTIL_EXPORT_UNIFORM_STRUCT_TO_HEADER(TestVec4fOutputMap);

static varying Vec4f
sample(const uniform Map*            uniform map,
             uniform ShadingTLState* uniform tls,
       const varying State&                  state)
{
    const uniform TestVec4fOutputMap* uniform me = MAP_GET_ISPC_CPTR(TestVec4fOutputMap, map);
    varying Color inValue;
    inValue = evalInAttr(map, tls, state);

    varying Vec4f outValue;
    outValue = Vec4f_ctor(inValue.r, inValue.g, inValue.b, 0.0);
    return outValue;
}

DEFINE_MAP_SHADER_WITH_TYPE(TestVec4fOutputMap, sample, Vec4f)

