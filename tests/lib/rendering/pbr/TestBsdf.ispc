// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

/// @file TestBsdf.ispc

#include "TestUtil.isph"

#include <moonray/rendering/pbr/integrator/BsdfSampler.isph>

#include <moonray/rendering/shading/ispc/bsdf/Bsdf.isph>
#include <moonray/rendering/shading/ispc/bsdf/BsdfSlice.isph>
#include <moonray/rendering/shading/ispc/Util.isph>
#include <moonray/rendering/shading/ispc/bsdf/fabric/BsdfFabric.isph>
#include <scene_rdl2/common/math/ispc/ReferenceFrame.isph>
#include <scene_rdl2/render/util/Random.isph>


//----------------------------------------------------------------------------
// Consistency test

// inputs/outputs of the consistency test
struct TestBsdfConsistency
{
    // inputs
    uniform Arena * uniform mArena;
    uniform int mRandomSeed;
    uniform int mRandomStream;
    varying Bsdf * uniform mBsdf;
    uniform bool mTestReciprocity;
    uniform int mRangeStart;
    uniform int mRangeEnd;
    uniform int mMaxSamplesPerLobe;
    uniform Vec3f mNg;
    uniform float mTol;

    // outputs
    uniform int mZeroSamplePdf;
    uniform int mZeroEvalPdf;
    uniform int mZeroEvalRecipPdf;
    uniform int mSampleCount;

    // output errors
    uniform int mInvalidPdf;
    uniform int mInvalidColor;
    uniform int mInvalidDirection;
    uniform int mInvalidEvalPdf;
    uniform int mInconsistentEvalPdf;
    uniform int mInvalidEvalColor;
    uniform int mInconsistentEvalColor;
    uniform int mInvalidRecipPdf;
    uniform int mInvalidRecipColor;
    uniform int mInconsistentRecipColor;
};

