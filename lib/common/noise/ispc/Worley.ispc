// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#include "Worley.isph"

// Export ispc enums and structs to c++
ISPC_UTIL_EXPORT_ENUM_TO_HEADER(NOISE_WorleyVersion);
ISPC_UTIL_EXPORT_ENUM_TO_HEADER(NOISE_WorleyConstants);
ISPC_UTIL_EXPORT_ENUM_TO_HEADER(NOISE_WorleyDistanceMethod);
ISPC_UTIL_EXPORT_ENUM_TO_HEADER(NOISE_GlitterConstants);
ISPC_UTIL_EXPORT_UNIFORM_STRUCT_TO_HEADER(NOISE_Worley);
ISPC_UTIL_EXPORT_UNIFORM_STRUCT_TO_HEADER(NOISE_WorleyPoint);
ISPC_UTIL_EXPORT_UNIFORM_STRUCT_TO_HEADER(NOISE_WorleySample);

// Sorts the first numPoints points by distance
void
sortByDistance(varying NOISE_WorleyPoint *uniform worleyPoints,
               const varying float weight, 
               const varying float distance, 
               const varying Vec3f& diff,
               const varying Vec3f& gradient,
               const varying Vec3f& position,
               const varying Vec3f& normal,
               const varying int id)
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
            worleyPoints[0].diff = diff;
            worleyPoints[0].gradient = gradient;
            worleyPoints[0].position = position;
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
            worleyPoints[1].diff = diff;
            worleyPoints[1].gradient = gradient;
            worleyPoints[1].position = position;
            worleyPoints[1].id = id;

        } else if (distance < worleyPoints[2].dist) {

            worleyPoints[3].dist = worleyPoints[2].dist;
            worleyPoints[3].diff = worleyPoints[2].diff;
            worleyPoints[3].gradient = worleyPoints[2].gradient;
            worleyPoints[3].position = worleyPoints[2].position;
            worleyPoints[3].id = worleyPoints[2].id;

            worleyPoints[2].dist = distance;
            worleyPoints[2].diff = diff;
            worleyPoints[2].gradient = gradient;
            worleyPoints[2].position = position;
            worleyPoints[2].id = id;

        } else {

            worleyPoints[3].dist = distance;
            worleyPoints[3].diff = diff;
            worleyPoints[3].gradient = gradient;
            worleyPoints[3].position = position;
            worleyPoints[3].id = id;

        }
    }
}

varying bool
alreadyVisited(const varying int ix,
               const varying int iy,
               const varying int iz,
               const varying int centerX,
               const varying int centerY,
               const varying int centerZ,
               const varying int k)
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
applyDistanceMethod(varying NOISE_WorleyPoint *uniform worleyPoints,
                    const int distanceMethod,
                    const float minkowskiNumber)
{
    for (uniform int i = 0; i < 4; i++) {
        const Vec3f diff = worleyPoints[i].diff;
        switch (distanceMethod) {
            case NOISE_WORLEY_DIST_LINEAR_SQUARED:
                worleyPoints[i].dist = sqrt(worleyPoints[i].dist);
                break;
            case NOISE_WORLEY_DIST_MANHATTAN:
                worleyPoints[i].dist = abs(diff.x) + abs(diff.y) + abs(diff.z);
                break;
            case NOISE_WORLEY_DIST_CHEBYSHEV:
                worleyPoints[i].dist = max(max(abs(diff.x), abs(diff.y)), abs(diff.z));
                break;
           case NOISE_WORLEY_DIST_QUADRATIC:
                {
                    const float a = diff.x;
                    const float b = diff.y;
                    const float c = diff.z;
                    worleyPoints[i].dist = a * a + a * b + a * c + b * c + b * b + c * c;
                }
                break;
            case NOISE_WORLEY_DIST_MINKOWSKI:
                {
                    const float mn = minkowskiNumber;
                    const float a = abs(diff.x);
                    const float b = abs(diff.y);
                    const float c = abs(diff.z);
                    worleyPoints[i].dist = pow(pow(a, mn) + pow(b, mn) + pow(c, mn), (1.0f / mn));
                }
                break;
        }
    }
}

// Helper to apply distance method to a single point
inline void
applyDistanceMethodToPoint(varying float &distance,
                           const varying Vec3f &diff,
                           const int distanceMethod,
                           const float minkowskiNumber)
{
    switch (distanceMethod) {
        case NOISE_WORLEY_DIST_LINEAR_SQUARED:
            distance = sqrt(distance);
            break;
        case NOISE_WORLEY_DIST_MANHATTAN:
            distance = abs(diff.x) + abs(diff.y) + abs(diff.z);
            break;
        case NOISE_WORLEY_DIST_CHEBYSHEV:
            distance = max(max(abs(diff.x), abs(diff.y)), abs(diff.z));
            break;
        case NOISE_WORLEY_DIST_QUADRATIC:
            {
                const float a = diff.x;
                const float b = diff.y;
                const float c = diff.z;
                distance = a * a + a * b + a * c + b * c + b * b + c * c;
            }
            break;
        case NOISE_WORLEY_DIST_MINKOWSKI:
            {
                const float mn = minkowskiNumber;
                const float a = abs(diff.x);
                const float b = abs(diff.y);
                const float c = abs(diff.z);
                distance = pow(pow(a, mn) + pow(b, mn) + pow(c, mn), (1.0f / mn));
            }
            break;
    }
}

