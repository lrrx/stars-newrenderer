#pragma once

#include <cstdint>
#include <string> 

namespace newstar {

struct StaticGlobalConfig {
    std::string const SHADER_BASE_PATH;
    std::string const CACHE_BASE_PATH;

    StaticGlobalConfig(
        std::string const& shaderBasePath,
        std::string const& cacheBasePath,
        size_t const gpuAccumulationLanes = 2,
        size_t const gpuTileSizePx = 64,
        size_t const gpuMaxJobChunks = 10,
        size_t const gpuMaxJobs = 100000,
        double const chunkSizeParsecs = 12000.0 / 64
    );

    //--- Starrenderer workload balancing config ---
    // if greater than 1, we create multiple copies of the screen buffer and distribute writes accross them (uniformly random) when rasterizing to the screen
    // due to how memory banks work, this should not have any major impact (couldn't measure any, except perhaps worse if too large)
    size_t const GPU_ACCUMULATION_LANES;

    // for tile-based rendering, the screen is divided into x * y tiles of GPU_TILE_SIZE_PX px size
    size_t const GPU_TILE_SIZE_PX;

    // max chunks bundled per job. With MAX_CHUNK_ROWS = 8 on the CPU side
    // (see datatypes.cpp gpuSerialize), peak rows per job = 8 * 16 = 128.
    size_t const GPU_MAX_JOB_CHUNKS;

    // total jobs across the screen per frame (primaries + overflow). Sized so the
    // SSBO is the source of truth for capacity; the shaders' MAX_JOBS define and
    // the buffer allocation both derive from this.
    size_t const GPU_MAX_JOBS;

    //Preprocessing, but also affects Workload Balancing
    //TODOR: REVEAL -> smaller chunks -> workload balancing?, move constant to config maybe
    double const CHUNK_SIZE_PARSECS; // 64^3 chunks, spanning 12^3 kpc^3, 12000 / 64 = 187.5

    //TODOR: make these externally configurable (at initialization) instead of them being compile time constants (piece by peace, clean refactoring)
    struct hardcoded {
        //--- FIXED GPU CONFIG ---
        //TODOR: mark constant as used for rasterization
        //TODOR: make configurable
        //TODOR: do GPU buffers really require rebuild if this is changed? allow change without requiring data rebuild
        static constexpr size_t GPU_THREAD_COUNT = 512; //how many parallel columns per batch
    };

};


void initStaticGlobalConfig(StaticGlobalConfig const& sgConfig);

StaticGlobalConfig const& scfg();

//temporary, just for refactoring
using scfg_h = StaticGlobalConfig::hardcoded;

}