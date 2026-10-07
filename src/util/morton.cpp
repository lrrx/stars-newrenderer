#include "morton.hpp"

//from https://github.com/tomas-o-dev/Morton-Z-Code-C-library/blob/master/MZC3D32.h [MIT License]

uint32_t ulMC3Dspread(uint32_t w) {
    w &=                0x000003ff; /* w = ---- ---- ---- ---- ---- --98 7654 3210 */
    w = (w | w << 16) & 0x030000ff; 
    w = (w | w <<  8) & 0x0300f00f;
    w = (w | w <<  4) & 0x030c30c3;
    w = (w | w <<  2) & 0x09249249; /* w = uu-- 9--8 --7- -6-- 5--4 --3- -2-- 1--0 */
    return w;
}

/* inverse of Spread */
uint32_t ulMC3Dcompact(uint32_t w)  {
	 w &=                  0x09249249;
	 w = (w ^ (w >>  2)) & 0x030c30c3;
	 w = (w ^ (w >>  4)) & 0x0f00f00f;
	 w = (w ^ (w >>  8)) & 0xff0000ff;
	 w = (w ^ (w >> 16)) & 0x0000ffff;
	 return (uint32_t)w;
}

uint32_t encode_morton(glm::uvec3 pos) {
    uint32_t x = pos.x & 0x3ff;
    uint32_t y = pos.y & 0x3ff;
    uint32_t z = pos.z & 0x3ff;
    
    x = ulMC3Dspread(x);
    y = ulMC3Dspread(y) << 1;
    z = ulMC3Dspread(z) << 2;
    
    return x | y | z;
}

glm::uvec3 decode_morton(uint32_t morton) {
    uint32_t x,y,z{};

    x = ulMC3Dcompact(morton);
    y = ulMC3Dcompact(morton >> 1);    
    z = ulMC3Dcompact(morton >> 2);    
    
    return glm::uvec3{x,y,z};
}