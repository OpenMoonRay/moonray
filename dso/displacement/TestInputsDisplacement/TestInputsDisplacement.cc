// Copyright 2026 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

/// @file TestInputsDisplacement.cc

#include "attributes.cc"
#include "TestInputsDisplacement_ispc_stubs.h"

#include <moonray/rendering/shading/MapApi.h>
#include <scene_rdl2/common/math/ispc/Typesv.h>
using namespace scene_rdl2::math;
using namespace scene_rdl2::rdl2;

RDL2_DSO_CLASS_BEGIN(TestInputsDisplacement, Displacement)
public:
    TestInputsDisplacement(SceneClass const &sceneClass, std::string const &name);
    void update();

private:
    static void displace(const Displacement *self, moonray::shading::TLState *tls,
                         const moonray::shading::State &state, Vec3f *sample);
    // mIspc needs to be the first use of storage by this derived class
    // since ISPC cheats and gets it's offset by knowing it's first
    ispc::TestInputsDisplacement mIspc;

public:

RDL2_DSO_CLASS_END(TestInputsDisplacement)

TestInputsDisplacement::TestInputsDisplacement(const SceneClass& sceneClass,
        const std::string& name) :
    Parent(sceneClass, name)
{
    mDisplaceFunc = TestInputsDisplacement::displace;
    mDisplaceFuncv = (DisplaceFuncv) ispc::TestInputsDisplacement_getDisplaceFunc();
}

