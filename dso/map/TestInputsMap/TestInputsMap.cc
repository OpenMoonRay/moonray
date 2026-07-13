// Copyright 2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

/// @file TestVec4fInputMap.cc

#include "attributes.cc"
#include "TestInputsMap_ispc_stubs.h"

#include <moonray/rendering/shading/MapApi.h>
#include <scene_rdl2/common/math/ispc/Typesv.h>
using namespace scene_rdl2::math;
using namespace scene_rdl2::rdl2;

RDL2_DSO_CLASS_BEGIN(TestInputsMap, Map)
public:
    TestInputsMap(SceneClass const &sceneClass, std::string const &name);
    void update();

private:
    static void sample(const Map *self, moonray::shading::TLState *tls,
                       const moonray::shading::State &state, Color *sample);
    static void sample_vector(const Map* self,
                              moonray::shading::TLState *tls,
                              const Statev* state,
                              scene_rdl2::rdl2::Colorv* sample,
                              SIMD_MASK_TYPE implicitMask);
    // mIspc needs to be the first use of storage by this derived class
    // since ISPC cheats and gets it's offset by knowing it's first
    ispc::TestInputsMap mIspc;
    SampleFuncv mIspcSampleFuncv;

public:

RDL2_DSO_CLASS_END(TestInputsMap)

void TestInputsMap::sample_vector(const Map* self,
                                  moonray::shading::TLState *tls,
                                  const Statev* state,
                                  scene_rdl2::rdl2::Colorv* sample,
                                  SIMD_MASK_TYPE implicitMask)
{
    const TestInputsMap* me = static_cast<const TestInputsMap*>(self);
    if (me->mIspc.choice != ispc::INPUT_STRING) {
        // (me->mIspcSampleFuncv)(self, tls, state, sample, implicitMask);
        ((SampleFuncv) ispc::TestInputsMap_getSampleFunc())(self, tls, state, sample, implicitMask);
    } else {
        String inStringValue = me->get(inStringAttr);
        int firstSpace = inStringValue.find(' ');
        int secondSpace = inStringValue.rfind(' ');
        Color outValue = Color(0.0,0.0,0.0);
        if ((firstSpace != std::string::npos) && (secondSpace != std::string::npos)) {
            outValue.r = std::stof(inStringValue.substr(0,firstSpace));
            outValue.g = std::stof(inStringValue.substr(firstSpace+1, secondSpace - firstSpace - 1));
            outValue.b = std::stof(inStringValue.substr(secondSpace+1));
        }
        int* mask = (int*)(&implicitMask);
        scene_rdl2::math::Colorv* result = (scene_rdl2::math::Colorv*)sample;
        for (int i=0; i<VLEN; i++) {
            if (mask[i]) {
                result->r[i]=outValue.r;
                result->g[i]=outValue.g;
                result->b[i]=outValue.b;
            }
        }
    }
}

TestInputsMap::TestInputsMap(const SceneClass& sceneClass,
        const std::string& name) :
    Parent(sceneClass, name)
{
    mSampleFunc = TestInputsMap::sample;
    mIspcSampleFuncv = (SampleFuncv) ispc::TestInputsMap_getSampleFunc();
    mSampleFuncv = TestInputsMap::sample_vector;
}