bool
cmpByLesserDist(const varying NOISE_WorleyPoint * varying x, 
                const varying NOISE_WorleyPoint * varying y)
{
    return x->dist < y->dist;
}

// Varying Swap Points
void
swapWorleyPoints(varying NOISE_WorleyPoint * varying pointA,
                 varying NOISE_WorleyPoint * varying pointB)
{
    varying NOISE_WorleyPoint pointTmp = *pointA;
    *pointA = *pointB;
    *pointB = pointTmp;
}

// Uniform Swap Points
void
swapWorleyPoints(varying NOISE_WorleyPoint * uniform pointA,
                 varying NOISE_WorleyPoint * uniform pointB)
{
    varying NOISE_WorleyPoint pointTmp = *pointA;
    *pointA = *pointB;
    *pointB = pointTmp;
}

int
partitionPoints(varying NOISE_WorleyPoint *uniform worleyPoints,
                const varying int low, 
                const varying int high,
                bool (*compar)(const varying NOISE_WorleyPoint * varying, const varying NOISE_WorleyPoint * varying))
{
    varying NOISE_WorleyPoint * varying pivotWorleyPoint = &worleyPoints[high];

    int i = (low - 1);
    for (int j = low; j <= high - 1; j++) {
        if (compar(&worleyPoints[j], pivotWorleyPoint)) {
            i++;
            uniform int uI, uJ;
            // Checking for uniform indexes reduces gather/scatter which are slower
            if (reduce_equal(i, &uI) && reduce_equal(j, &uJ))
                swapWorleyPoints(&worleyPoints[uI], &worleyPoints[uJ]);
            else
                swapWorleyPoints(&worleyPoints[i], &worleyPoints[j]);
        }
    }
    uniform int uI, uH;
    // Checking for uniform indexes reduces gather/scatter which are slower
    if (reduce_equal(i+1, &uI) && reduce_equal(high, &uH)) {
        swapWorleyPoints(&worleyPoints[uI], &worleyPoints[uH]);
    } else {
        swapWorleyPoints(&worleyPoints[i + 1], &worleyPoints[high]);
    }
    return (i + 1);
}

void
NOISE_worleySortPoints(varying NOISE_WorleyPoint *uniform worleyPoints,
                       const varying int low, 
                       const varying int high,
                       bool (*compar)(const varying NOISE_WorleyPoint * varying, const varying NOISE_WorleyPoint * varying))
{
    if (low < high) {
        const int pi = partitionPoints(worleyPoints, low, high, compar);
        NOISE_worleySortPoints(worleyPoints, low, pi - 1, compar);
        NOISE_worleySortPoints(worleyPoints, pi + 1, high, compar);
    }
}

void  
sortPoints(varying NOISE_WorleyPoint *uniform tmpPoints,
           const varying int tmpBegIndex,
           const varying int tmpCurrIndex)
{
    // Sort points based on distance from shading point
    NOISE_worleySortPoints(tmpPoints, tmpBegIndex, tmpCurrIndex - 1, &cmpByLesserDist);
}

void
collectPoints(varying NOISE_WorleyPoint *uniform tmpPoints,
              varying NOISE_WorleyPoint *uniform collectPoints,
              const varying int tmpBegIndex,
              const varying int tmpCurrIndex,
              varying int &collectCurrIndex,
              const varying int collectEndIndex)
{
    for (int i = tmpBegIndex; i < tmpCurrIndex; i++) {
        if (collectCurrIndex == collectEndIndex) {
            break; // reached NOISE_WORLEY_MAX_SEARCH_POINTS capacity, don't need to collect any more
        }
        if (tmpPoints[i].weight > 0.00001f) {
            // If the point is greater than then weight threshold then copy it
            // to the collector array's current postion then advance that 
            // current position
            collectPoints[collectCurrIndex] = tmpPoints[i];
            collectCurrIndex++;
        }
    }
}

