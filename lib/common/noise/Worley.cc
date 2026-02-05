// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

//

#include "Worley.h"
#include <scene_rdl2/render/util/Random.h>
#include <scene_rdl2/common/math/ReferenceFrame.h>

namespace moonray {
namespace noise {

using namespace scene_rdl2::math;

namespace {

int
worleyFactorial(const int m)
{
    if (m <= 1) {
        return 1;
    }

    return m*worleyFactorial(m - 1);
}

Vec3f
getGgxNormal(const Vec2f& randoms, 
             const float randomness)
{
    const float r1 = randoms.x;
    const float r2 = randoms.y;

    const float theta = scene_rdl2::math::atan(randomness * scene_rdl2::math::sqrt(r1) * scene_rdl2::math::rsqrt(1.0f - r1)); // GGX
    const float phi = sTwoPi * r2;

    float sinTheta, cosTheta;
    float sinPhi, cosPhi;

    scene_rdl2::math::sincos(theta, &sinTheta, &cosTheta);
    scene_rdl2::math::sincos(phi, &sinPhi, &cosPhi);

    Vec3f wn;
    wn.x = cosPhi * sinTheta;
    wn.y = sinPhi * sinTheta;
    wn.z = cosTheta;

    return wn;
}

// Sorts the first numPoints points by distance
void
sortByDistance(Worley_PointArray& worleyPoints,
               const float weight, 
               const float distance, 
               const Vec3f& diff,
               const Vec3f& gradient,
               const Vec3f& position,
               const Vec3f& normal,
               const int id)
{
    if (distance < worleyPoints[3].dist) {

        if (distance < worleyPoints[0].dist) {

            worleyPoints[3].dist = worleyPoints[2].dist;
            worleyPoints[3].diff = worleyPoints[2].diff;
            worleyPoints[3].gradient = worleyPoints[2].gradient;
            worleyPoints[3].position = worleyPoints[2].position;
            worleyPoints[3].id = worleyPoints[2].id;

            worleyPoints[2].dist = worleyPoints[1].dist;
            worleyPoints[2].diff = worleyPoints[1].diff;
            worleyPoints[2].gradient = worleyPoints[1].gradient;
            worleyPoints[2].position = worleyPoints[1].position;
            worleyPoints[2].id = worleyPoints[1].id;

            worleyPoints[1].dist = worleyPoints[0].dist;
            worleyPoints[1].diff = worleyPoints[0].diff;
            worleyPoints[1].gradient = worleyPoints[0].gradient;
            worleyPoints[1].position = worleyPoints[0].position;
            worleyPoints[1].id = worleyPoints[0].id;

            worleyPoints[0].dist = distance;
            asCpp(worleyPoints[0].diff) = diff;
            asCpp(worleyPoints[0].gradient) = gradient;
            asCpp(worleyPoints[0].position) = position;
            worleyPoints[0].id = id;

        } else if (distance < worleyPoints[1].dist) {

            worleyPoints[3].dist = worleyPoints[2].dist;
            worleyPoints[3].diff = worleyPoints[2].diff;
            worleyPoints[3].gradient = worleyPoints[2].gradient;
            worleyPoints[3].position = worleyPoints[2].position;
            worleyPoints[3].id = worleyPoints[2].id;

            worleyPoints[2].dist = worleyPoints[1].dist;
            worleyPoints[2].diff = worleyPoints[1].diff;
            worleyPoints[2].gradient = worleyPoints[1].gradient;
            worleyPoints[2].position = worleyPoints[1].position;
            worleyPoints[2].id = worleyPoints[1].id;

            worleyPoints[1].dist = distance;
            asCpp(worleyPoints[1].diff) = diff;
            asCpp(worleyPoints[1].gradient) = gradient;
            asCpp(worleyPoints[1].position) = position;
            worleyPoints[1].id = id;

        } else if (distance < worleyPoints[2].dist) {

            worleyPoints[3].dist = worleyPoints[2].dist;
            worleyPoints[3].diff = worleyPoints[2].diff;
            worleyPoints[3].gradient = worleyPoints[2].gradient;
            worleyPoints[3].position = worleyPoints[2].position;
            worleyPoints[3].id = worleyPoints[2].id;

            worleyPoints[2].dist = distance;
            asCpp(worleyPoints[2].diff) = diff;
            asCpp(worleyPoints[2].gradient) = gradient;
            asCpp(worleyPoints[2].position) = position;
            worleyPoints[2].id = id;

        } else {

            worleyPoints[3].dist = distance;
            asCpp(worleyPoints[3].diff) = diff;
            asCpp(worleyPoints[3].gradient) = gradient;
            asCpp(worleyPoints[3].position) = position;
            worleyPoints[3].id = id;

        }
    }
}

bool
alreadyVisited(const int ix,
               const int iy,
               const int iz,
               const int centerX,
               const int centerY,
               const int centerZ,
               const int k)
{
    const int kp = k-1;
    if (ix >= centerX - kp && ix <= centerX + kp &&
        iy >= centerY - kp && iy <= centerY + kp &&
        iz >= centerZ - kp && iz <= centerZ + kp) {
        return true;
    }
    return false;
}

void
applyDistanceMethod(Worley_PointArray& worleyPoints,
                    const int distanceMethod,
                    const float minkowskiNumber)
{
    for (int i = 0; i < 4; i++) {
        const Vec3f diff = asCpp(worleyPoints[i].diff);
        switch (distanceMethod) {
            case ispc::NOISE_WORLEY_DIST_LINEAR_SQUARED:
                worleyPoints[i].dist = scene_rdl2::math::sqrt(worleyPoints[i].dist);
                break;
            case ispc::NOISE_WORLEY_DIST_MANHATTAN:
                worleyPoints[i].dist = scene_rdl2::math::abs(diff.x) + scene_rdl2::math::abs(diff.y) + scene_rdl2::math::abs(diff.z);
                break;
            case ispc::NOISE_WORLEY_DIST_CHEBYSHEV:
                worleyPoints[i].dist =
                    scene_rdl2::math::max(scene_rdl2::math::max(scene_rdl2::math::abs(diff.x), scene_rdl2::math::abs(diff.y)),
                                          scene_rdl2::math::abs(diff.z));
                break;
           case ispc::NOISE_WORLEY_DIST_QUADRATIC:
                {
                    const float a = diff.x;
                    const float b = diff.y;
                    const float c = diff.z;
                    worleyPoints[i].dist = a * a + a * b + a * c + b * c + b * b + c * c;
                }
                break;
            case ispc::NOISE_WORLEY_DIST_MINKOWSKI:
                {
                    const float mn = minkowskiNumber;
                    const float a = scene_rdl2::math::abs(diff.x);
                    const float b = scene_rdl2::math::abs(diff.y);
                    const float c = scene_rdl2::math::abs(diff.z);
                    worleyPoints[i].dist =
                        scene_rdl2::math::pow(scene_rdl2::math::pow(a, mn) + scene_rdl2::math::pow(b, mn) +
                                scene_rdl2::math::pow(c, mn), (1.0f / mn));
                }
                break;
        }
    }
}

// Helper to apply distance method to a single point
inline void
applyDistanceMethodToPoint(float& distance,
                           const Vec3f& diff,
                           const int distanceMethod,
                           const float minkowskiNumber)
{
    switch (distanceMethod) {
        case ispc::NOISE_WORLEY_DIST_LINEAR_SQUARED:
            distance = scene_rdl2::math::sqrt(distance);
            break;
        case ispc::NOISE_WORLEY_DIST_MANHATTAN:
            distance = scene_rdl2::math::abs(diff.x) + scene_rdl2::math::abs(diff.y) + scene_rdl2::math::abs(diff.z);
            break;
        case ispc::NOISE_WORLEY_DIST_CHEBYSHEV:
            distance =
                scene_rdl2::math::max(scene_rdl2::math::max(scene_rdl2::math::abs(diff.x), scene_rdl2::math::abs(diff.y)),
                                      scene_rdl2::math::abs(diff.z));
            break;
        case ispc::NOISE_WORLEY_DIST_QUADRATIC:
            {
                const float a = diff.x;
                const float b = diff.y;
                const float c = diff.z;
                distance = a * a + a * b + a * c + b * c + b * b + c * c;
            }
            break;
        case ispc::NOISE_WORLEY_DIST_MINKOWSKI:
            {
                const float mn = minkowskiNumber;
                const float a = scene_rdl2::math::abs(diff.x);
                const float b = scene_rdl2::math::abs(diff.y);
                const float c = scene_rdl2::math::abs(diff.z);
                distance =
                    scene_rdl2::math::pow(scene_rdl2::math::pow(a, mn) + scene_rdl2::math::pow(b, mn) +
                            scene_rdl2::math::pow(c, mn), (1.0f / mn));
            }
            break;
    }
}

void  
sortPoints(const Worley_PointArray::iterator tmpWorleyPointsBeg,
           const Worley_PointArray::iterator tmpWorleyPointsCur)
{
    // Sort points based on distance from shading point
    std::sort(tmpWorleyPointsBeg, tmpWorleyPointsCur,
              [](const ispc::NOISE_WorleyPoint f1, const ispc::NOISE_WorleyPoint f2)
              { return f1.dist < f2.dist; });

}

void  
collectPoints(const Worley_PointArray::iterator tmpWorleyPointsBeg,
              const Worley_PointArray::iterator tmpWorleyPointsCur,
              Worley_PointArray::iterator& collectCurrItr,
              const Worley_PointArray::iterator collectEndItr)
{
    for (auto it = tmpWorleyPointsBeg; it != tmpWorleyPointsCur; it++) {
        if (collectCurrItr == collectEndItr) {
            break; // reached NOISE_Worley_MAX_POINTS capacity, don't need to collect any more
        }
        if ((*it).weight > 0.00001f) {
            // If the point is greater than then weight threshold then copy it
            // to the collector array's current postion then advance that 
            // current position
            *collectCurrItr++ = *it;
        }
    }
}

} // end anonymouse namespace

// Static data
bool Worley::sIsWorleyDataInitialized = false;
std::mutex Worley::sWorleyInitDataMutex;
std::vector<float> Worley::sWorleyPointsX;
std::vector<float> Worley::sWorleyPointsY;
std::vector<float> Worley::sWorleyPointsZ;
std::vector<float> Worley::sWorleyRandsForNormalsX;
std::vector<float> Worley::sWorleyRandsForNormalsY;

// Constructor
Worley::Worley(const int seed,
               const int tableSize,
               const ispc::NOISE_WorleyVersion version,
               const int distanceMethod,
               const float randomness,
               const bool addNormals,
               const bool useStaticTables,
               const bool useMapSort) : 
               Noise(seed, tableSize, useStaticTables)
{
    mIspcWorley.mVersion = version;
    mIspcWorley.mDistanceMethod = distanceMethod;
    mIspcWorley.mNoise = &mIspc;
    mIspcWorley.mHasNormals = addNormals;
    mIspcWorley.mUseStaticTables = useStaticTables;
    mIspcWorley.mUseMapSort = useMapSort;

    initPointProbabilities();

    if (useStaticTables) {
        std::lock_guard dataLock(sWorleyInitDataMutex); // Lock this part that initializes static data
        // Only construct the static data once to share across all instances
        if (!sIsWorleyDataInitialized) {
            buildStaticPointTables(addNormals, tableSize);
            sIsWorleyDataInitialized = true;
        }
        mIspcWorley.mPointsX = sWorleyPointsX.data();
        mIspcWorley.mPointsY = sWorleyPointsY.data();
        mIspcWorley.mPointsZ = sWorleyPointsZ.data();
        if (addNormals) {
            mIspcWorley.mRandsForNormalsX = sWorleyRandsForNormalsX.data();
            mIspcWorley.mRandsForNormalsY = sWorleyRandsForNormalsY.data();
        }
    } else {
        buildPointTables(addNormals, tableSize);
        mIspcWorley.mPointsX = mPointsX.data();
        mIspcWorley.mPointsY = mPointsY.data();
        mIspcWorley.mPointsZ = mPointsZ.data();
        if (addNormals) {
            mIspcWorley.mRandsForNormalsX = mRandsForNormalsX.data();
            mIspcWorley.mRandsForNormalsY = mRandsForNormalsY.data();
        }
    }

    // Normals need to be one copy per shader instance 
    // since they are computed based on the randomness
    if (addNormals) {
        mNormalsX.reserve(tableSize);
        mNormalsY.reserve(tableSize);
        mNormalsZ.reserve(tableSize);
        mIspcWorley.mNormalsX = mNormalsX.data();
        mIspcWorley.mNormalsY = mNormalsY.data();
        mIspcWorley.mNormalsZ = mNormalsZ.data();
        for (int i = 0; i < tableSize; ++i) {
            Vec2f randoms;
            randoms = Vec2f(mIspcWorley.mRandsForNormalsX[i], mIspcWorley.mRandsForNormalsY[i]);
            Vec3f worleyNormal = getGgxNormal(randoms, randomness * randomness);
            mNormalsX[i] = worleyNormal.x;
            mNormalsY[i] = worleyNormal.y;
            mNormalsZ[i] = worleyNormal.z;
        }
    }
}

void
Worley::buildPointTables(const bool addNormals, const int tableSize)
{
    mPointsX.reserve(tableSize);
    mPointsY.reserve(tableSize);
    mPointsZ.reserve(tableSize);
    if (addNormals) {
        mRandsForNormalsX.reserve(tableSize);
        mRandsForNormalsY.reserve(tableSize);
    }

    for (int i = 0; i < tableSize; ++i) {
        mPointsX[i] = mRandomNumberGenerator->getNextFloat();
        mPointsY[i] = mRandomNumberGenerator->getNextFloat();
        mPointsZ[i] = mRandomNumberGenerator->getNextFloat();
        if (addNormals) {
            mRandsForNormalsX[i] = mRandomNumberGenerator->getNextFloat();
            mRandsForNormalsY[i] = mRandomNumberGenerator->getNextFloat();
        }
    }
}

void
Worley::buildStaticPointTables(const bool addNormals, const int tableSize)
{
    sWorleyPointsX.reserve(tableSize);
    sWorleyPointsY.reserve(tableSize);
    sWorleyPointsZ.reserve(tableSize);
    if (addNormals) {
        sWorleyRandsForNormalsX.reserve(tableSize);
        sWorleyRandsForNormalsY.reserve(tableSize);
    }

    for (int i = 0; i < tableSize; ++i) {
        sWorleyPointsX[i] = sNoiseRandom.getNextFloat();
        sWorleyPointsY[i] = sNoiseRandom.getNextFloat();
        sWorleyPointsZ[i] = sNoiseRandom.getNextFloat();
        if (addNormals) {
            sWorleyRandsForNormalsX[i] = sNoiseRandom.getNextFloat();
            sWorleyRandsForNormalsY[i] = sNoiseRandom.getNextFloat();
        }
    }
}

void
Worley::initPointProbabilities()
{
    int m;
    float prob;
    for (m = ispc::NOISE_WORLEY_MIN_PROB_POINTS; m <= ispc::NOISE_WORLEY_MAX_PROB_POINTS; m++) {
        // probability of m feature points in the cube 
        prob = 1.0f / (pow(static_cast<float>(ispc::NOISE_WORLEY_LAMBDA), -m) * 
            scene_rdl2::math::exp(static_cast<float>(ispc::NOISE_WORLEY_LAMBDA)) * worleyFactorial(m));

        if (m == ispc::NOISE_WORLEY_MIN_PROB_POINTS) {
            mIspcWorley.mPointProbLow[m] = 0.0f;
        } else {
            mIspcWorley.mPointProbLow[m] = mIspcWorley.mPointProbHigh[m - 1];
        }

        mIspcWorley.mPointProbHigh[m] = mIspcWorley.mPointProbLow[m] + prob;
    }

    mIspcWorley.mPointProbHigh[ispc::NOISE_WORLEY_MAX_PROB_POINTS] = 
        std::numeric_limits<float>::max();
}

int
Worley::getPointCount(const int hash) const
{
    int low, m, high, prob_hash;
    float prob;

    // just grab a random value from the precomputed random values
    prob_hash = perm(hash + 5);
    prob = mIspcWorley.mPointsX[prob_hash];

    low = ispc::NOISE_WORLEY_MIN_PROB_POINTS;
    high = ispc::NOISE_WORLEY_MAX_PROB_POINTS;

    // binary search
    while (low <= high) {
        m = (low + high) / 2;
        if (prob < mIspcWorley.mPointProbLow[m]) {
            high = m - 1;
        } else if (prob >= mIspcWorley.mPointProbHigh[m]) {
            low = m + 1;
        } else {
            break;
        }
    }

    return m;
}

float
Worley::computeSphereDiskOverlap(const Vec3f& featurePt,
                                 const float radius,
                                 const ispc::NOISE_WorleySample& sample,
                                 Vec3f& diskDiff,
                                 float& distToPlane,
                                 Vec2f& uv) const
{
    // featurePoint = Worley Point
    // sample.position = shading point

    Vec3f pos = asCpp(sample.position);

    const Vec3f diff =  pos - featurePt;
    float deltaToPlane = dot(diff, asCpp(sample.normal));

    const float radiusP2 = radius * radius - deltaToPlane * deltaToPlane;
    if (radiusP2 < sEpsilon) return 0;
    float radiusProjection = scene_rdl2::math::sqrt(radiusP2);
    const Vec3f centerProjection = featurePt + deltaToPlane * asCpp(sample.normal);

    diskDiff = (pos - centerProjection);

    if (sample.compensateDeformation) {
        // Reduce the disk projection radius for compressed regions
        // We do this so that compressed regions are not "over-crowded" by
        // glitter flakes. We *could* expand the radius for stretch so we get
        // rid of 'negative space' but it is deemed less visually appealing atm.
        radiusProjection *= scene_rdl2::math::min(1.0f,
                                                  scene_rdl2::math::min(sample.compensationS,
                                                  sample.compensationT));

        // Reverse Shear along X Axis
        // Any shear components should be reversed before any Scaling
        diskDiff[0] = diskDiff[0] - sample.shearRefP_X * diskDiff[1];

        // Reverse Scale - both compression and stretching
        Vec3f xAxis = asCpp(sample.refX);
        Vec3f yAxis = asCpp(sample.refY);
        Vec3f zAxis = asCpp(sample.refZ);
        diskDiff = dot(diskDiff, xAxis) * sample.compensationS * xAxis +
                   dot(diskDiff, yAxis) * sample.compensationT * yAxis +
                   dot(diskDiff, zAxis) * zAxis;
    }

    distToPlane = length(diskDiff);
    float overlapRadius = radiusProjection + sample.radius - distToPlane;
    overlapRadius =  scene_rdl2::math::min(scene_rdl2::math::max(overlapRadius, 0.0f),
                                           2.0f * scene_rdl2::math::min(radiusProjection,
                                                                        sample.radius));
    if (mIspcWorley.mVersion == ispc::NOISE_WORLEY_V1) {
        overlapRadius /= scene_rdl2::math::max(sEpsilon, sample.footprintLength);
        return scene_rdl2::math::max(0.0f, overlapRadius);
    } else {

        // Compute Texture UVs

        // ReferenceFrame ctor() requires unit-length normal.
        if (!isNormalized(asCpp(sample.normal))) {
            return 0.0f;
        }

        // Use the flake normal to create an obj/ref->flake-space xform
        const ReferenceFrame diskFrame(asCpp(sample.normal));
        // vector from disk center to shading position
        const Vec3f& Plocal = diskDiff;
        // Xform Plocal to flake-space to
        // get Flake UVs with the frame centered at disk center
        // Normalize to be in [-1, 1] space.
        uv[0] =  dot(diskFrame.getX(), Plocal) / radiusProjection;
        uv[1] = -dot(diskFrame.getY(), Plocal) / radiusProjection;

        // this function should return the portion of the sample area
        // (ray footprint) that is covered by the intersection of the
        // surface and the worley point/sphere
        const float overlapArea = overlapRadius * overlapRadius * sPi;
        return (overlapArea > sample.footprintArea) ? 1.f : overlapArea / sample.footprintArea;
    }
}

bool
Worley::chooseFlakeStyle(int id,
                         const RandomTable* glitterRandoms,
                         const Flake_StyleArray* styleCDF,
                         unsigned int& selectedStyle) const
{
    MNRY_ASSERT(glitterRandoms != NULL);
    MNRY_ASSERT(styleCDF != NULL);
    const int tabMask = ispc::NOISE_WORLEY_GLITTER_TABLE_SIZE - 1;
    // Choose a Random Number
    const float r1 = (*glitterRandoms)[(7*id) & tabMask];      // [0, 1]
    if (r1 <= (*styleCDF)[0]) {
        selectedStyle = 0;
    } else if (r1 <= (*styleCDF)[1]) {
        selectedStyle = 1;
    } else {
        return false;
    }

    return true;
}

void
Worley::setFlakeStyleAndSize(int hashID,
                             const RandomTable* glitterRandoms,
                             const Flake_StyleArray* styleCDF,
                             const Flake_StyleArray* styleSizes,
                             float& flakeSize,
                             unsigned int& styleIndex) const
{
    MNRY_ASSERT(glitterRandoms != NULL);
    MNRY_ASSERT(styleCDF != NULL);
    MNRY_ASSERT(styleSizes != NULL);
    // Do we have a valid index
    if (chooseFlakeStyle(hashID, glitterRandoms, styleCDF, styleIndex)) {
        flakeSize = (*styleSizes)[styleIndex];
    } else {
        flakeSize = 0.f;
    }
}

void
Worley::checkCell(const int ix, const int iy, const int iz, 
                  const ispc::NOISE_WorleySample& noiseSample,
                  Worley_PointArray& worleyPoints,
                  Worley_PointArray::iterator& currItr,
                  const Worley_PointArray::iterator endItr,
                  const float jitter,
                  const bool checkOverlap,
                  const RandomTable* glitterRandoms,
                  const Flake_StyleArray* styleRadii,
                  const Flake_StyleArray* styleCDF,
                  const int distanceMethod,
                  const float minkowskiNumber) const
{
    const Vec3f& pos = asCpp(noiseSample.position);
    int hash = index3D(ix, iy, iz);

    // Static tables are all generated using the same seed for
    // all instances therefore we add the seed here to the hash
    // value.
    if (mIspcWorley.mUseStaticTables) {
        hash = (hash + mIspc.mSeed) % mIspc.mTableSize;
    }

    Vec3f position;
    Vec3f diff, gradient;
    Vec3f planeDiff;
    float distance, overlap;
    Vec2f uv;
    // The style index is chosen probabilistically right before
    // determining the sphere-disk overlap to choose the right flake size
    // if there are multiple glitter styles. And it is added to the worleyPoint
    // so the other glitter style parameters are based off the same style id
    // when they are set in the glitter library.
    unsigned int styleIndex;

    // For glitter (when checkOverlap==True) we get multiple points per cell,
    // but for the standard worley noise pattern we want 1 point per cell.
    const int pointCount = (mIspcWorley.mVersion == ispc::NOISE_WORLEY_V3) ?
                           (checkOverlap ? getPointCount(hash) : 1) :
                           getPointCount(hash);
    for (int i = 0; i < pointCount; i++) {
        position.x = static_cast<float>(ix) + 0.5f + (mIspcWorley.mPointsX[hash] - 0.5f) * jitter;
        position.y = static_cast<float>(iy) + 0.5f + (mIspcWorley.mPointsY[hash] - 0.5f) * jitter;
        position.z = static_cast<float>(iz) + 0.5f + (mIspcWorley.mPointsZ[hash] - 0.5f) * jitter;

        diff = pos - position;
        gradient = position - pos;

        overlap = 1.0f;
        if (checkOverlap) {
            if (styleRadii == NULL) {
                return;
            }
            float flakeSize;
            if (glitterRandoms != NULL && styleCDF != NULL) {
                setFlakeStyleAndSize(hash, glitterRandoms, styleCDF, styleRadii, flakeSize, styleIndex);
            } else {
                flakeSize = scene_rdl2::math::max((*styleRadii)[0], (*styleRadii)[1]);
            }

            overlap = computeSphereDiskOverlap(position,
                                               flakeSize,
                                               noiseSample, 
                                               planeDiff,
                                               distance,
                                               uv);
        } else {
            distance = dot(diff, diff);
            // Apply distance method transformation for V3 only
            if (mIspcWorley.mVersion == ispc::NOISE_WORLEY_V3) {
                applyDistanceMethodToPoint(distance, diff, distanceMethod, minkowskiNumber);
            }
        }

        // TODO: skip this if condition to get all flakes
        if (overlap > sEpsilon) {
            Vec3f worleyNormal;
            if (mIspcWorley.mHasNormals) {
                worleyNormal.x = mIspcWorley.mNormalsX[hash];
                worleyNormal.y = mIspcWorley.mNormalsY[hash];
                worleyNormal.z = mIspcWorley.mNormalsZ[hash];
                worleyNormal = worleyNormal +
                    planeDiff.x * asCpp(noiseSample.dNdx) + 
                    planeDiff.y * asCpp(noiseSample.dNdy);
            }

            if (mIspcWorley.mUseMapSort) {
                // Keep the first 4 points sorted based on distance
                sortByDistance(worleyPoints,
                               overlap,
                               distance,
                               diff,
                               gradient,
                               position,
                               worleyNormal,
                               hash);
            } else {
                // Create a new point
                ispc::NOISE_WorleyPoint worleyPoint;
                asCpp(worleyPoint.position) = position;
                worleyPoint.weight = overlap;
                worleyPoint.id = hash;
                worleyPoint.dist = distance;
                asCpp(worleyPoint.uv) = uv;
                asCpp(worleyPoint.diff) = diff;
                asCpp(worleyPoint.gradient) = gradient;
                asCpp(worleyPoint.normal) = worleyNormal;
                worleyPoint.styleIndex = styleIndex;

                // Stop if we have reached the maximum size of the array
                // which is ispc::NOISE_Worley_MAX_POINTS
                if (currItr == endItr) {
                    break;
                }

                // Copy the worleyPoint data to the current array position.
                // The points are sorted later.
                *currItr++ = worleyPoint;
            }
        }

        // increment the random hash
        hash = (hash + 1) & (mIspcWorley.mNoise->mTableSize - 1);
    }
}

void
Worley::worley3D(const float searchRadius,
                 const ispc::NOISE_WorleySample& noiseSample,
                 Worley_PointArray& worleyPoints,
                 const float jitter,
                 const int distanceMethod,
                 const float minkowskiNumber) const
{
    const Vec3f pos = asCpp(noiseSample.position);

    const int ix = (int)scene_rdl2::math::floor(pos.x);
    const int iy = (int)scene_rdl2::math::floor(pos.y);
    const int iz = (int)scene_rdl2::math::floor(pos.z);

    const float fx = pos.x - ix;
    const float fy = pos.y - iy;
    const float fz = pos.z - iz;

    // distance squared to near faces of center cube
    const float x2 = fx * fx;
    const float y2 = fy * fy;
    const float z2 = fz * fz;
    
    // distance squared to far faces of center cube
    const float mx2 = (1.0f - fx) * (1.0f - fx);
    const float my2 = (1.0f - fy) * (1.0f - fy);
    const float mz2 = (1.0f - fz) * (1.0f - fz);

    auto tmpCurItr = worleyPoints.begin();
    const auto tmpEndItr = worleyPoints.end();

    const int centerX = static_cast<int>(scene_rdl2::math::floor(pos.x));
    const int centerY = static_cast<int>(scene_rdl2::math::floor(pos.y));
    const int centerZ = static_cast<int>(scene_rdl2::math::floor(pos.z));

    checkCell(centerX, centerY, centerZ, noiseSample, worleyPoints,
              tmpCurItr, tmpEndItr, jitter, false, nullptr, nullptr, nullptr,
              distanceMethod, minkowskiNumber);

    // check all of the cubes adjacent to the faces of the center cube
    if ( x2 < worleyPoints[3].dist)
        checkCell(ix-1, iy, iz, noiseSample, worleyPoints,
                  tmpCurItr, tmpEndItr, jitter, false, nullptr, nullptr, nullptr,
                  distanceMethod, minkowskiNumber);
    if (mx2 < worleyPoints[3].dist)
        checkCell(ix+1, iy, iz, noiseSample, worleyPoints,
                  tmpCurItr, tmpEndItr, jitter, false, nullptr, nullptr, nullptr,
                  distanceMethod, minkowskiNumber);
    if ( y2 < worleyPoints[3].dist)
        checkCell(ix, iy-1, iz, noiseSample, worleyPoints,
                  tmpCurItr, tmpEndItr, jitter, false, nullptr, nullptr, nullptr,
                  distanceMethod, minkowskiNumber);
    if (my2 < worleyPoints[3].dist)
        checkCell(ix, iy+1, iz, noiseSample, worleyPoints,
                  tmpCurItr, tmpEndItr, jitter, false, nullptr, nullptr, nullptr,
                  distanceMethod, minkowskiNumber);
    if ( z2 < worleyPoints[3].dist)
        checkCell(ix, iy, iz-1, noiseSample, worleyPoints,
                  tmpCurItr, tmpEndItr, jitter, false, nullptr, nullptr, nullptr,
                  distanceMethod, minkowskiNumber);
    if (mz2 < worleyPoints[3].dist)
        checkCell(ix, iy, iz+1, noiseSample, worleyPoints,
                  tmpCurItr, tmpEndItr, jitter, false, nullptr, nullptr, nullptr,
                  distanceMethod, minkowskiNumber);
    
    // check all of the cubes that share edges of the center cubes
    if ( x2 < worleyPoints[3].dist &&  y2 < worleyPoints[3].dist)
        checkCell(ix-1, iy-1, iz, noiseSample, worleyPoints,
                  tmpCurItr, tmpEndItr, jitter, false, nullptr, nullptr, nullptr,
                  distanceMethod, minkowskiNumber);
    if ( x2 < worleyPoints[3].dist && my2 < worleyPoints[3].dist)
        checkCell(ix-1, iy+1, iz, noiseSample, worleyPoints,
                  tmpCurItr, tmpEndItr, jitter, false, nullptr, nullptr, nullptr,
                  distanceMethod, minkowskiNumber);
    if (mx2 < worleyPoints[3].dist &&  y2 < worleyPoints[3].dist)
        checkCell(ix+1, iy-1, iz, noiseSample, worleyPoints,
                  tmpCurItr, tmpEndItr, jitter, false, nullptr, nullptr, nullptr,
                  distanceMethod, minkowskiNumber);
    if (mx2 < worleyPoints[3].dist && my2 < worleyPoints[3].dist)
        checkCell(ix+1, iy+1, iz, noiseSample, worleyPoints,
                  tmpCurItr, tmpEndItr, jitter, false, nullptr, nullptr, nullptr,
                  distanceMethod, minkowskiNumber);

    if ( y2 < worleyPoints[3].dist &&  z2 < worleyPoints[3].dist)
        checkCell(ix, iy-1, iz-1, noiseSample, worleyPoints,
                  tmpCurItr, tmpEndItr, jitter, false, nullptr, nullptr, nullptr,
                  distanceMethod, minkowskiNumber);
    if ( y2 < worleyPoints[3].dist && mz2 < worleyPoints[3].dist)
        checkCell(ix, iy-1, iz+1, noiseSample, worleyPoints,
                  tmpCurItr, tmpEndItr, jitter, false, nullptr, nullptr, nullptr,
                  distanceMethod, minkowskiNumber);
    if (my2 < worleyPoints[3].dist &&  z2 < worleyPoints[3].dist)
        checkCell(ix, iy+1, iz-1, noiseSample, worleyPoints,
                  tmpCurItr, tmpEndItr, jitter, false, nullptr, nullptr, nullptr,
                  distanceMethod, minkowskiNumber);
    if (my2 < worleyPoints[3].dist && mz2 < worleyPoints[3].dist)
        checkCell(ix, iy+1, iz+1, noiseSample, worleyPoints,
                  tmpCurItr, tmpEndItr, jitter, false, nullptr, nullptr, nullptr,
                  distanceMethod, minkowskiNumber);
    
    if ( x2 < worleyPoints[3].dist &&  z2 < worleyPoints[3].dist)
        checkCell(ix-1, iy, iz-1, noiseSample, worleyPoints,
                  tmpCurItr, tmpEndItr, jitter, false, nullptr, nullptr, nullptr,
                  distanceMethod, minkowskiNumber);
    if ( x2 < worleyPoints[3].dist && mz2 < worleyPoints[3].dist)
        checkCell(ix-1, iy, iz+1, noiseSample, worleyPoints,
                  tmpCurItr, tmpEndItr, jitter, false, nullptr, nullptr, nullptr,
                  distanceMethod, minkowskiNumber);
    if (mx2 < worleyPoints[3].dist &&  z2 < worleyPoints[3].dist)
        checkCell(ix+1, iy, iz-1, noiseSample, worleyPoints,
                  tmpCurItr, tmpEndItr, jitter, false, nullptr, nullptr, nullptr,
                  distanceMethod, minkowskiNumber);
    if (mx2 < worleyPoints[3].dist && mz2 < worleyPoints[3].dist)
        checkCell(ix+1, iy, iz+1, noiseSample, worleyPoints,
                  tmpCurItr, tmpEndItr, jitter, false, nullptr, nullptr, nullptr,
                  distanceMethod, minkowskiNumber);

    // check the all of the cubes on the corners of the cetner cube
    if ( x2 < worleyPoints[3].dist &&  y2 < worleyPoints[3].dist &&  z2 < worleyPoints[3].dist)
        checkCell(ix-1, iy-1, iz-1, noiseSample, worleyPoints,
                  tmpCurItr, tmpEndItr, jitter, false, nullptr, nullptr, nullptr,
                  distanceMethod, minkowskiNumber);
    if ( x2 < worleyPoints[3].dist &&  y2 < worleyPoints[3].dist && mz2 < worleyPoints[3].dist)
        checkCell(ix-1, iy-1, iz+1, noiseSample, worleyPoints,
                  tmpCurItr, tmpEndItr, jitter, false, nullptr, nullptr, nullptr,
                  distanceMethod, minkowskiNumber);
    if ( x2 < worleyPoints[3].dist && my2 < worleyPoints[3].dist &&  z2 < worleyPoints[3].dist)
        checkCell(ix-1, iy+1, iz-1, noiseSample, worleyPoints,
                  tmpCurItr, tmpEndItr, jitter, false, nullptr, nullptr, nullptr,
                  distanceMethod, minkowskiNumber);
    if ( x2 < worleyPoints[3].dist && my2 < worleyPoints[3].dist && mz2 < worleyPoints[3].dist)
        checkCell(ix-1, iy+1, iz+1, noiseSample, worleyPoints,
                  tmpCurItr, tmpEndItr, jitter, false, nullptr, nullptr, nullptr,
                  distanceMethod, minkowskiNumber);

    if (mx2 < worleyPoints[3].dist &&  y2 < worleyPoints[3].dist &&  z2 < worleyPoints[3].dist)
        checkCell(ix+1, iy-1, iz-1, noiseSample, worleyPoints,
                  tmpCurItr, tmpEndItr, jitter, false, nullptr, nullptr, nullptr,
                  distanceMethod, minkowskiNumber);
    if (mx2 < worleyPoints[3].dist &&  y2 < worleyPoints[3].dist && mz2 < worleyPoints[3].dist)
        checkCell(ix+1, iy-1, iz+1, noiseSample, worleyPoints,
                  tmpCurItr, tmpEndItr, jitter, false, nullptr, nullptr, nullptr,
                  distanceMethod, minkowskiNumber);
    if (mx2 < worleyPoints[3].dist && my2 < worleyPoints[3].dist &&  z2 < worleyPoints[3].dist)
        checkCell(ix+1, iy+1, iz-1, noiseSample, worleyPoints,
                  tmpCurItr, tmpEndItr, jitter, false, nullptr, nullptr, nullptr,
                  distanceMethod, minkowskiNumber);
    if (mx2 < worleyPoints[3].dist && my2 < worleyPoints[3].dist && mz2 < worleyPoints[3].dist)
        checkCell(ix+1, iy+1, iz+1, noiseSample, worleyPoints,
                  tmpCurItr, tmpEndItr, jitter, false, nullptr, nullptr, nullptr,
                  distanceMethod, minkowskiNumber);
}

Worley_PointArray::iterator
Worley::searchPoints(const Flake_StyleArray* styleRadii,
                     const RandomTable* glitterRandoms,
                     const Flake_StyleArray* styleCDF,
                     const ispc::NOISE_WorleySample& noiseSample,
                     Worley_PointArray& worleyPoints,
                     const int maxRadiusLimit,
                     const float jitter) const
{
    const Vec3f& pos = asCpp(noiseSample.position);
    // Use one (the max) flake size for searching the worley noise.
    // For multiple glitter styles we choose the size probabilistically later,
    // we need to use a single consistent size here.
    const float radius = scene_rdl2::math::max((*styleRadii)[0], (*styleRadii)[1]) + noiseSample.searchRadius;
    const int ixMin = (int)scene_rdl2::math::floor(pos.x-radius);
    const int iyMin = (int)scene_rdl2::math::floor(pos.y-radius);
    const int izMin = (int)scene_rdl2::math::floor(pos.z-radius);
    const int ixMax = (int)scene_rdl2::math::floor(pos.x+radius);
    const int iyMax = (int)scene_rdl2::math::floor(pos.y+radius);
    const int izMax = (int)scene_rdl2::math::floor(pos.z+radius);

    Worley_PointArray tmpWorleyPoints;
    auto tmpCurItr = tmpWorleyPoints.begin();
    const auto tmpEndItr = tmpWorleyPoints.end();
    auto wptCurItr = worleyPoints.begin();
    const auto wptEndItr = worleyPoints.end();

    if (ixMin == ixMax && iyMin == iyMax && izMin == izMax) {
        checkCell(ixMin, iyMin, izMin, noiseSample,
                  tmpWorleyPoints, tmpCurItr, tmpEndItr, jitter,
                  true, glitterRandoms, styleRadii, styleCDF);

        if (mIspcWorley.mVersion == ispc::NOISE_WORLEY_V1) {
            sortPoints(tmpWorleyPoints.begin(), tmpCurItr);
        }
        collectPoints(tmpWorleyPoints.begin(), tmpCurItr, wptCurItr, wptEndItr);

    } else {
        // The variable k represents the level of 3d cubes surrounding
        // the cube the shading point is in.  It's initialized to 0 to
        // signify starting in the cube the shading point is in.  Level 
        // k == 1 refers to the 26 cubes surrounding the starting cube.
        // Level k == 2 refers to the 98 cubes (5x5x5 = 125 - 27 = 98)
        // surrounding level 1.  
        int k = 0;
        const int centerX = (int) scene_rdl2::math::floor(pos.x);
        const int centerY = (int) scene_rdl2::math::floor(pos.y);
        const int centerZ = (int) scene_rdl2::math::floor(pos.z);
        const int maxRadius = (maxRadiusLimit == -1) ? ((int)scene_rdl2::math::floor(radius)) + 1 : maxRadiusLimit;

        checkCell(centerX, centerY, centerZ, noiseSample,
                  tmpWorleyPoints, tmpCurItr, tmpEndItr, jitter,
                  true, glitterRandoms, styleRadii, styleCDF);

        for (k = 1; k <= maxRadius; k++) {
            for(int ix = centerX - k ; ix <= centerX + k; ++ix) {
                for(int iy = centerY - k ; iy <= centerY + k; ++iy) {
                    for(int iz = centerZ - k ; iz <= centerZ + k; ++iz) {
                        if (alreadyVisited(ix, iy, iz, centerX, centerY, centerZ, k)) continue;
                        const Vec3f cornerCenter = pos - Vec3f(ix+0.5f,iy+0.5f,iz+0.5f);
                        const float distanceSphereCube = 0.86603f + radius;
                        if(dot(cornerCenter, cornerCenter) < distanceSphereCube * distanceSphereCube) {
                            checkCell(ix, iy, iz, noiseSample,
                                      tmpWorleyPoints, tmpCurItr, tmpEndItr, jitter, true,
                                      glitterRandoms, styleRadii, styleCDF,
                                      ispc::NOISE_WORLEY_DIST_LINEAR, 0.0f);
                        }
                    }
                }
            }

            if (mIspcWorley.mVersion == ispc::NOISE_WORLEY_V1 && k==1) {
                sortPoints(tmpWorleyPoints.begin(), tmpCurItr);
            }
            collectPoints(tmpWorleyPoints.begin(), tmpCurItr, wptCurItr, wptEndItr);

            // Reset temporary points processing array
            tmpCurItr = tmpWorleyPoints.begin();

            // Stop collecting points once we've collected NOISE_Worley_MAX_POINTS
            if (wptCurItr == wptEndItr) break;
        }
    }

    return wptCurItr;
}

void
Worley::searchPointsFractalLevel(const int level,
                                 const float amplitude,
                                 const float jitter,
                                 const float minkowskiNumber,
                                 const ispc::NOISE_WorleySample& noiseSample,
                                 Worley_PointArray& worleyPoints,
                                 Worley_PointArray& myWorleyPoints) const
{
    // Get the points - distance method is applied in checkCell for V3, here for V1/V2
    worley3D(1.0f, noiseSample, worleyPoints, jitter, mIspcWorley.mDistanceMethod, minkowskiNumber);

    // Apply the distance method for V1 and V2 (V3 applies it in checkCell)
    if (mIspcWorley.mVersion != ispc::NOISE_WORLEY_V3) {
        if (mIspcWorley.mDistanceMethod != ispc::NOISE_WORLEY_DIST_LINEAR) {
            applyDistanceMethod(worleyPoints, mIspcWorley.mDistanceMethod, minkowskiNumber);
        }
    }

    // Accumulate worleyPoint data for the first four points
    for (int j = 0; j < 4; ++j) {
        // Sum the distance and gradient data with scale factor
        myWorleyPoints[j].dist += worleyPoints[j].dist * amplitude;
        asCpp(myWorleyPoints[j].gradient) += asCpp(worleyPoints[j].gradient).safeNormalize() * amplitude;

        // Keep the points and ids from the first level only
        if (level == 0) {
            myWorleyPoints[j].position = worleyPoints[j].position;
            myWorleyPoints[j].id = worleyPoints[j].id;
        }
    }
}

void
Worley::searchPointsFractal(const float jitter,
                            const float minkowskiNumber, 
                            const float maxLevel, 
                            ispc::NOISE_WorleySample& noiseSample,
                            Worley_PointArray& worleyPoints) const
{
    // Initialize the first four points which accumulate 
    // data for each fractal level
    Worley_PointArray myWorleyPoints;
    for (int i = 0; i < 4; ++i) {
        myWorleyPoints[i].id = 0;
        myWorleyPoints[i].dist = 0.0f;
        asCpp(myWorleyPoints[i].gradient) = Vec3f(0.0f);
        asCpp(myWorleyPoints[i].position) = Vec3f(0.0f);

        worleyPoints[i].id = 0;
        worleyPoints[i].dist = sMaxValue;
        asCpp(worleyPoints[i].gradient) = Vec3f(0.0f);
        asCpp(worleyPoints[i].position) = Vec3f(0.0f);
    }

    // Iterate the levels/octaves
    const int numLevels = (int)scene_rdl2::math::floor(maxLevel);
    float amplitude = 1.0f;
    for (int level = 0; level < numLevels; level++) {

        // Get and accumulate the points
        searchPointsFractalLevel(level, amplitude, jitter, minkowskiNumber, 
                                 noiseSample, worleyPoints, myWorleyPoints);

        // Halve the amplitude and double the frequency for the next level
        amplitude /= 2.0f;
        asCpp(noiseSample.position) = asCpp(noiseSample.position) * 2.0f;

        // Reset the distance for the temp points to sMaxValue
        for (int i = 0; i < 4; ++i) {
            worleyPoints[i].dist = sMaxValue;
        }
    }

    // Handle the final fractional level
    const float lastLevel = maxLevel - static_cast<float>(numLevels);
    if (!isZero(lastLevel)) {

        // Get and accumulate the points
        searchPointsFractalLevel(numLevels, amplitude, jitter, minkowskiNumber, 
                                 noiseSample, worleyPoints, myWorleyPoints);

        // Blend the average distance intensity
        // Average point of interest distances - these were obtained stochastically
        // from 1,000,000 samples of worley noise taken from a domain that was a
        // cube, 100 units per side.
        static float averages[4] = { 0.36887115126f, 0.49110174090f, 0.57151765397f, 0.63471361297f };
        for (int i = 0; i < 4; ++i) {
            myWorleyPoints[i].dist += averages[i] * amplitude * (1.0f - lastLevel);
        }
    }

    worleyPoints[0].dist = myWorleyPoints[0].dist;
    worleyPoints[1].dist = myWorleyPoints[1].dist;
    worleyPoints[2].dist = myWorleyPoints[2].dist;
    worleyPoints[3].dist = myWorleyPoints[3].dist;

    worleyPoints[0].id = myWorleyPoints[0].id;
    worleyPoints[1].id = myWorleyPoints[1].id;
    worleyPoints[2].id = myWorleyPoints[2].id;
    worleyPoints[3].id = myWorleyPoints[3].id;

    worleyPoints[0].position = myWorleyPoints[0].position;
    worleyPoints[1].position = myWorleyPoints[1].position;
    worleyPoints[2].position = myWorleyPoints[2].position;
    worleyPoints[3].position = myWorleyPoints[3].position;

    worleyPoints[0].gradient = myWorleyPoints[0].gradient;
    worleyPoints[1].gradient = myWorleyPoints[1].gradient;
    worleyPoints[2].gradient = myWorleyPoints[2].gradient;
    worleyPoints[3].gradient = myWorleyPoints[3].gradient;
}

Color
Worley::getCellColor(const int cellId) const
{
    Color cellColor(0.0f, 0.0f, 0.0f);
    const int hash = perm(cellId);
    cellColor.r = mIspcWorley.mPointsX[hash];
    cellColor.g = mIspcWorley.mPointsY[hash];
    cellColor.b = mIspcWorley.mPointsZ[hash];
    return cellColor;
}

const ispc::NOISE_Worley*
Worley::getIspcWorley() const
{
    return &mIspcWorley;
}

}
}

