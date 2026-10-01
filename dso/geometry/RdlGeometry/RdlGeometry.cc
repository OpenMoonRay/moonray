// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

/// @file RdlGeometry.cc

/*
 * RdlGeometry is a procedural that pretends to be a mesh, curves or points
 * primitive, depending on "geo_type". Like RdlMeshGeometry, RdlCurveGeometry
 * and RdlPointGeometry it stores its geometry data directly in the RDL scene,
 * but all of it (including single-valued settings such as "is_subd") comes
 * from the UserData objects in its "data" attribute rather than from
 * attributes of its own:
 *
 *  - A key matching the name (or an alias) of an attribute of the equivalent
 *    Rdl*Geometry supplies the value of that attribute. Its values must be
 *    the UserData type matching the attribute's type (e.g. bool values for
 *    the Bool "is_subd"), or the geometry is not generated. Single-valued
 *    attributes use the first value.
 *  - The attributes that hold one motion step each are instead a single key
 *    whose vec3f_values_0 and vec3f_values_1 hold the two motion steps:
 *    "vertex_list" in place of "vertex_list_0" and "vertex_list_1", and
 *    "velocity_list" in place of "velocity_list_0" and "velocity_list_1".
 *    The per-step names are ignored. Only the first set of values of every
 *    other key is read.
 *  - A key matching an attribute every Geometry has (e.g. "label") is
 *    ignored: set those on the RdlGeometry object itself.
 *  - Every other key is added as a primitive attribute, unless its values
 *    are vec4f or mat3f, which primitive attributes don't support.
 *
 * Because RdlGeometry keeps no geometry data outside of its UserData, the
 * renderer can free it all once the primitive is built (see
 * RdlGeometry::releaseInputData()).
 */

#include <moonray/rendering/geom/Api.h>
#include <moonray/rendering/geom/LocalMotionBlur.h>
#include <moonray/rendering/geom/ProceduralLeaf.h>
#include <moonray/rendering/geom/PrimitiveUserData.h>

#include <scene_rdl2/common/except/exceptions.h>
#include <scene_rdl2/common/platform/Platform.h>
#include <scene_rdl2/render/util/Strings.h>
#include <scene_rdl2/scene/rdl2/Geometry.h>
#include <scene_rdl2/scene/rdl2/UserData.h>

#include <algorithm>
#include <numeric>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "attributes.cc"

RDL2_DSO_CLASS_BEGIN(RdlGeometry, scene_rdl2::rdl2::Geometry)

public:
    RDL2_DSO_DEFAULT_CTOR(RdlGeometry)
    moonray::geom::Procedural* createProcedural() const;
    void destroyProcedural() const;
    void releaseInputData() override;

RDL2_DSO_CLASS_END(RdlGeometry)