varying float
NOISE_Worley_computeSphereDiskOverlap(const uniform NOISE_Worley * uniform n,
                                      const varying Vec3f& featurePt,
                                      const varying float radius,
                                      const varying NOISE_WorleySample& sample, 
                                      varying Vec3f& diskDiff,
                                      varying float& distToPlane,
                                      varying Vec2f& uv)
{
    const varying Vec3f diff =  sample.position - featurePt;
    const varying float deltaToPlane = dot(diff, sample.normal);
    const varying float radiusP2 =  radius * radius - deltaToPlane * deltaToPlane;
    if (radiusP2 < sEpsilon ) return 0.0f;
    varying float radiusProjection = sqrt(radiusP2);
    const varying Vec3f centerProjection = featurePt + deltaToPlane * sample.normal;
    diskDiff = sample.position - centerProjection;

    if (sample.compensateDeformation) {
        // Reduce the disk projection radius for compressed regions
        // We do this so that compressed regions are not "over-crowded" by
        // glitter flakes. We *could* expand the radius for stretch so we get
        // rid of 'negative space' but it is deemed less visually appealing atm.
        radiusProjection *= min(1.0f,
                                min(sample.compensationS,
                                    sample.compensationT));

        // Reverse Shear along X Axis
        // Any shear components should be reversed before any Scaling
        diskDiff.x = diskDiff.x - sample.shearRefP_X * diskDiff.y;

        // Reverse Scale - both compression and stretching
        Vec3f xAxis = sample.refX;
        Vec3f yAxis = sample.refY;
        Vec3f zAxis = sample.refZ;
        diskDiff = dot(diskDiff, xAxis) * sample.compensationS * xAxis +
                   dot(diskDiff, yAxis) * sample.compensationT * yAxis +
                   dot(diskDiff, zAxis) * zAxis;
    }

    distToPlane = length(diskDiff);
    varying float overlapRadius = radiusProjection + sample.radius - distToPlane;
    overlapRadius = min(max(overlapRadius, 0.0f), 2.0f * min(radiusProjection, sample.radius));
    if (n->mVersion == NOISE_WORLEY_V1) {
        overlapRadius /= max(sEpsilon, sample.footprintLength);
        return max(0.0f, overlapRadius);
    } else {
        // Compute Texture UVs

        // ReferenceFrame_init() requires unit-length normal.
        if (!isNormalized(sample.normal)) {
            return 0.0f;
        }

        // Use the flake normal to create an obj/ref->flake-space xform
        ReferenceFrame diskFrame;
        ReferenceFrame_init(diskFrame, sample.normal);
        // vector from disk center to shading position
        const varying Vec3f& Plocal = diskDiff;
        // Xform Plocal to flake-space to
        // get Flake UVs with the frame centered at disk center
        // Normalize to be in [-1, 1] space.
        uv.x =  dot(diskFrame.mX, Plocal) / radiusProjection;
        uv.y = -dot(diskFrame.mY, Plocal) / radiusProjection;

        // this function should return the portion of the sample area
        // (ray footprint) that is covered by the intersection of the
        // surface and the worley point/sphere
        const varying float overlapArea = overlapRadius * overlapRadius * sPi;
        return (overlapArea > sample.footprintArea) ? 1.f : overlapArea / sample.footprintArea;
    }
}

// Chooses a flake style for a given flake using its hash id.
// The style is chosen probabilistically using a CDF created
// by the specified frequencies of each glitter style.
varying bool
NOISE_Worley_chooseFlakeStyle(varying int id,
                              const uniform float (*glitterRandomTable)[NOISE_WORLEY_GLITTER_TABLE_SIZE],
                              const varying float (*flakeStyleCDF)[NOISE_WORLEY_GLITTER_NUM_STYLES],
                              varying unsigned int& selectedStyle)
{
    MNRY_ASSERT(glitterRandomTable != NULL);
    MNRY_ASSERT(flakeStyleCDF != NULL);
    const int tabMask = NOISE_WORLEY_GLITTER_TABLE_SIZE - 1;
    // Choose a Random Number
    const float r1 = (*glitterRandomTable)[(7*id) & tabMask];      // [0, 1]
    if (r1 <= (*flakeStyleCDF)[0]) {
        selectedStyle = 0;
    } else if (r1 <= (*flakeStyleCDF)[1]) {
        selectedStyle = 1;
    } else {
        return false;
    }

    return true;;
}

// Sets the size of the flake based on the style index which is
// also determined using the CDF of the styles and hash id of the flake.
// It is necessary to determine which size the flake is before computing
// the sphere disk overlap.
void
NOISE_Worley_setFlakeStyleAndSize(varying int hashID,
                                   const uniform float (*glitterRandomTable)[NOISE_WORLEY_GLITTER_TABLE_SIZE],
                                   const varying float (*flakeStyleCDF)[NOISE_WORLEY_GLITTER_NUM_STYLES],
                                   const varying float (*flakeStyleSizes)[NOISE_WORLEY_GLITTER_NUM_STYLES],
                                   varying float& flakeSize,
                                   varying unsigned int& styleIndex)
{
    MNRY_ASSERT(glitterRandomTable != NULL);
    MNRY_ASSERT(flakeStyleCDF != NULL);
    MNRY_ASSERT(flakeStyleSizes != NULL);
    // Do we have a valid index
    if (NOISE_Worley_chooseFlakeStyle(hashID, glitterRandomTable, flakeStyleCDF, styleIndex)) {
        flakeSize = (*flakeStyleSizes)[styleIndex];
    } else {
        flakeSize = 0.0f;
    }
}

// Returns the number of points to consider for the cell
uniform int
getPointCountUniform(const uniform NOISE_Worley * uniform n,
                     const uniform int hash)
{
    uniform int low, m, high, prob_hash;
    uniform float prob;
    low = NOISE_WORLEY_MIN_PROB_POINTS;
    high = NOISE_WORLEY_MAX_PROB_POINTS;

    prob_hash = NOISE_perm(n->mNoise, hash + 5);
    prob = n->mPointsX[prob_hash];
    uniform float lowV, highV;
    // binary search
    while (low <= high) {
        m = (low + high) / 2;
        lowV = n->mPointProbLow[m];
        highV = n->mPointProbHigh[m];
        if (prob < lowV) {
            high = m - 1;
        } else if (prob >= highV) {
            low = m + 1;
        } else {
            break;
        }
    }
    return m;
}

