#pragma once

#include <glm/glm.hpp>

namespace util {

uint32_t packIvec8(glm::ivec3 pos);
glm::ivec3 unpackIvec8(uint32_t v);

}