export void
#pragma ignore warning(all)
TestBsdf_testConsistency(uniform TestBsdfConsistency * uniform test)
{
    // initialize output
    test->mZeroSamplePdf = 0;
    test->mZeroEvalPdf = 0;
    test->mZeroEvalRecipPdf = 0;
    test->mSampleCount = 0;

    test->mInvalidPdf = 0;
    test->mInvalidColor = 0;
    test->mInvalidDirection = 0;
    test->mInvalidEvalPdf = 0;
    test->mInconsistentEvalPdf = 0;
    test->mInvalidEvalColor = 0;
    test->mInconsistentEvalColor = 0;
    test->mInvalidRecipPdf = 0;
    test->mInvalidRecipColor = 0;
    test->mInconsistentRecipColor = 0;

    varying Bsdf * uniform bsdf = test->mBsdf;
    assert(bsdf->mNumLobes > 0);
    // TODO: devise a test with a truly varying Ng
    const varying Vec3f Ng = test->mNg;

    // seed our random number generators and move them to the
    // start of our range
    varying DWARNGState rng;
    seed_rng(&rng, test->mRandomSeed + programIndex, test->mRandomStream + programIndex);

    uniform PbrTLState dummyTls;

    // loop over the sample range
    assert(test->mRangeEnd > test->mRangeStart);
    foreach (sample = test->mRangeStart ... test->mRangeEnd) {

        uniform bool foundError = false;

        // Generate random eye directions
        varying Vec3f wo;
        do {
            wo = sampleSphereUniform(frandom(&rng), frandom(&rng));
        } while (dot(wo, Ng) < sEpsilon);
        assert(isNormalized(wo));

        // Sample, but don't include cosine term since we want to
        // test bsdf reciprocity
        varying BsdfSlice slice;
        BsdfSlice_init(&slice, Ng, wo, BSDF_LOBE_TYPE_ALL,
            /* includeCosineTerm = */ false, /* entering = */ true, SHADOW_TERMINATOR_FIX_OFF);
        varying BsdfSampler bSampler;
        BsdfSampler_init(&bSampler, test->mArena, *bsdf, slice,
                test->mMaxSamplesPerLobe, true);

        uniform int lobeCount = BsdfSampler_getLobeCount(&bSampler);
        for (uniform int lobeIndex = 0; lobeIndex < lobeCount; ++lobeIndex) {
            varying int lobeSampleCount = BsdfSampler_getLobeSampleCount(&bSampler, lobeIndex);
            const varying BsdfLobe * uniform lobe = BsdfSampler_getLobe(&bSampler, lobeIndex);
            for (varying int s = 0; s < lobeSampleCount; ++s) {

                // draw the sample and test validity
                varying float r1 = frandom(&rng);
                varying float r2 = frandom(&rng);
                BsdfSample bsmp;
                BsdfSampler_sample(&dummyTls, &bSampler, lobeIndex, r1, r2, bsmp);

                foreach_active (lane) ++test->mSampleCount;

                // an exactly 0.f pdf value indicates an invalid sample
                if (bsmp.pdf == 0.f) {
                    foreach_active (lane) ++test->mZeroSamplePdf;
                    continue;
                }

                // Check the sample
                if (!isValidPdf(bsmp.pdf)) {
                    foreach_active (lane) ++test->mInvalidPdf;
                    foundError = true;
                }
                if (!isValidColor(bsmp.f)) {
                    foreach_active (lane) ++test->mInvalidColor;
                    foundError = true;
                }
                if (!isValidDirection(bsmp.wi)) {
                    foreach_active (lane) ++test->mInvalidDirection;
                    foundError = true;
                }

                // Check pdf for consistency with eval()
                varying float checkPdf;
                varying Color checkF = BsdfLobe_eval(lobe, slice, bsmp.wi, &checkPdf);
                if (checkPdf == 0.f) {
                    foreach_active (lane) ++test->mZeroEvalPdf;
                    continue;
                }
                if (!isValidPdf(checkPdf)) {
                    foreach_active(lane) ++test->mInvalidEvalPdf;
                    foundError = true;
                }
                varying float error = computeError(bsmp.pdf, checkPdf);
                if (error > test->mTol) {
                    foreach_active(lane) ++test->mInconsistentEvalPdf;
                    foundError = true;
                }

                // Check color for consistency with eval()
                if (!isValidColor(checkF)) {
                    foreach_active(lane) ++test->mInvalidEvalColor;
                    foundError = true;
                }
                varying float errorR = computeError(bsmp.f.r, checkF.r);
                varying float errorG = computeError(bsmp.f.g, checkF.g);
                varying float errorB = computeError(bsmp.f.b, checkF.b);
                if (errorR > test->mTol || errorG > test->mTol || errorB > test->mTol) {
                    foreach_active (lane) ++test->mInconsistentEvalColor;
                    foundError = true;
                }

                // Check eval() reciprocity
                varying BsdfSlice recipSlice;
                if (test->mTestReciprocity) {
                    BsdfSlice_init(&recipSlice, Ng, bsmp.wi, BSDF_LOBE_TYPE_ALL,
                        /* includeCosineTerm = */ false, /* entering = */ true,
                        SHADOW_TERMINATOR_FIX_OFF);
                    checkF = BsdfLobe_eval(lobe, recipSlice, wo, &checkPdf);
                    if (checkPdf == 0.f) {
                        foreach_active (lane) ++test->mZeroEvalRecipPdf;
                        continue;
                    }
                    if (!isValidPdf(checkPdf)) {
                        foreach_active (lane) ++test->mInvalidRecipPdf;
                        foundError = true;
                    }
                    if (!isValidColor(checkF)) {
                        foreach_active (lane) ++test->mInvalidRecipColor;
                        foundError = true;
                    }
                    errorR = computeError(bsmp.f.r, checkF.r);
                    errorG = computeError(bsmp.f.g, checkF.g);
                    errorB = computeError(bsmp.f.b, checkF.b);
                    if (errorR > test->mTol || errorG > test->mTol || errorB > test->mTol) {
                        foreach_active (lane) ++test->mInconsistentRecipColor;
                        foundError = true;
                    }
                }

                // For your debugging needs
                if (foundError) {
                    BsdfSampler_sample(&dummyTls, &bSampler, lobeIndex, r1, r2, bsmp);
                    checkF = BsdfLobe_eval(lobe, slice, bsmp.wi, &checkPdf);
                    checkF = BsdfLobe_eval(lobe, recipSlice, wo, &checkPdf);
                }
            }
        }
    }
}

