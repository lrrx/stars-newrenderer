#include "StarRenderer.hpp"
#include "newstar.hpp"
#include <cstdlib>
#include <optional>
#include <string>

#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif

#include <cstdint>
#include <cstddef>
#include <iostream>
#include <glm/glm.hpp>
#include <glm/gtx/string_cast.hpp>

#include "gl/shader_utils.hpp"
#include "gl/GpuStopwatch.hpp"

#include "preprocessing/gpu_datatypes.hpp"
#include "preprocessing/preprocessing.hpp"

//#define REBUILD_CACHE

#ifdef REBUILD_CACHE
#include "stars/stars.hpp"
#endif

#include <RawStar.hpp>
#include "util/glfw_keymap.hpp"
#include "util/generate_debug_string.hpp"
#include "util/time.hpp"

#include <log/log.hpp>

namespace newstar {

void initialize() {
    int version = gladLoaderLoadGLContext(&detail::g_gl_context);
    if (!version) {
        throw std::runtime_error("newstar: gladLoaderLoadGLContext failed");
    }

    ::newstar::log() << "newstar: glad initialized" << std::endl;

    return;
}

// if greater than 1, we create multiple copies of the screen buffer and distribute writes accross them (uniformly random) when rasterizing to the screen
// due to how memory banks work, this should not have any major impact (couldn't measure any, except perhaps worse if too large)
constexpr size_t GPU_ACCUMULATION_LANES = 2;

// for tile-based rendering, the screen is divided into x * y tiles of GPU_TILE_SIZE_PX px size
constexpr size_t GPU_TILE_SIZE_PX = 64;

// max chunks bundled per job. With MAX_CHUNK_ROWS = 8 on the CPU side
// (see datatypes.cpp gpuSerialize), peak rows per job = 8 * 16 = 128.
constexpr size_t GPU_MAX_JOB_CHUNKS = 10;

// total jobs across the screen per frame (primaries + overflow). Sized so the
// SSBO is the source of truth for capacity; the shaders' MAX_JOBS define and
// the buffer allocation both derive from this.
constexpr size_t GPU_MAX_JOBS = 100000;

/*double pixel_world_space_width(double dist) {
    return 2.0 * dist / (960.0); //tan(45°) = 1
}*/ //TODO: use this to generate dynamic LOD levels, for now we assume fixed precision

std::unordered_map<std::string, std::optional<size_t>> StarRenderer::drawListDefines() const {
    return {
        {"MAX_JOB_CHUNKS", GPU_MAX_JOB_CHUNKS}, //TODOR: pass non-debug-code constants as uniforms to avoid recompilation on screen resize
        {"TILE_SIZE", GPU_TILE_SIZE_PX}, //TODOR: compare performance between both approaches
        {"NUM_PRIMARY_TILES", mTileCount_flat},
        {"TILES_X", mTileCount.x},
        {"TILES_Y", mTileCount.y},
        {"MAX_JOBS", GPU_MAX_JOBS},
        {"HIGH_LOD_PX_THRESHOLD", mConfig.drawlist.HIGH_LOD_PX_THRESHOLD},
        {mConfig.drawlist.DEBUG_GRID_OVERLAY ? "DEBUG_GRID_OVERLAY" : "", std::nullopt},
        {mConfig.drawlist.DEBUG_VISUALIZE_BINNING ? "DEBUG_VISUALIZE_BINNING" : "", std::nullopt},
        {mConfig.drawlist.DEBUG_CULLING_MINIMAP ? "DEBUG_CULLING_MINIMAP" : "", std::nullopt},
        {mConfig.drawlist.DEBUG_VISUALIZE_LOD_RADIUS ? "DEBUG_VISUALIZE_LOD_RADIUS" : "", std::nullopt},
    };
}

std::unordered_map<std::string, std::optional<size_t>> StarRenderer::rasterDefines() const {
    return {
        {"MAX_JOB_CHUNKS", GPU_MAX_JOB_CHUNKS}, //TODOR: pass non-debug-code constants as uniforms to avoid recompilation on screen resize
        {"THREAD_COUNT", newstar::static_config::GPU_THREAD_COUNT},
        {"TILE_SIZE", GPU_TILE_SIZE_PX},
        {"TILES_X", mTileCount.x},
        {"TILES_Y", mTileCount.y},
        {"MAX_JOBS", GPU_MAX_JOBS},
        {"ACCUMULATION_LANES", GPU_ACCUMULATION_LANES}, //TODOR: comment or delete (not required from how memory banks work)
        {mConfig.raster.RASTERIZE_USE_TILE_ACCUMULATION ? "RASTERIZE_USE_TILE_ACCUMULATION" : "", std::nullopt},
        {mConfig.raster.WRITE_PROFILING_COUNTERS ? "WRITE_PROFILING_COUNTERS" : "", std::nullopt},
        {mConfig.raster.ENABLE_LUMI_THRESHOLD ? "ENABLE_LUMI_THRESHOLD" : "", std::nullopt},
    };
}

StarRenderer::StarRenderer(glm::uvec2 const& screenSize)
: mScreenSize{screenSize},
mTileCount{glm::uvec2(glm::ceil(glm::vec2(mScreenSize) / glm::vec2(GPU_TILE_SIZE_PX)))},
mTileCount_flat{mTileCount.x * mTileCount.y},
mConfig{},
mClearProgram{"clear.comp", {}},
mDrawListClearProgram{"drawlist_clear.comp", {
            {"MAX_JOB_CHUNKS", GPU_MAX_JOB_CHUNKS},
            {"NUM_PRIMARY_TILES", mTileCount_flat}
        }},
mDrawListProgram{"drawlist.comp", drawListDefines()},
mRasterProgram{"rasterize.comp", rasterDefines()},
mDebugStatsProgram{"debugstats.comp", {}},
mDebugVisualsProgram{"debugvisuals.comp", {
        {"MAX_JOB_CHUNKS", GPU_MAX_JOB_CHUNKS},
        {"NUM_PRIMARY_TILES", mTileCount_flat},
        {"TILE_SIZE", GPU_TILE_SIZE_PX}
    }},
 mAccumulationTexture{(mScreenSize * glm::uvec2(2,1) //TODOR: comment?
     * glm::uvec2(1, GPU_ACCUMULATION_LANES)), GpuTexture::Format::R32F}, //TODOR for COMMENT: twice as wide, interleaved 2-value storage
 mFrameTexture{(mScreenSize), GpuTexture::Format::RGBA8}
{
    {
        mScreenQuadProgram = createProgramFromFiles("quad_blit.vert", "quad_blit.frag");
        
        auto& uniforms = mProgramUniformLocations.screenQuad;
        uniforms.uResolution = GL.GetUniformLocation(mScreenQuadProgram, "uResolution");
        uniforms.uAccumulationTex = GL.GetUniformLocation(mScreenQuadProgram, "uAccumulationTex");
        uniforms.uHdrEnabled = GL.GetUniformLocation(mScreenQuadProgram, "uHdrEnabled");
        uniforms.uDebugTex = GL.GetUniformLocation(mScreenQuadProgram, "uDebugTex");
    }
}

void StarRenderer::preprocessStars(std::vector<RawStar> const& rawStars) {
    //TODOR: decide cache rebuild at runtime (by checking file date, size maybe) instead of compiletime ifdef
#ifdef REBUILD_CACHE //TODOR: preprocessing csv -> .cache, .cache -> .bin all in cosmoscout, for user accessibility
    std::vector<RawStar> rs{};
    ::newstar::log() << "reading cache" << std::endl;
    stars::readStarCache("full_gaia_stars_icrs.cache", rs, true);
    ::newstar::log() << "rs.size() " << rs.size() << std::endl;

    std::vector<GpuChunkMeta> gpuChunkMetas;
    std::vector<uint32_t> gpuBatchRowsFlat;
    std::vector<uint32_t> gpuBatchRowsHighPrecisionFlat;

    preprocessing::run(rs, gpuChunkMetas, gpuBatchRowsFlat, gpuBatchRowsHighPrecisionFlat);    //upload data to GPU
    mGpuChunkMetaSSBO.create(gpuChunkMetas, GL_STATIC_DRAW, "chunks_galactic.bin");
    mGpuBatchRowsFlatSSBO.create(gpuBatchRowsFlat, GL_STATIC_DRAW, "rowsFlat_galactic.bin");
    mGpuBatchRowsHighPrecisionFlatSSBO.create(gpuBatchRowsHighPrecisionFlat, GL_STATIC_DRAW, "rowsHighPrecisionFlat_galactic.bin");

#else
    mGpuChunkMetaSSBO.loadFromFile<GpuChunkMeta>("chunks_galactic.bin", GL_STATIC_DRAW);
    mGpuBatchRowsFlatSSBO.loadFromFile<uint32_t>("rowsFlat_galactic.bin", GL_STATIC_DRAW);
    mGpuBatchRowsHighPrecisionFlatSSBO.loadFromFile<uint32_t>("rowsHighPrecisionFlat_galactic.bin", GL_STATIC_DRAW);
#endif
    mChunkCount = mGpuChunkMetaSSBO.count();
}

void StarRenderer::prepareGpuBuffers() {
    GL.GenVertexArrays(1, &mScreenQuadVAOHandle);

    GL.GenBuffers(1, &mGpuDispatchHeaderSSBO);
    GL.BindBuffer(GL_SHADER_STORAGE_BUFFER, mGpuDispatchHeaderSSBO);
    // 16 B indirect-dispatch header (x,y,z,pad) + one tip pointer per primary tile.
    // The binning shader uses dh.tip_pointers[tileID_flat] to find each tile's
    // current write target, so the array must be sized to NUM_PRIMARY_TILES.
    size_t headerBytes = 16 + mTileCount_flat * sizeof(uint32_t);
    GL.BufferData(GL_SHADER_STORAGE_BUFFER, headerBytes, nullptr, GL_DYNAMIC_DRAW);

    GL.GenBuffers(1, &mGpuJobsSSBO);
    GL.BindBuffer(GL_SHADER_STORAGE_BUFFER, mGpuJobsSSBO);
    // Each Job = 16 B header (count, id_per_tile, tileID_flat, pad)
    //          + 16 B per ChunkMeta entry * GPU_MAX_JOB_CHUNKS.
    size_t const jobStrideBytes = 16 + 16 * GPU_MAX_JOB_CHUNKS;
    GL.BufferData(GL_SHADER_STORAGE_BUFFER,
        jobStrideBytes * GPU_MAX_JOBS * 10,
        nullptr, GL_DYNAMIC_COPY);

    GL.GenBuffers(1, &mGpuProfilingSSBO);
    GL.BindBuffer(GL_SHADER_STORAGE_BUFFER, mGpuProfilingSSBO);
    GL.BufferData(GL_SHADER_STORAGE_BUFFER, sizeof(GpuProfilingStruct),
                nullptr, GL_DYNAMIC_READ);

    size_t accumElems = size_t(mScreenSize.x) * 2 * size_t(mScreenSize.y) * GPU_ACCUMULATION_LANES;
    GL.GenBuffers(1, &mGpuAccumSSBO);
    GL.BindBuffer(GL_SHADER_STORAGE_BUFFER, mGpuAccumSSBO);
    GL.BufferData(GL_SHADER_STORAGE_BUFFER, accumElems * sizeof(int32_t), nullptr, GL_DYNAMIC_COPY);    
}

void StarRenderer::run(
    glm::mat4 modelViewMatrix,
    glm::mat4 inverseModelViewMatrix,
    glm::mat4 projectionMatrix,
    glm::mat4 inverseProjectionMatrix,
    float luminanceMultiplicator,
    bool hdrEnabled) {  
    GL.Disable(GL_DEPTH_TEST);
    
    // calculate camera matrices
    glm::mat4 const& uMatMV = modelViewMatrix;
    glm::mat4 const& uMatP = projectionMatrix;

    //invert in high precision to better deal with bad conditioning of matrix
    glm::mat4 const uInvMV = inverseModelViewMatrix;
    glm::mat4 const uInvP = inverseProjectionMatrix;

    glm::mat4 const uViewProj = uMatP * uMatMV;

    constexpr float parsecToMeter = 3.08567758e16;
    glm::vec3 cameraPosParsec = glm::vec3(uInvMV * glm::vec4(0,0,0,1)) / parsecToMeter;

    static glm::mat4 const icrsCorrectionMatrix = glm::mat4(1.f);//no correction if we just use given cosmoscout data//glm::inverse(stars::R_icrs_to_gal);

    GL.BindImageTexture(0, mAccumulationTexture.handle(), 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32F);
    GL.BindImageTexture(1, mFrameTexture.handle(), 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA8);

    static GpuStopwatch sw{};


    //// PASS: clear screen ////
    {
        sw.startTiming("clear");
        mClearProgram.bind();
        mClearProgram.setIVec2("uResolution", glm::ivec2{mScreenSize});
        GL.DispatchCompute((mScreenSize.x + 7) / 8, (mScreenSize.y + 7) / 8, 1);
        GL.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        sw.endTiming("clear");
    }

    //// SSBO BINDING ////
    {
        sw.startTiming("bindings");
        mGpuBatchRowsFlatSSBO.bind(0);
        mGpuBatchRowsHighPrecisionFlatSSBO.bind(6);
        mGpuChunkMetaSSBO.bind(1);
        GL.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, mGpuJobsSSBO); //generated on-gpu (gpu-driven rendering)
        GL.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, mGpuDispatchHeaderSSBO);
        GL.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, mGpuAccumSSBO);
        sw.endTiming("bindings");
    }

    //// PASS: clear draw list ////
    {
        sw.startTiming("drawlist_clear");
        mDrawListClearProgram.bind();
        // One thread per primary tile to seed count/tileID + tip_pointers[tile] = tile.
        GL.DispatchCompute(GLuint((mTileCount_flat + 63) / 64), 1, 1);
        GL.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        sw.endTiming("drawlist_clear");
    }

    static double t_start = util::time_now();

    //// PASS: generate draw list ////
    {
        sw.startTiming("drawlist");
        mDrawListProgram.bind();
        mDrawListProgram.setFloat("uTime", static_cast<float>(util::time_now() - t_start));
        mDrawListProgram.setUint("uChunkCount", static_cast<uint32_t>(mChunkCount));
        mDrawListProgram.setFloat("uChunkSize", static_config::CHUNK_SIZE_PARSECS);
        mDrawListProgram.setIVec2("uResolution", glm::ivec2{mScreenSize});
        mDrawListProgram.setUint("uShowMask", mConfig.drawlist.CHUNK_SHOW_MASK);
        mDrawListProgram.setUint("uMAX_JOB_WORK", mConfig.drawlist.MAX_JOB_WORK);
        mDrawListProgram.setMat4("uMatModel", icrsCorrectionMatrix);
        mDrawListProgram.setMat4("uViewProj", uViewProj);
        mDrawListProgram.setMat4("uInvP", uInvP);
        GL.DispatchCompute((mChunkCount + 63) / 64, 1, 1); //for each screentile
        GL.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT);
        sw.endTiming("drawlist");
    }

    { // prepare profiling ssbo
        GL.BindBuffer(GL_SHADER_STORAGE_BUFFER, mGpuProfilingSSBO);
        const uint32_t zero = 0;
        GL.ClearBufferData(GL_SHADER_STORAGE_BUFFER, GL_R32UI,
                        GL_RED_INTEGER, GL_UNSIGNED_INT, &zero);
        GL.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, mGpuProfilingSSBO);
    }

    //// PASS: workload-per-tile overlay (toggle with B) ////
    // Reads dh.tip_pointers + joblist tip jobs, writes a per-tile heat bar
    // into mFrameTexture. Sits between drawlist (which sets up the chain) and
    // raster (which is read-only on the chain). The drawlist barrier above
    // already publishes the SSBO writes; we add an image-access barrier after
    // so raster's own debug-overlay writes see a settled framebuffer.
    if (mConfig.WORKLOAD_OVERLAY_ENABLED) {
        sw.startTiming("debugworkload");
        mDebugVisualsProgram.bind();

        mDebugVisualsProgram.setIVec2("uResolution", glm::ivec2{mScreenSize});
        // Chunk count that saturates the heatmap. 4 chains deep is a useful
        // default: green = primary fits, yellow = some overflow, red = hot.
        mDebugVisualsProgram.setUint("uWorkloadFullChunks", static_cast<uint32_t>(4 * GPU_MAX_JOB_CHUNKS));
        GL.DispatchCompute(GLuint((mTileCount_flat + 63) / 64), 1, 1);
        GL.MemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        sw.endTiming("debugworkload");
    }


    //// PASS: main rasterization ////
    {
        sw.startTiming("raster");
        mRasterProgram.bind();

        mRasterProgram.setUint("uLumiThresholdEnabled", static_cast<uint32_t>(mConfig.raster.LUMI_THRESHOLD_ENABLED));
        mRasterProgram.setVec3("uObserverPosWorld", cameraPosParsec);
        mRasterProgram.setIVec2("uResolution", glm::ivec2{mScreenSize});
        mRasterProgram.setFloat("uChunkSize", newstar::static_config::CHUNK_SIZE_PARSECS);
        mRasterProgram.setMat4("uViewProj", uViewProj);
        mRasterProgram.setMat4("uInvP", uInvP);
        mRasterProgram.setMat4("uMatModel", icrsCorrectionMatrix);
        mRasterProgram.setUint("uStartLayer", 0u);
        mRasterProgram.setUint("uLayerCount", static_cast<uint32_t>(GPU_MAX_JOB_CHUNKS));
        ::newstar::log() << "newrenderer uniform: " << luminanceMultiplicator << std::endl;
        mRasterProgram.setFloat("uLuminanceMultiplicator", luminanceMultiplicator * mConfig.raster.DEBUG_LUMI_FACTOR);

        //indirect dispatch
        GL.BindBuffer(GL_DISPATCH_INDIRECT_BUFFER, mGpuDispatchHeaderSSBO);
        GL.DispatchComputeIndirect(0);   // x/y/z read from offset 0 of the bound buffer
        GL.MemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

        sw.endTiming("raster");
    }

    uint32_t gpuJobDispatchCount = 0;

    { // read back profiling ssbo from GPU
        GL.GetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                   sizeof(GpuProfilingStruct), &mProfilingStruct);
        GL.GetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
            sizeof(uint32_t), &gpuJobDispatchCount);
    }

    //// PASS: debug text overlay ////

    if(mConfig.DEBUG_OVERLAY_ENABLED)
    {
        std::string debugString;
        generateDebugString(sw.getAllMeasurements(), mProfilingStruct, cameraPosParsec, debugString);
        drawDebugOverlay(debugString, glm::ivec2{10, 30});

        std::string helpString = R"(KEYBOARD SHORTCUTS
1 HIGH_LOD_PX_THRESHOLD *= 2
2 HIGH_LOD_PX_THRESHOLD /= 2
G DEBUG_GRID_OVERLAY
M DEBUG_CULLING_MINIMAP
L DEBUG_VISUALIZE_LOD_RADIUS
B DEBUG_VISUALIZE_BINNING
3 toggle chunk group 1
4 toggle chunk group 2
5 toggle chunk group 3
B WORKLOAD_OVERLAY_ENABLED
T RASTERIZE_USE_TILE_ACCUM
P WRITE_PROFILING_COUNTERS
U ENABLE_LUMI_THRESHOLD
. DEBUG_LUMI_FACTOR *= 5.0;
; DEBUG_LUMI_FACTOR /= 5.0;
L LUMI_THRESHOLD_ENABLED
I DEBUG_OVERLAY_ENABLED
)";
        drawDebugOverlay(helpString, glm::ivec2{10, 600});
    }

    //// PASS: final onscreen composition pass ////
    {
        GL.UseProgram(mScreenQuadProgram);
        GL.BindVertexArray(mScreenQuadVAOHandle);

        auto const& uniforms = mProgramUniformLocations.screenQuad;

        GL.ActiveTexture(GL_TEXTURE0);
        GL.BindTexture(GL_TEXTURE_2D, mAccumulationTexture.handle());
        GL.ProgramUniform2i(mScreenQuadProgram, uniforms.uResolution, mScreenSize.x, mScreenSize.y);
        GL.ProgramUniform1i(mScreenQuadProgram, uniforms.uAccumulationTex, 0);
        GL.ProgramUniform1f(mScreenQuadProgram, uniforms.uHdrEnabled, hdrEnabled);
        
        GL.ActiveTexture(GL_TEXTURE1);
        GL.BindTexture(GL_TEXTURE_2D, mFrameTexture.handle());
        GL.ProgramUniform1i(mScreenQuadProgram, uniforms.uDebugTex, 1);

        GL.DrawArrays(GL_TRIANGLES, 0, 6);
    }

    //tidy up state so we don't leave TEXTURE0 or TEXTURE1 bound for later passes.
    GL.ActiveTexture(GL_TEXTURE1);
    GL.BindTexture(GL_TEXTURE_2D, 0);
    GL.ActiveTexture(GL_TEXTURE0);
    GL.BindTexture(GL_TEXTURE_2D, 0);
    GLenum e = GL.GetError();
    if(e != 0) ::newstar::err() << "GL err 0x" << std::hex << e << '\n';

    //handle config changes (TODO: implement for other parts of config)
    if(mConfig.raster != mPreviousFrameConfig.raster) {
        ::newstar::log() << "recompiling raster program" << std::endl;
        mRasterProgram.recompileFromFile("rasterize.comp", rasterDefines());
    };
    
    if(mConfig.drawlist != mPreviousFrameConfig.drawlist) {
        ::newstar::log() << "recompiling draw list program" << std::endl;
        mDrawListProgram.recompileFromFile("drawlist.comp", drawListDefines());
    }
    mPreviousFrameConfig = mConfig;
}

StarRenderer::~StarRenderer() {
    GL.DeleteBuffers(1, &mGpuJobsSSBO);

    GL.DeleteVertexArrays(1, &mScreenQuadVAOHandle);

    GL.DeleteProgram(mScreenQuadProgram);
}

void StarRenderer::drawDebugOverlay(std::string const& text, glm::ivec2 offset) {
    std::array<uint32_t, DEBUG_TEXT_BUFFER_WORDS> wordBuf;
    fillTextBufferPacked(text, wordBuf);

    mDebugStatsProgram.bind();

    mDebugStatsProgram.setIVec2("uResolution", glm::ivec2{mScreenSize});
    mDebugStatsProgram.setIVec2("uOffset", offset);
    mDebugStatsProgram.setUint("uLineCount", 32u);
    mDebugStatsProgram.setUintArray("uLineChars", wordBuf.data(), DEBUG_TEXT_BUFFER_WORDS);
    GL.DispatchCompute(32, 1, 1); //one work group per line, one thread per char
    GL.MemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    GL.Disable(GL_DEPTH_TEST);
    GL.Clear(GL_COLOR_BUFFER_BIT);
}

StarRendererConfig& StarRenderer::config() {
    return mConfig;
}

}