// Returns the number of points to consider for the cell
varying int
getPointCount(const uniform NOISE_Worley * uniform n,
              const varying int hash)
{
    int low, m, high, prob_hash;
    float prob;

    low = NOISE_WORLEY_MIN_PROB_POINTS;
    high = NOISE_WORLEY_MAX_PROB_POINTS;

    prob_hash = NOISE_perm(n->mNoise, hash + 5);
    prob = n->mPointsX[prob_hash];
    float lowV, highV;
    // binary search
    while (low <= high) {
        m = (low + high) / 2;
        lowV = n->mPointProbLow[m];
        highV = n->mPointProbHigh[m];
        if (prob < lowV) {
            high = m - 1;
        } else if (prob >= highV) {
            low = m + 1;
        } else {
            break;
        }
    }
    return m;
}

void
checkCell(const uniform NOISE_Worley * uniform n,
          const varying int ix, const varying int iy, const varying int iz,
          const varying NOISE_WorleySample& noiseSample,
          varying NOISE_WorleyPoint *uniform worleyPoints,
          varying int &currIndex,
          const varying int endIndex,
          varying float jitter,
          uniform bool checkOverlap = false,
          const uniform float (*glitterRandomTable)[NOISE_WORLEY_GLITTER_TABLE_SIZE] = NULL,
          const varying float (*flakeStyleRadii)[NOISE_WORLEY_GLITTER_NUM_STYLES] = NULL,
          const varying float (*flakeStyleCDF)[NOISE_WORLEY_GLITTER_NUM_STYLES] = NULL,
          const int distanceMethod = NOISE_WORLEY_DIST_LINEAR,
          const float minkowskiNumber = 0.0f)
{
    const varying Vec3f pos = noiseSample.position;
    int hash = NOISE_index3D(n->mNoise, ix, iy, iz);

    // Static tables are all generated using the same seed for
    // all instances therefore we add the seed here to the hash
    // value.
    if (n->mUseStaticTables) {
        hash = (hash + n->mNoise->mSeed) % n->mNoise->mTableSize;
    }

    varying Vec3f position;
    varying Vec3f diff, gradient;
    varying Vec3f planeDiff;
    varying Vec2f uv;
    float distance, overlap;
    // The style index is chosen probabilistically right before
    // determining the sphere-disk overlap to choose the right flake size
    // if there are multiple glitter styles. And it is added to the worleyPoint
    // so the other glitter style parameters are based off the same style id
    // when they are set in the glitter library.
    unsigned int styleIndex;

    // For glitter (when checkOverlap==True) we get multiple points per cell,
    // but for the standard worley noise pattern we want 1 point per cell.
    const int pointCount = (n->mVersion == NOISE_WORLEY_V3) ?
                           (checkOverlap ? getPointCount(n, hash) : 1) :
                           getPointCount(n, hash);
    for (int i = 0; i < pointCount; i++) {
        position.x = (float)(ix) + 0.5f + (n->mPointsX[hash] - 0.5f) * jitter;
        position.y = (float)(iy) + 0.5f + (n->mPointsY[hash] - 0.5f) * jitter;
        position.z = (float)(iz) + 0.5f + (n->mPointsZ[hash] - 0.5f) * jitter;

        diff = pos - position;
        gradient = position - pos;

        overlap = 1.0f;
        if (checkOverlap) {
            if (flakeStyleRadii == NULL) {
                return;
            }
            varying float flakeSize;
            if (glitterRandomTable != NULL && flakeStyleCDF != NULL) {
                NOISE_Worley_setFlakeStyleAndSize(hash, glitterRandomTable, flakeStyleCDF, flakeStyleRadii, flakeSize, styleIndex);
            } else {
                flakeSize = max((*flakeStyleRadii)[0], (*flakeStyleRadii)[1]);
            }

            overlap = NOISE_Worley_computeSphereDiskOverlap(n,
                                                            position,
                                                            flakeSize,
                                                            noiseSample,
                                                            planeDiff,
                                                            distance,
                                                            uv);
        } else {
            distance = dot(diff, diff);
            // Apply distance method transformation for V3 only
            if (n->mVersion == NOISE_WORLEY_V3) {
                applyDistanceMethodToPoint(distance, diff, distanceMethod, minkowskiNumber);
            }
        }

        if (overlap > sEpsilon) {
            varying Vec3f worleyNormal;
            if (n->mHasNormals) {
                worleyNormal.x = n->mNormalsX[hash];
                worleyNormal.y = n->mNormalsY[hash];
                worleyNormal.z = n->mNormalsZ[hash];
                worleyNormal = worleyNormal +
                    planeDiff.x * noiseSample.dNdx +
                    planeDiff.y * noiseSample.dNdy;
                worleyNormal = normalize(worleyNormal);
            }

            if (n->mUseMapSort) {
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
                // Copy the data to the current array position.
                // The points are sorted later
                worleyPoints[currIndex].position = position;
                worleyPoints[currIndex].weight = overlap;
                worleyPoints[currIndex].id = hash;
                worleyPoints[currIndex].dist = distance;
                worleyPoints[currIndex].uv = uv;
                worleyPoints[currIndex].diff = diff;
                worleyPoints[currIndex].gradient = gradient;
                worleyPoints[currIndex].normal = worleyNormal;
                worleyPoints[currIndex].styleIndex = styleIndex;

                // Stop if we have reached the maximum size of the array
                // which is ispc::NOISE_WORLEY_MAX_SEARCH_POINTS
                if (currIndex == endIndex) {
                    break;
                }

                currIndex++;
            }
        }

        // increment the random hash
        hash = (hash + 1) & (n->mNoise->mTableSize - 1);
    }
}

