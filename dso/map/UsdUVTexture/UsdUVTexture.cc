// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

/// @file UsdUVTexture.cc

#include "attributes.cc"
#include "UsdUVTexture_ispc_stubs.h"

#include <moonray/common/mcrt_macros/moonray_static_check.h>
#include <moonray/rendering/shading/BasicTexture.h>
#include <moonray/rendering/shading/MapApi.h>
#include <moonray/rendering/shading/UdimTexture.h>
#include <scene_rdl2/render/util/stdmemory.h>

using namespace moonray::shading;
using namespace scene_rdl2;
using namespace scene_rdl2::math;

static ispc::StaticUsdUVTextureData sStaticUsdUVTextureData;

namespace {

bool
isStPrimvarReader(const scene_rdl2::rdl2::SceneObject *obj)
{
    return obj != nullptr &&
           obj->getSceneClass().getName() == "UsdPrimvarReader_float2" &&
           obj->get<scene_rdl2::rdl2::String>("varname") == "st";
}

void
computeUVTransformMapJacobian(const scene_rdl2::rdl2::SceneObject *stBinding,
                              float jacobian[4])
{
    const scene_rdl2::rdl2::SceneClass &sceneClass = stBinding->getSceneClass();
    const auto spaceKey = sceneClass.getAttributeKey<scene_rdl2::rdl2::Int>("space");
    const auto scaleKey = sceneClass.getAttributeKey<scene_rdl2::rdl2::Vec2f>("scale");
    const auto offsetKey = sceneClass.getAttributeKey<scene_rdl2::rdl2::Vec2f>("offset");
    const scene_rdl2::rdl2::Int textureSpace = sceneClass.getEnumValue(spaceKey, "texture");
    if (stBinding->get(spaceKey) != textureSpace ||
        stBinding->getBinding(scaleKey) || stBinding->getBinding(offsetKey)) {
        return;
    }

    // UVTransformMap rotates first, then scales and translates, so J = S * R(theta).
    const Vec2f &scale = stBinding->get<scene_rdl2::rdl2::Vec2f>(scaleKey);
    float sinTheta, cosTheta;
    sincos(deg2rad(stBinding->get<scene_rdl2::rdl2::Float>("rotation_angle")),
           &sinTheta, &cosTheta);
    jacobian[0] =  scale.x * cosTheta;
    jacobian[1] =  scale.x * sinTheta;
    jacobian[2] = -scale.y * sinTheta;
    jacobian[3] =  scale.y * cosTheta;
}

void
computeUsdTransform2dJacobian(const scene_rdl2::rdl2::SceneObject *stBinding,
                              float jacobian[4])
{
    const scene_rdl2::rdl2::SceneClass &sceneClass = stBinding->getSceneClass();
    const auto inKey = sceneClass.getAttributeKey<scene_rdl2::rdl2::Rgb>("in");
    if (!isStPrimvarReader(stBinding->getBinding(inKey))) {
        return;
    }

    // UsdTransform2d scales, then rotates and translates, so J = R(theta) * S.
    const Vec2f &scale = stBinding->get<scene_rdl2::rdl2::Vec2f>("scale");
    float sinTheta, cosTheta;
    sincos(deg2rad(stBinding->get<scene_rdl2::rdl2::Float>("rotation")),
           &sinTheta, &cosTheta);
    jacobian[0] =  cosTheta * scale.x;
    jacobian[1] = -sinTheta * scale.y;
    jacobian[2] =  sinTheta * scale.x;
    jacobian[3] =  cosTheta * scale.y;
}

// Row-major 2x2 Jacobian mapping the shading state's st derivatives to derivatives
// of this shader's st input.  Left zero (which selects the finest mip level) unless
// the input resolves to the "st" primvar or a recognized texture-space transform.
void
computeStJacobian(const scene_rdl2::rdl2::SceneObject *stBinding, float jacobian[4])
{
    jacobian[0] = jacobian[1] = jacobian[2] = jacobian[3] = 0.0f;

    if (isStPrimvarReader(stBinding)) {
        jacobian[0] = jacobian[3] = 1.0f;
        return;
    }
    if (stBinding == nullptr) {
        return;
    }

    const std::string &className = stBinding->getSceneClass().getName();
    if (className == "UVTransformMap") {
        computeUVTransformMapJacobian(stBinding, jacobian);
    } else if (className == "UsdTransform2d") {
        computeUsdTransform2dJacobian(stBinding, jacobian);
    }
}

} // namespace

