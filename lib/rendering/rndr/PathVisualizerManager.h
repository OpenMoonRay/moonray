// Copyright 2025-2026 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>

#include <scene_rdl2/common/fb_util/FbTypes.h>
#include <scene_rdl2/common/grid_util/Arg.h>
#include <scene_rdl2/common/grid_util/Parser.h>
#include <scene_rdl2/common/grid_util/VectorPacket.h>
#include <scene_rdl2/common/math/Math.h>
#include <scene_rdl2/common/rec_time/RecTime.h>
#include <scene_rdl2/scene/rdl2/Geometry.h>

namespace scene_rdl2 { 
    namespace grid_util { class PathVisSimGlobalInfo; } 
    namespace math { class Color; }
    namespace rdl2 { class SceneVariables; }
}

namespace moonray {

namespace pbr { class Scene; class PathVisualizer; struct PathVisualizerParams; }

namespace rndr {

class RenderContext;

/// Manages the PathVisualizer object. The PathVisualizer doesn't exist (is nullptr) until the user
/// specifies that they want to start recording ray data. Until that time, the PathVisualizerManager
/// simply handles the parameters set by the user. The mTriggered member variable tells us whether the 
/// user specified we should start recording data for the PathVisualizer. During RenderContext::startFrame(), 
/// if mTriggered is true, we initialize the PathVisualizer object with the given parameters.
class PathVisualizerManager {
public:
    using Arg = scene_rdl2::grid_util::Arg;
    using Parser = scene_rdl2::grid_util::Parser; 
    using PosType = scene_rdl2::grid_util::VectorPacketLineStatus::PosType;

    PathVisualizerManager(RenderContext* renderContext);
    ~PathVisualizerManager();

    /// Checks if mTrigger is true -- if so, create the PathVisualizer object
    void initialize(const scene_rdl2::rdl2::SceneVariables& vars, pbr::Scene* scene);

    // ----------------------------------------------------------------------------

    // Starts the recording process.
    void startSimulation();

    // Sets the state to RECORD.
    // Indicates that we have started recording ray data.
    void setRecordState();

    // Sets the state to STOP_RECORD
    void stopSimulation();

    // Requests that the gui draw the visualization
    // This forces a draw, whereas the DRAW state
    // indicates that we CAN draw
    void requestDraw();

    // Creates the line segments, and sets state to DRAW
    void generateLines();

    /// Draws the visualization
    void draw(scene_rdl2::fb_util::RenderBuffer* renderBuffer);

    void printStats() const;

    using CrawlLineFunc = std::function<void(const scene_rdl2::math::Vec2i& s,
                                             const scene_rdl2::math::Vec2i& e,
                                             const uint8_t& flags,
                                             const float a,
                                             const float w,
                                             const bool drawEndPoint,
                                             const size_t nodeId,
                                             const PosType startPosType,
                                             const PosType endPosType)>;
    void crawlAllLines(const CrawlLineFunc& func);
    size_t getTotalLines() const;

    scene_rdl2::math::Color getColorByFlags(const uint8_t& flags) const;
    static scene_rdl2::grid_util::VectorPacketLineStatus::RayType flagsToRayType(const uint8_t& flags);

    // Indicates whether we need to restart rendering after recording ray data
    void setNeedsRenderRefresh(bool refresh);

    // Clear all PathVisualizer data
    void reset();

    // Each of these functions fills in the given variables with user-specified values,
    // but only if useSceneSamples is false. 
    void fillPixelSamples(int& samples) const;
    void fillLightSamples(int& samples) const;
    void fillBsdfSamples(int& samples) const;
    void fillMaxDepth(int& samples) const;

    void turnOn();
    void turnOff();
    void setRecorderCameraXform(const scene_rdl2::rdl2::Mat4d& xform);
    void setCachedCameraXform(const scene_rdl2::rdl2::Mat4d& xform);
    void setCameraXformWasCached(bool cached);

    // ----------------------------------------------------------------------------

    // Whether the PathVisualizer is turned on
    bool isOn() const;

    // Whether PathVisualizer has not been initialized
    bool isInNoneState() const;

    // Whether PathVisualizer is initialized
    bool isInReadyState() const;

    // Whether we should start recording
    bool isInStartRecordState() const;

    // Whether the PathVisualizer is recording
    bool isInRecordState() const;

    // Whether the PathVisualizer needs to stop recording
    bool isInStopRecordState() const;

    // Whether the gui needs to forcibly draw the visualization
    bool isDrawRequested() const;

    // Whether the PathVisualizer is ready for drawing
    bool isInDrawState() const;

    // Whether we need to restart rendering after recording is done
    bool getNeedsRenderRefresh() const;

    /// Check if the PathVisualizer has been created
    bool getPathVisualizerExists() const;

    /// Whether visualizer is gathering data, not yet ready for draw
    bool isProcessing() const;

    scene_rdl2::math::Vec2i getPixel() const;

    scene_rdl2::math::Mat4d getRecorderCameraXform() const;
    scene_rdl2::math::Mat4d getCachedCameraXform() const;
    bool getCameraXformWasCached() const;

    // Selects the next/previous node by incrementing/decrementing 
    // the mSelectedNode index. An index of -1 indicates that no 
    // node is selected. When the node is less than 0 or greater
    // than the size of the nodes vector, we reset the selected node
    // to -1 (no node selected state).
    void nextNode();
    void prevNode();