//----------------------------------------------------------------------------
// Pdf integral test

// inputs/outputs of the pdf integral test
struct TestBsdfPdfIntegral
{
    // inputs
    uniform Arena * uniform mArena;
    uniform int mRandomSeed;
    uniform int mRandomStream;
    varying Bsdf * uniform mBsdf;
    uniform int mRangeStart;
    uniform int mRangeEnd;
    uniform int mMaxSamplesPerLobe;
    uniform int mSpherical;
    uniform ReferenceFrame mFrame;
    uniform Vec3f mWo;

    // outputs
    uniform float mIntegral;
    uniform int mInvalidPdf;
};

export void
#pragma ignore warning(all)
TestBsdf_testPdfIntegral(uniform TestBsdfPdfIntegral * uniform test)
{
    // initialize output
    test->mIntegral = 0.f;
    test->mInvalidPdf = 0;

    // get inputs
    varying Bsdf * uniform bsdf = test->mBsdf;
    assert(bsdf->mNumLobes > 0);
    // TODO: devise a test that uses a truly varying reference frame
    varying ReferenceFrame frame = test->mFrame;
    // TODO: devise a test that uses a truly varying wo
    varying Vec3f wo = test->mWo;
    // Don't include the cosine term since we only want to get the pdf
    BsdfSlice slice;
    BsdfSlice_init(&slice, getN(frame), wo, BSDF_LOBE_TYPE_ALL,
                   /* includeCosineTerm = */ false,
                   /* entering = */ true,
                   SHADOW_TERMINATOR_FIX_OFF);
    varying BsdfSampler bSampler;
    BsdfSampler_init(&bSampler, test->mArena, *bsdf, slice,
            test->mMaxSamplesPerLobe, true);

    varying DWARNGState rng;
    seed_rng(&rng, test->mRandomSeed + programIndex, test->mRandomStream + programIndex);

    // naive lobe sampling, where every lobe is equally probable
    // here we are testing that if N lobes each sum to 1 with M
    // samples, then the same N lobes with M/N samples each will have
    // a total sum of 1.
    varying float integral = 0.f;
    assert(test->mRangeEnd > test->mRangeStart);
    foreach (sample = test->mRangeStart ... test->mRangeEnd) {
        uniform int lobeCount = BsdfSampler_getLobeCount(&bSampler);
        for (uniform int lobeIndex = 0; lobeIndex < lobeCount; ++lobeIndex) {
            varying int lobeSampleCount = BsdfSampler_getLobeSampleCount(&bSampler, lobeIndex);
            const varying BsdfLobe * uniform lobe = BsdfSampler_getLobe(&bSampler, lobeIndex);
            for (varying int s = 0; s < lobeSampleCount; ++s) {

                varying Vec3f wi;
                if (test->mSpherical) {
                    wi = sampleSphereUniform(frandom(&rng), frandom(&rng));
                } else {
                    wi = sampleLocalHemisphereUniform(frandom(&rng), frandom(&rng));
                    if (BsdfLobe_getType(lobe) & BSDF_LOBE_TYPE_TRANSMISSION) {
                        wi = neg(wi);
                    }
                    wi = localToGlobal(frame, wi);
                }
                assert(isNormalized(wi));

                varying float pdf;
                BsdfLobe_eval(lobe, slice, wi, &pdf);
                if (pdf > 0.f) {
                    if (!isValidPdf(pdf)) {
                        foreach_active (lane) ++test->mInvalidPdf;
                    } else {
                        integral += pdf * BsdfSampler_getInvLobeSampleCount(
                                &bSampler, lobeIndex);
                    }
                }
            }
        }
    }

    // reduce the results
    foreach_active (lane) {
        test->mIntegral += extract(integral / BsdfSampler_getLobeCount(&bSampler), lane);
    }
}

