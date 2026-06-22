// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#include "BasicTexture.h"

#include <moonray/rendering/shading/Shading.h>
#include <moonray/rendering/shading/Texture.h>

#include <moonray/common/mcrt_macros/moonray_static_check.h>
#include <moonray/rendering/shading/Intersection.h>
#include <moonray/rendering/shading/ShadingTLState.h>
#include <moonray/rendering/shading/ThreadLocalObjectState.h>
#include <moonray/rendering/mcrt_common/ProfileAccumulatorHandles.h>
#include <moonray/rendering/texturing/sampler/TextureSampler.h>

#include <OpenColorIO/OpenColorIO.h>

#include <scene_rdl2/render/logging/logging.h>

#ifdef __ARM_NEON__
// This works around OIIO including x86 based headers due to detection of SSE
// support due to sse2neon.h being included elsewhere
#define __IMMINTRIN_H
#define __NMMINTRIN_H
#endif

#include <OpenImageIO/texture.h>

#include <algorithm>
#include <cctype>
#include <memory>
#include <mutex>
#include <sstream>
#include <vector>

namespace OCIO = OCIO_NAMESPACE;

namespace moonray {
namespace shading {

struct TextureOpt;

namespace {
// accumulators are not run during displacement/render_prep
// but neither is ispc, so the displacement argument is ignored
intptr_t
CPP_startOIIOAccumulator(shading::TLState *tls, uint32_t displacement)
{
    auto *exclAcc = getExclusiveAccumulators(tls);
    if (exclAcc && exclAcc->push(EXCL_ACCUM_OIIO)) {
        return (intptr_t)exclAcc;
    }
    return 0;
}

void
CPP_stopOIIOAccumulator(intptr_t accumulator)
{
    if (accumulator) {
        auto exclAcc = (mcrt_common::ExclusiveAccumulators *)accumulator;
        MNRY_ASSERT(exclAcc->isRunning(EXCL_ACCUM_OIIO));
        exclAcc->pop();
    }
}

OIIO::TextureOpt::Wrap
getOIIOWrap(WrapType wrapType) {
    switch (wrapType) {
    case WrapType::Black:
        return OIIO::TextureOpt::WrapBlack;
    case WrapType::Clamp:
        return OIIO::TextureOpt::WrapClamp;
    case WrapType::Periodic:
        return OIIO::TextureOpt::WrapPeriodic;
    case WrapType::Mirror:
        return OIIO::TextureOpt::WrapMirror;
    default:
        return OIIO::TextureOpt::WrapDefault;
    }
}

} // namespace

namespace {

std::string
trim(std::string value)
{
    auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

std::string
normalized(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) {
                       if (c == '-' || c == ' ' || c == '.') return '_';
                       return static_cast<char>(std::tolower(c));
                   });
    return value;
}

bool
isRawColorSpace(const std::string& value)
{
    const std::string key = normalized(value);
    return key.empty() || key == "raw" || key == "data" || key == "none";
}

std::string
colorSpaceName(const OCIO::ConstColorSpaceRcPtr& colorSpace)
{
    if (!colorSpace) {
        return {};
    }
    const char* name = colorSpace->getName();
    return name ? std::string(name) : std::string();
}

std::string
resolveColorSpace(const OCIO::ConstConfigRcPtr& config,
                  const std::string& token)
{
    if (!config || token.empty()) {
        return {};
    }

    try {
        std::string name = colorSpaceName(config->getColorSpace(token.c_str()));
        if (!name.empty()) {
            return name;
        }
    } catch (const OCIO::Exception&) {
    }
    return {};
}

std::string
roleColorSpace(const OCIO::ConstConfigRcPtr& config, const char* role)
{
    return resolveColorSpace(config, role ? role : "");
}

std::string
renderingColorSpace(const OCIO::ConstConfigRcPtr& config)
{
    for (const char* role : {OCIO::ROLE_RENDERING,
                             OCIO::ROLE_SCENE_LINEAR,
                             "default_float",
                             "reference",
                             OCIO::ROLE_DEFAULT}) {
        std::string name = roleColorSpace(config, role);
        if (!name.empty()) {
            return name;
        }
    }
    return {};
}

std::string
sourceColorSpaceForTexture(const OCIO::ConstConfigRcPtr& config,
                           const std::string& filename,
                           const std::string& authoredSource)
{
    std::string source = trim(authoredSource);
    const std::string key = normalized(source);

    if (isRawColorSpace(source)) {
        return {};
    }

    if (key == "auto") {
        if (!config) {
            return {};
        }
        try {
            const char* resolved = config->getColorSpaceFromFilepath(filename.c_str());
            if (resolved && !isRawColorSpace(resolved)) {
                return resolveColorSpace(config, resolved);
            }
        } catch (const OCIO::Exception&) {
            return {};
        }
        return {};
    }

    if (config) {
        std::string resolved = resolveColorSpace(config, source);
        if (!resolved.empty() && !isRawColorSpace(resolved)) {
            return resolved;
        }
    }
    return {};
}

OCIO::ConstCPUProcessorRcPtr
createTextureProcessor(const std::string& filename,
                       const std::string& sourceColorSpace,
                       std::string* diagnostic)
{
    if (diagnostic) {
        diagnostic->clear();
    }

    OCIO::ConstConfigRcPtr config;
    try {
        config = OCIO::GetCurrentConfig();
    } catch (const OCIO::Exception& e) {
        if (diagnostic) {
            *diagnostic = std::string("OCIO config load failed: ") + e.what();
        }
        return {};
    }

    const std::string source = sourceColorSpaceForTexture(config, filename, sourceColorSpace);
    if (source.empty()) {
        const std::string key = normalized(trim(sourceColorSpace));
        if (diagnostic && !isRawColorSpace(sourceColorSpace) && key != "auto") {
            std::ostringstream out;
            out << "unsupported source_color_space=\"" << sourceColorSpace
                << "\" for active OCIO config; source conversion disabled";
            *diagnostic = out.str();
        }
        return {};
    }

    const std::string target = renderingColorSpace(config);
    if (target.empty() || source == target) {
        return {};
    }

    try {
        OCIO::ConstProcessorRcPtr processor =
            config->getProcessor(source.c_str(), target.c_str());
        if (diagnostic) {
            std::ostringstream out;
            out << "source=" << source << " target=" << target;
            *diagnostic = out.str();
        }
        return processor ? processor->getDefaultCPUProcessor() : OCIO::ConstCPUProcessorRcPtr();
    } catch (const OCIO::Exception& e) {
        if (diagnostic) {
            std::ostringstream out;
            out << "failed source=" << source << " target=" << target << ": " << e.what();
            *diagnostic = out.str();
        }
    }
    return {};
}

void
applyOcioProcessor(intptr_t processorPtr, float* rgba)
{
    if (!processorPtr || !rgba) {
        return;
    }
    const OCIO::CPUProcessor* processor =
        reinterpret_cast<const OCIO::CPUProcessor*>(processorPtr);
    try {
        processor->applyRGB(rgba);
    } catch (const OCIO::Exception&) {
    }
}

} // namespace

static ispc::BASIC_TEXTURE_StaticData sBasicTextureStaticData;

class BasicTexture::Impl
{
public:
    Impl(scene_rdl2::rdl2::Shader *shader,
         scene_rdl2::rdl2::ShaderLogEventRegistry& logEventRegistry) :
         mShader(shader),
         mWidth(0),
         mHeight(0),
         mPixelAspectRatio(0.0f)
    {
        mIspc.mShader = (intptr_t) shader;
        mIspc.mTextureHandles = nullptr;
        mIspc.mIsValid = false;

        // Setup various texture quality options
        mTextureOpt[TrilinearAnisotropic].anisotropic = 4;
        mTextureOpt[TrilinearAnisotropic].mipmode = OIIO::TextureOpt::MipModeAniso;
        mTextureOpt[TrilinearAnisotropic].interpmode = OIIO::TextureOpt::InterpBilinear;

        mTextureOpt[TrilinearIsotropic].anisotropic = 1;
        mTextureOpt[TrilinearIsotropic].mipmode = OIIO::TextureOpt::MipModeTrilinear;
        mTextureOpt[TrilinearIsotropic].interpmode = OIIO::TextureOpt::InterpBilinear;

        mTextureOpt[LinearMipClosestTexel].anisotropic = 1;
        mTextureOpt[LinearMipClosestTexel].mipmode = OIIO::TextureOpt::MipModeTrilinear;
        mTextureOpt[LinearMipClosestTexel].interpmode = OIIO::TextureOpt::InterpClosest;

        mTextureOpt[ClosestMipClosestTexel].anisotropic = 1;
        mTextureOpt[ClosestMipClosestTexel].mipmode = OIIO::TextureOpt::MipModeOneLevel;
        mTextureOpt[ClosestMipClosestTexel].interpmode = OIIO::TextureOpt::InterpClosest;

        // to allow for the possibility that we may someday create image maps
        // on multiple threads, we'll protect the writes of the class statics
        // with a mutex.
        static std::mutex errorMutex;
        std::lock_guard<std::mutex> lock(errorMutex);
        MOONRAY_START_THREADSAFE_STATIC_WRITE

        mIspc.mBasicTextureStaticDataPtr = &sBasicTextureStaticData;
        sBasicTextureStaticData.mStartAccumulator = (intptr_t)CPP_startOIIOAccumulator;
        sBasicTextureStaticData.mStopAccumulator = (intptr_t)CPP_stopOIIOAccumulator;

        sBasicTextureStaticData.sErrorSampleFail =
            logEventRegistry.createEvent(scene_rdl2::logging::ERROR_LEVEL,
                                          "unknown oiio sample failure");

        MOONRAY_FINISH_THREADSAFE_STATIC_WRITE
    }