    // Returns a formatted string containing information about the given node
    std::string getNodeInfo(const size_t nodeIndex) const;

    // Indicates whether the given node is currently selected.
    // If mSelectedNode == -1, it means no node is selected.
    // However, since this function is primarily used for drawing lines,
    // we treat -1 as a wildcard, so that all lines are drawn
    // in a "no node selected" state.
    bool isSelectedNode(const int nodeIndex) const;

    /// ------------------------- UI getters --------------------------------- //

    uint32_t getPixelX() const;
    uint32_t getPixelY() const;
    uint32_t getMaxDepth() const;

    bool isSample(const uint8_t flags) const;

    // Whether to show the ray with the given flag
    // based on the visibility flags
    bool showRay(const uint8_t& flag) const;

    bool getShowDirectRays() const;
    bool getShowIndirectRays() const;
    bool getShowSamples() const;
    bool getShowIndirectSpecularRays() const;
    bool getShowIndirectDiffuseRays() const;
    bool getShowDirectSpecularRays() const;
    bool getShowDirectDiffuseRays() const;
    bool getShowDirectLightRays() const;
    bool getShowDiffuseSamples() const;
    bool getShowSpecularSamples() const;
    bool getShowLightSamples() const;

    const scene_rdl2::math::Color& getCameraRayColor() const;
    const scene_rdl2::math::Color& getIndirectSpecularRayColor() const;
    const scene_rdl2::math::Color& getIndirectDiffuseRayColor() const;
    const scene_rdl2::math::Color& getDirectSpecularRayColor() const;
    const scene_rdl2::math::Color& getDirectDiffuseRayColor() const;
    const scene_rdl2::math::Color& getDirectLightRayColor() const;

    float getMaxRayLength() const;
    float getLineWidth() const;
    float getHiddenLineOpacity() const;

    bool getUseSceneSamples() const;
    uint32_t getPixelSamples() const;
    uint32_t getLightSamples() const;
    uint32_t getBsdfSamples() const;

    uint32_t getSelectedNode() const;

    /// ------------------------- UI setters --------------------------------- //

    void setPixelX(uint32_t px, bool update = false);
    void setPixelY(uint32_t py, bool update = false);
    void setPixel(uint32_t px, uint32_t py, bool update = false);
    void setMaxDepth(int depth, bool update = false);

    void setShowDirectRays(bool flag);
    void setShowIndirectRays(bool flag);
    void setShowSamples(bool flag);
    void setShowIndirectSpecularRays(bool flag);
    void setShowIndirectDiffuseRays(bool flag);
    void setShowDirectSpecularRays(bool flag);
    void setShowDirectDiffuseRays(bool flag);
    void setShowDirectLightRays(bool flag);
    void setShowDiffuseSamples(bool flag);
    void setShowSpecularSamples(bool flag);
    void setShowLightSamples(bool flag);

    void setCameraRayColor(scene_rdl2::math::Color color);
    void setIndirectSpecularRayColor(scene_rdl2::math::Color color);
    void setIndirectDiffuseRayColor(scene_rdl2::math::Color color);
    void setDirectSpecularRayColor(scene_rdl2::math::Color color);
    void setDirectDiffuseRayColor(scene_rdl2::math::Color color);
    void setDirectLightRayColor(scene_rdl2::math::Color color);

    void setMaxRayLength(float length, bool update = false);
    void setLineWidth(const float value);
    void setHiddenLineOpacity(float value);

    void setUseSceneSamples(bool useSceneSamples, bool update = false);
    void setPixelSamples(uint32_t samples, bool update = false);
    void setLightSamples(uint32_t samples, bool update = false);
    void setBsdfSamples(uint32_t samples, bool update = false);

    void setSelectedNode(int nodeIndex);

    /// ------------------------------

    void setupSimGlobalInfo(scene_rdl2::grid_util::PathVisSimGlobalInfo& globalInfo) const;
    bool getCamPos(scene_rdl2::math::Vec3f& camPos) const;
    std::vector<scene_rdl2::math::Vec3f> getCamRayIsectSfPos() const;

    size_t serializeNodeDataAll(std::string& buff) const;

    /// ------------------------------

    Parser& getParser() { return mParser; }

private:
    void parserConfigure();
    std::string showRecorderCameraXform() const;

    //------------------------------

    // pointer to the PathVisualizer object
    std::unique_ptr<pbr::PathVisualizer> mPathVisualizer;

    RenderContext* mRenderContext;

    // Collection of user parameters
    std::unique_ptr<pbr::PathVisualizerParams> mParams;

    // Pointer to the Scene -- nullptr until we initialize the PathVisualizer
    pbr::Scene* mScene;

    // Timing statistics variables
    scene_rdl2::rec_time::RecTime mSimulationRecTime;
    float mSimulationTime;
    float mGenerateLinesTime;

    Parser mParser;

    // Has the user turned the visualizer on?
    bool mOn;

    // Store the camera xform to use during simulation rendering,
    // when recording path visualizer information
    scene_rdl2::rdl2::Mat4d mRecorderCameraXform;

    // The cached navigation camera xform, which is stored when 
    // a simulation is started. This is used to restore the navigation
    // camera position after the simulation is done.
    scene_rdl2::math::Mat4d mCachedCameraXform;

    // Whether the navigation camera xform has been cached
    bool mCameraXformWasCached;

    // Index of the currently selected node
    // -1 indicates that no node is selected
    int mSelectedNode {-1};
};

} // end namespace rndr
} // end namespace moonray
