#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <RawStar.hpp>
#include <functional>

using NewStarInjectedLoggerFunctionType = std::function<void(std::string const&)>;

struct StarRendererConfig {
    glm::uvec2 screenResolution;
    size_t tileSizePx = 64;

    bool DEBUG_OVERLAY_ENABLED = false;
    float LUMINANCE_FACTOR = 3200.0;
    bool HDR_ENABLED = false;

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

        bool operator==(const ConfigRaster& other) const = default;

        bool operator!=(const ConfigRaster& other) const {
            return !(*this == other);
        }
    } raster;
};


void newstarInit(glm::uvec2 const& screenSize, std::string const& shaderBasePath, std::string const& cacheBasePath,
    NewStarInjectedLoggerFunctionType loggerFunction, NewStarInjectedLoggerFunctionType loggerErrorFunction
);
void newstarSetdata(std::vector<RawStar> const& rawStars);
void newstarRender(
    glm::mat4 modelViewMatrix,
    glm::mat4 inverseModelViewMatrix,
    glm::mat4 projectionMatrix,
    glm::mat4 inverseProjectionMatrix);

void newstarResizeScreen(glm::uvec2 screenSize);

StarRendererConfig& newstarConfig();

void newstarDeInit();