namespace {

using scene_rdl2::rdl2::UserData;

// Canonical names of the Rdl*Geometry attributes RdlGeometry reads from its
// UserData. These must match the attribute declarations in
// dso/geometry/RdlMesh, RdlCurve and RdlPoint and in
// scene_rdl2/scene/rdl2/CommonAttributes.h.
namespace key {
const std::string faceVertexCount("face_vertex_count");
const std::string verticesByIndex("vertices_by_index");
const std::string curvesVertexCount("curves_vertex_count");
// the two motion steps are the values_0 and values_1 of a single key
const std::string vertexList("vertex_list");
const std::string velocityList("velocity_list");
const std::string accelerationList("accleration_list"); // sic, as declared by the Rdl*Geometry
const std::string velocityScale("velocity_scale");
const std::string orientation("orientation");
const std::string partList("part_list");
const std::string partFaceCountList("part_face_count_list");
const std::string partFaceIndices("part_face_indices");
const std::string partIndices("part_indices");
const std::string uvList("uv_list");
const std::string normalList("normal_list");
const std::string radiusList("radius_list");
const std::string useScreenSpaceRadius("use_screen_space_radius");
const std::string curveType("curve_type");
const std::string isSubd("is_subd");
const std::string subdScheme("subd_scheme");
const std::string subdBoundary("subd_boundary");
const std::string subdFVarLinear("subd_fvar_linear");
const std::string subdCreaseIndices("subd_crease_indices");
const std::string subdCreaseSharpnesses("subd_crease_sharpnesses");
const std::string subdCornerIndices("subd_corner_indices");
const std::string subdCornerSharpnesses("subd_corner_sharpnesses");
const std::string primitiveAttributes("primitive_attributes");
// DECLARE_COMMON_MESH_ATTRIBUTES
const std::string meshResolution("mesh_resolution");
const std::string adaptiveError("adaptive_error");
const std::string smoothNormal("smooth_normal");
// DECLARE_COMMON_CURVES_ATTRIBUTES
const std::string tessellationRate("tessellation_rate");
const std::string curvesSubType("curves_subtype");
// DECLARE_COMMON_EXPLICIT_SHADING_ATTRIBUTES
const std::string explicitShading("use_explicit_shading_attributes");
// DECLARE_COMMON_MOTION_BLUR_ATTRIBUTES
const std::string useRotationMotionBlur("use_rotation_motion_blur");
const std::string motionBlurType("motion_blur_type");
const std::string curvedMotionBlurSampleCount("curved_motion_blur_sample_count");
const std::string primitiveAttributeFrame("primitive_attribute_frame");
// DECLARE_COMMON_LOCAL_MOTION_BLUR_POINT_ATTRIBUTES
const std::string localMotionBlurPositionList("local_motion_blur_position_list");
const std::string localMotionBlurOrientList("local_motion_blur_orient_list");
const std::string localMotionBlurScaleList("local_motion_blur_scale_list");
const std::string localMotionBlurRadiusList("local_motion_blur_radius_list");
const std::string localMotionBlurInnerRadiusList("local_motion_blur_inner_radius_list");
const std::string localMotionBlurMultiplierList("local_motion_blur_multiplier_list");
const std::string localMotionBlurStrengthMult("local_motion_blur_strength_multiplier");
const std::string localMotionBlurRadiusMult("local_motion_blur_radius_multiplier");
} // namespace key

// The type of a UserData's values, i.e. which of its typed keys holds them
enum class ValueType { BOOL, INT, FLOAT, STRING, COLOR, VEC2F, VEC3F, VEC4F, MAT3F, MAT4F };

const char*
typeName(ValueType type)
{
    switch (type) {
    case ValueType::BOOL: return "bool";
    case ValueType::INT: return "int";
    case ValueType::FLOAT: return "float";
    case ValueType::STRING: return "string";
    case ValueType::COLOR: return "color";
    case ValueType::VEC2F: return "vec2f";
    case ValueType::VEC3F: return "vec3f";
    case ValueType::VEC4F: return "vec4f";
    case ValueType::MAT3F: return "mat3f";
    case ValueType::MAT4F: return "mat4f";
    }
    return "unknown";
}

// An attribute of an Rdl*Geometry that RdlGeometry reads from a UserData key.
// The type is the UserData type that matches the attribute's type: e.g.
// "is_subd" is a Bool attribute, so it must be a bool UserData.
struct KeyInfo
{
    std::string mName;
    ValueType mType;
    std::vector<std::string> mAliases;
};

using KeyList = std::vector<KeyInfo>;

const KeyList sMotionBlurKeys = {
    { key::useRotationMotionBlur, ValueType::BOOL, {} },
    { key::motionBlurType, ValueType::INT, { "motion blur type" } },
    { key::curvedMotionBlurSampleCount, ValueType::INT, { "curved motion blur sample count" } },
    { key::primitiveAttributeFrame, ValueType::INT, {} },
};

const KeyList sExplicitShadingKeys = {
    { key::explicitShading, ValueType::BOOL, {} },
};

const KeyList sLocalMotionBlurKeys = {
    { key::localMotionBlurPositionList, ValueType::VEC3F, { "local motion blur position list" } },
    { key::localMotionBlurOrientList, ValueType::VEC4F, { "local motion blur orient list" } },
    { key::localMotionBlurScaleList, ValueType::VEC3F, { "local motion blur scale list" } },
    { key::localMotionBlurRadiusList, ValueType::FLOAT, { "local motion blur radius list" } },
    { key::localMotionBlurInnerRadiusList, ValueType::FLOAT, { "local motion blur inner radius list" } },
    { key::localMotionBlurMultiplierList, ValueType::FLOAT, { "local motion blur multiplier list" } },
    { key::localMotionBlurStrengthMult, ValueType::FLOAT, {} },
    { key::localMotionBlurRadiusMult, ValueType::FLOAT, {} },
};

// RdlMeshGeometry
const KeyList sMeshKeys = {
    { key::faceVertexCount, ValueType::INT, { "face vertex count" } },
    { key::verticesByIndex, ValueType::INT, { "vertices by index" } },
    { key::vertexList, ValueType::VEC3F, { "vertex list" } },
    { key::velocityList, ValueType::VEC3F, { "velocity list" } },
    { key::accelerationList, ValueType::VEC3F, { "acceleration list" } },
    { key::orientation, ValueType::INT, {} },
    { key::partList, ValueType::STRING, { "part list" } },
    { key::partFaceCountList, ValueType::INT, { "part face count list" } },
    { key::partFaceIndices, ValueType::INT, { "part face indices" } },
    { key::uvList, ValueType::VEC2F, { "uv list" } },
    { key::normalList, ValueType::VEC3F, { "normal list" } },
    { key::velocityScale, ValueType::FLOAT, { "velocity scale" } },
    { key::isSubd, ValueType::BOOL, { "is subd" } },
    { key::subdScheme, ValueType::INT, { "subd scheme" } },
    { key::subdBoundary, ValueType::INT, { "subd boundary" } },
    { key::subdFVarLinear, ValueType::INT, { "subd fvar linear" } },
    { key::subdCreaseIndices, ValueType::INT, { "subd crease indices" } },
    { key::subdCreaseSharpnesses, ValueType::FLOAT, { "subd crease sharpnesses" } },
    { key::subdCornerIndices, ValueType::INT, { "subd corner indices" } },
    { key::subdCornerSharpnesses, ValueType::FLOAT, { "subd corner sharpnesses" } },
    { key::meshResolution, ValueType::FLOAT, { "resolution factor", "subd resolution", "subd_resolution" } },
    { key::adaptiveError, ValueType::FLOAT, { "adaptive error" } },
    { key::smoothNormal, ValueType::BOOL, {} },
};

// RdlCurveGeometry
const KeyList sCurvesKeys = {
    { key::curvesVertexCount, ValueType::INT, {} },
    { key::vertexList, ValueType::VEC3F, { "vertex list" } },
    { key::velocityList, ValueType::VEC3F, { "velocity list" } },
    { key::accelerationList, ValueType::VEC3F, {} },
    { key::radiusList, ValueType::FLOAT, {} },
    { key::useScreenSpaceRadius, ValueType::BOOL, { "use screen space radius" } },
    { key::partList, ValueType::STRING, { "part list" } },
    { key::partIndices, ValueType::INT, { "part indices" } },
    { key::velocityScale, ValueType::FLOAT, {} },
    { key::uvList, ValueType::VEC2F, {} },
    { key::curveType, ValueType::INT, {} },
    { key::tessellationRate, ValueType::INT, {} },
    { key::curvesSubType, ValueType::INT, {} },
};

// RdlPointGeometry
const KeyList sPointsKeys = {
    { key::vertexList, ValueType::VEC3F, { "vertex list" } },
    { key::velocityList, ValueType::VEC3F, { "velocity list" } },
    { key::accelerationList, ValueType::VEC3F, {} },
    { key::radiusList, ValueType::FLOAT, { "radius list" } },
    { key::useScreenSpaceRadius, ValueType::BOOL, { "use screen space radius" } },
    { key::partList, ValueType::STRING, { "part list" } },
    { key::partIndices, ValueType::INT, { "part indices" } },
    { key::velocityScale, ValueType::FLOAT, { "velocity scale" } },
};

// The names and aliases of the Rdl*Geometry attributes RdlGeometry doesn't
// read, mapped to what to use instead
const std::unordered_map<std::string, std::string>&
getUnsupportedKeys()
{
    static const std::string sVertexList("use the \"vertex_list\" key, with the motion steps in its "
                                         "vec3f_values_0 and vec3f_values_1");
    static const std::string sVelocityList("use the \"velocity_list\" key, with the motion steps in its "
                                           "vec3f_values_0 and vec3f_values_1");
    static const std::string sPrimitiveAttributes("add primitive attribute UserData to \"data\" directly");
    static const std::unordered_map<std::string, std::string> sUnsupportedKeys = {
        { "vertex_list_0", sVertexList },
        { "vertex list 0", sVertexList },
        { "vertex_list_1", sVertexList },
        { "vertex list 1", sVertexList },
        { "vertex_list_mb", sVertexList },
        { "vertex list mb", sVertexList },
        { "velocity_list_0", sVelocityList },
        { "velocity list 0", sVelocityList },
        { "velocity_list_1", sVelocityList },
        { "velocity list 1", sVelocityList },
        { "velocity_list_B", sVelocityList },
        { "velocity list B", sVelocityList },
        { key::primitiveAttributes, sPrimitiveAttributes },
        { "primitive attributes", sPrimitiveAttributes },
    };
    return sUnsupportedKeys;
}

bool
isMotionStepKey(const std::string& name)
{
    return name == key::vertexList || name == key::velocityList;
}

// Maps every name and alias of a geo type's attributes to the attribute
using KeyTable = std::unordered_map<std::string, const KeyInfo*>;

KeyTable
makeKeyTable(std::initializer_list<const KeyList*> keyLists)
{
    KeyTable table;
    for (const KeyList* keyList : keyLists) {
        for (const KeyInfo& keyInfo : *keyList) {
            table.emplace(keyInfo.mName, &keyInfo);
            for (const std::string& alias : keyInfo.mAliases) {
                table.emplace(alias, &keyInfo);
            }
        }
    }
    return table;
}

const KeyTable&
getKeyTable(int geoType)
{
    static const KeyTable sMeshTable = makeKeyTable({&sMeshKeys, &sMotionBlurKeys,
        &sExplicitShadingKeys, &sLocalMotionBlurKeys});
    static const KeyTable sCurvesTable = makeKeyTable({&sCurvesKeys, &sMotionBlurKeys,
        &sExplicitShadingKeys, &sLocalMotionBlurKeys});
    static const KeyTable sPointsTable = makeKeyTable({&sPointsKeys, &sMotionBlurKeys,
        &sExplicitShadingKeys, &sLocalMotionBlurKeys});

    switch (geoType) {
    case GEO_TYPE_CURVES:
        return sCurvesTable;
    case GEO_TYPE_POINTS:
        return sPointsTable;
    case GEO_TYPE_MESH:
    default:
        return sMeshTable;
    }
}

// The values RdlGeometry reads from its UserData in place of attributes,
// looked up by canonical attribute name. A key of the wrong type is an error
// (see isValid()). Getters return an empty vector or the given default, like
// an unset attribute would, if a key is missing.
class GeometryData
{
public:
    GeometryData(const scene_rdl2::rdl2::Geometry* rdlGeometry, int geoType);

    // False if any key had values of the wrong type, in which case the
    // geometry must not be generated
    bool isValid() const { return mValid; }

    // Whether a key is attribute data rather than a primitive attribute
    bool isReserved(const std::string& key) const
    {
        return mReservedKeys.count(key) != 0;
    }

    const scene_rdl2::rdl2::IntVector& getInts(const std::string& name) const;
    const scene_rdl2::rdl2::FloatVector& getFloats(const std::string& name) const;
    const scene_rdl2::rdl2::StringVector& getStrings(const std::string& name) const;
    const scene_rdl2::rdl2::Vec2fVector& getVec2fs(const std::string& name) const;
    // motionStep selects vec3f_values_0 or vec3f_values_1
    const scene_rdl2::rdl2::Vec3fVector& getVec3fs(const std::string& name, int motionStep = 0) const;
    const scene_rdl2::rdl2::Vec4fVector& getVec4fs(const std::string& name) const;

