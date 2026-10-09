#pragma once

#include <glad/gl.h> 

namespace newstar::detail {

// single, static library-wide GL context. set once by initialize()
inline GladGLContext g_gl_context{};

inline GladGLContext& gl() { return g_gl_context; }

} // namespace newstar::detail

#define GL ::newstar::detail::gl()