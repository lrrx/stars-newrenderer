#include "StaticGlobalConfig.hpp"

#include <cassert>
#include <memory>

namespace newstar{

std::unique_ptr<StaticGlobalConfig> _staticGlobalConfig;

StaticGlobalConfig::StaticGlobalConfig(std::string const& shaderBasePath,
    std::string const& cacheBasePath,
    size_t const gpuAccumulationLanes,
    size_t const gpuTileSizePx,
    size_t const gpuMaxJobChunks,
    size_t const gpuMaxJobs,
    double const chunkSizeParsecs
) 
: SHADER_BASE_PATH{shaderBasePath},
CACHE_BASE_PATH{cacheBasePath},
GPU_ACCUMULATION_LANES{gpuAccumulationLanes},
GPU_TILE_SIZE_PX{gpuTileSizePx},
GPU_MAX_JOB_CHUNKS{gpuMaxJobChunks},
GPU_MAX_JOBS{gpuMaxJobs},
CHUNK_SIZE_PARSECS{chunkSizeParsecs}
{}

void initStaticGlobalConfig(StaticGlobalConfig const& sgConfig) {
    _staticGlobalConfig = std::make_unique<StaticGlobalConfig>(sgConfig);
}

StaticGlobalConfig const& scfg() {
    assert(_staticGlobalConfig != nullptr);
    return *_staticGlobalConfig;
}

}