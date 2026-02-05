// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

//

#pragma once

#pragma warning disable 1711 // Warnings about assignemnt to statically allocated data

#include "Noise.h"
#include "Noise_ispc_stubs.h"
#include "Worley_ispc_stubs.h"
#include <scene_rdl2/common/math/Color.h>

#include <array>

namespace moonray {
namespace noise {

typedef std::array<ispc::NOISE_WorleyPoint, ispc::NOISE_WORLEY_MAX_SEARCH_POINTS> Worley_PointArray;
typedef float Flake_StyleArray[ispc::NOISE_WORLEY_GLITTER_NUM_STYLES];
typedef float RandomTable[ispc::NOISE_WORLEY_GLITTER_TABLE_SIZE];

class Worley : public Noise
{
public:
    // Constructs a Worley object with the following options:
    //
    // seed -            Seeds the random number generator used to calculate the number of
    //                   points in a cell, point locations, cell id, and optionally normals.
    //
    // tableSize -       Sets the size of the random tables used for the points.  Needs
    //                   to be large enough to avoid repeating patterns.
    //
    // version -         Sets the version to use.   Currently there are two versions and the
    //                   only difference is how the overlap of the points is calculated.
    //                   Version one divides the overlap by the footprint length.   This is
    //                   the wrong behavior but is maintained for backward compatibility.
    //                   Version two produces the correct behavior by instead dividing by
    //                   the footprint area.
    //
    // distanceMethod -  The method used to modulate the distance from the points to 
    //                   the sample location.  Used to create patterns with the worley noise map.
    //
    // randomness -      If addNormals is true, controls how much the point normals randomly
    //                   deviate from the surface normal.
    //
    // addNormals -      Adds random normals to the found points for use with the
    //                   glitter flake material.
    //
    // useStaticTables - If set, a single static table of noise is used and the seed acts
    //                   as an offset.   Otherwise each instance gets it's own noise table
    //                   constructed using the seed.
    //
    // useOldSort -      If set, uses the old sorting style from the worley noise map
    //                   which keeps the first n points sorted by distance.   Otherwise
    //                   all of the points are sorted using std::sort for c++ and 
    //                   quicksort for ispc.
    explicit Worley(const int seed = 0,
                    const int tableSize = 2048,
                    const ispc::NOISE_WorleyVersion version = ispc::NOISE_WORLEY_V1,
                    const int distanceMethod = ispc::NOISE_WORLEY_DIST_LINEAR,
                    const float randomness = 0.5f,
                    const bool addNormals = false,
                    const bool useStaticTables = false,
                    const bool useMapSort = true);

    ~Worley() { }

    // Get a pointer to the worley noise data, required to use the 
    // library functions in ISPC during shade.
    const ispc::NOISE_Worley* getIspcWorley() const;

    // Returns maxLevel octaves/levels of accumulated worley noise data 
    // into an array of points.  The first four points are sorted by distance
    // and returned with accumulated data from the levels.  The distance 
    // and gradient data are summed for each level with a scale factor which 
    // halves for every level.  The position and id data are not accumulated 
    // and only the first level is returned. 
    void searchPointsFractal(const float jitter,
                             const float minkowskiNumber, 
                             const float maxLevel, 
                             ispc::NOISE_WorleySample& noiseSample,
                             Worley_PointArray& worleyPoints) const;

    // Fills an array with worley points found within the search
    // radius relative to the noiseSample's position.  If checkOverlap
    // is true, then the noiseSample's normal is used to calculate
    // the amount that the point overlaps the surface and which is set
    // as the worley point's weight.  The maxRadiusLimit, if not set
    // to -1, is used to set a maximum number of border cells to search
    // regardless of the searchRadius.  The jitter sets how far of the
    // grid that the points are randomly offset.
    Worley_PointArray::iterator searchPoints(const Flake_StyleArray* styleRadii,
                                             const RandomTable* glitterRandoms,
                                             const Flake_StyleArray* styleCDF,
                                             const ispc::NOISE_WorleySample& noiseSample,
                                             Worley_PointArray& worleyPoints,
                                             const int maxRadiusLimit = -1,
                                             const float jitter = 1.0f) const;

    // Get a random color based on cell id
    scene_rdl2::math::Color getCellColor(const int cellId) const;
    
private:

