#pragma once

#include <string> 

namespace newstar {

struct StaticGlobalConfig {
    std::string const SHADER_BASE_PATH;
    std::string const CACHE_BASE_PATH;

    StaticGlobalConfig(std::string const& shaderBasePath, std::string const& cacheBasePath);

    //TODOR: make these externally configurable (at initialization) instead of them being compile time constants (piece by peace, clean refactoring)
    struct hardcoded {
        //--- FIXED GPU CONFIG ---
        //TODOR: mark constant as used for rasterization
        //TODOR: make configurable
        //TODOR: do GPU buffers really require rebuild if this is changed? allow change without requiring data rebuild
        static constexpr size_t GPU_THREAD_COUNT = 512; //how many parallel columns per batch

        //--- Starrenderer workload balancing config ---
        // if greater than 1, we create multiple copies of the screen buffer and distribute writes accross them (uniformly random) when rasterizing to the screen
        // due to how memory banks work, this should not have any major impact (couldn't measure any, except perhaps worse if too large)
        static constexpr size_t GPU_ACCUMULATION_LANES = 2;

        // for tile-based rendering, the screen is divided into x * y tiles of GPU_TILE_SIZE_PX px size
        static constexpr size_t GPU_TILE_SIZE_PX = 64;

        // max chunks bundled per job. With MAX_CHUNK_ROWS = 8 on the CPU side
        // (see datatypes.cpp gpuSerialize), peak rows per job = 8 * 16 = 128.
        static constexpr size_t GPU_MAX_JOB_CHUNKS = 10;

        // total jobs across the screen per frame (primaries + overflow). Sized so the
        // SSBO is the source of truth for capacity; the shaders' MAX_JOBS define and
        // the buffer allocation both derive from this.
        static constexpr size_t GPU_MAX_JOBS = 100000;

        //Preprocessing, but also affects Workload Balancing
        //TODOR: REVEAL -> smaller chunks -> workload balancing?, move constant to config maybe
        static constexpr double CHUNK_SIZE_PARSECS = 12000.0 / 64; // 64^3 chunks, spanning 12^3 kpc^3, 12000 / 64 = 187.5
    };
};

//temporary, just for refactoring
using scfg = StaticGlobalConfig;
using scfg_h = StaticGlobalConfig::hardcoded;

void initStaticGlobalConfig(StaticGlobalConfig const& sgConfig);

StaticGlobalConfig const& staticGlobalConfig();

}