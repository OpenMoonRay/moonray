// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#pragma once
#include <scene_rdl2/common/platform/HybridVaryingData.hh>


//----------------------------------------------------------------------------

// Identifies where the primary ray comes from
#define SUBPIXEL_MEMBERS                                                \
    /* Pixel location, this doubles as a 32-bit sort key for the */     \
    /* radiance queues. */                                              \
    HVD_MEMBER(uint32_t, mPixel);                                       \
    HVD_MEMBER(int, mSubpixelIndex);                                    \
    HVD_MEMBER(float, mSubpixelX);                                      \
    HVD_MEMBER(float, mSubpixelY);                                      \
    HVD_MEMBER(int, mPixelSamples);                                     \
    HVD_MEMBER(float, mSampleClampingValue);                            \
    HVD_MEMBER(float, mPrimaryRayDiffScale);                            \
    HVD_MEMBER(float, mTextureDiffScale);                               \
    HVD_PTR(void *, mCryptomatteBuffer);                                \
    HVD_PTR(void *, mCryptomatteResultsArray);                          \
    HVD_PTR(DeferredNode *, mDeferredNodesHead);                        \
    HVD_PTR(DeferredNode **, mDeferredNodesTailPtr);                    \
    HVD_MEMBER(int, mNumDeferredNodes);                                 \
    HVD_ISPC_PAD(mPad, 4)


#define SUBPIXEL_VALIDATION(vlen)                                       \
    HVD_BEGIN_VALIDATION(Subpixel, vlen);                               \
    HVD_VALIDATE(Subpixel, mPixel);                                     \
    HVD_VALIDATE(Subpixel, mSubpixelIndex);                             \
    HVD_VALIDATE(Subpixel, mSubpixelX);                                 \
    HVD_VALIDATE(Subpixel, mSubpixelY);                                 \
    HVD_VALIDATE(Subpixel, mPixelSamples);                              \
    HVD_VALIDATE(Subpixel, mSampleClampingValue);                       \
    HVD_VALIDATE(Subpixel, mPrimaryRayDiffScale);                       \
    HVD_VALIDATE(Subpixel, mTextureDiffScale);                          \
    HVD_VALIDATE(Subpixel, mCryptomatteBuffer);                         \
    HVD_VALIDATE(Subpixel, mCryptomatteResultsArray);                   \
    HVD_VALIDATE(Subpixel, mDeferredNodesHead);                         \
    HVD_VALIDATE(Subpixel, mDeferredNodesTailPtr);                      \
    HVD_VALIDATE(Subpixel, mNumDeferredNodes);                          \
    HVD_END_VALIDATION


//----------------------------------------------------------------------------

#define PATH_VERTEX_MEMBERS                                                 /*  size */\
    HVD_MEMBER(HVD_NAMESPACE(scene_rdl2::math, Color), pathThroughput);     /*   12  */\
    /* Frame buffer path weight. */                                                    \
    HVD_MEMBER(float, pathPixelWeight);                                     /*   16  */\
    HVD_MEMBER(float, aovPathPixelWeight);                                  /*   20  */\
    HVD_MEMBER(float, pathDistance);                                        /*   24  */\
    HVD_MEMBER(HVD_NAMESPACE(scene_rdl2::math, Vec2f), minRoughness);       /*   32  */\
    HVD_MEMBER(int, diffuseDepth);                                          /*   36  */\
    HVD_MEMBER(int, volumeDepth);                                           /*   40  */\
    HVD_MEMBER(int, glossyDepth);                                           /*   44  */\
    HVD_MEMBER(int, mirrorDepth);                                           /*   48  */\
    HVD_MEMBER(int, nonMirrorDepth);                                        /*   52  */\
    HVD_MEMBER(int, presenceDepth);                                         /*   56  */\
    HVD_MEMBER(float, totalPresence);                                       /*   60  */\
    HVD_MEMBER(int, hairDepth);                                             /*   64  */\
    HVD_MEMBER(int, subsurfaceDepth);                                       /*   68  */\
    HVD_MEMBER(float, accumOpacity);                                        /*   72  */\
    /* for lpe aovs */                                                                 \
    HVD_MEMBER(int, lpeStateId);                                            /*   76  */\
    /* only used by bundling incoherent ray */                                         \
    /* queue, invalid in all other cases    */                                         \
    HVD_MEMBER(int, lpeStateIdLight);                                       /*   80  */\
    HVD_MEMBER(int, lobeType)                                               /*   84  */


#define PATH_VERTEX_VALIDATION(vlen)                \
    HVD_BEGIN_VALIDATION(PathVertex, vlen);         \
    HVD_VALIDATE(PathVertex, pathThroughput);       \
    HVD_VALIDATE(PathVertex, pathPixelWeight);      \
    HVD_VALIDATE(PathVertex, aovPathPixelWeight);   \
    HVD_VALIDATE(PathVertex, pathDistance);         \
    HVD_VALIDATE(PathVertex, minRoughness);         \
    HVD_VALIDATE(PathVertex, diffuseDepth);         \
    HVD_VALIDATE(PathVertex, volumeDepth);          \
    HVD_VALIDATE(PathVertex, glossyDepth);          \
    HVD_VALIDATE(PathVertex, mirrorDepth);          \
    HVD_VALIDATE(PathVertex, nonMirrorDepth);       \
    HVD_VALIDATE(PathVertex, presenceDepth);        \
    HVD_VALIDATE(PathVertex, totalPresence);        \
    HVD_VALIDATE(PathVertex, hairDepth);            \
    HVD_VALIDATE(PathVertex, subsurfaceDepth);      \
    HVD_VALIDATE(PathVertex, accumOpacity);         \
    HVD_VALIDATE(PathVertex, lpeStateId);           \
    HVD_VALIDATE(PathVertex, lpeStateIdLight);      \
    HVD_VALIDATE(PathVertex, lobeType);             \
    HVD_END_VALIDATION


