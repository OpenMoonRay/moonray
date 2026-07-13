// Copyright 2026 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

/// @file "TestIntOutputMap.ispc"

#include "attributes.isph"

#include <moonray/rendering/shading/ispc/MapApi.isph>

struct TestIntOutputMap {
};
ISPC_UTIL_EXPORT_UNIFORM_STRUCT_TO_HEADER(TestIntOutputMap);

static varying int
sample(const uniform Map*            uniform map,
       uniform ShadingTLState* uniform tls,
       const varying State&                  state)
{
    const uniform TestIntOutputMap* uniform me = MAP_GET_ISPC_CPTR(TestIntOutputMap, map);
    varying Color inValue;
    inValue = evalInAttr(map, tls, state);

    varying int outValue;
    outValue = (int)(inValue.r*255) | ((int)(inValue.g*255))<<8 | ((int)(inValue.b*255)) << 16;
    return outValue;
}

DEFINE_MAP_SHADER_WITH_TYPE(TestIntOutputMap, sample, int)

