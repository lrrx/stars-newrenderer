#include "preprocessing.hpp"

#include <cstdlib>
#include <unordered_map>
#include <iostream>

#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif

#include <glm/gtx/string_cast.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/integer.hpp>

#include <RawStar.hpp>
#include "preprocessing/datatypes.hpp"
#include "preprocessing/gpu_datatypes.hpp"
#include "util/ivec8_packing.hpp"

#include <log/log.hpp>

namespace {

static inline size_t starsOOB = 0;

void binStarsToChunks(std::vector<RawStar> const& raw_stars, std::unordered_map<uint32_t, RawChunk>& chunksOut) {
    ::newstar::log() << "global position of first raw star: " << glm::to_string(raw_stars.front().mPosition) << std::endl;
    ::newstar::log() << "number of input raw rawStars:" << raw_stars.size() << std::endl;
    
    //store to internal chunks first, copy out later while merging overlapping rawStars
    std::unordered_map<uint32_t, RawChunk> chunksInternal;
    
    for(RawStar const& raw_star : raw_stars) {
        glm::vec3 const globalChunkSpacePosition = raw_star.mPosition / newstar::static_config::CHUNK_SIZE_PARSECS;
        if(glm::length(globalChunkSpacePosition) < 0.0001) continue; //filter out "erroneous"(?) stars very close to center landing on 0,0,0, causing perf. spike

        glm::vec3 const chunkCoordFloored = glm::floor(globalChunkSpacePosition);
        glm::ivec3 const chunkCoord{chunkCoordFloored};
        
        uint32_t chunkCoordEncoded = util::packIvec8(chunkCoord);
        if(chunkCoordEncoded == 0xffffffff) {
            starsOOB++;
            continue;
        }
        
        if(chunksInternal.find(chunkCoordEncoded) == chunksInternal.end()) {
            chunksInternal[chunkCoordEncoded].globalPosEncoded = chunkCoordEncoded;
            chunksInternal[chunkCoordEncoded].globalPos = globalChunkSpacePosition;
        }
        
        RawStar raw_star_local = raw_star;

        raw_star_local.mPosition = globalChunkSpacePosition - chunkCoordFloored;
        
        chunksInternal[chunkCoordEncoded].stars.emplace_back(raw_star_local);
    }
    
    chunksOut = chunksInternal;

    //debug stats
    size_t stars_in_chunks = 0;
    for(auto const& chunk : chunksOut) {
        stars_in_chunks += chunk.second.stars.size();
    }

    ::newstar::log() << "stars_in_chunks: " << stars_in_chunks + starsOOB << std::endl; 
    ::newstar::log() << "total number of chunks: " << chunksOut.size() << " chunks" << std::endl;
}

void buildChunkLevels(
    std::vector<GpuChunkMeta>& gpuChunkMetasOut,
    std::vector<GpuBatchRow>& gpuBatchRows,
    std::vector<GpuBatchRow>& gpuBatchRowsHighPrecision,
    std::unordered_map<uint32_t, RawChunk> const& chunks,
    std::unordered_map<uint32_t, RawChunk>& leftoversChunks,
    std::unordered_map<uint32_t, RawChunk>& sparseChunks
) {
        //copy delta rawStars from chunks sequentially, pass global offset ptr to chunk
    for(auto const& [id, chunk] : chunks) {
        if(chunk.stars.size() < 64) {
            glm::ivec3 posDec = util::unpackIvec8(chunk.globalPosEncoded);
            glm::ivec3 mod16 = ((posDec % 16) + 16) % 16;
            glm::ivec3 globPos = (posDec - mod16) / 16;
            uint32_t globPosEnc = util::packIvec8(globPos);

            for(auto const& star : chunk.stars) {
                sparseChunks[globPosEnc].globalPosEncoded = globPosEnc;
                sparseChunks[globPosEnc].stars.push_back(star);
                sparseChunks[globPosEnc].stars.back().mPosition /= 16.0;
                sparseChunks[globPosEnc].stars.back().mPosition += glm::dvec3(mod16) / 16.0;
            }

            continue;
        }

        //REVEAL(waste of memory fetches if preprocessing doesnt redistribute padding stars into less dense chunks, see rasterize.comp PADDING_STAR=0xffffffff side too)

        size_t starCountAligned = chunk.stars.size() - chunk.stars.size() % newstar::static_config::GPU_THREAD_COUNT;
        QuantizedChunk quantizedChunk{chunk.stars, starCountAligned, chunk.globalPosEncoded, 1};
        quantizedChunk.gpuSerialize(gpuBatchRows, gpuBatchRowsHighPrecision, gpuChunkMetasOut);

        glm::ivec3 globalPosDecoded = util::unpackIvec8(chunk.globalPosEncoded);
        glm::ivec3 mod4 = ((globalPosDecoded % 4) + 4) % 4;
        glm::ivec3 globalPosSparse = (globalPosDecoded - mod4) / 4;
        uint32_t globalPosSparseEncoded = util::packIvec8(globalPosSparse);

        for(auto it = chunk.stars.begin() + starCountAligned; it != chunk.stars.end(); it++) {     
            leftoversChunks[globalPosSparseEncoded].globalPosEncoded = globalPosSparseEncoded;
            leftoversChunks[globalPosSparseEncoded].stars.push_back(*it);
            leftoversChunks[globalPosSparseEncoded].stars.back().mPosition /= 4.0;
            leftoversChunks[globalPosSparseEncoded].stars.back().mPosition += glm::dvec3(mod4) / 4.0;
        }
    }
}

void serializeAndFlattenChunks(
    std::vector<uint32_t>& gpuBatchRowsFlatOut,
    std::vector<uint32_t>& gpuBatchRowsHighPrecisionFlatOut,
    std::vector<GpuChunkMeta>& gpuChunkMetasOut,
    std::unordered_map<uint32_t, RawChunk> const& leftoversChunks,
    std::unordered_map<uint32_t, RawChunk> const& sparseChunks,
    std::vector<GpuBatchRow>& gpuBatchRows,
    std::vector<GpuBatchRow>& gpuBatchRowsHighPrecision
) {
    for(auto const& [id, chunk] : leftoversChunks) {
        QuantizedChunk quantizedChunk{chunk.stars, chunk.stars.size(), chunk.globalPosEncoded, 4};
        quantizedChunk.gpuSerialize(gpuBatchRows, gpuBatchRowsHighPrecision, gpuChunkMetasOut);
    }

    for(auto const& [id, chunk] : sparseChunks) {
        QuantizedChunk quantizedChunk{chunk.stars, chunk.stars.size(), chunk.globalPosEncoded, 16};
        quantizedChunk.gpuSerialize(gpuBatchRows, gpuBatchRowsHighPrecision, gpuChunkMetasOut);
    }

    for(auto const& gpuBatchRow : gpuBatchRows) {
        //serialize main batch table
        for(size_t i = 0; i < newstar::static_config::GPU_THREAD_COUNT; i++) {
            gpuBatchRowsFlatOut.push_back(gpuBatchRow[i * 2 + 0]);
            gpuBatchRowsFlatOut.push_back(gpuBatchRow[i * 2 + 1]);
        }
    }

    for(auto const& rowHighP : gpuBatchRowsHighPrecision) {
        //serialize main batch table
        for(size_t i = 0; i < newstar::static_config::GPU_THREAD_COUNT; i++) {
            gpuBatchRowsHighPrecisionFlatOut.push_back(rowHighP[i]);
        }
    }
}

void countPaddingZerosDebug(std::vector<GpuBatchRow> const& gpuBatchRows,
    std::vector<GpuBatchRow> const& gpuBatchRowsHighPrecision) {
    size_t non_zero_entries = 0;

    for(auto const& row : gpuBatchRowsHighPrecision) {
        for(auto const& x : row) {
            if(x != 0) non_zero_entries++;
        }
    }

    size_t padding_stars_count = 0;

    for(auto const& row : gpuBatchRows) {
        for(auto const& x : row) {
            if(x == 0xFFFFFFFFu) padding_stars_count++;
        }
    }

    ::newstar::log() << "padding_stars_count" << padding_stars_count << std::endl;
    ::newstar::log() << "non_zero_entries in gpu rows" << non_zero_entries << std::endl;
}

} // namespace

