#include "StarRenderer.hpp"
#include <StaticGlobalConfig.hpp>
#include "newstar.hpp"
#include <optional>
#include <string>

#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif

#include <cstdint>
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

/*double pixel_world_space_width(double dist) {
    return 2.0 * dist / (960.0); //tan(45°) = 1
}*/ //TODO: use this to generate dynamic LOD levels, for now we assume fixed precision

std::unordered_map<std::string, std::optional<size_t>> StarRenderer::drawListDefines() const {
    return {
        {"MAX_JOB_CHUNKS", scfg().GPU_MAX_JOB_CHUNKS},
        {"TILE_SIZE", scfg().GPU_TILE_SIZE_PX},
        {"NUM_PRIMARY_TILES", mTileCount_flat},
        {"TILES_X", mTileCount.x},
        {"TILES_Y", mTileCount.y},
        {"MAX_JOBS", scfg().GPU_MAX_JOBS},
        {"HIGH_LOD_PX_THRESHOLD", mConfig.drawlist.HIGH_LOD_PX_THRESHOLD},
        {mConfig.drawlist.DEBUG_GRID_OVERLAY ? "DEBUG_GRID_OVERLAY" : "", std::nullopt},
        {mConfig.drawlist.DEBUG_VISUALIZE_BINNING ? "DEBUG_VISUALIZE_BINNING" : "", std::nullopt},
        {mConfig.drawlist.DEBUG_CULLING_MINIMAP ? "DEBUG_CULLING_MINIMAP" : "", std::nullopt},
        {mConfig.drawlist.DEBUG_VISUALIZE_LOD_RADIUS ? "DEBUG_VISUALIZE_LOD_RADIUS" : "", std::nullopt},
    };
}

std::unordered_map<std::string, std::optional<size_t>> StarRenderer::rasterDefines() const {
    return {
        {"MAX_JOB_CHUNKS", scfg().GPU_MAX_JOB_CHUNKS},
        {"THREAD_COUNT", scfg_h::GPU_THREAD_COUNT},
        {"TILE_SIZE", scfg().GPU_TILE_SIZE_PX},
        {"TILES_X", mTileCount.x},
        {"TILES_Y", mTileCount.y},
        {"MAX_JOBS", scfg().GPU_MAX_JOBS},
        {"ACCUMULATION_LANES", scfg().GPU_ACCUMULATION_LANES}, //TODOR: comment or delete (not required from how memory banks work)
        {mConfig.raster.RASTERIZE_USE_TILE_ACCUMULATION ? "RASTERIZE_USE_TILE_ACCUMULATION" : "", std::nullopt},
        {mConfig.raster.WRITE_PROFILING_COUNTERS ? "WRITE_PROFILING_COUNTERS" : "", std::nullopt},
        {mConfig.raster.ENABLE_LUMI_THRESHOLD ? "ENABLE_LUMI_THRESHOLD" : "", std::nullopt},
    };
}

namespace {
glm::uvec2 computeTileCount(glm::uvec2 const& screenSize) {
    uint32_t const t = static_cast<uint32_t>(scfg().GPU_TILE_SIZE_PX);
    return (screenSize + glm::uvec2(t - 1u)) / glm::uvec2(t); //ceil division
}
}

// Accumulation texture: twice as wide (interleaved 2-value storage: weighted temperature + luminance),
// stacked GPU_ACCUMULATION_LANES times vertically.
glm::uvec2 StarRenderer::accumulationTextureSize() const {
    return (mScreenSize * glm::uvec2(2, 1)) * glm::uvec2(1, scfg().GPU_ACCUMULATION_LANES);
}

std::unordered_map<std::string, std::optional<size_t>> StarRenderer::drawListClearDefines() const {
    return {
        {"MAX_JOB_CHUNKS", scfg().GPU_MAX_JOB_CHUNKS},
        {"NUM_PRIMARY_TILES", mTileCount_flat}
    };
}

std::unordered_map<std::string, std::optional<size_t>> StarRenderer::debugVisualsDefines() const {
    return {
        {"MAX_JOB_CHUNKS", scfg().GPU_MAX_JOB_CHUNKS},
        {"NUM_PRIMARY_TILES", mTileCount_flat},
        {"TILES_X", mTileCount.x},
        {"TILE_SIZE", scfg().GPU_TILE_SIZE_PX}
    };
}