//----------------------------------------------------------------------------
// Eval integral task

// intputs/outputs of the eval integral test
struct TestBsdfEvalIntegral
{
    // inputs
    uniform Arena * uniform mArena;
    uniform int mSeed;
    varying Bsdf * uniform mBsdf;
    uniform int mRangeStart;
    uniform int mRangeEnd;
    uniform int mMaxSamplesPerLobe;
    uniform ReferenceFrame mFrame;
    uniform Vec3f mWo;

    // outputs
    uniform Color mIntegralUniform;
    uniform Color mIntegralImportance;

    // output errors
    uniform int mInvalidPdf;
    uniform int mInvalidEvalColor;
    uniform int mInvalidSampleColor;
    uniform int mInvalidDirection;
};

export void
#pragma ignore warning(all)
TestBsdf_testEvalIntegral(uniform TestBsdfEvalIntegral * uniform test)
{
    // initialize outputs
    test->mIntegralUniform = Color_ctor(0.f);
    test->mIntegralImportance = Color_ctor(0.f);
    test->mInvalidPdf = 0;
    test->mInvalidEvalColor = 0;
    test->mInvalidSampleColor = 0;
    test->mInvalidDirection = 0;

    // get inputs
    varying Bsdf * uniform bsdf = test->mBsdf;
    assert(bsdf->mNumLobes > 0);

    // TODO: devise a test that uses a truly varying reference frame
    varying ReferenceFrame frame = test->mFrame;
    // TODO: devise a test that uses a truly varying wo
    varying Vec3f wo = test->mWo;

    // include cosine term
    BsdfSlice slice;
    BsdfSlice_init(&slice, getN(frame), wo, BSDF_LOBE_TYPE_ALL,
                   /* includeCosineTerm = */ true,
                   /* entering = */ true,
                   SHADOW_TERMINATOR_FIX_OFF);

    varying BsdfSampler bSampler;
    BsdfSampler_init(&bSampler, test->mArena, *bsdf, slice,
            test->mMaxSamplesPerLobe, true);
    varying BsdfSample bsmp;

    // seed our random number generators and move them to the
    // start of our range
    varying DWARNGState rng;
    seed_rng(&rng, test->mSeed + programIndex, 9281 + programIndex /* Select random stream */);
    foreach (i = 0 ... test->mRangeStart) {
        frandom(&rng);
    }

    uniform PbrTLState dummyTls;

    const varying float pdfUniform = 1.f / (Bsdf_getIsSpherical(bsdf)? sFourPi : sTwoPi);

    varying Color integralUniform = Color_ctor(0.f);
    varying Color integralImportance = Color_ctor(0.f);

    assert(test->mRangeEnd > test->mRangeStart);
    foreach (sample = test->mRangeStart ... test->mRangeEnd) {
        uniform int lobeCount = BsdfSampler_getLobeCount(&bSampler);
        for (uniform int lobeIndex = 0; lobeIndex < lobeCount; ++lobeIndex) {
            varying int lobeSampleCount = BsdfSampler_getLobeSampleCount(&bSampler, lobeIndex);
            const varying BsdfLobe * uniform lobe = BsdfSampler_getLobe(&bSampler, lobeIndex);
            for (varying int s = 0; s < lobeSampleCount; ++s) {

                varying float r1 = frandom(&rng);
                varying float r2 = frandom(&rng);

                // compute integrated bsdf using uniform sampling
                varying Vec3f wi;
                if (Bsdf_getIsSpherical(bsdf)) {
                    wi = sampleSphereUniform(r1, r2);
                } else {
                    wi = sampleLocalHemisphereUniform(r1, r2);
                    if (BsdfLobe_getType(lobe) & BSDF_LOBE_TYPE_TRANSMISSION) {
                        wi = neg(wi);
                    }
                    wi = localToGlobal(frame, wi);
                }
                assert(isNormalized(wi));
                varying Color f = BsdfLobe_eval(lobe, slice, wi, NULL);
                if (!isValidColor(f)) {
                    foreach_active (lane) ++test->mInvalidEvalColor;
                } else {
                    integralUniform = integralUniform + f * 
                            (BsdfSampler_getInvLobeSampleCount(&bSampler, lobeIndex) / pdfUniform);
                }

                // compute the integrated bsdf using importance sampling
                BsdfSampler_sample(&dummyTls, &bSampler, lobeIndex, r1, r2, bsmp);
                if (BsdfSample_isValid(&bsmp)) {
                    if (!isValidPdf(bsmp.pdf)) {
                        foreach_active (lane) ++test->mInvalidPdf;
                    }
                    if (!isValidColor(bsmp.f)) {
                        foreach_active (lane) ++test->mInvalidSampleColor;
                    }
                    if (!isValidDirection(bsmp.wi)) {
                        foreach_active (lane) ++test->mInvalidDirection;
                    }

                    integralImportance = integralImportance + bsmp.f *
                            (BsdfSampler_getInvLobeSampleCount(&bSampler, lobeIndex) / bsmp.pdf);
                }
            }
        }
    }

    // outputs
    foreach_active (lane) {
        test->mIntegralUniform.r += extract(integralUniform.r, lane);
        test->mIntegralUniform.g += extract(integralUniform.g, lane);
        test->mIntegralUniform.b += extract(integralUniform.b, lane);
        test->mIntegralImportance.r += extract(integralImportance.r, lane);
        test->mIntegralImportance.g += extract(integralImportance.g, lane);
        test->mIntegralImportance.b += extract(integralImportance.b, lane);
    }
}

