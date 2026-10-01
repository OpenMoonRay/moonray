// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

/// @file attributes.cc

#include <scene_rdl2/scene/rdl2/rdl2.h>

using namespace scene_rdl2;

RDL2_DSO_ATTR_DECLARE

    rdl2::AttributeKey<rdl2::Int>               attrGeoType;
    const int GEO_TYPE_MESH = 0;
    const int GEO_TYPE_CURVES = 1;
    const int GEO_TYPE_POINTS = 2;

    rdl2::AttributeKey<rdl2::SceneObjectVector> attrData;

RDL2_DSO_ATTR_DEFINE(rdl2::Geometry)

    attrGeoType =
        sceneClass.declareAttribute<rdl2::Int>("geo_type", GEO_TYPE_MESH,
                                               rdl2::FLAGS_ENUMERABLE, rdl2::INTERFACE_GENERIC, { "geo type" });
    sceneClass.setEnumValue(attrGeoType, GEO_TYPE_MESH, "mesh");
    sceneClass.setEnumValue(attrGeoType, GEO_TYPE_CURVES, "curves");
    sceneClass.setEnumValue(attrGeoType, GEO_TYPE_POINTS, "points");
    sceneClass.setMetadata(attrGeoType, "label", "geo type");
    sceneClass.setMetadata(attrGeoType, "comment", "The type of primitive to generate. \"mesh\", "
        "\"curves\" and \"points\" read their data from the same keys as the attributes of "
        "RdlMeshGeometry, RdlCurveGeometry and RdlPointGeometry respectively");
    sceneClass.setGroup("Geometry", attrGeoType);

    attrData =
        sceneClass.declareAttribute<rdl2::SceneObjectVector>("data", rdl2::FLAGS_NONE, rdl2::INTERFACE_USERDATA);
    sceneClass.setMetadata(attrData, "label", "data");
    sceneClass.setMetadata(attrData, "comment", "Vector of UserData holding all of the data for the "
        "geometry. A key matching the name of an attribute of the RdlMeshGeometry, RdlCurveGeometry or "
        "RdlPointGeometry (depending on \"geo type\") supplies the value of that attribute, and must "
        "be the UserData type matching the attribute's type, with single-valued attributes taking "
        "the first value. The motion steps of \"vertex_list\" and "
        "\"velocity_list\" are the vec3f_values_0 and vec3f_values_1 of a single key, in place of the "
        "per-step \"_0\" and \"_1\" attributes. Every other key is added as a primitive "
        "attribute. When rendering with the moonray command-line renderer, the UserData values are "
        "freed once all geometry has been generated, after which they can no longer be updated");
    sceneClass.setGroup("Geometry", attrData);

RDL2_DSO_ATTR_END
