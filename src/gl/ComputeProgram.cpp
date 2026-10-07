#include "ComputeProgram.hpp"

#include "gl/shader_utils.hpp"
#include <log/log.hpp>

namespace newstar {

ComputeProgram::ComputeProgram(std::string const& path, std::unordered_map<std::string, std::optional<size_t>> const& defines) :
mProgram{
    createComputeProgramFromFile(path, defines)},
    mPath{path}
{}

ComputeProgram::~ComputeProgram() { releaseGL(); }

void ComputeProgram::releaseGL() noexcept {
    if (mProgram != 0) {
        GL.DeleteProgram(mProgram);
        mProgram = 0;
    }
}

void ComputeProgram::recompileFromFile(std::string const& path, std::unordered_map<std::string, std::optional<size_t>> const& defines) noexcept {
    GLuint const newProgram = createComputeProgramFromFile(path, defines);
    releaseGL();
    mProgram = newProgram;
    mPath = path;
    uniformCache_.clear();
}


void ComputeProgram::bind() const noexcept {
    GL.UseProgram(mProgram);
}

//uniform caching
int ComputeProgram::locationOf(std::string_view uniformName) {
    auto it = uniformCache_.find(uniformName);
    if(it != uniformCache_.end()) {
        return it->second;
    }

    const std::string owned(uniformName);
    const GLint loc = GL.GetUniformLocation(mProgram, owned.c_str());
    if (loc == -1) {
        ::newstar::err() << "ComputeProgram:" << mPath
                  << "] warning: uniform '" << owned
                  << "' not found (unused or optimized out)\n";
    }
    uniformCache_.emplace(owned, loc);
    return loc;
}

void ComputeProgram::setFloat(std::string_view name, float const x) {
    GL.ProgramUniform1f(mProgram, locationOf(name), x);
}

void ComputeProgram::setInt(std::string_view name, int32_t const x) {
    GL.ProgramUniform1i(mProgram, locationOf(name), x);
}

void ComputeProgram::setUint(std::string_view name, uint32_t const x) {
    GL.ProgramUniform1ui(mProgram, locationOf(name), x);
}

void ComputeProgram::setFloatArray(std::string_view name, float const* data, size_t count) {
    bind();
    GL.ProgramUniform1fv(mProgram, locationOf(name), static_cast<GLsizei>(count), data);
}

void ComputeProgram::setIntArray(std::string_view name, int32_t const* data, size_t count) {
    bind();
    GL.ProgramUniform1iv(mProgram, locationOf(name), static_cast<GLsizei>(count), data);
}

void ComputeProgram::setUintArray(std::string_view name, uint32_t const* data, size_t count) {
    bind();
    GL.ProgramUniform1uiv(mProgram, locationOf(name), static_cast<GLsizei>(count), data);
}

void ComputeProgram::setMat4Raw(std::string_view name, float const* columnMajor16, bool transpose) {
    bind();
    GL.ProgramUniformMatrix4fv(mProgram, locationOf(name), 1, transpose ? GL_TRUE : GL_FALSE, columnMajor16);
}

void ComputeProgram::setMat3Raw(std::string_view name, float const* columnMajor9, bool transpose) {
    bind();
    GL.ProgramUniformMatrix3fv(mProgram, locationOf(name), 1, transpose ? GL_TRUE : GL_FALSE, columnMajor9);
}

void ComputeProgram::setVec2(std::string_view name, glm::vec2 const& v) {
    bind();
    GL.ProgramUniform2f(mProgram, locationOf(name), v.x, v.y);
}

void ComputeProgram::setVec3(std::string_view name, glm::vec3 const& v) {
    bind();
    GL.ProgramUniform3f(mProgram, locationOf(name), v.x, v.y, v.z);
}

void ComputeProgram::setVec4(std::string_view name, glm::vec4 const& v) {
    bind();
    GL.ProgramUniform4f(mProgram, locationOf(name), v.x, v.y, v.z, v.w);
}

void ComputeProgram::setIVec2(std::string_view name, glm::ivec2 const& v) {
    bind();
    GL.ProgramUniform2i(mProgram, locationOf(name), v.x, v.y);
}

void ComputeProgram::setIVec3(std::string_view name, glm::ivec3 const& v) {
    bind();
    GL.ProgramUniform3i(mProgram, locationOf(name), v.x, v.y, v.z);
}

void ComputeProgram::setUVec2(std::string_view name, glm::uvec2 const& v) {
    bind();
    GL.ProgramUniform2ui(mProgram, locationOf(name), v.x, v.y);
}

void ComputeProgram::setUVec3(std::string_view name, glm::uvec3 const& v) {
    bind();
    GL.ProgramUniform3ui(mProgram, locationOf(name), v.x, v.y, v.z);
}

}