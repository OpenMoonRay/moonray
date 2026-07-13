// Copyright 2024-2026 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

/// @file "TestRgbaOutputMap.ispc"

#include "attributes.isph"

#include <moonray/rendering/shading/ispc/MapApi.isph>

struct TestRgbaOutputMap {
    uniform bool disableMode;
};
ISPC_UTIL_EXPORT_UNIFORM_STRUCT_TO_HEADER(TestRgbaOutputMap);

static varying Col4f
sample(const uniform Map*            uniform map,
             uniform ShadingTLState* uniform tls,
       const varying State&                  state)
{
    const uniform TestRgbaOutputMap* uniform me = MAP_GET_ISPC_CPTR(TestRgbaOutputMap, map);
    varying Color inValue;
    inValue = evalInAttr(map, tls, state);

    varying Col4f outValue;
    outValue = Col4f_ctor(inValue.r, inValue.g, inValue.b, 0.0);
    return outValue;
}

DEFINE_MAP_SHADER_WITH_TYPE(TestRgbaOutputMap, sample, Col4f)