void
TestInputsDisplacement::update()
{
    String select = get(selectAttr);
    int size = get(arraySizeAttr);
    int index = get(arrayIndexAttr);
    if ((select == "bool") || (select == "Bool")) {
        mIspc.choice= ispc::INPUT_BOOL;
    } else if ((select == "int") || (select == "Int")) {
        mIspc.choice= ispc::INPUT_INT;
    } else if (select == "String") {
        String inStringValue = get(inStringAttr);
        int firstSpace = inStringValue.find(' ');
        int secondSpace = inStringValue.rfind(' ');
        if ((firstSpace != std::string::npos) && (secondSpace != std::string::npos)) {
            mIspc.displacementValue.x = std::stof(inStringValue.substr(0,firstSpace));
            mIspc.displacementValue.y = std::stof(inStringValue.substr(firstSpace+1, secondSpace - firstSpace - 1));
            mIspc.displacementValue.z = std::stof(inStringValue.substr(secondSpace+1));
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
        mIspc.displacementValue.x = (inIntValue&0xFF)/255.0;
        mIspc.displacementValue.y = ((inIntValue>>8)&0xFF)/255.0;
        mIspc.displacementValue.z = ((inIntValue>>16)&0xFF)/255.0;
        mIspc.choice= ispc::INPUT_INTVECTOR;
    } else if (select == "StringVector") {
        String inStringValue = get(inStringVectorAttr)[index];
        int firstSpace = inStringValue.find(' ');
        int secondSpace = inStringValue.rfind(' ');
        if ((firstSpace != std::string::npos) && (secondSpace != std::string::npos)) {
            mIspc.displacementValue.x = std::stof(inStringValue.substr(0,firstSpace));
            mIspc.displacementValue.y = std::stof(inStringValue.substr(firstSpace+1, secondSpace - firstSpace - 1));
            mIspc.displacementValue.z = std::stof(inStringValue.substr(secondSpace+1));
        }
        mIspc.choice= ispc::INPUT_STRINGVECTOR;
    } else if (select == "FloatVector") {
        float inFloatValue = get(inFloatVectorAttr)[index];
        mIspc.displacementValue.x = inFloatValue;
        mIspc.displacementValue.y = inFloatValue;
        mIspc.displacementValue.z = inFloatValue;
        mIspc.choice= ispc::INPUT_FLOATVECTOR;
    } else if (select == "Vec2fVector") {
        Vec2f inVec2fValue = get(inVec2fVectorAttr)[index];
        mIspc.displacementValue.x = inVec2fValue.x;
        mIspc.displacementValue.y = inVec2fValue.y;
        mIspc.displacementValue.z = 0.0f;
        mIspc.choice= ispc::INPUT_VEC2FVECTOR;
    } else if (select == "Vec3fVector") {
        Vec3f inVec3fValue = get(inVec3fVectorAttr)[index];
        mIspc.displacementValue.x = inVec3fValue.x;
        mIspc.displacementValue.y = inVec3fValue.y;
        mIspc.displacementValue.z = inVec3fValue.z;
        mIspc.choice= ispc::INPUT_VEC3FVECTOR;
    } else if (select == "Vec4fVector") {
        Vec4f inVec4fValue = get(inVec4fVectorAttr)[index];
        mIspc.displacementValue.x = inVec4fValue.x;
        mIspc.displacementValue.y = inVec4fValue.y;
        mIspc.displacementValue.z = inVec4fValue.z;
        mIspc.choice= ispc::INPUT_VEC4FVECTOR;
    } else if (select == "RgbVector") {
        Color inColorValue = get(inRgbVectorAttr)[index];
        mIspc.displacementValue.x = inColorValue.r;
        mIspc.displacementValue.y = inColorValue.g;
        mIspc.displacementValue.z = inColorValue.b;
        mIspc.choice= ispc::INPUT_RGBVECTOR;
    } else if (select == "RgbaVector") {
        Rgba inRgbaValue = get(inRgbaVectorAttr)[index];
        mIspc.displacementValue.x = inRgbaValue.r;
        mIspc.displacementValue.y = inRgbaValue.g;
        mIspc.displacementValue.z = inRgbaValue.b;
        mIspc.choice= ispc::INPUT_RGBAVECTOR;
    } else {
        mIspc.choice= -1;
    }
}

void
TestInputsDisplacement::displace(const Displacement* self, moonray::shading::TLState *tls,
                 const moonray::shading::State& state, Vec3f* sample)
{
    const TestInputsDisplacement* me = static_cast<const TestInputsDisplacement*>(self);

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

    // Vector attibutes are fetched in update since all Vector inputs are uniform

    Vec3f outValue = Vec3f(0.0,0.0,0.0);

    switch (me->mIspc.choice) {
      case ispc::INPUT_BOOL:
        if (inBoolValue) {
            outValue = Vec3f(0.75, 0.75, 0.75);
        } else {
            outValue = Vec3f(0.25, 0.25, 0.25);
        }
        break;
      case ispc::INPUT_INT:
        outValue.x = (inIntValue&0xFF)/255.0;
        outValue.y = ((inIntValue>>8)&0xFF)/255.0;
        outValue.z = ((inIntValue>>16)&0xFF)/255.0;
        break;
      case ispc::INPUT_STRING:
        outValue = Vec3f(me->mIspc.displacementValue.x, me->mIspc.displacementValue.y, me->mIspc.displacementValue.z);
        break;
      case ispc::INPUT_FLOAT:
        outValue = Vec3f(inFloatValue);
        break;
      case ispc::INPUT_VEC2F:
        outValue = Vec3f(inVec2fValue.x, inVec2fValue.y, 0.0f);
        break;
      case ispc::INPUT_VEC3F:
        outValue = inVec3fValue;
        break;
      case ispc::INPUT_VEC4F:
        outValue = Vec3f(inVec4fValue.x, inVec4fValue.y, inVec4fValue.z);
        break;
      case ispc::INPUT_COLOR:
        outValue = Vec3f(inColorValue.r, inColorValue.g, inColorValue.b);
        break;
      case ispc::INPUT_RGBA:
        outValue = Vec3f(inRgbaValue.r,inRgbaValue.g, inRgbaValue.b);
        break;
      case ispc::INPUT_MAT3F:
        outValue = Vec3f(inMat3fValue.vx.x,inMat3fValue.vy.y, inMat3fValue.vz.z);
        break;
      case ispc::INPUT_MAT4F:
        outValue = Vec3f(inMat4fValue.vx.x,inMat4fValue.vy.y, inMat4fValue.vz.z);
        break;
      case ispc::INPUT_SCENEOBJECT:
        if (inSceneObjectValue == nullptr) {
            outValue= Vec3f(0.1,0.1,0.1);
        } else {
            outValue = inSceneObjectValue->get<Vec3f>(std::string("color_value"));
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
        // Vector results are computed in update since all Vector inputs are uniform
        outValue = Vec3f(me->mIspc.displacementValue.x, me->mIspc.displacementValue.y, me->mIspc.displacementValue.z);
        break;
      default:
        break;
    }

    *sample = outValue;
}