StarRenderer::StarRenderer(glm::uvec2 const& screenSize)
: mScreenSize{screenSize},
mTileCount{computeTileCount(mScreenSize)},
mTileCount_flat{size_t(mTileCount.x) * mTileCount.y},
mConfig{},
mClearProgram{"clear.comp", {}},
mDrawListClearProgram{"drawlist_clear.comp", drawListClearDefines()},
mDrawListProgram{"drawlist.comp", drawListDefines()},
mRasterProgram{"rasterize.comp", rasterDefines()},
mDebugStatsProgram{"debugstats.comp", {}},
mDebugVisualsProgram{"debugvisuals.comp", debugVisualsDefines()},
 mAccumulationTexture{accumulationTextureSize(), GpuTexture::Format::R32F},
 mFrameTexture{mScreenSize, GpuTexture::Format::RGBA8}
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
    //std::vector<RawStar> const& rs = rawStars;
    std::vector<RawStar> rs;
    ::newstar::log() << "reading cache" << std::endl;
    stars::readStarCache("full_gaia_stars_icrs.cache", rs, true);
    ::newstar::log() << "rs.size() " << rs.size() << std::endl;

    std::vector<GpuChunkMeta> gpuChunkMetas;
    std::vector<uint32_t> gpuBatchRowsFlat;
    std::vector<uint32_t> gpuBatchRowsHighPrecisionFlat;

    preprocessing::run(rs, gpuChunkMetas, gpuBatchRowsFlat, gpuBatchRowsHighPrecisionFlat);    //upload data to GPU
    mGpuChunkMetaSSBO.allocateFromData(gpuChunkMetas, GL_STATIC_DRAW, "chunks_galactic.bin");
    mGpuBatchRowsFlatSSBO.allocateFromData(gpuBatchRowsFlat, GL_STATIC_DRAW, "rowsFlat_galactic.bin");
    mGpuBatchRowsHighPrecisionFlatSSBO.allocateFromData(gpuBatchRowsHighPrecisionFlat, GL_STATIC_DRAW, "rowsHighPrecisionFlat_galactic.bin");

#else
    mGpuChunkMetaSSBO.loadFromFile<GpuChunkMeta>("chunks_galactic.bin", GL_STATIC_DRAW);
    mGpuBatchRowsFlatSSBO.loadFromFile<uint32_t>("rowsFlat_galactic.bin", GL_STATIC_DRAW);
    mGpuBatchRowsHighPrecisionFlatSSBO.loadFromFile<uint32_t>("rowsHighPrecisionFlat_galactic.bin", GL_STATIC_DRAW);
#endif
    mChunkCount = mGpuChunkMetaSSBO.count();
}

// Buffers whose size depends on the screen size / tile grid.
void StarRenderer::allocateScreenDependentBuffers() {
    {
        // 16 B indirect-dispatch header (x,y,z,pad) + one tip pointer per primary tile.
        // The binning shader uses dh.tip_pointers[tileID_flat] to find each tile's
        // current write target, so the array must be sized to NUM_PRIMARY_TILES.
        size_t headerBytes = 16 + mTileCount_flat * sizeof(uint32_t);
        mGpuDispatchHeaderSSBO.allocateEmpty(headerBytes, GL_DYNAMIC_DRAW);
    }

    {
        size_t accumElems = size_t(mScreenSize.x) * 2 * size_t(mScreenSize.y) * scfg().GPU_ACCUMULATION_LANES;
        mGpuAccumSSBO.allocateEmpty(accumElems * sizeof(int32_t), GL_DYNAMIC_COPY);
    }
}

void StarRenderer::resize(glm::uvec2 const& newScreenSize) {
    if (newScreenSize.x == 0 || newScreenSize.y == 0) return; //minimized window, keep current resources
    if (newScreenSize == mScreenSize) return;

    mScreenSize = newScreenSize;
    mTileCount = computeTileCount(mScreenSize);
    mTileCount_flat = size_t(mTileCount.x) * mTileCount.y;

    //textures (same handles, new storage)
    mAccumulationTexture.resize(accumulationTextureSize());
    mFrameTexture.resize(mScreenSize);

    //SSBOs (only once the initial allocation happened; otherwise prepareGpuBuffers() will use the new size)
    if (mBuffersPrepared) {
        allocateScreenDependentBuffers();
    }

    //shaders whose #defines encode tile grid dimensions
    mDrawListClearProgram.recompileFromFile("drawlist_clear.comp", drawListClearDefines());
    mDrawListProgram.recompileFromFile("drawlist.comp", drawListDefines());
    mRasterProgram.recompileFromFile("rasterize.comp", rasterDefines());
    mDebugVisualsProgram.recompileFromFile("debugvisuals.comp", debugVisualsDefines());

    ::newstar::log() << "newstar: resized to " << mScreenSize.x << "x" << mScreenSize.y
                     << " (" << mTileCount.x << "x" << mTileCount.y << " tiles)" << std::endl;
}

