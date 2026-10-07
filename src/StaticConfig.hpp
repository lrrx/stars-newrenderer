#pragma once

#include <string>

namespace newstar {
namespace static_config {

//--- FIXED GPU CONFIG ---
//TODOR: mark constant as used for rasterization
//TODOR: make configurable
//TODOR: do GPU buffers really require rebuild if this is changed? allow change without requiring data rebuild
constexpr size_t GPU_THREAD_COUNT = 512; //how many parallel columns per batch

//--- PATHS
static std::string const SR_BASE_PATH = "/home/user/git/stars/"; //TODOR: file path in config, pass to library interface + dynamic values
static std::string const SHADER_BASE_PATH = SR_BASE_PATH + "newrenderer/src/shaders/";
static std::string const CACHE_BASE_PATH = SR_BASE_PATH + "data/";

//--- PREPROCESSING --
//TODOR: REVEAL -> smaller chunks -> workload balancing?, move constant to config maybe
inline constexpr double CHUNK_SIZE_PARSECS = 12000.0 / 64; // 64^3 chunks, spanning 12^3 kpc^3, 12000 / 64 = 187.5

}
}