    bool getBool(const std::string& name, bool defaultValue) const;
    int getInt(const std::string& name, int defaultValue) const;
    float getFloat(const std::string& name, float defaultValue) const;

private:
    void add(const UserData* userData, const std::string& userDataKey, ValueType type, bool hasSecondStep);
    const UserData* find(const std::string& name, ValueType type) const;

    const scene_rdl2::rdl2::Geometry* mRdlGeometry;
    const KeyTable& mKeyTable;
    // The UserData holding each attribute's values, by canonical name
    std::unordered_map<std::string, const UserData*> mEntries;
    std::unordered_set<std::string> mReservedKeys;
    bool mValid = true;
};

GeometryData::GeometryData(const scene_rdl2::rdl2::Geometry* rdlGeometry, int geoType)
    : mRdlGeometry(rdlGeometry)
    , mKeyTable(getKeyTable(geoType))
{
    for (const scene_rdl2::rdl2::SceneObject* sceneObject : rdlGeometry->get(attrData)) {
        const UserData* userData = sceneObject ? sceneObject->asA<UserData>() : nullptr;
        if (!userData) {
            continue;
        }
        if (userData->hasBoolData()) {
            add(userData, userData->getBoolKey(), ValueType::BOOL, false);
        }
        if (userData->hasIntData()) {
            add(userData, userData->getIntKey(), ValueType::INT, false);
        }
        if (userData->hasFloatData()) {
            add(userData, userData->getFloatKey(), ValueType::FLOAT, userData->hasFloatData1());
        }
        if (userData->hasStringData()) {
            add(userData, userData->getStringKey(), ValueType::STRING, false);
        }
        if (userData->hasColorData()) {
            add(userData, userData->getColorKey(), ValueType::COLOR, userData->hasColorData1());
        }
        if (userData->hasVec2fData()) {
            add(userData, userData->getVec2fKey(), ValueType::VEC2F, userData->hasVec2fData1());
        }
        if (userData->hasVec3fData()) {
            add(userData, userData->getVec3fKey(), ValueType::VEC3F, userData->hasVec3fData1());
        }
        if (userData->hasVec4fData()) {
            add(userData, userData->getVec4fKey(), ValueType::VEC4F, userData->hasVec4fData1());
        }
        if (userData->hasMat3fData()) {
            add(userData, userData->getMat3fKey(), ValueType::MAT3F, userData->hasMat3fData1());
        }
        if (userData->hasMat4fData()) {
            add(userData, userData->getMat4fKey(), ValueType::MAT4F, userData->hasMat4fData1());
        }
    }
}

void
GeometryData::add(const UserData* userData, const std::string& userDataKey, ValueType type, bool hasSecondStep)
{
    const auto unsupported = getUnsupportedKeys().find(userDataKey);
    if (unsupported != getUnsupportedKeys().end()) {
        mRdlGeometry->warn("UserData(\"", userData->getName(), "\") key \"", userDataKey,
            "\" is not supported: ", unsupported->second, ". Ignoring it.");
        mReservedKeys.insert(userDataKey);
        return;
    }

    const auto it = mKeyTable.find(userDataKey);
    if (it == mKeyTable.end()) {
        // Attributes common to all Geometry (other than RdlGeometry's own)
        // are read from the RdlGeometry itself by the renderer, so they can't
        // be supplied as data, and aren't primitive attributes either.
        const std::string& name = userDataKey;
        if (name != "geo_type" && name != "data") {
            try {
                mRdlGeometry->getSceneClass().getAttribute(name);
                mRdlGeometry->warn("UserData(\"", userData->getName(), "\") key \"", name,
                    "\" is a Geometry attribute, which must be set on the RdlGeometry itself. Ignoring it.");
                mReservedKeys.insert(name);
                return;
            } catch (const scene_rdl2::except::KeyError&) {
                // not a Geometry attribute
            }
        }
        // a primitive attribute, if it's a type primitive attributes support
        if (type == ValueType::VEC4F || type == ValueType::MAT3F) {
            mRdlGeometry->warn("UserData(\"", userData->getName(), "\") key \"", userDataKey, "\" has ",
                typeName(type), " values, which primitive attributes don't support. Ignoring it.");
        }
        return;
    }

    mReservedKeys.insert(userDataKey);
    const KeyInfo& keyInfo = *it->second;
    if (type != keyInfo.mType) {
        mRdlGeometry->error("UserData(\"", userData->getName(), "\") key \"", userDataKey, "\" has ",
            typeName(type), " values, but \"", keyInfo.mName, "\" requires ", typeName(keyInfo.mType), " values.");
        mValid = false;
        return;
    }
    if (hasSecondStep && !isMotionStepKey(keyInfo.mName)) {
        mRdlGeometry->warn("UserData(\"", userData->getName(), "\") key \"", userDataKey,
            "\" only uses its first set of values.");
    }
    const auto inserted = mEntries.emplace(keyInfo.mName, userData);
    if (!inserted.second) {
        mRdlGeometry->warn("UserData(\"", userData->getName(), "\") key \"", userDataKey,
            "\" duplicates \"", keyInfo.mName, "\" in UserData(\"", inserted.first->second->getName(),
            "\"). Ignoring it.");
    }
}

// The UserData holding an attribute's values. The type was checked by add(),
// so the type passed in only guards against reading a different typed key.
const UserData*
GeometryData::find(const std::string& name, ValueType type) const
{
    const auto it = mEntries.find(name);
    if (it == mEntries.end()) {
        return nullptr;
    }
    const auto keyInfo = mKeyTable.find(name);
    MNRY_ASSERT(keyInfo != mKeyTable.end() && keyInfo->second->mType == type);
    if (keyInfo == mKeyTable.end() || keyInfo->second->mType != type) {
        return nullptr;
    }
    return it->second;
}

const scene_rdl2::rdl2::IntVector&
GeometryData::getInts(const std::string& name) const
{
    static const scene_rdl2::rdl2::IntVector sEmpty;
    const UserData* userData = find(name, ValueType::INT);
    return userData ? userData->getIntValues() : sEmpty;
}

const scene_rdl2::rdl2::FloatVector&
GeometryData::getFloats(const std::string& name) const
{
    static const scene_rdl2::rdl2::FloatVector sEmpty;
    const UserData* userData = find(name, ValueType::FLOAT);
    return userData ? userData->getFloatValues() : sEmpty;
}

const scene_rdl2::rdl2::StringVector&
GeometryData::getStrings(const std::string& name) const
{
    static const scene_rdl2::rdl2::StringVector sEmpty;
    const UserData* userData = find(name, ValueType::STRING);
    return userData ? userData->getStringValues() : sEmpty;
}

const scene_rdl2::rdl2::Vec2fVector&
GeometryData::getVec2fs(const std::string& name) const
{
    static const scene_rdl2::rdl2::Vec2fVector sEmpty;
    const UserData* userData = find(name, ValueType::VEC2F);
    return userData ? userData->getVec2fValues() : sEmpty;
}

const scene_rdl2::rdl2::Vec3fVector&
GeometryData::getVec3fs(const std::string& name, int motionStep) const
{
    static const scene_rdl2::rdl2::Vec3fVector sEmpty;
    const UserData* userData = find(name, ValueType::VEC3F);
    if (!userData) {
        return sEmpty;
    }
    return motionStep == 0 ? userData->getVec3fValues0() : userData->getVec3fValues1();
}

const scene_rdl2::rdl2::Vec4fVector&
GeometryData::getVec4fs(const std::string& name) const
{
    static const scene_rdl2::rdl2::Vec4fVector sEmpty;
    const UserData* userData = find(name, ValueType::VEC4F);
    return userData ? userData->getVec4fValues() : sEmpty;
}

bool
GeometryData::getBool(const std::string& name, bool defaultValue) const
{
    const UserData* userData = find(name, ValueType::BOOL);
    if (!userData || userData->getBoolValues().empty()) {
        return defaultValue;
    }
    return userData->getBoolValues().front();
}

int
GeometryData::getInt(const std::string& name, int defaultValue) const
{
    const UserData* userData = find(name, ValueType::INT);
    if (!userData || userData->getIntValues().empty()) {
        return defaultValue;
    }
    return userData->getIntValues().front();
}

float
GeometryData::getFloat(const std::string& name, float defaultValue) const
{
    const UserData* userData = find(name, ValueType::FLOAT);
    if (!userData || userData->getFloatValues().empty()) {
        return defaultValue;
    }
    return userData->getFloatValues().front();
}

const scene_rdl2::rdl2::String sDefaultPartName("");

} // anonymous namespace