void
worley3D(const uniform NOISE_Worley * uniform n, 
         const varying float searchRadius,
         const varying NOISE_WorleySample& noiseSample,
         varying NOISE_WorleyPoint *uniform worleyPoints,
         const varying float jitter,
         const int distanceMethod = NOISE_WORLEY_DIST_LINEAR,
         const float minkowskiNumber = 0.0f)
{
    const Vec3f pos = noiseSample.position;

    const int ix = (int)floor(pos.x);
    const int iy = (int)floor(pos.y);
    const int iz = (int)floor(pos.z);

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

    int tmpCurrIndex = 0;
    const int tmpEndIndex = NOISE_WORLEY_MAX_SEARCH_POINTS;

    const int centerX = (int)floor(pos.x);
    const int centerY = (int)floor(pos.y);
    const int centerZ = (int)floor(pos.z);

    checkCell(n, centerX, centerY, centerZ, noiseSample, worleyPoints,
              tmpCurrIndex, tmpEndIndex, jitter, false, NULL, NULL, NULL,
              distanceMethod, minkowskiNumber);

    // check all of the cubes adjacent to the faces of the center cube
    if ( x2 < worleyPoints[3].dist)
        checkCell(n, ix-1, iy, iz, noiseSample, worleyPoints,
                  tmpCurrIndex, tmpEndIndex, jitter, false, NULL, NULL, NULL,
                  distanceMethod, minkowskiNumber);
    if (mx2 < worleyPoints[3].dist)
        checkCell(n, ix+1, iy, iz, noiseSample, worleyPoints,
                  tmpCurrIndex, tmpEndIndex, jitter, false, NULL, NULL, NULL,
                  distanceMethod, minkowskiNumber);
    if ( y2 < worleyPoints[3].dist)
        checkCell(n, ix, iy-1, iz, noiseSample, worleyPoints,
                  tmpCurrIndex, tmpEndIndex, jitter, false, NULL, NULL, NULL,
                  distanceMethod, minkowskiNumber);
    if (my2 < worleyPoints[3].dist)
        checkCell(n, ix, iy+1, iz, noiseSample, worleyPoints,
                  tmpCurrIndex, tmpEndIndex, jitter, false, NULL, NULL, NULL,
                  distanceMethod, minkowskiNumber);
    if ( z2 < worleyPoints[3].dist)
        checkCell(n, ix, iy, iz-1, noiseSample, worleyPoints,
                  tmpCurrIndex, tmpEndIndex, jitter, false, NULL, NULL, NULL,
                  distanceMethod, minkowskiNumber);
    if (mz2 < worleyPoints[3].dist)
        checkCell(n, ix, iy, iz+1, noiseSample, worleyPoints,
                  tmpCurrIndex, tmpEndIndex, jitter, false, NULL, NULL, NULL,
                  distanceMethod, minkowskiNumber);
    
    // check all of the cubes that share edges of the center cubes
    if ( x2 < worleyPoints[3].dist &&  y2 < worleyPoints[3].dist)
        checkCell(n, ix-1, iy-1, iz, noiseSample, worleyPoints,
                  tmpCurrIndex, tmpEndIndex, jitter, false, NULL, NULL, NULL,
                  distanceMethod, minkowskiNumber);
    if ( x2 < worleyPoints[3].dist && my2 < worleyPoints[3].dist)
        checkCell(n, ix-1, iy+1, iz, noiseSample, worleyPoints,
                  tmpCurrIndex, tmpEndIndex, jitter, false, NULL, NULL, NULL,
                  distanceMethod, minkowskiNumber);
    if (mx2 < worleyPoints[3].dist &&  y2 < worleyPoints[3].dist)
        checkCell(n, ix+1, iy-1, iz, noiseSample, worleyPoints,
                  tmpCurrIndex, tmpEndIndex, jitter, false, NULL, NULL, NULL,
                  distanceMethod, minkowskiNumber);
    if (mx2 < worleyPoints[3].dist && my2 < worleyPoints[3].dist)
        checkCell(n, ix+1, iy+1, iz, noiseSample, worleyPoints,
                  tmpCurrIndex, tmpEndIndex, jitter, false, NULL, NULL, NULL,
                  distanceMethod, minkowskiNumber);

    if ( y2 < worleyPoints[3].dist &&  z2 < worleyPoints[3].dist)
        checkCell(n, ix, iy-1, iz-1, noiseSample, worleyPoints,
                  tmpCurrIndex, tmpEndIndex, jitter, false, NULL, NULL, NULL,
                  distanceMethod, minkowskiNumber);
    if ( y2 < worleyPoints[3].dist && mz2 < worleyPoints[3].dist)
        checkCell(n, ix, iy-1, iz+1, noiseSample, worleyPoints,
                  tmpCurrIndex, tmpEndIndex, jitter, false, NULL, NULL, NULL,
                  distanceMethod, minkowskiNumber);
    if (my2 < worleyPoints[3].dist &&  z2 < worleyPoints[3].dist)
        checkCell(n, ix, iy+1, iz-1, noiseSample, worleyPoints,
                  tmpCurrIndex, tmpEndIndex, jitter, false, NULL, NULL, NULL,
                  distanceMethod, minkowskiNumber);
    if (my2 < worleyPoints[3].dist && mz2 < worleyPoints[3].dist)
        checkCell(n, ix, iy+1, iz+1, noiseSample, worleyPoints,
                  tmpCurrIndex, tmpEndIndex, jitter, false, NULL, NULL, NULL,
                  distanceMethod, minkowskiNumber);
    
    if ( x2 < worleyPoints[3].dist &&  z2 < worleyPoints[3].dist)
        checkCell(n, ix-1, iy, iz-1, noiseSample, worleyPoints,
                  tmpCurrIndex, tmpEndIndex, jitter, false, NULL, NULL, NULL,
                  distanceMethod, minkowskiNumber);
    if ( x2 < worleyPoints[3].dist && mz2 < worleyPoints[3].dist)
        checkCell(n, ix-1, iy, iz+1, noiseSample, worleyPoints,
                  tmpCurrIndex, tmpEndIndex, jitter, false, NULL, NULL, NULL,
                  distanceMethod, minkowskiNumber);
    if (mx2 < worleyPoints[3].dist &&  z2 < worleyPoints[3].dist)
        checkCell(n, ix+1, iy, iz-1, noiseSample, worleyPoints,
                  tmpCurrIndex, tmpEndIndex, jitter, false, NULL, NULL, NULL,
                  distanceMethod, minkowskiNumber);
    if (mx2 < worleyPoints[3].dist && mz2 < worleyPoints[3].dist)
        checkCell(n, ix+1, iy, iz+1, noiseSample, worleyPoints,
                  tmpCurrIndex, tmpEndIndex, jitter, false, NULL, NULL, NULL,
                  distanceMethod, minkowskiNumber);

    // check the all of the cubes on the corners of the cetner cube
    if ( x2 < worleyPoints[3].dist &&  y2 < worleyPoints[3].dist &&  z2 < worleyPoints[3].dist)
        checkCell(n, ix-1, iy-1, iz-1, noiseSample, worleyPoints,
                  tmpCurrIndex, tmpEndIndex, jitter, false, NULL, NULL, NULL,
                  distanceMethod, minkowskiNumber);
    if ( x2 < worleyPoints[3].dist &&  y2 < worleyPoints[3].dist && mz2 < worleyPoints[3].dist)
        checkCell(n, ix-1, iy-1, iz+1, noiseSample, worleyPoints,
                  tmpCurrIndex, tmpEndIndex, jitter, false, NULL, NULL, NULL,
                  distanceMethod, minkowskiNumber);
    if ( x2 < worleyPoints[3].dist && my2 < worleyPoints[3].dist &&  z2 < worleyPoints[3].dist)
        checkCell(n, ix-1, iy+1, iz-1, noiseSample, worleyPoints,
                  tmpCurrIndex, tmpEndIndex, jitter, false, NULL, NULL, NULL,
                  distanceMethod, minkowskiNumber);
    if ( x2 < worleyPoints[3].dist && my2 < worleyPoints[3].dist && mz2 < worleyPoints[3].dist)
        checkCell(n, ix-1, iy+1, iz+1, noiseSample, worleyPoints,
                  tmpCurrIndex, tmpEndIndex, jitter, false, NULL, NULL, NULL,
                  distanceMethod, minkowskiNumber);

    if (mx2 < worleyPoints[3].dist &&  y2 < worleyPoints[3].dist &&  z2 < worleyPoints[3].dist)
        checkCell(n, ix+1, iy-1, iz-1, noiseSample, worleyPoints,
                  tmpCurrIndex, tmpEndIndex, jitter, false, NULL, NULL, NULL,
                  distanceMethod, minkowskiNumber);
    if (mx2 < worleyPoints[3].dist &&  y2 < worleyPoints[3].dist && mz2 < worleyPoints[3].dist)
        checkCell(n, ix+1, iy-1, iz+1, noiseSample, worleyPoints,
                  tmpCurrIndex, tmpEndIndex, jitter, false, NULL, NULL, NULL,
                  distanceMethod, minkowskiNumber);
    if (mx2 < worleyPoints[3].dist && my2 < worleyPoints[3].dist &&  z2 < worleyPoints[3].dist)
        checkCell(n, ix+1, iy+1, iz-1, noiseSample, worleyPoints,
                  tmpCurrIndex, tmpEndIndex, jitter, false, NULL, NULL, NULL,
                  distanceMethod, minkowskiNumber);
    if (mx2 < worleyPoints[3].dist && my2 < worleyPoints[3].dist && mz2 < worleyPoints[3].dist)
        checkCell(n, ix+1, iy+1, iz+1, noiseSample, worleyPoints,
                  tmpCurrIndex, tmpEndIndex, jitter, false, NULL, NULL, NULL,
                  distanceMethod, minkowskiNumber);
}

