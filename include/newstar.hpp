#pragma once

#include <glm/glm.hpp>
#include <array>
#include <vector>
#include <RawStar.hpp>

struct StarRendererConfig {
    glm::uvec2 screenResolution;
    size_t tileSizePx = 64;

    bool DEBUG_OVERLAY_ENABLED = false;
            

    struct ConfigDrawlist {
        //drawlist-------------
        uint32_t HIGH_LOD_PX_THRESHOLD = 200;
        bool DEBUG_GRID_OVERLAY = false;
        bool DEBUG_VISUALIZE_BINNING = false;
        bool DEBUG_CULLING_MINIMAP = false;
        bool DEBUG_VISUALIZE_LOD_RADIUS = false;

        uint32_t MAX_JOB_WORK = 4;
        uint32_t CHUNK_SHOW_MASK = 0x1 | 0x4 | 0x10;

        //---------------------

        bool operator==(const ConfigDrawlist& other) const = default;
        bool operator!=(const ConfigDrawlist& other) const {
            return !(*this == other);
        }
        
    } drawlist;

    bool WORKLOAD_OVERLAY_ENABLED = false; //REVEAL(tilebased workload overlay, drawlist, workload balancing)
    
    struct ConfigRaster {
        bool RASTERIZE_USE_TILE_ACCUMULATION = true;
        bool WRITE_PROFILING_COUNTERS = true;
        bool ENABLE_LUMI_THRESHOLD = false;
        bool LUMI_THRESHOLD_ENABLED = false;
        float DEBUG_LUMI_FACTOR = 32.0;

        bool operator==(const ConfigRaster& other) const = default;

        bool operator!=(const ConfigRaster& other) const {
            return !(*this == other);
        }
    } raster;
};

//TODOR: remove maybe since boilerplate? <-klären
void newstarInit(glm::uvec2 screenSize);
void newstarSetdata(std::vector<RawStar> const& rawStars);
void newstarRender(
    glm::mat4 modelViewMatrix,
    glm::mat4 inverseModelViewMatrix,
    glm::mat4 projectionMatrix,
    glm::mat4 inverseProjectionMatrix,
    float luminanceMultiplicator,
    bool hdrEnabled);

StarRendererConfig& newstarConfig();

void newstarDeInit();