//----------------------------------------------------------------------------

RDL2_DSO_CLASS_BEGIN(UsdUVTexture, scene_rdl2::rdl2::Map)

public:
    UsdUVTexture(const scene_rdl2::rdl2::SceneClass &sceneClass, const std::string &name);
    ~UsdUVTexture() override;
    void update() override;

private:
    static void sample(const scene_rdl2::rdl2::Map *self, moonray::shading::TLState *tls,
                       const moonray::shading::State &state, Color *sample);

    ispc::UsdUVTexture mIspc;
    std::unique_ptr<moonray::shading::BasicTexture> mTexture;
    std::unique_ptr<moonray::shading::UdimTexture> mUdimTexture;

RDL2_DSO_CLASS_END(UsdUVTexture)

//----------------------------------------------------------------------------

UsdUVTexture::UsdUVTexture(const scene_rdl2::rdl2::SceneClass &sceneClass, const std::string &name):
    Parent(sceneClass, name)
{
    mSampleFunc = UsdUVTexture::sample;
    mSampleFuncv = (scene_rdl2::rdl2::SampleFuncv) ispc::UsdUVTexture_getSampleFunc();

    mIspc.mUsdUVTextureDataPtr = (ispc::StaticUsdUVTextureData*)&sStaticUsdUVTextureData;

    // register shade time event messages.  we require and expect
    // these events to have the same value across all instances of the shader.
    // no conditional registration of events is allowed.
    // to allow for the possibility that we may someday create these maps
    // on multiple threads, we'll protect the writes of the class statics
    // with a mutex.
    static tbb::mutex errorMutex;
    tbb::mutex::scoped_lock lock(errorMutex);
    MOONRAY_START_THREADSAFE_STATIC_WRITE
    sStaticUsdUVTextureData.sErrorInvalidUdimCoord =
        sLogEventRegistry.createEvent(scene_rdl2::logging::ERROR_LEVEL,
                                      "invalid udim coordinate");
    MOONRAY_FINISH_THREADSAFE_STATIC_WRITE
}

UsdUVTexture::~UsdUVTexture()
{
}

void
UsdUVTexture::update()
{
    computeStJacobian(getBinding(attrSt), mIspc.mStJacobian);

    const std::string filename = get(attrFile);
    const std::size_t udimPos = filename.find("<UDIM>");
    const bool areWeAUdim = udimPos != std::string::npos;

    const scene_rdl2::rdl2::SceneVariables &sv = getSceneClass().getSceneContext()->getSceneVariables();
    mIspc.mFatalColor = asIspc(sv.get(scene_rdl2::rdl2::SceneVariables::sFatalColor));

    // If wrap types are set to the default which uses the metadata from the file
    // then fall back to a sensible mode.  Udim tiles are independent textures, so a
    // filter footprint that spills over a tile edge must clamp to the edge texels
    // (which approximate the neighboring tile) rather than wrap around this tile.
    const moonray::shading::WrapType defaultWrap = areWeAUdim ?
        moonray::shading::WrapType::Clamp : moonray::shading::WrapType::Periodic;

    moonray::shading::WrapType wrapS = static_cast<moonray::shading::WrapType>(get(attrWrapS));
    if (wrapS == moonray::shading::WrapType::Default) {
        wrapS = defaultWrap;
    }

    moonray::shading::WrapType wrapT = static_cast<moonray::shading::WrapType>(get(attrWrapT));
    if (wrapT == moonray::shading::WrapType::Default) {
        wrapT = defaultWrap;
    }

    std::string errorStr;
    if (areWeAUdim) {
        // Udim update, if needed
        mTexture.reset();
        mIspc.mTexture = nullptr;
        bool needsUpdate = false;
        if (!mUdimTexture) {
            needsUpdate = true;
            mUdimTexture = fauxstd::make_unique<moonray::shading::UdimTexture>(this);
            mIspc.mUdimTexture = &mUdimTexture->getUdimTextureData();
        }

        if (needsUpdate ||
            hasChanged(attrFile) ||
            hasChanged(attrWrapS) ||
            hasChanged(attrWrapT) ||
            hasChanged(attrFallback)) {

            if (!mUdimTexture->update(this,
                                      sLogEventRegistry,
                                      filename,
                                      static_cast<ispc::TEXTURE_GammaMode>(get(attrSourceColorSpace)),
                                      wrapS,
                                      wrapT,
                                      true, // use default/fallback color
                                      get(attrFallback),
                                      asCpp(mIspc.mFatalColor),
                                      errorStr)) {
                Logger::fatal(getSceneClass().getName(), "(\"", getName() , "\"): ", errorStr);
                mUdimTexture = nullptr;
                mIspc.mUdimTexture = nullptr;
                return;
            }
        }
    } else {
        // Non-udim update, if needed
        mUdimTexture.reset();
        mIspc.mUdimTexture = nullptr;
        bool needsUpdate = false;
        if (!mTexture) {
            needsUpdate = true;
            mTexture = fauxstd::make_unique<moonray::shading::BasicTexture>(this, sLogEventRegistry);
            mIspc.mTexture = &mTexture->getBasicTextureData();
        }
        if (needsUpdate ||
            hasChanged(attrFile) ||
            hasChanged(attrWrapS) ||
            hasChanged(attrWrapT) ||
            hasChanged(attrFallback)) {

            if (!mTexture->update(filename,
                                  static_cast<ispc::TEXTURE_GammaMode>(get(attrSourceColorSpace)),
                                  wrapS,
                                  wrapT,
                                  true, // use default/fallback color
                                  get(attrFallback),
                                  asCpp(mIspc.mFatalColor),
                                  errorStr)) {
                Logger::fatal(getSceneClass().getName(), "(\"", getName() , "\"): ", errorStr);
                mTexture = nullptr;
                mIspc.mTexture = nullptr;
                return;
            }
        }
    }

    if ((mTexture && !mTexture->isValid()) ||
        (mUdimTexture && !mUdimTexture->isValid())) {
        fatal(errorStr);
    }
}