//----------------------------------------------------------------------------
// Fabric shadow terminator test

struct TestFabricShadowTerminator
{
    // Inputs
    uniform Vec3f mN;
    uniform Vec3f mNg;
    uniform Vec3f mTangent;
    uniform Vec3f mThreadDirection;
    uniform Vec3f mWo;
    uniform Vec3f mWi;
    uniform float mRoughness;
    uniform Color mColor;
    uniform ShadowTerminatorFix mShadowTerminatorFix;

    // Outputs
    uniform Color mResult;
    uniform float mPdf;
};

export void
TestBsdf_evalFabricShadowTerminator(
    uniform TestFabricShadowTerminator * uniform test)
{
    // Broadcast the shared inputs into per-lane values.
    const varying Vec3f N = test->mN;
    const varying Vec3f Ng = test->mNg;
    const varying Vec3f tangent = test->mTangent;
    const varying Vec3f threadDirection = test->mThreadDirection;
    const varying Vec3f wo = test->mWo;
    const varying Vec3f wi = test->mWi;
    const varying Color color = test->mColor;

    // Match the C++ fabric settings. The final true selects DWA fabric.
    varying FabricBsdfLobe lobe;
    FabricBsdfLobe_init(
        &lobe,
        N,
        tangent,
        threadDirection,
        0.0f,
        test->mRoughness,
        color,
        true);

    // Include the cosine term and use the requested correction mode.
    varying BsdfSlice slice;
    BsdfSlice_init(
        &slice,
        Ng,
        wo,
        BSDF_LOBE_TYPE_ALL,
        true,
        true,
        test->mShadowTerminatorFix);

    // Evaluate the production fabric implementation in each SIMD lane.
    // The evaluation function expects a pointer to the base BsdfLobe type.
    varying float pdf = 0.0f;
    const varying Color result = FabricBsdfLobe_eval(
        (varying BsdfLobe * uniform) &lobe,
        slice,
        wi,
        &pdf);

    // All lanes have identical inputs; return lane zero for the C++ comparison.
    test->mResult.r = extract(result.r, 0);
    test->mResult.g = extract(result.g, 0);
    test->mResult.b = extract(result.b, 0);
    test->mPdf = extract(pdf, 0);
}
