// Copyright 2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

/// @file "TestInputsDisplacement.ispc"

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
    INPUT_RGBAVECTOR = 19
};
ISPC_UTIL_EXPORT_ENUM_TO_HEADER(InputSelection);

struct TestInputsDisplacement {
    uniform int choice;
    uniform Vec3f displacementValue;
};
ISPC_UTIL_EXPORT_UNIFORM_STRUCT_TO_HEADER(TestInputsDisplacement);

static varying Vec3f
displace(const uniform Displacement *            uniform  map,
         uniform ShadingTLState * uniform  tls,
       const varying State &                   state)
{
    varying Vec3f outValue;
    const uniform TestInputsDisplacement* uniform me = DISPLACEMENT_GET_ISPC_CPTR(TestInputsDisplacement, map);
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
            outValue = Vec3f_ctor(0.75, 0.75, 0.75);
        } else {
            outValue = Vec3f_ctor(0.25, 0.25, 0.25);
        }
        break;
      case INPUT_INT:
        outValue.x = (inIntValue&0xFF)/255.0;
        outValue.y = ((inIntValue>>8)&0xFF)/255.0;
        outValue.z = ((inIntValue>>16)&0xFF)/255.0;
        break;
      case INPUT_STRING:
        outValue = me->displacementValue;
		break;
      case INPUT_FLOAT:
        outValue = Vec3f_ctor(inFloatValue);
        break;
      case INPUT_VEC2F:
        outValue = Vec3f_ctor(inVec2fValue.x, inVec2fValue.y, 0.0f);
        break;
      case INPUT_VEC3F:
        outValue = inVec3fValue;
        break;
      case INPUT_VEC4F:
        outValue = Vec3f_ctor(inVec4fValue.x, inVec4fValue.y, inVec4fValue.z);
        break;
      case INPUT_COLOR:
        outValue = Vec3f_ctor(inColorValue.r, inColorValue.g, inColorValue.b);
        break;
      case INPUT_RGBA:
        outValue = Vec3f_ctor(inRgbaValue.r, inRgbaValue.g, inRgbaValue.b);
        break;
      case INPUT_MAT3F:
       outValue = Vec3f_ctor(inMat3fValue.vx.x,inMat3fValue.vy.y, inMat3fValue.vz.z);
       break;
      case INPUT_MAT4F:
        outValue = Vec3f_ctor(inMat4fValue.vx.x,inMat4fValue.vy.y, inMat4fValue.vz.z);
        break;
      case INPUT_INTVECTOR:
      case INPUT_STRINGVECTOR:
      case INPUT_FLOATVECTOR:
      case INPUT_VEC2FVECTOR:
      case INPUT_VEC3FVECTOR:
      case INPUT_VEC4FVECTOR:
      case INPUT_RGBVECTOR:
      case INPUT_RGBAVECTOR:
        outValue = me->displacementValue;
        break;
    }
    return outValue;
}

DEFINE_DISPLACEMENT_SHADER(TestInputsDisplacement, displace)