    Impl(const Impl& other) =delete;
    Impl& operator= (const Impl& other) =delete;

    ~Impl() {}

    bool
    update(const std::string &filename,
           ispc::TEXTURE_GammaMode gammaMode,
           const std::string& sourceColorSpace,
           WrapType wrapS,
           WrapType wrapT,
           bool useDefaultColor,
           const scene_rdl2::math::Color& defaultColor,
           const scene_rdl2::math::Color& fatalColor,
           std::string &errorMsg)
    {
        init();
        mSourceColorSpace = sourceColorSpace;

        mIspc.mUseDefaultColor = useDefaultColor;
        mIspc.mDefaultColor.r = defaultColor.r;
        mIspc.mDefaultColor.g = defaultColor.g;
        mIspc.mDefaultColor.b = defaultColor.b;
        mIspc.mFatalColor.r = fatalColor.r;
        mIspc.mFatalColor.g = fatalColor.g;
        mIspc.mFatalColor.b = fatalColor.b;
        mIspc.mIsValid = false;
        mIspc.mOcioProcessor = 0;
        mOcioProcessor.reset();

        mTextureHandles.assign(1, nullptr);
        mIspc.mTextureHandles = reinterpret_cast<int64_t *>(&mTextureHandles[0]);

        // load and prepare the texture...
        std::string errorString;
        texture::TextureSampler *textureSampler = texture::getTextureSampler();
        texture::TextureHandle *handle = textureSampler->getHandle(filename, errorString);
        if (handle && checkTextureWindow(textureSampler, handle, filename, errorMsg)) {
            mTextureHandles[0] = handle;
            mIspc.mIsValid = true;
        } else {
            std::ostringstream os;
            os << "FATAL: failed to open texture file \"" <<
                  filename << "\" (" << errorString << ")";
            errorMsg = os.str();
            textureSampler->unregisterMapForInvalidation(mShader);
            mIspc.mIsValid = false;
            if (mIspc.mUseDefaultColor) {
                return true;
            } else {
                return false;
            }
        }

        // This will early out and do minimal work if the
        // filename and 'this' pointer haven't changed.
        //
        // NOTE: This line was giving this exception after a reload:
        //  terminate called after throwing an instance of 'except::KeyError'
        //  what():  No Attribute named 'texture' on SceneClass 'ProjectTriplanarMap'.
        textureSampler->registerMapForInvalidation(filename, mShader, false);

        for (int i=0; i < QualityCount; ++i) {
            mTextureOpt[i].swrap = getOIIOWrap(wrapS);
            mTextureOpt[i].twrap = getOIIOWrap(wrapT);
            mTextureOpt[i].subimagename.clear();
        }

        MNRY_ASSERT(mTextureHandles[0]);

#       if OIIO_VERSION < OIIO_MAKE_VERSION(3,0,0)
        OIIO::TextureSystem *textureSystem = textureSampler->getTextureSystem();
#       else
        std::shared_ptr<OIIO::TextureSystem> textureSystem = textureSampler->getTextureSystem();
#       endif

        // retrieve resolution and pixel aspect ratio information. This
        // info may be needed in certain contexts, see ProjectCameraMap_v2 shader
        OIIO::ImageSpec spec;
        OIIO::ustring ufilename(filename.c_str());
        textureSystem->get_imagespec(ufilename, 0, spec);
        mWidth = spec.width;
        mHeight = spec.height;
        mPixelAspectRatio = spec.get_float_attribute("PixelAspectRatio", 1.0f);

        mIspc.mWidth = mWidth;
        mIspc.mHeight = mHeight;
        mIspc.mPixelAspectRatio = mPixelAspectRatio;

        mIspc.mTextureOptions = (intptr_t) mTextureOpt;
        mIspc.mApplyGamma = getApplyGamma(gammaMode, spec.nchannels);
        mIspc.mIs8bit = (spec.format == OIIO::TypeDesc::UINT8);
        std::string ocioDiagnostic;
        mOcioProcessor = createTextureProcessor(filename, mSourceColorSpace, &ocioDiagnostic);
        if (!ocioDiagnostic.empty() &&
            (ocioDiagnostic.find("unsupported") == 0 ||
             ocioDiagnostic.find("failed") == 0 ||
             ocioDiagnostic.find("OCIO config load failed") == 0)) {
            scene_rdl2::logging::Logger::warn("ImageMap OCIO: ", ocioDiagnostic);
        }
        mIspc.mOcioProcessor = reinterpret_cast<intptr_t>(mOcioProcessor.get());
        if (mOcioProcessor) {
            mIspc.mApplyGamma = false;
        }
        mIspc.mIsValid = true;

        if (mIspc.mUseDefaultColor) {
            return true;
        } else {
            return mIspc.mIsValid;
        }
    }

