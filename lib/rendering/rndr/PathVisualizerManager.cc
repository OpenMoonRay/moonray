// Copyright 2025-2026 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "PathVisualizerManager.h"

#include <scene_rdl2/common/grid_util/PathVisSimGlobalInfo.h>
#include <scene_rdl2/common/math/Color.h>
#include <scene_rdl2/scene/rdl2/rdl2.h>
#include <moonray/rendering/pbr/core/PathVisualizer.h>
#include <moonray/rendering/pbr/core/Scene.h>
#include <moonray/rendering/rt/EmbreeAccelerator.h>
#include <moonray/rendering/rndr/RenderContext.h>

namespace moonray {
namespace rndr {

PathVisualizerManager::PathVisualizerManager(RenderContext* renderContext) 
    : mPathVisualizer(), 
      mScene(nullptr), 
      mRenderContext(renderContext), 
      mOn(false),
      mRecorderCameraXform(), 
      mCachedCameraXform(), 
      mCameraXformWasCached(false)
{
    mParams = std::make_unique<pbr::PathVisualizerParams>();
    mPathVisualizer = std::make_unique<pbr::PathVisualizer>();

    parserConfigure();
}

PathVisualizerManager::~PathVisualizerManager() {}

void PathVisualizerManager::initialize(const scene_rdl2::rdl2::SceneVariables& vars, pbr::Scene* scene)
{
    // Only create the path visualizer once
    if (!isInNoneState()) { return; }

    mParams->mMaxRayLength = scene_rdl2::math::length(scene->getEmbreeAccelerator()->getBounds().size());

    const unsigned width = vars.getRezedWidth();
    const unsigned height = vars.getRezedHeight();
    pbr::PathVisualizerParams* params = mParams.get();
    params->setPixelRange(0, 0, width - 1, height - 1);
    mPathVisualizer->initialize(width, height, params);

    mScene = scene;
    mScene->setPathVisualizer(mPathVisualizer.get());

    mRecorderCameraXform = mRenderContext->getCamera()->get(scene_rdl2::rdl2::Node::sNodeXformKey);
}

/// ----------------------------------------------------------------------

void PathVisualizerManager::startSimulation()
{
    // PathVisualizer is in NONE state if it hasn't been initialized,
    /// and OFF if it has been turned off by the user
    if (isInNoneState() || !isOn()) { return; }

    mSimulationRecTime.start();

    mPathVisualizer->setState(pbr::State::START_RECORD);
    mPathVisualizer->reset();
}

void PathVisualizerManager::setRecordState()
{
    MNRY_ASSERT(isInStartRecordState());
    mPathVisualizer->setState(pbr::State::RECORD);    
}

void PathVisualizerManager::stopSimulation()
{
    MNRY_ASSERT(isInRecordState());
    mPathVisualizer->setState(pbr::State::STOP_RECORD);

    // If the selected node is out of bounds, reset it
    if (mSelectedNode >= mPathVisualizer->getNodeCount()) {
        mSelectedNode = -1;
    }

    mSimulationTime = mSimulationRecTime.end();
}

void PathVisualizerManager::requestDraw()
{
    MNRY_ASSERT(isInStopRecordState());
    mPathVisualizer->setState(pbr::State::REQUEST_DRAW);
}

void PathVisualizerManager::generateLines()
{
    if (!isOn()) { return; }

    scene_rdl2::rec_time::RecTime timer;
    timer.start();
    mPathVisualizer->setState(pbr::State::GENERATE_LINES);
    mPathVisualizer->generateLines(mScene);
    mPathVisualizer->setState(pbr::State::DRAW);
    mGenerateLinesTime = timer.end();
}

void PathVisualizerManager::draw(scene_rdl2::fb_util::RenderBuffer* renderBuffer)
{
    if (!isOn()) { return; }

    MNRY_ASSERT(isInDrawState());
    mPathVisualizer->draw(renderBuffer, mScene);
}

void PathVisualizerManager::printStats() const
{
    if (getTotalLines() == 0) { return; }

    const size_t lightSampleRayCount = mPathVisualizer->getLightSampleRayCount();
    const size_t bsdfSampleRayCount = mPathVisualizer->getBsdfSampleRayCount();
    const size_t diffuseRayCount = mPathVisualizer->getDiffuseRayCount();
    const size_t specularRayCount = mPathVisualizer->getSpecularRayCount();
    const size_t occlRayCount = lightSampleRayCount + bsdfSampleRayCount;
    const size_t total = occlRayCount + diffuseRayCount + specularRayCount;

    std::cout << "\n\n=====================================\n";
    std::cout <<   "===     Path Visualizer Stats     ===\n";
    std::cout <<   "=====================================\n";
    std::cout << "Total # rays: " << total << std::endl;
    std::cout << "Total occlusion rays: " << occlRayCount << std::endl;
    std::cout << "Total light sample rays: " << lightSampleRayCount << std::endl;
    std::cout << "Total bsdf sample rays: " << bsdfSampleRayCount << std::endl;
    std::cout << "Total diffuse rays: " << diffuseRayCount << std::endl;
    std::cout << "Total specular rays: " << specularRayCount << std::endl;
    std::cout << "\n";
    std::cout << "Simulation time (s): " << mSimulationTime << std::endl;
    std::cout << "Generate lines time (s): " << mGenerateLinesTime << std::endl;
    std::cout << "Avg time per line (ms): " << (mGenerateLinesTime / getTotalLines() * 1000) << std::endl;
    std::cout << "=====================================\n\n";
}

void
PathVisualizerManager::crawlAllLines(const CrawlLineFunc& func)
{
    if (!mPathVisualizer) return;
    mPathVisualizer->crawlAllLines(func);
}

size_t
PathVisualizerManager::getTotalLines() const
{
    return mPathVisualizer->getTotalLines();
}

// static function
scene_rdl2::grid_util::VectorPacketLineStatus::RayType
PathVisualizerManager::flagsToRayType(const uint8_t& flags)
{
    return pbr::PathVisualizer::flagsToRayType(flags);
}

scene_rdl2::math::Color
PathVisualizerManager::getColorByFlags(const uint8_t& flags) const
{
    return mPathVisualizer->getColorByFlags(flags);
}

void PathVisualizerManager::setNeedsRenderRefresh(const bool refresh)
{
    mPathVisualizer->setNeedsRenderRefresh(refresh);
}

void PathVisualizerManager::reset()
{
    MNRY_ASSERT(!isInNoneState());
    mPathVisualizer->reset();
}

void PathVisualizerManager::fillPixelSamples(int& samples) const
{
    if (!mParams->mUseSceneSamples) {
        samples = mParams->mPixelSamples;
    }
}

void PathVisualizerManager::fillLightSamples(int& samples) const
{
    if (!mParams->mUseSceneSamples) {
        samples = mParams->mLightSamples;
    }
}

void PathVisualizerManager::fillBsdfSamples(int& samples) const
{
    if (!mParams->mUseSceneSamples) {
        samples = mParams->mBsdfSamples;
    }
}

void PathVisualizerManager::fillMaxDepth(int& samples) const
{
    samples = mParams->mMaxDepth;
}

void PathVisualizerManager::turnOn()
{
    mOn = true;

    mPathVisualizer->setOn(mOn);
    mParams->setOn(mOn);

    startSimulation();
}
void PathVisualizerManager::turnOff()
{
    mOn = false;

    mPathVisualizer->setOn(mOn);
    mParams->setOn(mOn);

    //
    // This is for the vecPacket communication, more precise and sync.
    // Especially for the timing of PathVisualizer turning off to on,
    // there is some garbage data is still left before generating updated lines.
    // vecPacket might pick garbage data and send it to the downstream.
    // We must clean up old lines when the path visualizer is off.
    //
    mPathVisualizer->resetLines();
}

void PathVisualizerManager::setRecorderCameraXform(const scene_rdl2::math::Mat4d& xform)
{
    mRecorderCameraXform = xform;
}

void PathVisualizerManager::setCachedCameraXform(const scene_rdl2::math::Mat4d& xform)
{
    mCachedCameraXform = xform;
    mCameraXformWasCached = true;
}

void PathVisualizerManager::setCameraXformWasCached(const bool cached)
{
    mCameraXformWasCached = cached;
}

void PathVisualizerManager::nextNode()
{
    if (mPathVisualizer->getNodeCount() == 0) { return; }

    mSelectedNode = mSelectedNode + 1;
    // When we pass the last node, loop back to a "no nodes selected"
    // state, indicated by -1, before starting back at index 0
    if (mSelectedNode >= mPathVisualizer->getNodeCount()) {
        mSelectedNode = -1;
    }
}

void PathVisualizerManager::prevNode()
{
    if (mPathVisualizer->getNodeCount() == 0) { return; }

    mSelectedNode = mSelectedNode - 1;
    // When we go to the previous node from a "no nodes selected"
    // state, indicated by -1, loop back to the highest node index
    if (mSelectedNode < -1) {
        mSelectedNode = mPathVisualizer->getNodeCount() - 1;
    }
}

std::string PathVisualizerManager::getNodeInfo(const size_t nodeIndex) const
{
    if (!mPathVisualizer) return "";
    return mPathVisualizer->getNodeInfo(nodeIndex);
}

// Check if the given node index is the selected node
// We treat -1 as a special case meaning no node is selected.
// But here we consider -1 as a wildcard that matches any node,
// as this function is primarily used for drawing lines that match the node index,
// and when no node is selected, we want to see all lines
bool PathVisualizerManager::isSelectedNode(const int nodeIndex) const
{
    return mSelectedNode == -1 || mSelectedNode == nodeIndex;
}

/// ----------------------------- Getters ------------------------------------

bool PathVisualizerManager::isOn() const
{
    return mOn;
}

bool PathVisualizerManager::isInNoneState() const
{
    return mPathVisualizer->getState() == pbr::State::NONE;
}

bool PathVisualizerManager::isInReadyState() const
{
    return mPathVisualizer->getState() == pbr::State::READY;
}

bool PathVisualizerManager::isInStartRecordState() const
{
    return mPathVisualizer->getState() == pbr::State::START_RECORD;
}

bool PathVisualizerManager::isInRecordState() const
{
    return mPathVisualizer->getState() == pbr::State::RECORD;
}

bool PathVisualizerManager::isInStopRecordState() const
{
    return mPathVisualizer->getState() == pbr::State::STOP_RECORD;
}

bool PathVisualizerManager::isDrawRequested() const
{
    return mPathVisualizer->getState() == pbr::State::REQUEST_DRAW;
}

bool PathVisualizerManager::isInDrawState() const
{
    return mPathVisualizer->getState() == pbr::State::DRAW;
}

bool PathVisualizerManager::getNeedsRenderRefresh() const
{
    return mPathVisualizer->getNeedsRenderRefresh();
}

bool PathVisualizerManager::isProcessing() const
{
    return isInStartRecordState() || isInRecordState() || isInStopRecordState();
}

scene_rdl2::math::Vec2i PathVisualizerManager::getPixel() const
{
    return scene_rdl2::math::Vec2i(mParams->mPixelX, mParams->mPixelY);
}

scene_rdl2::math::Mat4d 
PathVisualizerManager::getRecorderCameraXform() const
{
    return mRecorderCameraXform;
}

scene_rdl2::math::Mat4d 
PathVisualizerManager::getCachedCameraXform() const
{
    return mCachedCameraXform;
}

bool 
PathVisualizerManager::getCameraXformWasCached() const
{
    return mCameraXformWasCached;
}

/// ------------------------- UI getters --------------------------------- //

uint32_t PathVisualizerManager::getPixelX() const { return mParams->mPixelX; }
uint32_t PathVisualizerManager::getPixelY() const { return mParams->mPixelY; }
uint32_t PathVisualizerManager::getMaxDepth() const { return mParams->mMaxDepth; }

bool PathVisualizerManager::isSample(const uint8_t flags) const
{
    const auto flag = static_cast<pbr::PathVisualizer::Flags>(flags);
    return pbr::PathVisualizer::isSpecularSample(flag) ||
           pbr::PathVisualizer::isDiffuseSample(flag) ||
           pbr::PathVisualizer::isLightSample(flag);
}

bool PathVisualizerManager::showRay(const uint8_t& flag) const
{
    const auto flags = static_cast<pbr::PathVisualizer::Flags>(flag);

    if (pbr::PathVisualizer::isCameraRay(flags)) { return true; }
    if (pbr::PathVisualizer::isIndirectSpecularRay(flags)) { return getShowIndirectSpecularRays(); }
    if (pbr::PathVisualizer::isIndirectDiffuseRay(flags)) { return getShowIndirectDiffuseRays(); }
    if (pbr::PathVisualizer::isDirectSpecularRay(flags)) { return getShowDirectSpecularRays(); }
    if (pbr::PathVisualizer::isDirectDiffuseRay(flags)) { return getShowDirectDiffuseRays(); }
    if (pbr::PathVisualizer::isDirectLightRay(flags)) { return getShowDirectLightRays(); }
    if (pbr::PathVisualizer::isSpecularSample(flags)) { return getShowSpecularSamples(); }
    if (pbr::PathVisualizer::isDiffuseSample(flags)) { return getShowDiffuseSamples(); }
    if (pbr::PathVisualizer::isLightSample(flags)) { return getShowLightSamples(); }

    return true;
}

bool PathVisualizerManager::getShowDirectRays() const { return mParams->mDirectRaysOn; }
bool PathVisualizerManager::getShowIndirectRays() const { return mParams->mIndirectRaysOn; }
bool PathVisualizerManager::getShowSamples() const { return mParams->mSamplesOn; }
bool PathVisualizerManager::getShowIndirectSpecularRays() const
{ 
    return mParams->mIndirectRaysOn && mParams->mIndirectSpecularRaysOn;
}
bool PathVisualizerManager::getShowIndirectDiffuseRays() const
{ 
    return mParams->mIndirectRaysOn && mParams->mIndirectDiffuseRaysOn;
}
bool PathVisualizerManager::getShowDirectSpecularRays() const
{ 
    return mParams->mDirectRaysOn && mParams->mDirectSpecularRaysOn;
}
bool PathVisualizerManager::getShowDirectDiffuseRays() const
{
    return mParams->mDirectRaysOn && mParams->mDirectDiffuseRaysOn;
}
bool PathVisualizerManager::getShowDirectLightRays() const
{
    return mParams->mDirectRaysOn && mParams->mDirectLightRaysOn;
}
bool PathVisualizerManager::getShowSpecularSamples() const
{
    return mParams->mSamplesOn && mParams->mSpecularSamplesOn;
}
bool PathVisualizerManager::getShowDiffuseSamples() const
{
    return mParams->mSamplesOn && mParams->mDiffuseSamplesOn;
}
bool PathVisualizerManager::getShowLightSamples() const
{
    return mParams->mSamplesOn && mParams->mLightSamplesOn;
}

const scene_rdl2::math::Color& PathVisualizerManager::getCameraRayColor() const
{
    return mParams->mCameraRayColor;
}
const scene_rdl2::math::Color& PathVisualizerManager::getIndirectSpecularRayColor() const
{
    return mParams->mIndirectSpecularRayColor;
}
const scene_rdl2::math::Color& PathVisualizerManager::getIndirectDiffuseRayColor() const
{
    return mParams->mIndirectDiffuseRayColor;
}
const scene_rdl2::math::Color& PathVisualizerManager::getDirectSpecularRayColor() const
{
    return mParams->mDirectSpecularRayColor;
}
const scene_rdl2::math::Color& PathVisualizerManager::getDirectDiffuseRayColor() const
{
    return mParams->mDirectDiffuseRayColor;
}
const scene_rdl2::math::Color& PathVisualizerManager::getDirectLightRayColor() const
{
    return mParams->mDirectLightRayColor;
}

float PathVisualizerManager::getMaxRayLength() const { return mParams->mMaxRayLength; }
float PathVisualizerManager::getHiddenLineOpacity() const { return mParams->mHiddenLineOpacity; }
float PathVisualizerManager::getLineWidth() const { return mParams->mLineWidth; }

bool PathVisualizerManager::getUseSceneSamples() const { return mParams->mUseSceneSamples; }
uint32_t PathVisualizerManager::getPixelSamples() const { return std::sqrt(mParams->mPixelSamples); }
uint32_t PathVisualizerManager::getLightSamples() const { return std::sqrt(mParams->mLightSamples); }
uint32_t PathVisualizerManager::getBsdfSamples() const  { return std::sqrt(mParams->mBsdfSamples); }

uint32_t PathVisualizerManager::getSelectedNode() const { return mSelectedNode; }

/// ------------------------- UI setters --------------------------------- //

void PathVisualizerManager::setPixelX(uint32_t px, bool update)
{
    mParams->mPixelX = px;
    mRecorderCameraXform = mRenderContext->getCamera()->get(scene_rdl2::rdl2::Node::sNodeXformKey);
    if (update) { startSimulation(); }
}
void PathVisualizerManager::setPixelY(uint32_t py, bool update)
{
    mParams->mPixelY = py;
    mRecorderCameraXform = mRenderContext->getCamera()->get(scene_rdl2::rdl2::Node::sNodeXformKey);
    if (update) { startSimulation(); }
}
void PathVisualizerManager::setPixel(uint32_t px, uint32_t py, bool update)
{
    mParams->mPixelX = px;
    mParams->mPixelY = py;
    mRecorderCameraXform = mRenderContext->getCamera()->get(scene_rdl2::rdl2::Node::sNodeXformKey);
    if (update) { startSimulation(); }
}
void PathVisualizerManager::setMaxDepth(int depth, bool update)
{
    mParams->mMaxDepth = depth;
    if (update) { startSimulation(); }
}

void PathVisualizerManager::setShowDirectRays(bool flag)
{
    mParams->mDirectRaysOn = flag;
    startSimulation();
}
void PathVisualizerManager::setShowIndirectRays(bool flag)
{
    mParams->mIndirectRaysOn = flag;
    startSimulation();
}
void PathVisualizerManager::setShowSamples(bool flag)
{
    mParams->mSamplesOn = flag;
    startSimulation();
}
void PathVisualizerManager::setShowIndirectSpecularRays(bool flag)
{
    mParams->mIndirectSpecularRaysOn = flag;
    startSimulation();
}
void PathVisualizerManager::setShowIndirectDiffuseRays(bool flag)
{
    mParams->mIndirectDiffuseRaysOn = flag;
    startSimulation();
}
void PathVisualizerManager::setShowDirectSpecularRays(bool flag)
{
    mParams->mDirectSpecularRaysOn = flag;
    startSimulation();
}
void PathVisualizerManager::setShowDirectDiffuseRays(bool flag)
{
    mParams->mDirectDiffuseRaysOn = flag;
    startSimulation();
}
void PathVisualizerManager::setShowDirectLightRays(bool flag)
{
    mParams->mDirectLightRaysOn = flag;
    startSimulation();
}
void PathVisualizerManager::setShowSpecularSamples(bool flag)
{
    mParams->mSpecularSamplesOn = flag;
    startSimulation();
}
void PathVisualizerManager::setShowDiffuseSamples(bool flag)
{
    mParams->mDiffuseSamplesOn = flag;
    startSimulation();
}
void PathVisualizerManager::setShowLightSamples(bool flag)
{
    mParams->mLightSamplesOn = flag;
    startSimulation();
}

void PathVisualizerManager::setCameraRayColor(const scene_rdl2::math::Color color)
{ 
    mParams->mCameraRayColor = color;
}
void PathVisualizerManager::setIndirectSpecularRayColor(const scene_rdl2::math::Color color)
{
    mParams->mIndirectSpecularRayColor = color;
}
void PathVisualizerManager::setIndirectDiffuseRayColor(const scene_rdl2::math::Color color)
{
    mParams->mIndirectDiffuseRayColor = color;
}
void PathVisualizerManager::setDirectSpecularRayColor(const scene_rdl2::math::Color color)
{
    mParams->mDirectSpecularRayColor = color;
}
void PathVisualizerManager::setDirectDiffuseRayColor(const scene_rdl2::math::Color color)
{
    mParams->mDirectDiffuseRayColor = color;
}
void PathVisualizerManager::setDirectLightRayColor(const scene_rdl2::math::Color color)
{
    mParams->mDirectLightRayColor = color;
}

void PathVisualizerManager::setMaxRayLength(const float length, const bool update)
{
    mParams->mMaxRayLength = length;
    if (update) { startSimulation(); }
}
void PathVisualizerManager::setLineWidth(const float value) { mParams->mLineWidth = value; }
void PathVisualizerManager::setHiddenLineOpacity(const float value) { mParams->mHiddenLineOpacity = value; }
void PathVisualizerManager::setUseSceneSamples(const bool useSceneSamples, const bool update)
{ 
    mParams->mUseSceneSamples = useSceneSamples; 
    if (update) { startSimulation(); }
}
void PathVisualizerManager::setPixelSamples(const uint32_t samples, const bool update)
{ 
    mParams->mPixelSamples = samples * samples; 
    if (update) { startSimulation(); }
}
void PathVisualizerManager::setLightSamples(const uint32_t samples, const bool update)
{ 
    mParams->mLightSamples = samples * samples; 
    if (update) { startSimulation(); }
}
void PathVisualizerManager::setBsdfSamples(const uint32_t samples, const bool update)
{ 
    mParams->mBsdfSamples = samples * samples; 
    if (update) { startSimulation(); }
}

void PathVisualizerManager::setSelectedNode(const int nodeIndex) { mSelectedNode = nodeIndex; }

//------------------------------------------------------------------------------------------

void
PathVisualizerManager::setupSimGlobalInfo(scene_rdl2::grid_util::PathVisSimGlobalInfo& globalInfo) const
{
    globalInfo.setPathVisActive(isOn());
    if (!isOn()) return;
    
    globalInfo.setSamples(mParams->mPixelX, mParams->mPixelY, mParams->mMaxDepth,
                          mParams->mPixelSamples, mParams->mLightSamples, mParams->mBsdfSamples);
    globalInfo.setRayTypeSelection(mParams->mUseSceneSamples,
                                   mParams->mDirectRaysOn,
                                   mParams->mIndirectSpecularRaysOn,
                                   mParams->mIndirectDiffuseRaysOn,
                                   mParams->mDirectDiffuseRaysOn || mParams->mDirectSpecularRaysOn,
                                   mParams->mDirectLightRaysOn);
    globalInfo.setColor(mParams->mCameraRayColor,
                        mParams->mIndirectSpecularRayColor,
                        mParams->mIndirectDiffuseRayColor,
                        mParams->mDirectDiffuseRayColor,
                        mParams->mDirectLightRayColor);
    globalInfo.setLineWidth(mParams->mLineWidth);
}

bool
PathVisualizerManager::getCamPos(scene_rdl2::math::Vec3f& camPos) const
{
    return mPathVisualizer->getCamPos(camPos);
}

std::vector<scene_rdl2::math::Vec3f>
PathVisualizerManager::getCamRayIsectSfPos() const
{
    return mPathVisualizer->getCamRayIsectSfPos();
}

size_t
PathVisualizerManager::serializeNodeDataAll(std::string& buff) const
//
// Return non-zero data size even if node total and vtx total both are zero,
// because the data size is encoded.
//
{
    return mPathVisualizer->serializeNodeDataAll(buff);
}

//------------------------------------------------------------------------------------------

void
PathVisualizerManager::parserConfigure()
{
    mParser.description("PathVisualizerManager command");

    mParser.opt("pathVis", "...command...", "pathVisualizer command",
                [&](Arg& arg) { return mPathVisualizer->getParser().main(arg.childArg()); });
    mParser.opt("param", "...command...", "parameters",
                [&](Arg& arg) { return mParams->getParser().main(arg.childArg()); });
    // Ideally, we would update the debug console command name to showRecorderCamXform,
    // but we are intentionally keeping it for now so that the arras path visualizer
    // can still operate until we can make the appropriate changes in arras.
    mParser.opt("showInitCamXform", "", "show recorderCameraXform",
                [&](Arg& arg) { return arg.msg(showRecorderCameraXform() + '\n'); });
}

std::string
PathVisualizerManager::showRecorderCameraXform() const
{
    auto showMtx = [](const scene_rdl2::rdl2::Mat4d& mtx) {
        auto showF = [](const float f) {
            std::ostringstream ostr;
            ostr << std::setw(10) << std::fixed << std::setprecision(5) << f;
            return ostr.str();
        };
        std::ostringstream ostr;
        ostr << showF(mtx.vx.x) << ", " << showF(mtx.vx.y) << ", " << showF(mtx.vx.z) << ", " << showF(mtx.vx.w) << '\n'
             << showF(mtx.vy.x) << ", " << showF(mtx.vy.y) << ", " << showF(mtx.vy.z) << ", " << showF(mtx.vy.w) << '\n'
             << showF(mtx.vz.x) << ", " << showF(mtx.vz.y) << ", " << showF(mtx.vz.z) << ", " << showF(mtx.vz.w) << '\n'
             << showF(mtx.vw.x) << ", " << showF(mtx.vw.y) << ", " << showF(mtx.vw.z) << ", " << showF(mtx.vw.w);
        return ostr.str();
    };

    std::ostringstream ostr;
    ostr << "mRecorderCameraXform {\n"
         << scene_rdl2::str_util::addIndent(showMtx(mRecorderCameraXform)) + '\n'
         << "}";
    return ostr.str();
}

} // end namespace rndr
} // end namespace moonray