varying int
NOISE_worleySearchPoints(const uniform NOISE_Worley * uniform n,
                         uniform ShadingTLState* uniform tls,
                         const varying float (*flakeStyleRadii)[NOISE_WORLEY_GLITTER_NUM_STYLES],
                         const uniform float (*glitterRandomTable)[NOISE_WORLEY_GLITTER_TABLE_SIZE],
                         const varying float (*flakeStyleCDF)[NOISE_WORLEY_GLITTER_NUM_STYLES],
                         const varying NOISE_WorleySample& noiseSample,
                         varying NOISE_WorleyPoint *uniform worleyPoints,
                         const uniform int maxRadiusLimit,
                         const varying float jitter)
{
    const varying Vec3f pos = noiseSample.position;
    // Use one (the max) flake size for searching the worley noise.
    // For multiple glitter styles we choose the size probabilistically later,
    // we need to use a single consistent size here.
    const float varying radius = max((*flakeStyleRadii)[0], (*flakeStyleRadii)[1]) + noiseSample.searchRadius;
    const varying int ixMin = (int)floor(pos.x - radius);
    const varying int iyMin = (int)floor(pos.y - radius);
    const varying int izMin = (int)floor(pos.z - radius);
    const varying int ixMax = (int)floor(pos.x + radius);
    const varying int iyMax = (int)floor(pos.y + radius);
    const varying int izMax = (int)floor(pos.z + radius);

    uniform Arena * uniform arena = tls->mArena;
    varying NOISE_WorleyPoint *uniform tmpWorleyPoints = (varying NOISE_WorleyPoint *uniform)
        Arena_allocArray(arena, NOISE_WORLEY_MAX_SEARCH_POINTS, sizeof(varying NOISE_WorleyPoint));

    int tmpCurrIndex = 0;
    const int tmpEndIndex = NOISE_WORLEY_MAX_SEARCH_POINTS;
    int wptCurrIndex = 0;
    const int wptEndIndex = NOISE_WORLEY_MAX_SEARCH_POINTS;

    if (ixMin == ixMax && iyMin == iyMax && izMin == izMax) {
        checkCell(n, ixMin, iyMin, izMin, noiseSample, tmpWorleyPoints,
                  tmpCurrIndex, tmpEndIndex, jitter,
                  true, glitterRandomTable, flakeStyleRadii, flakeStyleCDF,
                  NOISE_WORLEY_DIST_LINEAR, 0.0f);

        if (n->mVersion == NOISE_WORLEY_V1) {
            sortPoints(tmpWorleyPoints, 0, tmpCurrIndex);
        }
        collectPoints(tmpWorleyPoints, worleyPoints, 0, tmpCurrIndex, wptCurrIndex, wptEndIndex);
    } else {
        // The variable k represents the level of 3d cubes surrounding
        // the cube the shading point is in.  It's initialized to 0 to
        // signify starting in the cube the shading point is in.  Level 
        // k == 1 refers to the 26 cubes surrounding the starting cube.
        // Level k == 2 refers to the 98 cubes (5x5x5 = 125 - 27 = 98)
        // surrounding level 1.  
        int k = 0;
        const int centerX = (int)floor(pos.x);
        const int centerY = (int)floor(pos.y);
        const int centerZ = (int)floor(pos.z);
        const int maxRadius = ((int)floor(radius)) + 1;

        checkCell(n, centerX, centerY, centerZ, noiseSample, tmpWorleyPoints,
                  tmpCurrIndex, tmpEndIndex, jitter,
                  true, glitterRandomTable, flakeStyleRadii, flakeStyleCDF,
                  NOISE_WORLEY_DIST_LINEAR, 0.0f);

        for (k = 1; k <= maxRadius; k++) {
            for(int ix = centerX - k ; ix <= centerX + k; ++ix) {
                for(int iy = centerY - k ; iy <= centerY + k; ++iy) {
                    for(int iz = centerZ - k ; iz <= centerZ + k; ++iz) {
                        if (alreadyVisited(ix, iy, iz, centerX, centerY, centerZ, k)) continue;
                        const Vec3f cornerCenter = pos - Vec3f_ctor(ix+0.5f,iy+0.5f,iz+0.5f);
                        const float distanceSphereCube = 0.86603f + radius;
                        if(dot(cornerCenter, cornerCenter) < distanceSphereCube*distanceSphereCube) {
                            checkCell(n, ix, iy, iz, noiseSample, tmpWorleyPoints,
                                      tmpCurrIndex, tmpEndIndex, jitter,
                                      true, glitterRandomTable, flakeStyleRadii, flakeStyleCDF,
                                      NOISE_WORLEY_DIST_LINEAR, 0.0f);
                        }
                    }
                }
            }

            if (n->mVersion == NOISE_WORLEY_V1 && k==1) {
                sortPoints(tmpWorleyPoints, 0, tmpCurrIndex);
            }
            collectPoints(tmpWorleyPoints, worleyPoints, 0, tmpCurrIndex, wptCurrIndex, wptEndIndex);

            // Reset temporary points processing array
            tmpCurrIndex = 0;

            // Stop collecting points once we've collected NOISE_WORLEY_MAX_SEARCH_POINTS
            if (wptCurrIndex == wptEndIndex) break;
        }
    }


    return wptCurrIndex;
}