    scene_rdl2::math::Color4
    sample(shading::TLState *tls,
           const shading::State& state,
           scene_rdl2::math::Vec2f st,
           float *derivatives) const
    {
        if (!mIspc.mIsValid && mIspc.mUseDefaultColor) {
            return scene_rdl2::math::Color4(scene_rdl2::math::asCpp(mIspc.mDefaultColor).r,
                                            scene_rdl2::math::asCpp(mIspc.mDefaultColor).g,
                                            scene_rdl2::math::asCpp(mIspc.mDefaultColor).b,
                                            1.0f);
        }

        const int index = getTextureOptionIndex(state.isDisplacement(), state);

        const texture::TextureOptions& options = mTextureOpt[index];

        scene_rdl2::math::Color4 result;

        OIIO::TextureSystem *texSys = MNRY_VERIFY(tls->mTextureSystem);

        EXCL_ACCUMULATOR_PROFILE(tls, EXCL_ACCUM_OIIO);

        // dwa_texture *must* be given 4 floats for the result.
        ALIGN(16) float tmp[4];

        bool res = texSys->texture(
            const_cast<texture::TextureHandle *>(getTextureHandle()),
            tls->mOIIOThreadData,
            const_cast<OIIO::TextureOpt&>(options),
            st[0], st[1],
            derivatives[0], derivatives[1],
            derivatives[2], derivatives[3],
            4, tmp
        );

        if (res) {
            if (mIspc.mApplyGamma && mIspc.mIs8bit) { // actually INVERSE gamma
                tmp[0] = tmp[0] > 0.0f ? powf(tmp[0], 2.2f) : 0.0f;
                tmp[1] = tmp[1] > 0.0f ? powf(tmp[1], 2.2f) : 0.0f;
                tmp[2] = tmp[2] > 0.0f ? powf(tmp[2], 2.2f) : 0.0f;
                // don't gamma the alpha channel
            }
            applyOcioProcessor(mIspc.mOcioProcessor, tmp);
            result[0] = tmp[0];
            result[1] = tmp[1];
            result[2] = tmp[2];
            result[3] = tmp[3];
        } else {
            logEvent(mIspc.mBasicTextureStaticDataPtr->sErrorSampleFail);
            result[0] = result[1] = result[2] = result[3] = 0.f;
        }

        return result;
    }

