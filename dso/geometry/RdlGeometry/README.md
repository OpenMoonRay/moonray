RdlGeometry Procedural
======================

RdlGeometry is a procedural that pretends to be a mesh, curves or points
primitive, with all of its data stored directly in the RDL2 scene as UserData.
It produces the same primitives as RdlMeshGeometry, RdlCurveGeometry and
RdlPointGeometry, but reads every value from UserData instead of from
attributes of its own.

Attributes
----------

- **geo_type** (Enum): "mesh", "curves" or "points"
- **data** (SceneObjectVector of UserData): all of the data for the geometry

Each key in the **data** UserData is handled as follows:

- A key matching the name, or an alias, of an attribute of RdlMeshGeometry
  (mesh), RdlCurveGeometry (curves) or RdlPointGeometry (points) supplies the
  value of that attribute. Its values must be the UserData type matching the
  attribute's type: e.g. "bool_values" for the Bool "is_subd", "int_values"
  for the Int (enum) "subd_scheme" and "float_values" for the Float
  "mesh_resolution". Values of any other type are an error, and the geometry
  is not generated. Single-valued attributes use the first value.
  Only the first set of values (e.g. "float_values_0") of these keys is used.
- The attributes that hold one motion step each are replaced by a single key
  whose "vec3f_values_0" and "vec3f_values_1" hold the two motion steps:
  "vertex_list" (for "vertex_list_0" and "vertex_list_1") and "velocity_list"
  (for "velocity_list_0" and "velocity_list_1"). Leave "vec3f_values_1" empty
  for a single motion step. The per-step names, and their aliases such as
  "vertex_list_mb", are ignored with a warning.
- A key matching an attribute that every Geometry has (e.g. "label" or
  "side_type") is ignored with a warning: set it on the RdlGeometry itself.
- Every other key is added as a primitive attribute, exactly as
  "primitive_attributes" does for the other procedurals. Primitive attributes
  don't support vec4f or mat3f values, so those keys are ignored with a
  warning.

Usage
-----

A triangle:

```
RdlGeometry("triangle") {
    ["geo_type"] = "mesh",
    ["data"] = {
        UserData("triangle_P") {
            ["vec3f_key"] = "vertex_list",
            ["vec3f_values_0"] = { Vec3(-1, 0, 0), Vec3(0, 2, 0), Vec3(1, 0, 0) },
        },
        UserData("triangle_indices") {
            ["int_key"] = "vertices_by_index",
            ["int_values"] = { 0, 1, 2 },
        },
        UserData("triangle_face_vertex_count") {
            ["int_key"] = "face_vertex_count",
            ["int_values"] = { 3 },
        },
        UserData("triangle_is_subd") {
            ["bool_key"] = "is_subd",
            ["bool_values"] = { false },
        },
        UserData("triangle_Cd") {
            ["color_key"] = "Cd",
            ["color_values_0"] = { Rgb(1, 0, 0), Rgb(0, 1, 0), Rgb(0, 0, 1) },
        },
    },
}
```

Releasing the data
------------------

When the moonray command-line renderer renders a scene without deltas files,
the scene can never be updated, so once every procedural has been generated
the values of the UserData in **data** are freed rather than kept alongside
the primitive built from them. From then on, the RdlGeometry and its UserData
can't be updated (beginUpdate() throws), written out (the Ascii and Binary
writers throw) or generated again (generate() throws). Since a UserData can be
shared, this also applies to any other object that references the same
UserData. Other renderers, such as moonray_gui, Hydra and Arras, never release
the data.
