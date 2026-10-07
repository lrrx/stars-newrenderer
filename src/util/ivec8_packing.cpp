#include "ivec8_packing.hpp"

namespace util {


uint32_t packIvec8(glm::ivec3 pos) {
    constexpr int32_t offset = 0x7f;
    if(std::abs(pos.x) > offset || std::abs(pos.y) > offset || std::abs(pos.z) > offset) {
        return 0xffffffff;
        //std::cout << "assertion failed in packIvec8" << std::endl;
        //exit(-1);
    }

    //glm::uvec3 shifted = pos + glm::ivec3(offset);

    uint32_t x = (pos.x + offset) & 0xff;
    uint32_t y = (pos.y + offset) & 0xff;
    uint32_t z = (pos.z + offset) & 0xff;

    return (x << 16) | (y << 8) | z;
}

glm::ivec3 unpackIvec8(uint32_t v) {
    constexpr int32_t offset = 0x7f;

    uint8_t x = (v >> 16) & 0xff;
    uint8_t y = (v >> 8) & 0xff;
    uint8_t z = (v >> 0) & 0xff;

    glm::ivec3 pos{x,y,z};
    glm::ivec3 unshifted = pos - glm::ivec3(offset);

    return unshifted;
}

} //namespace util