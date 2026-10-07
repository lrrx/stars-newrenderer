#pragma once

#include <cstdint>
#include <vector>
#include <RawStar.hpp>

#include "gpu_datatypes.hpp"

namespace newstar {

namespace preprocessing {

void run(
    std::vector<RawStar> const& rawStarsIn,
    std::vector<GpuChunkMeta>& gpuChunkMetasOut,
    std::vector<uint32_t>& gpuBatchRowsFlatOut,
    std::vector<uint32_t>& gpuBatchRowsHighPrecisionFlatOut
);
} //namespace preprocessing
} //namespace newstar