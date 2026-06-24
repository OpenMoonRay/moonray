// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

//
//
#include "OiioReader.h"

namespace moonray {
namespace rndr {

bool
OiioReader::readData(const int subImageId)
{
    if (!mIn) return false;

    if (!mIn->seek_subimage(subImageId, mSpec)) {
        return false;           // failed to seek
    }

    const OIIO::ImageSpec &spec = mSpec;
    size_t size = static_cast<size_t>(spec.width) * static_cast<size_t>(spec.height) * static_cast<size_t>(spec.nchannels);
    try {
        mPixels.resize(size);
    } catch (const std::bad_alloc& e) {
        return false; // memory allocation failed
    }
    if (mPixels.empty()) {
        return false;           // internal pixels buffer resize failed
    }

#   if OIIO_VERSION < OIIO_MAKE_VERSION(3,0,0)
    mIn->read_image(OIIO::TypeDesc::FLOAT, mPixels.data());
#   else
    const int mipLevel = 0;
    const int chBegin = 0;
    mIn->read_image(subImageId, mipLevel, chBegin, spec.nchannels, OIIO::TypeDesc::FLOAT, mPixels.data());
#   endif
    return true;
}

std::string
OiioReader::showSpec(const std::string &hd) const
{
    std::ostringstream ostr;

    if (!mIn) {
        ostr << "file open failed.";
        return ostr.str();
    }

    ostr << OiioUtils::showImageSpec(hd, mSpec);

    return ostr.str();
}

} // namespace rndr
} // namespace moonray