void
searchPointsFractalLevel(const uniform NOISE_Worley * uniform n, 
                         const varying int level,
                         const varying float amplitude,
                         const varying float jitter,
                         const varying float minkowskiNumber,
                         const varying NOISE_WorleySample& noiseSample,
                         varying NOISE_WorleyPoint *uniform worleyPoints,
                         varying NOISE_WorleyPoint *uniform myWorleyPoints)
{
    // Get the points - distance method is applied in checkCell for V3, here for V1/V2
    worley3D(n, 1.0f, noiseSample, worleyPoints, jitter, n->mDistanceMethod, minkowskiNumber);

    // Apply the distance method for V1 and V2 (V3 applies it in checkCell)
    if (n->mVersion != NOISE_WORLEY_V3) {
        if (n->mDistanceMethod != NOISE_WORLEY_DIST_LINEAR) {
            applyDistanceMethod(worleyPoints, n->mDistanceMethod, minkowskiNumber);
        }
    }

    // Accumulate worleyPoint data for the first four points
    for (uniform int j = 0; j < 4; ++j) {
        // Sum the distance and gradient data with scale factor
        myWorleyPoints[j].dist += worleyPoints[j].dist * amplitude;
        myWorleyPoints[j].gradient = myWorleyPoints[j].gradient + normalize(worleyPoints[j].gradient) * amplitude;

        // Keep the points and ids from the first level only
        if (level == 0) {
            myWorleyPoints[j].position = worleyPoints[j].position;
            myWorleyPoints[j].id = worleyPoints[j].id;
        }
    }

}

