// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

/// @file Bsdf.ispc

#include "Bsdf.isph"
#include "../bssrdf/Bssrdf.isph"
#include "BsdfSlice.isph"
#include "BsdfUtil.isph"

#include <scene_rdl2/common/platform/IspcUtil.isph>


#pragma ignore warning(all)
ISPC_UTIL_EXPORT_STRUCT_TO_HEADER(Bsdf);
#pragma ignore warning(all)
ISPC_UTIL_EXPORT_STRUCT_TO_HEADER(BsdfLobe);
ISPC_UTIL_EXPORT_ENUM_TO_HEADER(BsdfLobeName);

uniform bool
BsdfLobe_getProperty(const varying BsdfLobe * uniform lobe,
                     uniform BsdfLobeProperty property,
                     varying float * uniform dest)
{
    uniform bool handled = true;

    BsdfLobeGetPropertyFn getPropertyFn = (BsdfLobeGetPropertyFn) lobe->mGetPropertyFn;
    if (!getPropertyFn(lobe, property, dest)) {
        // handle what we can
        switch (property) {
        case BSDF_LOBE_PROPERTY_COLOR:
            {
                varying Color c = BsdfLobe_computeScaleAndFresnel(lobe, 1.f);
                *dest       = c.r;
                *(dest + 1) = c.g;
                *(dest + 2) = c.b;
            }
            break;
        default:
            handled = false;
        }
    }

    return handled;
}

void
Bsdf_show(const varying Bsdf &bsdf,
          const uniform Material * const uniform material)
{
    CPP_printBsdfHeader(material, &bsdf);

    for (uniform int i = 0; i < bsdf.mNumLobes; ++i) {
        varying BsdfLobe * uniform lobe = bsdf.mLobes[i];
        const uniform intptr_t lightSet = BsdfLobe_getLightSet(lobe);
        if (lightSet != 0) {
            CPP_printLightSetInfo((const uniform LightSet * uniform) lightSet);
        } else {
            print("<LightSet> : '<none>'\n");
        }
        print("label: %\n", lobe->mLabel);
        BsdfLobe_show(lobe, 0);
        print("\n");
    }

    for (uniform int i = 0; i < bsdf.mNumBssrdfs; ++i) {
        varying Bssrdf * uniform bssrdf = bsdf.mBssrdfs[i];
        const uniform LightSet * uniform lightSet = Bssrdf_getLightSet(bssrdf);
        if (lightSet != 0) {
            CPP_printLightSetInfo(lightSet);
        } else {
            print("<LightSet> : '<none>'\n");
        }
        Bssrdf_show(bssrdf, 0);
        print("\n");
    }

    print("[emission] = % % %\n", BSDF_UTIL_EXTRACT(bsdf.mSelfEmission.r),
                                  BSDF_UTIL_EXTRACT(bsdf.mSelfEmission.g),
                                  BSDF_UTIL_EXTRACT(bsdf.mSelfEmission.b));

    print("\n==========================================================\n");
}
export void * uniform getBsdfShowFn() { return Bsdf_show; }

void
Bsdf_setPostScatterExtraAovs(varying Bsdf * uniform bsdf,
                             uniform int numExtraAovs,
                             const uniform int * uniform labelIds,
                             const varying Color * uniform colors)
{
    bsdf->mPostScatterExtraAovs.mNum = numExtraAovs;
    bsdf->mPostScatterExtraAovs.mLabelIds = labelIds;
    bsdf->mPostScatterExtraAovs.mColors = colors;
}

