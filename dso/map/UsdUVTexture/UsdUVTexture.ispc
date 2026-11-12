// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

/// @file UsdUVTexture.ispc

#include "attributes.isph"

#include <moonray/rendering/shading/ispc/BasicTexture.isph>
#include <moonray/rendering/shading/ispc/ColorCorrect.isph>
#include <moonray/rendering/shading/ispc/MapApi.isph>
#include <moonray/rendering/shading/ispc/UdimTexture.isph>

enum OutputMode {
    OUTPUT_RGB = 0,
    OUTPUT_R   = 1,
    OUTPUT_G   = 2,
    OUTPUT_B   = 3,
    OUTPUT_A   = 4
};
ISPC_UTIL_EXPORT_ENUM_TO_HEADER(OutputMode);

struct StaticUsdUVTextureData {
    // shade time error events - these can be static as long as we
    // ensure they have the same value across all instances.
    // no conditional registration of log events is allowed.
    uniform int sErrorInvalidUdimCoord;
};
ISPC_UTIL_EXPORT_UNIFORM_STRUCT_TO_HEADER(StaticUsdUVTextureData);

struct UsdUVTexture
{
    const uniform BASIC_TEXTURE_Data * uniform mTexture;
    const uniform UDIM_TEXTURE_Data * uniform mUdimTexture;
    uniform Color mFatalColor;

    // Row-major 2x2 Jacobian mapping the state's st derivatives to derivatives of
    // this shader's st input.  Zero when the input isn't a recognized "st" chain.
    uniform float mStJacobian[4];

    uniform StaticUsdUVTextureData* uniform mUsdUVTextureDataPtr;
};
ISPC_UTIL_EXPORT_UNIFORM_STRUCT_TO_HEADER(UsdUVTexture);

static Color
sample(const uniform Map* uniform map,
       uniform ShadingTLState* uniform tls,
       const varying State& state)
{
    const uniform UsdUVTexture * uniform me = MAP_GET_ISPC_PTR(UsdUVTexture, map);

    if (!me->mTexture && !me->mUdimTexture) {
        return getAttrFallback(map);
    }

    Color result = Color_ctor(0.0f, 0.0f, 0.0f);

    const Vec2f uv = evalAttrSt(map, tls, state);
    varying Vec2f st = uv;

    const uniform float * uniform J = me->mStJacobian;
    const varying float dsdx = J[0] * getdSdx(state) + J[1] * getdTdx(state);
    const varying float dtdx = J[2] * getdSdx(state) + J[3] * getdTdx(state);
    const varying float dsdy = J[0] * getdSdy(state) + J[1] * getdTdy(state);
    const varying float dtdy = J[2] * getdSdy(state) + J[3] * getdTdy(state);

    // Flipping t reverses its direction of change, so negate its derivatives.
    // Udim tiling only translates st, so the same derivatives apply to both paths.
    float derivatives[4] = { dsdx, -dtdx, dsdy, -dtdy };

    varying Col4f tx;
    if (me->mTexture) {
        // Flip the t coordinate about 0.5.
        st.y = 1.0 - st.y;

        tx = BASIC_TEXTURE_sample(me->mTexture,
                                  tls,
                                  state,
                                  st,
                                  derivatives);
    } else if (me->mUdimTexture) {
        // compute udim index
        varying int udim = UDIM_TEXTURE_compute_udim(me->mUdimTexture,
                                                     tls, st);
        if (udim == -1) {
            logEvent(map, me->mUsdUVTextureDataPtr->sErrorInvalidUdimCoord);
            return me->mUdimTexture->mFatalColor;
        }

        // take fractional parts of st
        st.x = st.x - (int)st.x;
        st.y = st.y - (int)st.y;

        // Flip the t coordinate about 0.5.
        st.y = 1.0 - st.y;

        tx = UDIM_TEXTURE_sample(me->mUdimTexture,
                                 tls,
                                 state,
                                 udim,
                                 st,
                                 derivatives);
    }

    switch (getAttrOutputMode(map)) {
    case OUTPUT_R:
        result = Color_ctor(tx.r);
        break;
    case OUTPUT_G:
        result = Color_ctor(tx.g);
        break;
    case OUTPUT_B:
        result = Color_ctor(tx.b);
        break;
    case OUTPUT_A:
        result = Color_ctor(tx.a);
        break;
    default:
        result = Color_ctor(tx.r, tx.g, tx.b);
    }

    Color scl = getAttrScale(map);
    Color bs = getAttrBias(map);
    result = result * scl + bs;

    return result;
}

DEFINE_MAP_SHADER(UsdUVTexture, sample)