void
NOISE_worleySearchPointsFractal(const uniform NOISE_Worley * uniform n,
                                uniform ShadingTLState* uniform tls,
                                const varying float jitter,
                                const varying float minkowskiNumber,
                                const varying float maxLevel, 
                                varying NOISE_WorleySample& noiseSample,
                                varying NOISE_WorleyPoint *uniform worleyPoints)
{
    // Initialize the first four points which accumulate 
    // data for each fractal level
    uniform Arena * uniform arena = tls->mArena;
    varying NOISE_WorleyPoint *uniform myWorleyPoints = (varying NOISE_WorleyPoint *uniform)
        Arena_allocArray(arena, NOISE_WORLEY_MAX_SEARCH_POINTS, sizeof(varying NOISE_WorleyPoint));

    for (uniform int i = 0; i < 4; ++i) {
        myWorleyPoints[i].id = 0;
        myWorleyPoints[i].dist = 0.0f;
        myWorleyPoints[i].gradient = Vec3f_ctor(0.0f);
        myWorleyPoints[i].position = Vec3f_ctor(0.0f);

        worleyPoints[i].id = 0;
        worleyPoints[i].dist = sMaxValue;
        worleyPoints[i].gradient = Vec3f_ctor(0.0f);
        worleyPoints[i].position = Vec3f_ctor(0.0f);
    }

    // Iterate the levels/octaves
    const int numLevels = (int)floor(maxLevel);
    float amplitude = 1.0f;
    for (int level = 0; level < numLevels; level++) {

        // Get and accumulate the points
        searchPointsFractalLevel(n, level, amplitude, jitter, minkowskiNumber,
                                 noiseSample, worleyPoints, myWorleyPoints);

        amplitude /= 2.0f;
        noiseSample.position = noiseSample.position * 2.0f;

        // Reset the distance for the temp points to sMaxValue
        for (uniform int i = 0; i < 4; ++i) {
            worleyPoints[i].dist = sMaxValue;
        }
    }

    // Handle the final fractional level
    const float lastLevel = maxLevel - (float)numLevels;
    if (!isZero(lastLevel)) {

        // Get and accumulate the points
        searchPointsFractalLevel(n, numLevels, amplitude, jitter, minkowskiNumber,
                                 noiseSample, worleyPoints, myWorleyPoints);

        // Blend the average distance intensity
        // Average point of interest distances - these were obtained stochastically
        // from 1,000,000 samples of worley noise taken from a domain that was a
        // cube, 100 units per side.
        static float averages[4] = { 0.36887115126f, 0.49110174090f, 0.57151765397f, 0.63471361297f };
        for (uniform int i = 0; i < 4; ++i) {
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

// Get a random color based on cell id
varying Color 
NOISE_worleyGetCellColor(const uniform NOISE_Worley * uniform n, 
                         const int cellId)
{
    Color cellColor;
    const int hash = NOISE_perm(n->mNoise, cellId);
    cellColor.r = n->mPointsX[hash];
    cellColor.g = n->mPointsY[hash];
    cellColor.b = n->mPointsZ[hash];
    return cellColor;
}

