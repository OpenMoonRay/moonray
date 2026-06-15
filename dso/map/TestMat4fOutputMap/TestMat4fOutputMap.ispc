// Copyright 2024-2026 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

/// @file "TestMat4fOutputMap.ispc"

#include "attributes.isph"

#include <moonray/rendering/shading/ispc/MapApi.isph>

struct TestMat4fOutputMap {
    uniform bool disableMode;
};
ISPC_UTIL_EXPORT_UNIFORM_STRUCT_TO_HEADER(TestMat4fOutputMap);

static varying Mat4f
sample(const uniform Map*            uniform map,
             uniform ShadingTLState* uniform tls,
       const varying State&                  state)
{
    const uniform TestMat4fOutputMap* uniform me = MAP_GET_ISPC_CPTR(TestMat4fOutputMap, map);
    varying Color inValue;
    inValue = evalInAttr(map, tls, state);

    varying Mat4f outValue;
    outValue = Mat4f_ctor(inValue.r, 0.0f,      0.0f,      0.0f,
                          0.0f,      inValue.g, 0.0f,      0.0f,
                          0.0f,      0.0f,      inValue.b, 0.0f,
                          0.0f,      0.0f,      0.0f,      1.0f);
    return outValue;
}

DEFINE_MAP_SHADER_WITH_TYPE(TestMat4fOutputMap, sample, Mat4f)
