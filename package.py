# Copyright 2024-2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

# -*- coding: utf-8 -*-
import os, sys

name = 'moonray'

if 'early' not in locals() or not callable(early):
    def early(): return lambda x: x

@early()
def version():
    """
    Increment the build in the version.
    """
    _version = '18.0'
    from rezbuild import earlybind
    return earlybind.version(this, _version)

description = 'Moonray package'

authors = [
    'PSW Rendering and Shading',
    'moonbase-dev@dreamworks.com'
]

help = ('For assistance, '
        "please contact the folio's owner at: moonbase-dev@dreamworks.com")

variants = [
    [   # variant 0
        'os-rocky-9',
        'opt_level-optdebug',
        'refplat-vfx2023.1',
        'openimageio-2.4.8.0.x',
        'gcc-11.x',
        # 'openvdb-11',
        'openvdb-11.0.0.2', # temp fix to work around a package issue with openvdb-11.0.0.3
        'zlib-1.2.11.x'
    ],
    [   # variant 1
        'os-rocky-9',
        'opt_level-debug',
        'refplat-vfx2023.1',
        'openimageio-2.4.8.0.x',
        'gcc-11.x',
        # 'openvdb-11',
        'openvdb-11.0.0.2', # temp fix to work around a package issue with openvdb-11.0.0.3
        'zlib-1.2.11.x'
    ],
    [   # variant 2
        'os-rocky-9',
        'opt_level-optdebug',
        'refplat-vfx2023.1',
        'openimageio-2.4.8.0.x',
        'clang-17.0.6.x',
        'openvdb-10', # openvdb-11 requires llvm-15 which conflicts with clang-17's llvm-17, so it stays at openvdb-10.
        'zlib-1.2.11.x'
    ],
    [   # variant 3
        'os-rocky-9',
        'opt_level-optdebug',
        'refplat-vfx2024.0',
        'openimageio-2.4.8.0.x',
        'gcc-11.x',
        # 'openvdb-11',
        'openvdb-11.0.0.2', # temp fix to work around a package issue with openvdb-11.0.0.3
        'zlib-1.2.11.x'
    ],
    [   # variant 4
        'os-rocky-9',
        'opt_level-optdebug',
        'refplat-vfx2025.0',
        'openimageio-3.0',
        'gcc-11.x',
        # 'openvdb-12',
        'openvdb-12.1.1.0', # temp fix to work around a package issue with openvdb-12.1.1.1
        'zlib-1.2.11.x'
    ],
    [   # variant 5
        'os-rocky-9',
        'opt_level-optdebug',
        'refplat-houdini21.0',
        'openimageio-3.0',
        'gcc-11.x',
        # 'openvdb-12',
        'openvdb-12.1.1.0', # temp fix to work around a package issue with openvdb-12.1.1.1
        'zlib-1.2.11.x'
    ],
    [   # variant 6
        'os-rocky-9',
        'opt_level-optdebug',
        'refplat-vfx2022.0',
        'openimageio-2.3.20.0.x',
        'gcc-9.3.x.1',
        'openvdb-9',
        'zlib-1.2.11.x',
        'opensubdiv-3.5.0.x.0'
    ],
]

conf_rats_variants = variants[0:2]
conf_CI_variants = variants

# Add ephemeral package to each variant.
for i, variant in enumerate(variants):
    variant.insert(0, '.moonray_variant-%d' % i)

requires = [
    'amorphous',
    'boost',
    'cuda-12.1.0.x',
    'embree-4.2.0.x',
    'imath-3',
    'mcrt_denoise-7.0',
    'mkl',
    'openexr',
    'opensubdiv',
    'openvdb',
    'optix-7.6.0.x',
    'random123-1.08.3',
    'scene_rdl2-16.0',
]

private_build_requires = [
    'cmake_modules-1.1',
    'cppunit',
    'ispc-1.20.0.x',
    'python-3.9|3.10|3.11'
]

commandstr = lambda i: "cd build/"+os.path.join(*variants[i])+"; ctest -j $(nproc)"
testentry = lambda i: ("variant%d" % i,
                       { "command": commandstr(i),
                        "requires": ["cmake"],
                        "on_variants": {
                            "type": "requires",
                            "value": [".moonray_variant-%d" % i],
                            },
                        "run_on": "explicit",
                        }, )
testlist = [testentry(i) for i in range(len(variants))]
tests = dict(testlist)

def commands():
    prependenv('CMAKE_MODULE_PATH', '{root}/lib64/cmake')
    prependenv('CMAKE_PREFIX_PATH', '{root}')
    prependenv('SOFTMAP_PATH', '{root}')
    prependenv('RDL2_DSO_PATH', '{root}/rdl2dso')
    prependenv('LD_LIBRARY_PATH', '{root}/lib64')
    prependenv('PATH', '{root}/bin')
    prependenv('MOONRAY_CLASS_PATH', '{root}/coredata')

uuid = '355edd2d-293f-4725-afc4-73182082debd'

config_version = 0