void
TestInputsMap::update()
{
    String select = get(selectAttr);
    int size = get(arraySizeAttr);
    int index = get(arrayIndexAttr);
    if ((select == "bool") || (select == "Bool")) {
        mIspc.choice= ispc::INPUT_BOOL;
    } else if (select == "RawBool") {
        mIspc.choice= ispc::INPUT_RAW_BOOL;
    } else if ((select == "int") || (select == "Int")) {
        mIspc.choice= ispc::INPUT_INT;
    } else if (select == "RawInt") {
        mIspc.choice= ispc::INPUT_RAW_INT;
    } else if (select == "String") {
        String inStringValue = get(inStringAttr);
        int firstSpace = inStringValue.find(' ');
        int secondSpace = inStringValue.rfind(' ');
        if ((firstSpace != std::string::npos) && (secondSpace != std::string::npos)) {
            mIspc.colorValue.r = std::stof(inStringValue.substr(0,firstSpace));
            mIspc.colorValue.g = std::stof(inStringValue.substr(firstSpace+1, secondSpace - firstSpace - 1));
            mIspc.colorValue.b = std::stof(inStringValue.substr(secondSpace+1));
        } else {
            mIspc.colorValue.r = 0.0;
            mIspc.colorValue.g = 0.0;
            mIspc.colorValue.b = 0.0;
        }
        mIspc.choice= ispc::INPUT_STRING;
    } else if (select == "Float") {
        mIspc.choice= ispc::INPUT_FLOAT;
    } else if (select == "Vec2f") {
        mIspc.choice= ispc::INPUT_VEC2F;
    } else if (select == "Vec3f") {
        mIspc.choice= ispc::INPUT_VEC3F;
    } else if (select == "Vec4f") {
        mIspc.choice= ispc::INPUT_VEC4F;
    } else if (select == "Color") {
        mIspc.choice= ispc::INPUT_COLOR;
    } else if (select == "Rgba") {
        mIspc.choice= ispc::INPUT_RGBA;
    } else if (select == "Mat3f") {
        mIspc.choice= ispc::INPUT_MAT3F;
    } else if (select == "Mat4f") {
        mIspc.choice= ispc::INPUT_MAT4F;
    } else if (select == "SceneObject") {
        mIspc.choice= ispc::INPUT_SCENEOBJECT;
    } else if (select == "IntVector") {
        int inIntValue = get(inIntVectorAttr)[index];
        mIspc.colorValue.r = (inIntValue&0xFF)/255.0;
        mIspc.colorValue.g = ((inIntValue>>8)&0xFF)/255.0;
        mIspc.colorValue.b = ((inIntValue>>16)&0xFF)/255.0;
        mIspc.choice= ispc::INPUT_INTVECTOR;
    } else if (select == "StringVector") {
        String inStringValue = get(inStringVectorAttr)[index];
        int firstSpace = inStringValue.find(' ');
        int secondSpace = inStringValue.rfind(' ');
        if ((firstSpace != std::string::npos) && (secondSpace != std::string::npos)) {
            mIspc.colorValue.r = std::stof(inStringValue.substr(0,firstSpace));
            mIspc.colorValue.g = std::stof(inStringValue.substr(firstSpace+1, secondSpace - firstSpace - 1));
            mIspc.colorValue.b = std::stof(inStringValue.substr(secondSpace+1));
        } else {
            mIspc.colorValue.r = 0.0;
            mIspc.colorValue.g = 0.0;
            mIspc.colorValue.b = 0.0;
        }
        mIspc.choice= ispc::INPUT_STRINGVECTOR;
    } else if (select == "FloatVector") {
        float inFloatValue = get(inFloatVectorAttr)[index];
        mIspc.colorValue.r = inFloatValue;
        mIspc.colorValue.g = inFloatValue;
        mIspc.colorValue.b = inFloatValue;
        mIspc.choice= ispc::INPUT_FLOATVECTOR;
    } else if (select == "Vec2fVector") {
        Vec2f inVec2fValue = get(inVec2fVectorAttr)[index];
        mIspc.colorValue.r = inVec2fValue.x;
        mIspc.colorValue.g = inVec2fValue.y;
        mIspc.colorValue.b = 0.0f;
        mIspc.choice= ispc::INPUT_VEC2FVECTOR;
    } else if (select == "Vec3fVector") {
        Vec3f inVec3fValue = get(inVec3fVectorAttr)[index];
        mIspc.colorValue.r = inVec3fValue.x;
        mIspc.colorValue.g = inVec3fValue.y;
        mIspc.colorValue.b = inVec3fValue.z;
        mIspc.choice= ispc::INPUT_VEC3FVECTOR;
    } else if (select == "Vec4fVector") {
        Vec4f inVec4fValue = get(inVec4fVectorAttr)[index];
        mIspc.colorValue.r = inVec4fValue.x;
        mIspc.colorValue.g = inVec4fValue.y;
        mIspc.colorValue.b = inVec4fValue.z;
        mIspc.choice= ispc::INPUT_VEC4FVECTOR;
    } else if (select == "RgbVector") {
        Color inRgbValue = get(inRgbVectorAttr)[index];
        mIspc.colorValue.r = inRgbValue.r;
        mIspc.colorValue.g = inRgbValue.g;
        mIspc.colorValue.b = inRgbValue.b;
        mIspc.choice= ispc::INPUT_RGBVECTOR;
    } else if (select == "RgbaVector") {
        Rgba inRgbaValue = get(inRgbaVectorAttr)[index];
        mIspc.colorValue.r = inRgbaValue.r;
        mIspc.colorValue.g = inRgbaValue.g;
        mIspc.colorValue.b = inRgbaValue.b;
        mIspc.choice= ispc::INPUT_RGBAVECTOR;
    } else {
        mIspc.choice= -1;
    }
}