void StarRenderer::prepareGpuBuffers() {
    GL.GenVertexArrays(1, &mScreenQuadVAOHandle);

    allocateScreenDependentBuffers();

    // Each Job = 16 B header (count, id_per_tile, tileID_flat, pad)
    //          + 16 B per ChunkMeta entry * GPU_MAX_JOB_CHUNKS.
    {
        size_t const jobStrideBytes = 16 + 16 * scfg().GPU_MAX_JOB_CHUNKS;
        mGpuJobsSSBO.allocateEmpty(jobStrideBytes * scfg().GPU_MAX_JOBS * 10, GL_DYNAMIC_COPY);
    }

    mGpuProfilingSSBO.allocateEmpty(sizeof(GpuProfilingStruct), GL_DYNAMIC_READ);

    mBuffersPrepared = true;
}

void StarRenderer::run(
    glm::mat4 modelViewMatrix,
    glm::mat4 inverseModelViewMatrix,
    glm::mat4 projectionMatrix,
    glm::mat4 inverseProjectionMatrix) {  
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


    //// SSBO BINDING ////
    {
        sw.startTiming("bindings");
        mGpuBatchRowsFlatSSBO.bind(0);
        mGpuBatchRowsHighPrecisionFlatSSBO.bind(6);
        mGpuChunkMetaSSBO.bind(1);
        mGpuJobsSSBO.bind(2); //generated on-gpu (gpu-driven rendering)
        mGpuDispatchHeaderSSBO.bind(5);
        mGpuAccumSSBO.bind(7);
        sw.endTiming("bindings");
    }

    //// PASS: clear screen ////
    {
        sw.startTiming("clear");
        mClearProgram.bind();
        mClearProgram.setIVec2("uResolution", glm::ivec2{mScreenSize});
        GL.DispatchCompute((mScreenSize.x + 7) / 8, (mScreenSize.y + 7) / 8, 1);
        GL.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        sw.endTiming("clear");
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
        mDrawListProgram.setFloat("uChunkSize", scfg().CHUNK_SIZE_PARSECS);
        mDrawListProgram.setIVec2("uResolution", glm::ivec2{mScreenSize});
        mDrawListProgram.setUint("uShowMask", mConfig.drawlist.CHUNK_SHOW_MASK);
        mDrawListProgram.setUint("uMAX_JOB_WORK", mConfig.drawlist.MAX_JOB_WORK);
        mDrawListProgram.setMat4("uMatModel", icrsCorrectionMatrix);
        mDrawListProgram.setMat4("uViewProj", uViewProj);
        mDrawListProgram.setMat4("uInvP", uInvP);
        GL.DispatchCompute((mChunkCount + 63) / 64, 1, 1); //for each screentile
        GL.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        sw.endTiming("drawlist");
    }

    { // prepare profiling ssbo
        mGpuProfilingSSBO.bind(4); //bind buffer + bind slot (meaning that we can access buffer data already)
        const uint32_t zero = 0;
        GL.ClearBufferData(GL_SHADER_STORAGE_BUFFER, GL_R32UI,
                        GL_RED_INTEGER, GL_UNSIGNED_INT, &zero);
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
        mDebugVisualsProgram.setUint("uWorkloadFullChunks", static_cast<uint32_t>(4 * scfg().GPU_MAX_JOB_CHUNKS));
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
        mRasterProgram.setFloat("uChunkSize", scfg().CHUNK_SIZE_PARSECS);
        mRasterProgram.setMat4("uViewProj", uViewProj);
        mRasterProgram.setMat4("uInvP", uInvP);
        mRasterProgram.setMat4("uMatModel", icrsCorrectionMatrix);
        mRasterProgram.setUint("uStartLayer", 0u);
        mRasterProgram.setUint("uLayerCount", static_cast<uint32_t>(scfg().GPU_MAX_JOB_CHUNKS));
        //::newstar::log() << "newrenderer uniform: " << luminanceMultiplicator << std::endl;
        mRasterProgram.setFloat("uLuminanceMultiplicator", mConfig.LUMINANCE_FACTOR);

        //indirect dispatch
        GL.BindBuffer(GL_DISPATCH_INDIRECT_BUFFER, mGpuDispatchHeaderSSBO.getID());
        GL.DispatchComputeIndirect(0);   // x/y/z read from offset 0 of the bound buffer
        GL.MemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT
                        | GL_TEXTURE_FETCH_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
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
        drawDebugOverlay(debugString, glm::ivec2{10, 500});

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
. LUMINANCE_FACTOR *= 5.0;
; LUMINANCE_FACTOR /= 5.0;
L LUMI_THRESHOLD_ENABLED
I DEBUG_OVERLAY_ENABLED
)";
        drawDebugOverlay(helpString, glm::ivec2{10, 1000});
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
        GL.ProgramUniform1f(mScreenQuadProgram, uniforms.uHdrEnabled, mConfig.HDR_ENABLED);
        
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
    GL.MemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
    GL.Disable(GL_DEPTH_TEST);
    GL.Clear(GL_COLOR_BUFFER_BIT);
}

StarRendererConfig& StarRenderer::config() {
    return mConfig;
}

}