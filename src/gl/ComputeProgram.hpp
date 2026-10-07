#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <gl/gl_context.hpp>
#include <glm/glm.hpp>

namespace newstar {

// ---------------------------------------------------------------------
// Transparent string hashing so we can look up the uniform-location
// cache with a std::string_view (no temporary std::string allocation).
// ---------------------------------------------------------------------
struct StringHash {
    using is_transparent = void;
    [[nodiscard]] size_t operator()(std::string_view sv) const noexcept {
        return std::hash<std::string_view>{}(sv);
    }
};

class ComputeProgram {
public:
    ComputeProgram(std::string const& path, std::unordered_map<std::string, std::optional<size_t>> const& defines);
    ~ComputeProgram();
    
    ComputeProgram() = delete;
    ComputeProgram(const ComputeProgram&) = delete;
    ComputeProgram& operator=(const ComputeProgram&) = delete;
    ComputeProgram(ComputeProgram&& other) = delete;
    ComputeProgram& operator=(ComputeProgram&& other) = delete;

    bool isValid() const noexcept { return mProgram != 0; }
    GLuint id() const noexcept { return mProgram; }

    void recompileFromFile(std::string const& path, std::unordered_map<std::string, std::optional<size_t>> const& defines) noexcept;

    void bind() const noexcept;

    void setFloat(std::string_view name, float const x);
    void setInt(std::string_view name, int32_t const x);
    void setUint(std::string_view name, uint32_t const x);
    void setFloatArray(std::string_view name, float const* data, size_t count);
    void setIntArray(std::string_view name, int32_t const* data, size_t count);
    void setUintArray(std::string_view name, uint32_t const* data, size_t count);
    void setMat4Raw(std::string_view name, float const* columnMajor16,
                 bool transpose = false);
    void setMat3Raw(std::string_view name, float const* columnMajor9,
                 bool transpose = false);
    void setVec2(std::string_view name, glm::vec2 const& v);
    void setVec3(std::string_view name, glm::vec3 const& v);
    void setVec4(std::string_view name, glm::vec4 const& v);
    void setIVec2(std::string_view name, glm::ivec2 const& v);
    void setIVec3(std::string_view name, glm::ivec3 const& v);
    void setUVec2(std::string_view name, glm::uvec2 const& v);
    void setUVec3(std::string_view name, glm::uvec3 const&);

    void setMat3(std::string_view name, glm::mat3 const& m, bool transpose = false) {
        setMat3Raw(name, &m[0][0], transpose);
    }
    void setMat4(std::string_view name, glm::mat4 const& m, bool transpose = false) {
        setMat4Raw(name, &m[0][0], transpose);
    }

private:
    int locationOf(std::string_view name);
    void releaseGL() noexcept;

    std::string mPath;
    uint32_t mProgram = 0;

    // string_view keys are safe here because we always insert with an
    // owned std::string (see locationOf); the transparent hash lets us
    // query with a view without allocating.
    std::unordered_map<std::string, int, StringHash, std::equal_to<>> uniformCache_;
};

}