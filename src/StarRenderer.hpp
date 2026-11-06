#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>

#include "gl/ComputeProgram.hpp"
#include "gl/SSBO.hpp"
#include "gl/gl_context.hpp"
#include "gl/GpuTexture.hpp"
#include "newstar.hpp"
#include "preprocessing/gpu_datatypes.hpp"

#include <RawStar.hpp>

namespace newstar {

struct Viewport {
    uint32_t id;
    glm::uvec2 windowSize;
    
};

void initialize();

class StarRenderer {
public:
    //TODO add chunk size as test parameter (limit not on distance in chunks, but in parsecs)
    StarRenderer(glm::uvec2 const& screenSize);
    void preprocessStars(std::vector<RawStar> const& rawStars);
    void prepareGpuBuffers();

    //Call when the viewport size changes (before the next run()). Reallocates all
    //screen-size dependent textures/buffers and recompiles the shaders whose
    //#defines encode the tile grid. Zero-sized requests (e.g. minimized window) are ignored.
    //The caller remains responsible for glViewport of the final composition pass.
    void resize(glm::uvec2 const& newScreenSize);
    glm::uvec2 const& screenSize() const { return mScreenSize; }

    StarRendererConfig& config();

    ~StarRenderer();

    void run(
        glm::mat4 modelViewMatrix,
        glm::mat4 inverseModelViewMatrix,
        glm::mat4 projectionMatrix,
        glm::mat4 inverseProjectionMatrix
    );

private:
    glm::uvec2 mScreenSize;
    glm::uvec2 mTileCount;
    size_t mTileCount_flat;
    size_t mChunkCount = 0;
    bool mBuffersPrepared = false;

    StarRendererConfig mConfig;
    StarRendererConfig mPreviousFrameConfig;

private:
    ComputeProgram mClearProgram;
    ComputeProgram mDrawListClearProgram;
    ComputeProgram mDrawListProgram;
    ComputeProgram mRasterProgram;
    ComputeProgram mDebugStatsProgram;
    ComputeProgram mDebugVisualsProgram;

    //no ComputeProgram, so manually handles (ComputeProgram wrapper class not applicable)
    GLuint mScreenQuadProgram;
    GLuint mScreenQuadVAOHandle;

private: //framebuffers
    GpuTexture mAccumulationTexture; //for accumulating interleaved 32bit luminance-weighted temperature + 32 bit luminance
    GpuTexture mFrameTexture;
private:
    SSBO mGpuJobsSSBO; //TODOR: resort binding id order
    SSBO mGpuDispatchHeaderSSBO;
    SSBO mGpuAccumSSBO;
    SSBO mGpuChunkMetaSSBO;
    SSBO mGpuBatchRowsFlatSSBO;
    SSBO mGpuBatchRowsHighPrecisionFlatSSBO;

    SSBO mGpuProfilingSSBO;
    GpuProfilingStruct mProfilingStruct{};

private:
    void recompileDrawListProgram();
    void recompileRasterProgram();

    //(re)allocate the buffers whose size depends on screen size / tile count
    void allocateScreenDependentBuffers();
    glm::uvec2 accumulationTextureSize() const;
    std::unordered_map<std::string, std::optional<size_t>> drawListClearDefines() const;
    std::unordered_map<std::string, std::optional<size_t>> debugVisualsDefines() const;
    void drawDebugOverlay(std::string const& text, glm::ivec2 offset);

    //build #define maps for the programs that require them
    //used for rebuilding them at runtime, when corresponding config change triggers this
    std::unordered_map<std::string, std::optional<size_t>> drawListDefines() const;
    std::unordered_map<std::string, std::optional<size_t>> rasterDefines() const;

private:
    struct {
        struct {
            GLuint uResolution;
            GLuint uAccumulationTex;
            GLuint uHdrEnabled;
            GLuint uDebugTex;
        } screenQuad;
    } mProgramUniformLocations;
};

}