    // Like searchPoints but optimized for the WorleyMap with no loop
    // and only checking only the immediate surrounding cells
    void worley3D(const float searchRadius,
                  const ispc::NOISE_WorleySample& noiseSample,
                  Worley_PointArray& worleyPoints,
                  const float jitter,
                  const int distanceMethod = ispc::NOISE_WORLEY_DIST_LINEAR,
                  const float minkowskiNumber = 0.0f) const;

    // Searches for points and accumulates the data for a single level/octave
    void searchPointsFractalLevel(const int level,
                                  const float amplitude,
                                  const float jitter,
                                  const float minkowskiNumber,
                                  const ispc::NOISE_WorleySample& noiseSample,
                                  Worley_PointArray& worleyPoints,
                                  Worley_PointArray& myWorleyPoints) const;

    // Check the cell at the specified index
    void checkCell(const int ix, const int iy, const int iz,
                   const ispc::NOISE_WorleySample& noiseSample,
                   Worley_PointArray& worleyPoints,
                   Worley_PointArray::iterator& currItr,
                   const Worley_PointArray::iterator endItr,
                   const float jitter,
                   const bool checkOverlap = false,
                   const RandomTable* glitterRandoms = NULL,
                   const Flake_StyleArray* styleRadii = NULL,
                   const Flake_StyleArray* styleCDF = NULL,
                   const int distanceMethod = ispc::NOISE_WORLEY_DIST_LINEAR,
                   const float minkowskiNumber = 0.0f) const;

    // Simple utility function for computing overlap between ray footprint and spheres.
    // The normals should be computed by passing in the surface normal
    // at the intersection point and the normal differentials dNdx and
    // dNdy. The point is projected to the surface and the aligned 2D distance
    // should be used to add the differential to dNdx and dNdy
    inline float computeSphereDiskOverlap(const scene_rdl2::math::Vec3f& featurePt,
                                          const float radius, const ispc::NOISE_WorleySample& sample,
                                          scene_rdl2::math::Vec3f& diskDiff, float& distToPlane,
                                          scene_rdl2::math::Vec2f& uv) const;

    // Chooses a flake style for a given flake using its hash id.
    // The style is chosen probabilistically using a CDF created
    // by the specified frequencies of each glitter style.
    bool chooseFlakeStyle(int id,
                          const RandomTable* glitterRandoms,
                          const Flake_StyleArray* styleCDF,
                          unsigned int& selectedStyle) const;

    // Sets the size of the flake based on the style index which is
    // also determined using the CDF of the styles and hash id of the flake.
    // It is necessary to determine which size the flake is before computing
    // the sphere disk overlap.
    void setFlakeStyleAndSize(int hashID,
                              const RandomTable* glitterRandoms,
                              const Flake_StyleArray* styleCDF,
                              const Flake_StyleArray* styleSizes,
                              float& flakeSize,
                              unsigned int& styleIndex) const;

    // Build the worley specific noise tables
    void buildPointTables(const bool addNormals, const int tableSize);
    void buildStaticPointTables(const bool addNormals, const int tableSize);

    // Builds table of probabilities for each point count between 
    // Worley_MinPoints and SHADER_WORLEY_MaxPoints
    void initPointProbabilities(void);

    // Returns the number of points to consider for the cell
    int getPointCount(const int hash) const;

    std::vector<float> mPointsX;
    std::vector<float> mPointsY;
    std::vector<float> mPointsZ;
    std::vector<float> mRandsForNormalsX;
    std::vector<float> mRandsForNormalsY;

    // Normals need to be one copy per shader instance 
    // since they are computed based on the randomness
    std::vector<float> mNormalsX;
    std::vector<float> mNormalsY;
    std::vector<float> mNormalsZ;

    // Static data
    static bool sIsWorleyDataInitialized;
    static std::mutex sWorleyInitDataMutex;

    static std::vector<float> sWorleyPointsX;
    static std::vector<float> sWorleyPointsY;
    static std::vector<float> sWorleyPointsZ;
    static std::vector<float> sWorleyRandsForNormalsX;
    static std::vector<float> sWorleyRandsForNormalsY;

    ispc::NOISE_Worley mIspcWorley;
};

}
}