namespace newstar {

namespace preprocessing {

//TODOR: cognitive complexity
void run(std::vector<RawStar> const& rawStarsIn,
    std::vector<GpuChunkMeta>& gpuChunkMetasOut,
    std::vector<uint32_t>& gpuBatchRowsFlatOut,
    std::vector<uint32_t>& gpuBatchRowsHighPrecisionFlatOut
) {
    ::newstar::log() << "rawStars.size() " << rawStarsIn.size() << std::endl;
    
    std::unordered_map<uint32_t, RawChunk> chunks;

    binStarsToChunks(rawStarsIn, chunks);

    gpuChunkMetasOut.reserve(chunks.size());

    std::unordered_map<uint32_t, RawChunk> leftoversChunks;
    std::unordered_map<uint32_t, RawChunk> sparseChunks;

    std::vector<GpuBatchRow> gpuBatchRows;
    std::vector<GpuBatchRow> gpuBatchRowsHighPrecision;

    buildChunkLevels( gpuChunkMetasOut,
    gpuBatchRows,
    gpuBatchRowsHighPrecision,
    chunks,
    leftoversChunks,
    sparseChunks);

    serializeAndFlattenChunks(gpuBatchRowsFlatOut,
    gpuBatchRowsHighPrecisionFlatOut,
    gpuChunkMetasOut,
    leftoversChunks,
    sparseChunks,
    gpuBatchRows,
    gpuBatchRowsHighPrecision);

    countPaddingZerosDebug(gpuBatchRows, gpuBatchRowsHighPrecision);
}

} //namespace preprocessing
} //namespace newstar