void
TestInputsMap::sample(const Map* self, moonray::shading::TLState *tls,
                 const moonray::shading::State& state, Color* sample)
{
    const TestInputsMap* me = static_cast<const TestInputsMap*>(self);

    Bool   inBoolValue   = evalBool(  me, inBoolAttr,   tls, state);
    Int    inIntValue    = evalInt(   me, inIntAttr,    tls, state);
    Float  inFloatValue  = evalFloat( me, inFloatAttr,  tls, state);
    Vec2f  inVec2fValue  = evalVec2f( me, inVec2fAttr,  tls, state);
    Vec3f  inVec3fValue  = evalVec3f( me, inVec3fAttr,  tls, state);
    Vec4f  inVec4fValue  = evalVec4f( me, inVec4fAttr,  tls, state);
    Color  inColorValue  = evalColor( me, inColorAttr,  tls, state);
    Rgba   inRgbaValue   = evalRgba(  me, inRgbaAttr,   tls, state);
    Mat3f  inMat3fValue  = evalMat3f( me, inMat3fAttr,  tls, state);
    Mat4f  inMat4fValue  = evalMat4f( me, inMat4fAttr,  tls, state);
    SceneObject* inSceneObjectValue = evalSceneObject( me, inSceneObjectAttr,  tls, state);

    Color outValue = Color(0.0,0.0,0.0);

    switch (me->mIspc.choice) {
      case ispc::INPUT_BOOL:
        if (inBoolValue) {
            outValue = Color(0.75, 0.75, 0.75);
        } else {
            outValue = Color(0.25, 0.25, 0.25);
        }
        break;
      case ispc::INPUT_RAW_BOOL:
        if (inBoolValue) {
            outValue = Color(1.0,1.0,1.0);
        } else {
            outValue = Color(0.0,0.0,0.0);
        }
        break;
      case ispc::INPUT_INT:
        outValue.r = (inIntValue&0xFF)/255.0;
        outValue.g = ((inIntValue>>8)&0xFF)/255.0;
        outValue.b = ((inIntValue>>16)&0xFF)/255.0;
        break;
      case ispc::INPUT_RAW_INT:
        outValue = Color(inIntValue,inIntValue,inIntValue);
        break;
      case ispc::INPUT_STRING:
        outValue = Color(me->mIspc.colorValue.r, me->mIspc.colorValue.g, me->mIspc.colorValue.b);
        break;
      case ispc::INPUT_FLOAT:
        outValue = Color(inFloatValue);
        break;
      case ispc::INPUT_VEC2F:
        outValue = Color(inVec2fValue.x, inVec2fValue.y, 0.0f);
        break;
      case ispc::INPUT_VEC3F:
        outValue = Color(inVec3fValue.x, inVec3fValue.y, inVec3fValue.z);
        break;
      case ispc::INPUT_VEC4F:
        outValue = Color(inVec4fValue.x, inVec4fValue.y, inVec4fValue.z);
        break;
      case ispc::INPUT_COLOR:
        outValue = inColorValue;
        break;
      case ispc::INPUT_RGBA:
        outValue = Color(inRgbaValue.r,inRgbaValue.g, inRgbaValue.b);
        break;
      case ispc::INPUT_MAT3F:
        outValue = Color(inMat3fValue.vx.x,inMat3fValue.vy.y, inMat3fValue.vz.z);
        break;
      case ispc::INPUT_MAT4F:
        outValue = Color(inMat4fValue.vx.x,inMat4fValue.vy.y, inMat4fValue.vz.z);
        break;
      case ispc::INPUT_SCENEOBJECT:
        if (inSceneObjectValue == nullptr) {
            outValue= Color(0.1,0.1,0.1);
        } else {
            outValue = inSceneObjectValue->get<Color>(std::string("color_value"));
        }
        break;
      case ispc::INPUT_INTVECTOR:
      case ispc::INPUT_STRINGVECTOR:
      case ispc::INPUT_FLOATVECTOR:
      case ispc::INPUT_VEC2FVECTOR:
      case ispc::INPUT_VEC3FVECTOR:
      case ispc::INPUT_VEC4FVECTOR:
      case ispc::INPUT_RGBVECTOR:
      case ispc::INPUT_RGBAVECTOR:
        outValue = Color(me->mIspc.colorValue.r, me->mIspc.colorValue.g, me->mIspc.colorValue.b);
        break;
      default:
        break;
    }

    *sample = outValue;
}

