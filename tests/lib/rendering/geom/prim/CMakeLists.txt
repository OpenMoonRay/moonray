# Copyright 2023-2024 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

set(target moonray_rendering_geom_prim_tests)

add_executable(${target})

target_sources(${target}
    PRIVATE
        main.cc
        TestInterpolator.cc
        TestPrimAttr.cc
        TestPrimUtils.cc
        TestRdlGeometry.cc
)

target_link_libraries(${target}
    PRIVATE
        Moonray::rendering_geom
        Moonray::rendering_mcrt_common
        Moonray::rendering_pbr
        Moonray::rendering_rndr
        Moonray::rendering_shading
        SceneRdl2::common_math
        SceneRdl2::pdevunit
        SceneRdl2::scene_rdl2
        SceneRdl2::render_util
        TBB::tbb
)

# Set standard compile/link options
Moonray_cxx_compile_definitions(${target})
Moonray_cxx_compile_features(${target})
Moonray_cxx_compile_options(${target})
Moonray_link_options(${target})

# Build the DSOs needed by the tests
moonray_dso_simple(rendering_geom_prim_TestGeometry
    SKIP_INSTALL
    DSO_NAME TestGeometry
    SOURCE_DIR dso/geometry/TestGeometry
    DEPENDENCIES
        Moonray::rendering_geom
        Moonray::rendering_shading
        SceneRdl2::scene_rdl2
)

moonray_dso_simple(rendering_geom_prim_TestMaterial
    SKIP_INSTALL
    DSO_NAME TestMaterial
    SOURCE_DIR dso/material/TestMaterial
    DEPENDENCIES
        Moonray::rendering_geom
        SceneRdl2::scene_rdl2
)

# TestRdlGeometry compares RdlGeometry with the Rdl*Geometry procedurals it
# mirrors, so build the real DSOs from their own sources into this test's
# directory. TEST_DSO keeps them from colliding with the DSO targets of the
# same names in dso/geometry.
foreach(dsoName RdlGeometry RdlMesh RdlCurve RdlPoint)
    if(dsoName STREQUAL "RdlGeometry")
        set(dsoClass RdlGeometry)
    else()
        set(dsoClass ${dsoName}Geometry)
    endif()
    moonray_dso_simple(rendering_geom_prim_Test${dsoClass}
        SKIP_INSTALL
        TEST_DSO
        DSO_NAME ${dsoClass}
        SOURCE_DIR ../../../../../dso/geometry/${dsoName}
        DEPENDENCIES
            Moonray::rendering_geom
            SceneRdl2::render_util
            SceneRdl2::scene_rdl2
    )
endforeach()

add_test(NAME ${target} COMMAND ${target})
set_tests_properties(${target} PROPERTIES
    LABELS "unit"
    WORKING_DIRECTORY $<TARGET_FILE_DIR:${target}>
)