namespace moonray {
namespace geom {

namespace {

// The number of position, velocity and acceleration samples used for motion blur
struct MotionSamples
{
    scene_rdl2::rdl2::MotionBlurType mType;
    int mNumPos = 1;
    int mNumVel = 0;
    int mNumAcc = 0;
};

// Picks the samples for the requested motion blur type, falling back on the
// static case if we don't have sufficient data for it
MotionSamples
pickMotionSamples(const scene_rdl2::rdl2::Geometry* rdlGeometry, const GeometryData& data, size_t vertCount)
{
    const bool pos1Valid = sizeCheck(rdlGeometry, key::vertexList + " vec3f_values_1",
                                     data.getVec3fs(key::vertexList, 1).size(), vertCount);
    const bool vel0Valid = sizeCheck(rdlGeometry, key::velocityList + " vec3f_values_0",
                                     data.getVec3fs(key::velocityList, 0).size(), vertCount);
    const bool vel1Valid = sizeCheck(rdlGeometry, key::velocityList + " vec3f_values_1",
                                     data.getVec3fs(key::velocityList, 1).size(), vertCount);
    const bool acc0Valid = sizeCheck(rdlGeometry, key::accelerationList, data.getVec3fs(key::accelerationList).size(), vertCount);

    MotionSamples samples;
    samples.mType = static_cast<scene_rdl2::rdl2::MotionBlurType>(
        data.getInt(key::motionBlurType, static_cast<int>(scene_rdl2::rdl2::MotionBlurType::BEST)));

    // Set motion blur type to static if motion blur is disabled for the scene
    const scene_rdl2::rdl2::SceneVariables &sv = rdlGeometry->getSceneClass().getSceneContext()->getSceneVariables();
    if (!sv.get(scene_rdl2::rdl2::SceneVariables::sEnableMotionBlur)) {
        samples.mType = scene_rdl2::rdl2::MotionBlurType::STATIC;
    }

    bool err = false;
    switch (samples.mType) {
    case scene_rdl2::rdl2::MotionBlurType::STATIC:
        break;
    case scene_rdl2::rdl2::MotionBlurType::VELOCITY:
        if (vel0Valid) {
            samples.mNumVel = 1;
        } else {
            err = true;
        }
        break;
    case scene_rdl2::rdl2::MotionBlurType::FRAME_DELTA:
        if (pos1Valid) {
            samples.mNumPos = 2;
        } else {
            err = true;
        }
        break;
    case scene_rdl2::rdl2::MotionBlurType::ACCELERATION:
        if (vel0Valid && acc0Valid) {
            samples.mNumVel = 1;
            samples.mNumAcc = 1;
        } else {
            err = true;
        }
        break;
    case scene_rdl2::rdl2::MotionBlurType::HERMITE:
        if (pos1Valid && vel0Valid && vel1Valid) {
            samples.mNumPos = 2;
            samples.mNumVel = 2;
        } else {
            err = true;
        }
        break;
    case scene_rdl2::rdl2::MotionBlurType::BEST:
        if (pos1Valid && vel0Valid && vel1Valid) {
            // use Hermite mb type
            samples.mNumPos = 2;
            samples.mNumVel = 2;
        } else if (vel0Valid && acc0Valid) {
            // use acceleration mb type
            samples.mNumVel = 1;
            samples.mNumAcc = 1;
        } else if (pos1Valid) {
            // use frame delta mb type
            samples.mNumPos = 2;
        } else if (vel0Valid) {
            // use velocity mb type
            samples.mNumVel = 1;
        }
        // else just keep static mb type
        break;
    default:
        err = true;
        break;
    }

    if (err) {
        rdlGeometry->warn("Insufficient data for requested motion blur type. "
                          "Falling back to static case.");
    }
    return samples;
}

void
addVelocityAndAcceleration(const GeometryData& data,
                           const MotionSamples& samples,
                           shading::PrimitiveAttributeTable& primitiveAttributeTable)
{
    if (samples.mNumVel > 0) {
        const float velocityScale = data.getFloat(key::velocityScale, 1.0f);
        std::vector<std::vector<Vec3f>> velocities;
        for (int motionStep = 0; motionStep < samples.mNumVel; ++motionStep) {
            const scene_rdl2::rdl2::Vec3fVector& procVelList = data.getVec3fs(key::velocityList, motionStep);
            velocities.emplace_back(procVelList.begin(), procVelList.end());
            for (Vec3f& velocity : velocities.back()) {
                velocity *= velocityScale;
            }
        }
        primitiveAttributeTable.addAttribute(shading::StandardAttributes::sVelocity,
                                             shading::RATE_VERTEX, std::move(velocities));
    }

    if (samples.mNumAcc > 0) {
        const scene_rdl2::rdl2::Vec3fVector& procAccList0 = data.getVec3fs(key::accelerationList);
        std::vector<Vec3f> accelerations(procAccList0.begin(), procAccList0.end());
        primitiveAttributeTable.addAttribute(shading::StandardAttributes::sAcceleration,
                                             shading::RATE_VERTEX, std::move(accelerations));
    }
}

void
addPrimitiveAttributes(const scene_rdl2::rdl2::Geometry* rdlGeometry,
                       const GeometryData& data,
                       const RateCounts& rates,
                       shading::PrimitiveAttributeTable& primitiveAttributeTable)
{
    const scene_rdl2::rdl2::PrimitiveAttributeFrame primitiveAttributeFrame =
        static_cast<scene_rdl2::rdl2::PrimitiveAttributeFrame>(data.getInt(key::primitiveAttributeFrame,
            static_cast<int>(scene_rdl2::rdl2::PrimitiveAttributeFrame::BOTH_MOTION_STEPS)));

    const bool useFirstFrame = (primitiveAttributeFrame != scene_rdl2::rdl2::PrimitiveAttributeFrame::SECOND_MOTION_STEP);
    const bool useSecondFrame = (primitiveAttributeFrame != scene_rdl2::rdl2::PrimitiveAttributeFrame::FIRST_MOTION_STEP);

    processArbitraryData(rdlGeometry,
                         rdlGeometry->get(attrData),
                         primitiveAttributeTable,
                         rates,
                         useFirstFrame,
                         useSecondFrame,
                         [&data](const std::string& key) { return data.isReserved(key); });
}

// Same as RdlMeshGeometry's per face assignment, but ignores invalid part data
// rather than reading out of bounds
LayerAssignmentId
createPerFaceAssignmentId(const scene_rdl2::rdl2::Geometry* rdlGeometry,
                          const GeometryData& data,
                          const scene_rdl2::rdl2::Layer* rdlLayer,
                          const size_t faceCount,
                          bool partsValid)
{
    const int meshAssignmentId = rdlLayer->getAssignmentId(rdlGeometry, sDefaultPartName);

    const scene_rdl2::rdl2::StringVector& partList = data.getStrings(key::partList);
    if (partList.empty() || !partsValid) {
        return LayerAssignmentId(meshAssignmentId);
    }

    const scene_rdl2::rdl2::IntVector& partFaceCountList = data.getInts(key::partFaceCountList);
    if (partList.size() != partFaceCountList.size()) {
        rdlGeometry->warn("part list is incorrect size for partface count list, skipping");
        return LayerAssignmentId(meshAssignmentId);
    }

    const scene_rdl2::rdl2::IntVector& partFaceIndices = data.getInts(key::partFaceIndices);
    std::vector<int> faceAssignmentIds(faceCount, meshAssignmentId);
    size_t begin = 0;
    for (size_t i = 0; i < partList.size(); ++i) {
        const int partAssignmentId = rdlLayer->getAssignmentId(rdlGeometry, partList[i]);
        for (size_t pF = begin; pF < begin + partFaceCountList[i]; ++pF) {
            faceAssignmentIds[partFaceIndices[pF]] = partAssignmentId;
        }
        begin += partFaceCountList[i];
    }
    return LayerAssignmentId(std::move(faceAssignmentIds));
}

// Same as RdlPointGeometry/RdlCurveGeometry's per point/curve assignment
LayerAssignmentId
createPerElementAssignmentId(const scene_rdl2::rdl2::Geometry* rdlGeometry,
                             const GeometryData& data,
                             const scene_rdl2::rdl2::Layer* rdlLayer,
                             const size_t elementCount)
{
    const scene_rdl2::rdl2::StringVector& partList = data.getStrings(key::partList);
    const scene_rdl2::rdl2::IntVector& partIndices = data.getInts(key::partIndices);

    const int defaultAssignmentId = rdlLayer->getAssignmentId(rdlGeometry, sDefaultPartName);
    if (partList.empty()) {
        return LayerAssignmentId(defaultAssignmentId);
    }

    if (partIndices.size() && elementCount != partIndices.size()) {
        rdlGeometry->warn("part_indices size does not match the number of elements");
    }

    bool warn = false;
    std::vector<int> assignmentIds(elementCount, defaultAssignmentId);
    for (size_t i = 0; i < partIndices.size() && i < elementCount; ++i) {
        const int partIndex = partIndices[i];
        if (partIndex >= 0 && size_t(partIndex) < partList.size()) {
            assignmentIds[i] = rdlLayer->getAssignmentId(rdlGeometry, partList[partIndex]);
        } else if (!warn) {
            rdlGeometry->warn("part_indices contains values outside of part_list length");
            warn = true;
        }
    }
    return LayerAssignmentId(std::move(assignmentIds));
}

// Same as RdlMeshGeometry's getVertexData()
VertexBuffer<Vec3fa, InterleavedTraits>
getMeshVertexData(const scene_rdl2::rdl2::Geometry* rdlGeometry,
                  const GeometryData& data,
                  shading::PrimitiveAttributeTable& primitiveAttributeTable,
                  const RateCounts& rates,
                  MotionSamples& samples)
{
    const scene_rdl2::rdl2::Vec3fVector& procPosList0 = data.getVec3fs(key::vertexList, 0);
    const scene_rdl2::rdl2::Vec3fVector& procPosList1 = data.getVec3fs(key::vertexList, 1);
    const size_t vertCount = procPosList0.size();

    samples = pickMotionSamples(rdlGeometry, data, vertCount);

    // Like RdlMeshGeometry, a static mesh uses the second motion step if there is one
    const bool pos1Valid = procPosList1.size() == vertCount;
    const scene_rdl2::rdl2::Vec3fVector& staticPosList =
        (pos1Valid && samples.mType == scene_rdl2::rdl2::MotionBlurType::STATIC) ? procPosList1 : procPosList0;

    VertexBuffer<Vec3fa, InterleavedTraits> vertices(vertCount, samples.mNumPos);
    for (size_t i = 0; i < vertCount; i++) {
        const auto& p = staticPosList[i];
        vertices(i, 0) = Vec3fa(p[0], p[1], p[2], 0.f);
    }
    if (samples.mNumPos == 2) {
        for (size_t i = 0; i < vertCount; i++) {
            const auto& p = procPosList1[i];
            vertices(i, 1) = Vec3fa(p[0], p[1], p[2], 0.f);
        }
    }

    addVelocityAndAcceleration(data, samples, primitiveAttributeTable);

    const scene_rdl2::rdl2::Vec2fVector& procUVList = data.getVec2fs(key::uvList);
    if (!procUVList.empty()) {
        std::vector<Vec2f> textureUV(procUVList.begin(), procUVList.end());
        primitiveAttributeTable.addAttribute(shading::StandardAttributes::sSt,
                                             pickRate(rdlGeometry, key::uvList, procUVList.size(), rates),
                                             std::move(textureUV));
    }

    const scene_rdl2::rdl2::Vec3fVector& procNormalList = data.getVec3fs(key::normalList);
    if (!procNormalList.empty()) {
        std::vector<Vec3f> normals(procNormalList.begin(), procNormalList.end());
        primitiveAttributeTable.addAttribute(shading::StandardAttributes::sNormal,
                                             pickRate(rdlGeometry, key::normalList, procNormalList.size(), rates),
                                             std::move(normals));
    }

    return vertices;
}

// Builds the same SubdivisionMesh or PolygonMesh as RdlMeshGeometry
std::unique_ptr<Primitive>
buildMesh(const scene_rdl2::rdl2::Geometry* rdlGeometry,
          const GeometryData& data,
          const scene_rdl2::rdl2::Layer* rdlLayer,
          const local_motion_blur::LocalMotionBlur* localMotionBlur)
{
    static const std::string sPrimitiveName("generated_mesh");

    const bool isSubd = data.getBool(key::isSubd, true);

    shading::PrimitiveAttributeTable primitiveAttributeTable;

    const scene_rdl2::rdl2::IntVector& procFaceVertexCount = data.getInts(key::faceVertexCount);
    PolygonMesh::FaceVertexCount faceVertexCount(procFaceVertexCount.begin(), procFaceVertexCount.end());

    const scene_rdl2::rdl2::IntVector& procIndices = data.getInts(key::verticesByIndex);
    PolygonMesh::IndexBuffer indices(procIndices.begin(), procIndices.end());
    if (indices.empty()) {
        return nullptr;
    }

    const size_t vertCount = data.getVec3fs(key::vertexList, 0).size();
    const size_t faceCount = faceVertexCount.size();
    const size_t faceVaryingCount = procIndices.size();

    // Fill in table of faces->parts
    const scene_rdl2::rdl2::IntVector& partFaceCountList = data.getInts(key::partFaceCountList);
    const scene_rdl2::rdl2::IntVector& partFaceIndices = data.getInts(key::partFaceIndices);
    const size_t partCount = partFaceCountList.size();
    bool partsValid =
        std::accumulate(partFaceCountList.begin(), partFaceCountList.end(), 0ll) == (long long)partFaceIndices.size();
    for (int partFaceCount : partFaceCountList) {
        partsValid &= partFaceCount >= 0;
    }
    for (int faceIndex : partFaceIndices) {
        partsValid &= faceIndex >= 0 && size_t(faceIndex) < faceCount;
    }
    if (!partsValid) {
        rdlGeometry->warn("total part face count does not match part face indices count, "
                          "or a part face index is out of range, skipping");
    }
    PolygonMesh::FaceToPartBuffer faceToPart(faceCount, 0);
    if (partsValid) {
        for (size_t p = 0, j = 0; p < partCount; p++) {
            for (int i = 0; i < partFaceCountList[p]; i++, j++) {
                faceToPart[partFaceIndices[j]] = p;
            }
        }
    }
    if (faceVertexCount.empty()) {
        return nullptr;
    }
    const RateCounts rates{partCount, faceCount, vertCount, vertCount, faceVaryingCount};

    // Get the vertices, velocities, uvs, normals etc.
    MotionSamples samples;
    PolygonMesh::VertexBuffer vertices =
        getMeshVertexData(rdlGeometry, data, primitiveAttributeTable, rates, samples);
    if (vertices.empty()) {
        return nullptr;
    }

    // Apply local motion blur to the vertex buffer before building the primitive.
    if (localMotionBlur) {
        const shading::XformSamples parent2Root = {scene_rdl2::math::Xform3f(scene_rdl2::math::one)};
        localMotionBlur->apply(samples.mNumPos, samples.mNumVel, samples.mNumAcc,
                               parent2Root, vertices, primitiveAttributeTable);
    }

    LayerAssignmentId layerAssignmentId =
        createPerFaceAssignmentId(rdlGeometry, data, rdlLayer, faceCount, partsValid);

    const bool singleSided = rdlGeometry->getSideType() == scene_rdl2::rdl2::Geometry::SINGLE_SIDED;
    const float meshResolution = data.getFloat(key::meshResolution, 2.0f);
    // adaptive error is only used when adaptive tessellation is enabled
    float adaptiveError = data.getFloat(key::adaptiveError, 0.0f);
    // TODO rotation motion blur involves instancing logic that would
    // break adaptive tessellation right now.
    if (isSubd && data.getBool(key::useRotationMotionBlur, false)) {
        adaptiveError = 0.0f;
    }

    addPrimitiveAttributes(rdlGeometry, data, rates, primitiveAttributeTable);

    // Add explicit shading primitive attribute if explicit shading is enabled
    if (data.getBool(key::explicitShading, false) &&
        !addExplicitShading(rdlGeometry, primitiveAttributeTable)) {
        return nullptr;
    }

    if (!isSubd) {
        removeUnassignedFaces(rdlLayer,
                              layerAssignmentId,
                              faceToPart,
                              faceVertexCount,
                              indices,
                              &primitiveAttributeTable);

        // the mesh doesn't have any assigned materials, skip generating the primitive
        if (faceVertexCount.empty() || indices.empty()) {
            return nullptr;
        }
    }

    constexpr int ORIENTATION_LEFT_HANDED = 1;
    if (data.getInt(key::orientation, 0) == ORIENTATION_LEFT_HANDED) {
        // Reverse the vertex ordering
        size_t indexOffset = 0;
        for (size_t i = 0; i < faceVertexCount.size(); i++) {
            std::reverse(indices.begin() + indexOffset, indices.begin() + indexOffset + faceVertexCount[i]);
            indexOffset += faceVertexCount[i];
        }
        primitiveAttributeTable.reverseFaceVaryingAttributes(faceVertexCount);
    }

    const int curvedMotionBlurSampleCount = data.getInt(key::curvedMotionBlurSampleCount, 10);

    if (!isSubd) {
        std::unique_ptr<PolygonMesh> primitive =
            createPolygonMesh(std::move(faceVertexCount),
                              std::move(indices),
                              std::move(vertices),
                              std::move(layerAssignmentId),
                              std::move(primitiveAttributeTable));
        primitive->setMeshResolution(meshResolution);
        primitive->setAdaptiveError(adaptiveError);
        primitive->setName(sPrimitiveName);
        primitive->setIsSingleSided(singleSided);
        primitive->setIsNormalReversed(rdlGeometry->getReverseNormals());
        primitive->setParts(partCount, std::move(faceToPart));
        primitive->setSmoothNormal(data.getBool(key::smoothNormal, true));
        primitive->setCurvedMotionBlurSampleCount(curvedMotionBlurSampleCount);
        return primitive;
    }

    SubdivisionMesh::Scheme scheme = SubdivisionMesh::Scheme::CATMULL_CLARK;
    const int procScheme = data.getInt(key::subdScheme, 1);
    if (SubdivisionMesh::isValidScheme(procScheme)) {
        // Values in the Rdl attribute align with those of the enum, so cast when valid:
        scheme = static_cast<SubdivisionMesh::Scheme>(procScheme);
    }

    std::unique_ptr<SubdivisionMesh> primitive =
        createSubdivisionMesh(scheme,
                              std::move(faceVertexCount),
                              std::move(indices),
                              std::move(vertices),
                              std::move(layerAssignmentId),
                              std::move(primitiveAttributeTable));

    SubdivisionMesh::BoundaryInterpolation subdBoundary =
        SubdivisionMesh::BoundaryInterpolation::EDGE_AND_CORNER;
    const int procBoundary = data.getInt(key::subdBoundary, 2);
    if (SubdivisionMesh::isValidBoundaryInterpolation(procBoundary)) {
        subdBoundary = static_cast<SubdivisionMesh::BoundaryInterpolation>(procBoundary);
    } else {
        rdlGeometry->warn("Unknown subd boundary value, defaulting to 'edge and corner'.");
    }
    SubdivisionMesh::FVarLinearInterpolation subdFVarLinear =
        SubdivisionMesh::FVarLinearInterpolation::CORNERS_ONLY;
    const int procFVarLinear = data.getInt(key::subdFVarLinear, 1);
    if (SubdivisionMesh::isValidFVarLinearInterpolation(procFVarLinear)) {
        subdFVarLinear = static_cast<SubdivisionMesh::FVarLinearInterpolation>(procFVarLinear);
    } else {
        rdlGeometry->warn("Unknown subd fvar linear value, defaulting to 'corners only'.");
    }
    primitive->setSubdBoundaryInterpolation(subdBoundary);
    primitive->setSubdFVarLinearInterpolation(subdFVarLinear);

    // set optional subdivision creases and corners
    const scene_rdl2::rdl2::IntVector& procCreaseIndices = data.getInts(key::subdCreaseIndices);
    const scene_rdl2::rdl2::FloatVector& procCreaseSharpnesses = data.getFloats(key::subdCreaseSharpnesses);
    if (!procCreaseIndices.empty() && !procCreaseSharpnesses.empty()) {
        primitive->setSubdCreases(
            SubdivisionMesh::IndexBuffer(procCreaseIndices.begin(), procCreaseIndices.end()),
            SubdivisionMesh::SharpnessBuffer(procCreaseSharpnesses.begin(), procCreaseSharpnesses.end()));
    }
    const scene_rdl2::rdl2::IntVector& procCornerIndices = data.getInts(key::subdCornerIndices);
    const scene_rdl2::rdl2::FloatVector& procCornerSharpnesses = data.getFloats(key::subdCornerSharpnesses);
    if (!procCornerIndices.empty() && !procCornerSharpnesses.empty()) {
        primitive->setSubdCorners(
            SubdivisionMesh::IndexBuffer(procCornerIndices.begin(), procCornerIndices.end()),
            SubdivisionMesh::SharpnessBuffer(procCornerSharpnesses.begin(), procCornerSharpnesses.end()));
    }

    primitive->setMeshResolution(meshResolution);
    primitive->setAdaptiveError(adaptiveError);
    primitive->setName(sPrimitiveName);
    primitive->setIsSingleSided(singleSided);
    primitive->setIsNormalReversed(rdlGeometry->getReverseNormals());
    primitive->setModifiability(Primitive::Modifiability::DEFORMABLE);
    primitive->setParts(partCount, std::move(faceToPart));
    primitive->setCurvedMotionBlurSampleCount(curvedMotionBlurSampleCount);
    return primitive;
}

// Builds the same Curves as RdlCurveGeometry
std::unique_ptr<Primitive>
buildCurves(const scene_rdl2::rdl2::Geometry* rdlGeometry,
            const GeometryData& data,
            const scene_rdl2::rdl2::Layer* rdlLayer,
            const local_motion_blur::LocalMotionBlur* localMotionBlur)
{
    Curves::Type type = Curves::Type::UNKNOWN;
    switch (data.getInt(key::curveType, 1)) {
    case 0:
        type = Curves::Type::LINEAR;
        break;
    case 1:
        type = Curves::Type::BEZIER;
        break;
    case 2:
        type = Curves::Type::BSPLINE;
        break;
    default:
        rdlGeometry->warn("Unknown curve type, defaulting to Bezier.");
        type = Curves::Type::BEZIER;
    }

    Curves::SubType subtype = Curves::SubType::UNKNOWN;
    switch (data.getInt(key::curvesSubType, 0)) {
    case 0:
        subtype = Curves::SubType::RAY_FACING;
        break;
    case 1:
        subtype = Curves::SubType::ROUND;
        break;
    case 2:
        subtype = Curves::SubType::NORMAL_ORIENTED;
        break;
    default:
        rdlGeometry->warn("Unknown curve subtype, defaulting to ray facing.");
        subtype = Curves::SubType::RAY_FACING;
    }

    const int tessellationRate = data.getInt(key::tessellationRate, 4);

    shading::PrimitiveAttributeTable primitiveAttributeTable;

    const scene_rdl2::rdl2::Vec3fVector& procPosList0 = data.getVec3fs(key::vertexList, 0);
    const scene_rdl2::rdl2::Vec3fVector& procPosList1 = data.getVec3fs(key::vertexList, 1);
    const size_t vertCount = procPosList0.size();

    const MotionSamples samples = pickMotionSamples(rdlGeometry, data, vertCount);

    // number of vertices per curve
    const scene_rdl2::rdl2::IntVector& curvesVertexCount = data.getInts(key::curvesVertexCount);
    Curves::CurvesVertexCount vertexCounts(curvesVertexCount.begin(), curvesVertexCount.end());

    // Figure out number of segments for varying. These are only correct if each
    // curve has a correct number of vertices and the total number of vertices
    // matches the sum of the vertex counts. However if that is false
    // Curves::checkPrimitiveData() will produce an error.
    size_t varyingCount;
    switch (type) {
    case Curves::Type::LINEAR:
        varyingCount = vertCount;
        break;
    case Curves::Type::BEZIER:
        varyingCount = (vertCount - vertexCounts.size()) / 3 + vertexCounts.size();
        break;
    case Curves::Type::BSPLINE:
    default:
        varyingCount = vertCount - 2 * vertexCounts.size();
        break;
    }

    // Handle radius interpolations
    const scene_rdl2::rdl2::StringVector& partList = data.getStrings(key::partList);
    const scene_rdl2::rdl2::IntVector& partIndices = data.getInts(key::partIndices);
    const RateCounts rates{partList.size(), vertexCounts.size(), varyingCount, vertCount, 0};
    const scene_rdl2::rdl2::FloatVector& rv = data.getFloats(key::radiusList);
    std::vector<float> radius;
    AttributeRate radiusRate = pickRate(rdlGeometry, key::radiusList, rv.size(), rates);
    if (radiusRate == AttributeRate::RATE_PART && partIndices.size() < vertexCounts.size()) {
        rdlGeometry->warn("part_indices has fewer values than there are curves, ignoring radius_list");
        radiusRate = AttributeRate::RATE_UNKNOWN;
    }
    switch (radiusRate) {
    case AttributeRate::RATE_UNKNOWN:
    default:
        radius.assign(vertCount, 0.5f);
        break;
    case AttributeRate::RATE_CONSTANT:
        radius.assign(vertCount, rv[0]);
        break;
    case AttributeRate::RATE_UNIFORM: // per-curve
        radius.reserve(vertCount);
        for (size_t i = 0; i < vertexCounts.size(); ++i)
            for (size_t n = vertexCounts[i]; n--;)
                radius.emplace_back(rv[i]);
        break;
    case AttributeRate::RATE_PART:
        radius.reserve(vertCount);
        for (size_t i = 0; i < vertexCounts.size(); ++i)
            for (size_t n = vertexCounts[i]; n--;)
                radius.emplace_back(rv[partIndices[i]]);
        break;
    case AttributeRate::RATE_VARYING: // per-segment
        switch (type) {
        case Curves::Type::LINEAR:
            radius.assign(rv.begin(), rv.begin() + vertCount);
            break;
        case Curves::Type::BEZIER: {
            radius.reserve(vertCount);
            size_t i = 0;
            for (size_t count : vertexCounts) {
                radius.emplace_back(rv[i]);
                for (size_t j = 0; j < count; j += 3) {
                    radius.emplace_back((rv[i]*2 + rv[i+1])/3);
                    radius.emplace_back((rv[i] + rv[i+1]*2)/3);
                    radius.emplace_back(rv[i+1]);
                    i++;
                }
            }
            break;}
        case Curves::Type::BSPLINE: {
            radius.reserve(vertCount);
            size_t i = 0;
            for (size_t count : vertexCounts) {
                radius.emplace_back(2*rv[i] - rv[i+1]);
                for (size_t j = 0; j < count-2; j++)
                    radius.emplace_back(rv[i++]);
                radius.emplace_back(2*rv[i-1] - rv[i-2]);
            }
            break;}
        default:
            break;
        }
        break;
    case AttributeRate::RATE_VERTEX:
        radius.assign(rv.begin(), rv.begin() + vertCount);
        break;
    }
    // Radius values are read for every vertex below, so guard against curve
    // counts that don't add up to the number of vertices
    radius.resize(vertCount, 0.5f);

    // Copy vertices, radius values are stored in the aligned channel
    Curves::VertexBuffer vertices(vertCount, samples.mNumPos);
    for (size_t i = 0; i < vertCount; i++) {
        const auto& p = procPosList0[i];
        vertices(i, 0) = Vec3fa(p[0], p[1], p[2], radius[i]);
    }
    if (samples.mNumPos == 2) {
        for (size_t i = 0; i < vertCount; i++) {
            const auto& p = procPosList1[i];
            vertices(i, 1) = Vec3fa(p[0], p[1], p[2], radius[i]);
        }
    }

    addVelocityAndAcceleration(data, samples, primitiveAttributeTable);

    // Apply local motion blur to vertex buffer before building the primitive
    if (localMotionBlur) {
        const shading::XformSamples parent2Root = {scene_rdl2::math::Xform3f(scene_rdl2::math::one)};
        localMotionBlur->apply(samples.mNumPos, samples.mNumVel, samples.mNumAcc,
                               parent2Root, vertices, primitiveAttributeTable);
    }

    LayerAssignmentId layerAssignmentId =
        createPerElementAssignmentId(rdlGeometry, data, rdlLayer, vertexCounts.size());

    addPrimitiveAttributes(rdlGeometry, data, rates, primitiveAttributeTable);

    // try to add UVs if we haven't already
    if (!primitiveAttributeTable.hasAttribute(shading::StandardAttributes::sSt) &&
        !primitiveAttributeTable.hasAttribute(shading::StandardAttributes::sUv)) {
        const scene_rdl2::rdl2::Vec2fVector& stList = data.getVec2fs(key::uvList);
        if (!stList.empty()) {
            primitiveAttributeTable.addAttribute(shading::StandardAttributes::sUv,
                                                 pickRate(rdlGeometry, key::uvList, stList.size(), rates),
                                                 std::vector<Vec2f>(stList.begin(), stList.end()));
        }
    }

    // Add explicit shading primitive attribute if explicit shading is enabled
    if (data.getBool(key::explicitShading, false) &&
        !addExplicitShading(rdlGeometry, primitiveAttributeTable)) {
        return nullptr;
    }

    if (data.getBool(key::useScreenSpaceRadius, false)) {
        applyScreenSpaceRadius(rdlGeometry, vertices, vertexCounts, primitiveAttributeTable);
    }

    // Check the validity of the curves data and print out any error messages
    std::string errorMessage;
    const Primitive::DataValidness dataValid = Curves::checkPrimitiveData(type,
                                                                          subtype,
                                                                          tessellationRate,
                                                                          vertexCounts,
                                                                          vertices,
                                                                          primitiveAttributeTable,
                                                                          &errorMessage);
    if (dataValid != Primitive::DataValidness::VALID) {
        rdlGeometry->error(errorMessage);
        return nullptr;
    }

    std::unique_ptr<Curves> primitive = createCurves(type,
                                                     subtype,
                                                     tessellationRate,
                                                     std::move(vertexCounts),
                                                     std::move(vertices),
                                                     std::move(layerAssignmentId),
                                                     std::move(primitiveAttributeTable));
    if (primitive) {
        primitive->setCurvedMotionBlurSampleCount(data.getInt(key::curvedMotionBlurSampleCount, 10));
    }
    return primitive;
}

// Builds the same Points as RdlPointGeometry
std::unique_ptr<Primitive>
buildPoints(const scene_rdl2::rdl2::Geometry* rdlGeometry,
            const GeometryData& data,
            const scene_rdl2::rdl2::Layer* rdlLayer,
            const local_motion_blur::LocalMotionBlur* localMotionBlur)
{
    shading::PrimitiveAttributeTable primitiveAttributeTable;

    const scene_rdl2::rdl2::Vec3fVector& procPosList0 = data.getVec3fs(key::vertexList, 0);
    const scene_rdl2::rdl2::Vec3fVector& procPosList1 = data.getVec3fs(key::vertexList, 1);
    const size_t vertCount = procPosList0.size();

    const MotionSamples samples = pickMotionSamples(rdlGeometry, data, vertCount);

    Points::VertexBuffer vertices(vertCount, samples.mNumPos);
    for (size_t i = 0; i < vertCount; i++) {
        const auto& p = procPosList0[i];
        vertices(i, 0) = Vec3f(p[0], p[1], p[2]);
    }
    if (samples.mNumPos == 2) {
        for (size_t i = 0; i < vertCount; i++) {
            const auto& p = procPosList1[i];
            vertices(i, 1) = Vec3f(p[0], p[1], p[2]);
        }
    }

    addVelocityAndAcceleration(data, samples, primitiveAttributeTable);

    // Apply local motion blur to vertex buffer before building the primitive
    if (localMotionBlur) {
        const shading::XformSamples parent2Root = {scene_rdl2::math::Xform3f(scene_rdl2::math::one)};
        localMotionBlur->apply(samples.mNumPos, samples.mNumVel, samples.mNumAcc,
                               parent2Root, vertices, primitiveAttributeTable);
    }

    // radius buffer
    const scene_rdl2::rdl2::StringVector& partList = data.getStrings(key::partList);
    const scene_rdl2::rdl2::IntVector& partIndices = data.getInts(key::partIndices);
    const scene_rdl2::rdl2::FloatVector& rv = data.getFloats(key::radiusList);
    const RateCounts rates{partList.size(), 0, 0, vertCount, 0};
    AttributeRate radiusRate = pickRate(rdlGeometry, key::radiusList, rv.size(), rates);
    if (radiusRate == AttributeRate::RATE_PART && partIndices.size() < vertCount) {
        rdlGeometry->warn("part_indices has fewer values than there are points, ignoring radius_list");
        radiusRate = AttributeRate::RATE_UNKNOWN;
    }
    Points::RadiusBuffer radius(vertCount);
    switch (radiusRate) {
    case AttributeRate::RATE_UNKNOWN:
        radius.assign(vertCount, 0.5f);
        break;
    case AttributeRate::RATE_CONSTANT:
        radius.assign(vertCount, rv[0]);
        break;
    case AttributeRate::RATE_PART:
        radius.clear();
        radius.reserve(vertCount);
        for (size_t i = 0; i < vertCount; ++i)
            radius.emplace_back(rv[partIndices[i]]);
        break;
    default:
        radius.assign(rv.begin(), rv.begin() + vertCount);
        break;
    }

    LayerAssignmentId layerAssignmentId = createPerElementAssignmentId(rdlGeometry, data, rdlLayer, vertCount);

    addPrimitiveAttributes(rdlGeometry, data, rates, primitiveAttributeTable);

    // Add explicit shading primitive attribute if explicit shading is enabled
    if (data.getBool(key::explicitShading, false) &&
        !addExplicitShading(rdlGeometry, primitiveAttributeTable)) {
        return nullptr;
    }

    if (data.getBool(key::useScreenSpaceRadius, false)) {
        applyScreenSpaceRadius(rdlGeometry, vertices, primitiveAttributeTable, radius);
    }

    std::unique_ptr<Points> primitive = createPoints(std::move(vertices),
                                                     std::move(radius),
                                                     std::move(layerAssignmentId),
                                                     std::move(primitiveAttributeTable));
    if (primitive) {
        primitive->setCurvedMotionBlurSampleCount(data.getInt(key::curvedMotionBlurSampleCount, 10));
    }
    return primitive;
}

} // anonymous namespace

class RdlGeometryProcedural : public ProceduralLeaf
{
public:
    explicit RdlGeometryProcedural(const State &state)
        : ProceduralLeaf(state)
    {}

