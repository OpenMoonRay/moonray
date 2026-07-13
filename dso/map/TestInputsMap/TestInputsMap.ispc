// Copyright 2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

/// @file "TestInputsMap.ispc"

#include "attributes.isph"

#include <moonray/rendering/shading/ispc/MapApi.isph>

enum InputSelection {
    INPUT_BOOL = 0,
    INPUT_INT = 1,
    INPUT_STRING = 2,
    INPUT_FLOAT = 3,
    INPUT_VEC2F = 4,
    INPUT_VEC3F = 5,
    INPUT_VEC4F = 6,
    INPUT_COLOR = 7,
    INPUT_RGBA = 8,
    INPUT_MAT3F = 9,
    INPUT_MAT4F = 10,
    INPUT_SCENEOBJECT = 11,
    INPUT_INTVECTOR = 12,
    INPUT_STRINGVECTOR = 13,
    INPUT_FLOATVECTOR = 14,
    INPUT_VEC2FVECTOR = 15,
    INPUT_VEC3FVECTOR = 16,
    INPUT_VEC4FVECTOR = 17,
    INPUT_RGBVECTOR = 18,
    INPUT_RGBAVECTOR = 19,
    INPUT_RAW_INT = 20,
    INPUT_RAW_BOOL = 21
};
ISPC_UTIL_EXPORT_ENUM_TO_HEADER(InputSelection);

struct TestInputsMap {
    uniform int choice;
    uniform Color colorValue;
};
ISPC_UTIL_EXPORT_UNIFORM_STRUCT_TO_HEADER(TestInputsMap);

static varying Color
sample(const uniform Map *            uniform  map,
             uniform ShadingTLState * uniform  tls,
       const varying State &                   state)
{
    varying Color outValue;
    const uniform TestInputsMap* uniform me = MAP_GET_ISPC_CPTR(TestInputsMap, map);
    varying bool inBoolValue = evalInBoolAttr(map, tls, state);
    varying int inIntValue = evalInIntAttr(map, tls, state);
    varying float inFloatValue = evalInFloatAttr(map, tls, state);
    varying Vec2f inVec2fValue = evalInVec2fAttr(map, tls, state);
    varying Vec3f inVec3fValue = evalInVec3fAttr(map, tls, state);
    varying Vec4f inVec4fValue = evalInVec4fAttr(map, tls, state);
    varying Color inColorValue = evalInColorAttr(map, tls, state);
    varying Col4f inRgbaValue = evalInRgbaAttr(map, tls, state);
    varying Mat3f inMat3fValue = evalInMat3fAttr(map, tls, state);
    varying Mat4f inMat4fValue = evalInMat4fAttr(map, tls, state);

    switch (me->choice) {
      case INPUT_BOOL:
        if (inBoolValue) {
            outValue = Color_ctor(0.75, 0.75, 0.75);
        } else {
            outValue = Color_ctor(0.25, 0.25, 0.25);
        }
        break;
      case INPUT_RAW_BOOL:
        if (inBoolValue) {
            outValue = Color_ctor(1.0,1.0,1.0);
        } else {
            outValue = Color_ctor(0.0,0.0,0.0);
        }
        break;
      case INPUT_INT:
        outValue.r = (inIntValue&0xFF)/255.0;
        outValue.g = ((inIntValue>>8)&0xFF)/255.0;
        outValue.b = ((inIntValue>>16)&0xFF)/255.0;
        break;
      case INPUT_RAW_INT:
        outValue = Color_ctor(inIntValue, inIntValue, inIntValue);
        break;
      case INPUT_STRING:
        outValue = me->colorValue;
        break;
      case INPUT_FLOAT:
        outValue = Color_ctor(inFloatValue);
        break;
      case INPUT_VEC2F:
        outValue = Color_ctor(inVec2fValue.x, inVec2fValue.y, 0.0f);
        break;
      case INPUT_VEC3F:
        outValue = Color_ctor(inVec3fValue.x, inVec3fValue.y, inVec3fValue.z);
        break;
      case INPUT_VEC4F:
        outValue = Color_ctor(inVec4fValue.x, inVec4fValue.y, inVec4fValue.z);
        break;
      case INPUT_COLOR:
        outValue = inColorValue;
        break;
      case INPUT_RGBA:
        outValue = Color_ctor(inRgbaValue.r, inRgbaValue.g, inRgbaValue.b);
        break;
      case INPUT_MAT3F:
       outValue = Color_ctor(inMat3fValue.vx.x,inMat3fValue.vy.y, inMat3fValue.vz.z);
       break;
      case INPUT_MAT4F:
        outValue = Color_ctor(inMat4fValue.vx.x,inMat4fValue.vy.y, inMat4fValue.vz.z);
        break;

      case INPUT_INTVECTOR:
      case INPUT_STRINGVECTOR:
      case INPUT_FLOATVECTOR:
      case INPUT_VEC2FVECTOR:
      case INPUT_VEC3FVECTOR:
      case INPUT_VEC4FVECTOR:
      case INPUT_RGBVECTOR:
      case INPUT_RGBAVECTOR:
        outValue = me->colorValue;
        break;
    }
    return outValue;
}

DEFINE_MAP_SHADER(TestInputsMap, sample)