void
UsdUVTexture::sample(const scene_rdl2::rdl2::Map *self,
                     moonray::shading::TLState *tls,
                     const moonray::shading::State &state,
                     Color *sample)
{
    UsdUVTexture const *me = static_cast<UsdUVTexture const *>(self);

    if (!me->mTexture && !me->mUdimTexture) {
        *sample = me->get(attrFallback);
        return;
    }

    Vec2f st = evalVec2f(me, attrSt, tls, state);

    const float * const jacobian = me->mIspc.mStJacobian;
    const float dsdx = jacobian[0] * state.getdSdx() + jacobian[1] * state.getdTdx();
    const float dtdx = jacobian[2] * state.getdSdx() + jacobian[3] * state.getdTdx();
    const float dsdy = jacobian[0] * state.getdSdy() + jacobian[1] * state.getdTdy();
    const float dtdy = jacobian[2] * state.getdSdy() + jacobian[3] * state.getdTdy();

    // Flipping t reverses its direction of change, so negate its derivatives.
    // Udim tiling only translates st, so the same derivatives apply to both paths.
    float derivatives[4] = { dsdx, -dtdx, dsdy, -dtdy };

    Color4 tx;
    if (me->mTexture) {
        // Flip the t coordinate about 0.5.
        st.y = 1.0 - st.y;

        // sample the texture
        tx = me->mTexture->sample(tls,
                                  state,
                                  st,
                                  derivatives);
    } else if (me->mUdimTexture) {
        // compute udim index
        int udim = me->mUdimTexture->computeUdim(tls, st.x, st.y);
        if (udim == -1) {
            logEvent(me, me->mIspc.mUsdUVTextureDataPtr->sErrorInvalidUdimCoord);
            *sample = asCpp(me->mIspc.mFatalColor);
            return;
        }

        // take fractional parts of st
        st.x = st.x - int(st.x);
        st.y = st.y - int(st.y);

        // Flip the t coordinate about 0.5.
        st.y = 1.0 - st.y;

        // sample the texture
        tx = me->mUdimTexture->sample(tls,
                                      state,
                                      udim,
                                      st,
                                      derivatives);
    }

    Color rgb;
    switch (me->get(attrOutputMode)) {
    case ispc::OUTPUT_R:
        rgb = Color(tx.r);
        break;
    case ispc::OUTPUT_G:
        rgb = Color(tx.g);
        break;
    case ispc::OUTPUT_B:
        rgb = Color(tx.b);
        break;
    case ispc::OUTPUT_A:
        rgb = Color(tx.a);
        break;
    default:
        rgb = Color(tx);
    }

    rgb = rgb * me->get(attrScale) + me->get(attrBias);
    *sample = rgb;
}
