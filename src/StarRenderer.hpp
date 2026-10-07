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

// Host calls this once after making its GL context current,
// before constructing any newstar objects.
void initialize();

class StarRenderer {
public:
    //TODO add chunk size as test parameter (limit not on distance in chunks, but in parsecs)
    StarRenderer(glm::uvec2 const& screenSize); //TODO: pass this to renderer as well, handle necessary texture update there
    void preprocessStars(std::vector<RawStar> const& rawStars);
    void prepareGpuBuffers();

    StarRendererConfig& config();

    ~StarRenderer();

    void run(
        glm::mat4 modelViewMatrix,
        glm::mat4 inverseModelViewMatrix,
        glm::mat4 projectionMatrix,
        glm::mat4 inverseProjectionMatrix,
        float luminanceMultiplicator,
        bool hdrEnabled
    );

private:
    glm::uvec2 const mScreenSize; //TODO: handle viewport resizing
    glm::uvec2 const mTileCount;
    size_t const mTileCount_flat;
    size_t mChunkCount;

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
    GLuint mGpuJobsSSBO; //TODOR: resort binding id order
    GLuint mGpuDispatchHeaderSSBO;
    GLuint mGpuAccumSSBO;
    SSBO mGpuChunkMetaSSBO;
    SSBO mGpuBatchRowsFlatSSBO;
    SSBO mGpuBatchRowsHighPrecisionFlatSSBO;

    GLuint mGpuProfilingSSBO;
    GpuProfilingStruct mProfilingStruct{};

private:
    void recompileDrawListProgram();
    void recompileRasterProgram();
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