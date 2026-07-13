// Copyright 2024-2026 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

/// @file "TestBoolOutputMap.ispc"

#include "attributes.isph"

#include <moonray/rendering/shading/ispc/MapApi.isph>

struct TestBoolOutputMap {
    uniform bool disableMode;
};
ISPC_UTIL_EXPORT_UNIFORM_STRUCT_TO_HEADER(TestBoolOutputMap);

static varying bool
sample(const uniform Map*            uniform map,
             uniform ShadingTLState* uniform tls,
       const varying State&                  state)
{
    const uniform TestBoolOutputMap* uniform me = MAP_GET_ISPC_CPTR(TestBoolOutputMap, map);
    varying Color inValue;
    inValue = evalInAttr(map, tls, state);

    varying bool outValue;
    outValue = ((inValue.r+inValue.b+inValue.g) > 1.5);

    return outValue;
}

DEFINE_MAP_SHADER_WITH_TYPE(TestBoolOutputMap, sample, bool)