//----------------------------------------------------------------------------

#define MAX_LIGHTSET_LIST_LIGHTSETS 8

// The intptr_t lightsets pointers are split into 32-bit hi and lo parts
// because we can't pass around a 64-bit value intact.

#define RDL2_LIGHTSET_LIST_MEMBERS                                     \
    HVD_MEMBER(int, mNumLightSets);                                    \
    HVD_ARRAY(uint32_t, mLightSetsPtrLo, MAX_LIGHTSET_LIST_LIGHTSETS); \
    HVD_ARRAY(uint32_t, mLightSetsPtrHi, MAX_LIGHTSET_LIST_LIGHTSETS)


#define RDL2_LIGHTSET_LIST_VALIDATION(vlen)          \
    HVD_BEGIN_VALIDATION(Rdl2LightSetList, vlen);    \
    HVD_VALIDATE(Rdl2LightSetList, mNumLightSets);   \
    HVD_VALIDATE(Rdl2LightSetList, mLightSetsPtrLo); \
    HVD_VALIDATE(Rdl2LightSetList, mLightSetsPtrHi); \
    HVD_END_VALIDATION


//----------------------------------------------------------------------------

#if CACHE_LINE_SIZE == 128
#define RAY_STATE_MEMBERS_PAD   76
#else
#define RAY_STATE_MEMBERS_PAD   28
#endif

#define RAY_STATE_MEMBERS                                                   \
    HVD_MEMBER(HVD_NAMESPACE(mcrt_common, RayDifferential), mRay);          \
    HVD_MEMBER(PathVertex, mPathVertex);                                    \
    HVD_MEMBER(uint32_t, mSequenceID);                                      \
    HVD_MEMBER(Subpixel, mSubpixel);                                        \
    HVD_MEMBER(uint32_t, mPad0);                                            \
    HVD_MEMBER(uint32_t, mTilePass);                                        \
    HVD_MEMBER(uint32_t, mRayStateIdx);                                     \
    HVD_ISPC_PAD(mPad1, 4);                                                 \
    HVD_PTR(HVD_NAMESPACE(shading, Intersection) *, mAOSIsect);             \
    HVD_MEMBER(uint32_t, mDeepDataHandle);                                  \
    HVD_MEMBER(uint32_t, mCryptomatteDataHandle);                           \
    HVD_MEMBER(HVD_NAMESPACE(scene_rdl2::math, Vec3f), mCryptoRefP);        \
    HVD_MEMBER(HVD_NAMESPACE(scene_rdl2::math, Vec3f), mCryptoP0);          \
    HVD_MEMBER(HVD_NAMESPACE(scene_rdl2::math, Vec3f), mCryptoRefN);        \
    HVD_MEMBER(HVD_NAMESPACE(scene_rdl2::math, Vec2f), mCryptoUV);          \
    HVD_MEMBER(HVD_NAMESPACE(scene_rdl2::math, Color), mVolRad);            \
    HVD_MEMBER(HVD_NAMESPACE(scene_rdl2::math, Color), mVolTr);             \
    HVD_MEMBER(HVD_NAMESPACE(scene_rdl2::math, Color), mVolTh);             \
    HVD_MEMBER(HVD_NAMESPACE(scene_rdl2::math, Color), mVolTalpha);         \
    HVD_MEMBER(HVD_NAMESPACE(scene_rdl2::math, Color), mVolTm);             \
    HVD_MEMBER(uint32_t, mVolHit);                                          \
    HVD_MEMBER(float, mVolumeSurfaceT);                                     \
    HVD_MEMBER(Rdl2LightSetList, mParentLobeLightSets);                     \
    HVD_ISPC_PAD(pad, RAY_STATE_MEMBERS_PAD)


#define RAY_STATE_VALIDATION(vlen)                                          \
    HVD_BEGIN_VALIDATION(RayState, vlen);                                   \
    HVD_VALIDATE(RayState, mRay);                                           \
    HVD_VALIDATE(RayState, mPathVertex);                                    \
    HVD_VALIDATE(RayState, mSequenceID);                                    \
    HVD_VALIDATE(RayState, mSubpixel);                                      \
    HVD_VALIDATE(RayState, mPad0);                                          \
    HVD_VALIDATE(RayState, mTilePass);                                      \
    HVD_VALIDATE(RayState, mRayStateIdx);                                   \
    HVD_VALIDATE(RayState, mAOSIsect);                                      \
    HVD_VALIDATE(RayState, mDeepDataHandle);                                \
    HVD_VALIDATE(RayState, mCryptomatteDataHandle);                         \
    HVD_VALIDATE(RayState, mCryptoRefP);                                    \
    HVD_VALIDATE(RayState, mCryptoP0);                                      \
    HVD_VALIDATE(RayState, mCryptoRefN);                                    \
    HVD_VALIDATE(RayState, mCryptoUV);                                      \
    HVD_VALIDATE(RayState, mVolRad);                                        \
    HVD_VALIDATE(RayState, mVolTr);                                         \
    HVD_VALIDATE(RayState, mVolTh);                                         \
    HVD_VALIDATE(RayState, mVolTalpha);                                     \
    HVD_VALIDATE(RayState, mVolTm);                                         \
    HVD_VALIDATE(RayState, mVolHit);                                        \
    HVD_VALIDATE(RayState, mVolumeSurfaceT);                                \
    HVD_VALIDATE(RayState, mParentLobeLightSets);                           \
    HVD_END_VALIDATION


//----------------------------------------------------------------------------