    void generate(const GenerateContext &generateContext,
                  const shading::XformSamples &parent2render) override;
};

void
RdlGeometryProcedural::generate(const GenerateContext &generateContext,
                                const shading::XformSamples &parent2render)
{
    const scene_rdl2::rdl2::Geometry *rdlGeometry = generateContext.getRdlGeometry();
    const scene_rdl2::rdl2::Layer *rdlLayer = generateContext.getRdlLayer();

    // Once the data has been released it is empty, so regenerating would
    // silently produce no geometry, or the wrong geometry. This should never
    // happen: the data is only released when the geometry is never generated
    // again.
    if (rdlGeometry->isDataReleased()) {
        throw scene_rdl2::except::RuntimeError(scene_rdl2::util::buildString("RdlGeometry(\"",
            rdlGeometry->getName(), "\") can't be generated again after its data has been released."));
    }
    for (const scene_rdl2::rdl2::SceneObject* sceneObject : rdlGeometry->get(attrData)) {
        if (sceneObject && sceneObject->isDataReleased()) {
            throw scene_rdl2::except::RuntimeError(scene_rdl2::util::buildString("RdlGeometry(\"",
                rdlGeometry->getName(), "\") can't be generated because the data of UserData(\"",
                sceneObject->getName(), "\") has been released."));
        }
    }

    int geoType = rdlGeometry->get(attrGeoType);
    if (geoType != GEO_TYPE_MESH && geoType != GEO_TYPE_CURVES && geoType != GEO_TYPE_POINTS) {
        rdlGeometry->error("Unknown geo_type ", geoType, ".");
        return;
    }
    const GeometryData data(rdlGeometry, geoType);
    if (!data.isValid()) {
        // the errors have been reported
        return;
    }

    // Local motion blur initialization
    shading::XformSamples parent2renderFix = parent2render;
    std::unique_ptr<local_motion_blur::LocalMotionBlur> localMotionBlur;
    if (generateContext.isMotionBlurOn() && rdlGeometry->getUseLocalMotionBlur()) {
        localMotionBlur = local_motion_blur::createFromPointLists(
            generateContext,
            data.getVec3fs(key::localMotionBlurPositionList),
            data.getVec4fs(key::localMotionBlurOrientList),
            data.getVec3fs(key::localMotionBlurScaleList),
            data.getFloats(key::localMotionBlurRadiusList),
            data.getFloats(key::localMotionBlurInnerRadiusList),
            data.getFloats(key::localMotionBlurMultiplierList),
            data.getFloat(key::localMotionBlurStrengthMult, 1.0f),
            data.getFloat(key::localMotionBlurRadiusMult, 1.0f),
            parent2renderFix);
    }

    std::unique_ptr<Primitive> primitive;
    switch (geoType) {
    case GEO_TYPE_MESH:
        primitive = buildMesh(rdlGeometry, data, rdlLayer, localMotionBlur.get());
        break;
    case GEO_TYPE_CURVES:
        primitive = buildCurves(rdlGeometry, data, rdlLayer, localMotionBlur.get());
        break;
    case GEO_TYPE_POINTS:
        primitive = buildPoints(rdlGeometry, data, rdlLayer, localMotionBlur.get());
        break;
    }

    if (primitive) {
        // may need to convert the primitive to instance to handle
        // rotation motion blur
        std::unique_ptr<Primitive> p =
            convertForMotionBlur(generateContext,
                                 std::move(primitive),
                                 (data.getBool(key::useRotationMotionBlur, false) && parent2renderFix.size() > 1));

        addPrimitive(std::move(p),
                     generateContext.getMotionBlurParams(),
                     parent2renderFix);
    }
}

} // namespace geom
} // namespace moonray

moonray::geom::Procedural*
RdlGeometry::createProcedural() const
{
    moonray::geom::State state;
    // Do not call state.setName here since scene_rdl2::rdl2::rdlLayer::assignmentId already
    // use rdlGeometry name.

    return new moonray::geom::RdlGeometryProcedural(state);
}

void
RdlGeometry::destroyProcedural() const
{
    delete mProcedural;
}

void
RdlGeometry::releaseInputData()
{
    // All of the geometry data is in the UserData, which the renderer only
    // releases once every Geometry that could share it has been generated
    for (scene_rdl2::rdl2::SceneObject* sceneObject : get(attrData)) {
        if (UserData* userData = sceneObject ? sceneObject->asA<UserData>() : nullptr) {
            userData->releaseData();
        }
    }
    markDataReleased();
}
