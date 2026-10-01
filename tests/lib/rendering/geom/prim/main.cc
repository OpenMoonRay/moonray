// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "TestPrimAttr.h"
#include "TestInterpolator.h"
#include "TestRdlGeometry.h"
#include <moonray/rendering/mcrt_common/ThreadLocalState.h>
#include <scene_rdl2/pdevunit/pdevunit.h>

#include <thread>

int
main(int argc, char *argv[])
{
    moonray::mcrt_common::TLSInitParams initParams;
    initParams.mUnitTests = true;
    initParams.mDesiredNumTBBThreads = std::thread::hardware_concurrency();
    moonray::mcrt_common::initTLS(initParams);

    CPPUNIT_TEST_SUITE_REGISTRATION(moonray::geom::unittest::TestRenderingPrimAttr);
    CPPUNIT_TEST_SUITE_REGISTRATION(moonray::geom::unittest::TestInterpolator);
    CPPUNIT_TEST_SUITE_REGISTRATION(moonray::geom::unittest::TestRdlGeometry);

    int result = pdevunit::run(argc, argv);
    moonray::mcrt_common::cleanUpTLS();
    return result;
}


