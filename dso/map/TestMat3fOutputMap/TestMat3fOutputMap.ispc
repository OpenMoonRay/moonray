// Copyright 2024-2026 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

/// @file "TestMat3fOutputMap.ispc"

#include "attributes.isph"

#include <moonray/rendering/shading/ispc/MapApi.isph>

struct TestMat3fOutputMap {
};
ISPC_UTIL_EXPORT_UNIFORM_STRUCT_TO_HEADER(TestMat3fOutputMap);

static varying Mat3f
sample(const uniform Map*            uniform map,
             uniform ShadingTLState* uniform tls,
       const varying State&                  state)
{
    const uniform TestMat3fOutputMap* uniform me = MAP_GET_ISPC_CPTR(TestMat3fOutputMap, map);
    varying Color inValue;
    inValue = evalInAttr(map, tls, state);

    varying Mat3f outValue;
    outValue = Mat3f_ctor(inValue.r, 0.0f,      0.0f,
                          0.0f,      inValue.g, 0.0f,
                          0.0f,      0.0f,      inValue.b);
    return outValue;
}

DEFINE_MAP_SHADER_WITH_TYPE(TestMat3fOutputMap, sample, Mat3f)