    void
    init()
    {
        mTextureHandles.clear();

        texture::TextureSampler *textureSampler = texture::getTextureSampler();
        if (textureSampler) {
            textureSampler->unregisterMapForInvalidation(mShader);
        }

        mIspc.mApplyGamma = false;
        mIspc.mIs8bit = false;
        mIspc.mOcioProcessor = 0;
        mIspc.mIsValid = false;
        mOcioProcessor.reset();
        mWidth = 0;
        mHeight = 0;
        mPixelAspectRatio = 0.0f;
    }

    const texture::TextureHandle*
    getTextureHandle() const
    {
        return static_cast<const texture::TextureHandle *>(mTextureHandles[0]);
    }

    void
    logEvent(int error) const
    {
        scene_rdl2::rdl2::Shader::getLogEventRegistry().log(mShader, error);
    }

    ispc::BASIC_TEXTURE_Data mIspc;

    scene_rdl2::rdl2::Shader *mShader;
    std::vector<texture::TextureHandle*> mTextureHandles;
    texture::TextureOptions mTextureOpt[QualityCount];
    OCIO::ConstCPUProcessorRcPtr mOcioProcessor;
    std::string mSourceColorSpace;

    int mWidth;
    int mHeight;
    float mPixelAspectRatio;
};

// -------------------------------------------------------------

BasicTexture::BasicTexture(
    scene_rdl2::rdl2::Shader *shader,
    scene_rdl2::rdl2::ShaderLogEventRegistry& logEventRegistry) :
    mImpl(std::make_unique<Impl>(shader, logEventRegistry))
{}

BasicTexture::~BasicTexture()
{
    mImpl->init();
}

bool
BasicTexture::update(const std::string &filename,
                     ispc::TEXTURE_GammaMode gammaMode,
                     const std::string& sourceColorSpace,
                     WrapType wrapS,
                     WrapType wrapT,
                     bool useDefaultColor,
                     const scene_rdl2::math::Color& defaultColor,
                     const scene_rdl2::math::Color& fatalColor,
                     std::string &errorMsg)
{
    return mImpl->update(filename,
                         gammaMode,
                         sourceColorSpace,
                         wrapS,
                         wrapT,
                         useDefaultColor,
                         defaultColor,
                         fatalColor,
                         errorMsg);
}

scene_rdl2::math::Color4
BasicTexture::sample(shading::TLState *tls,
                     const shading::State& state,
                     const scene_rdl2::math::Vec2f& st,
                     float *derivatives) const
{
    return mImpl->sample(tls, state, st, derivatives);
}

const ispc::BASIC_TEXTURE_Data&
BasicTexture::getBasicTextureData() const
{
    return mImpl->mIspc;
}

void
BasicTexture::getDimensions(int &x, int& y) const
{
    x = mImpl->mWidth;
    y = mImpl->mHeight;
}

bool
BasicTexture::isValid() const
{
    return mImpl->mIspc.mIsValid;
}

float
BasicTexture::getPixelAspectRatio() const
{
    return mImpl->mPixelAspectRatio;
}

void CPP_oiioTexture(const ispc::BASIC_TEXTURE_Data *tx,
                     shading::TLState *tls,
                     const uint32_t displacement,
                     const int pathType,
                     const float *derivatives,
                     const float *st,
                     float *result)
{
    if (!tx->mIsValid && tx->mUseDefaultColor) {
        result[0] = tx->mDefaultColor.r;
        result[1] = tx->mDefaultColor.g;
        result[2] = tx->mDefaultColor.b;
        result[3] = 1.0f;
        return;
    }

    texture::TextureHandle *textureHandle = (reinterpret_cast<texture::TextureHandle **>(tx->mTextureHandles))[0];
    texture::TLState::Perthread *threadInfo = tls->mOIIOThreadData;
    OIIO::TextureSystem *texSys = MNRY_VERIFY(tls->mTextureSystem);
    const int index = getTextureOptionIndex(displacement != 0, 
        static_cast<shading::Intersection::PathType>(pathType));
    texture::TextureOptions *options = (reinterpret_cast<texture::TextureOptions *>(tx->mTextureOptions));

    float s = st[0];
    float t = st[1];
    float dsdx = derivatives[0];
    float dtdx = derivatives[1];
    float dsdy = derivatives[2];
    float dtdy = derivatives[3];
    const int nChannels = 4;

    bool res = texSys->texture(textureHandle,
                               threadInfo,
                               options[index],
                               s, t,
                               dsdx, dsdy, dtdx, dtdy,
                               nChannels,
                               result);

    if (res) {
        if (tx->mApplyGamma && tx->mIs8bit) { // actually INVERSE gamma
            result[0] = pow(result[0], 2.2f);
            result[1] = pow(result[1], 2.2f);
            result[2] = pow(result[2], 2.2f);
            // don't gamma the alpha channel
        }
        applyOcioProcessor(tx->mOcioProcessor, result);
    } else {
        scene_rdl2::rdl2::Shader* const shader = reinterpret_cast<scene_rdl2::rdl2::Shader*>(tx->mShader);
        scene_rdl2::rdl2::Shader::getLogEventRegistry().log(shader, tx->mBasicTextureStaticDataPtr->sErrorSampleFail);
        result[0] = result[1] = result[2] = result[3] = 0.f;
    }
}

} // end namespace shading
} // end namespace moonray

//
