#pragma once

#include <cstdint>
#include <glm/glm.hpp>

uint32_t encode_morton(glm::uvec3 pos);
glm::uvec3 decode_morton(uint32_t morton);