#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <stdexcept>
#include <type_traits>
#include <iostream>

#include "gl_context.hpp"

#include <StaticConfig.hpp>
#include <log/log.hpp>

// Simple SSBO wrapper with RAII, since we need to create a lot of SSBOs for the star render pipeline
class SSBO {
public:
    SSBO() : mCount(0), mHandle(0) {}

    ~SSBO() {
        cleanup();
    }

    // don't allow copying for now
    SSBO(const SSBO&) = delete;
    SSBO& operator=(const SSBO&) = delete;

    // don't allow moving for now
    SSBO(SSBO&& other) = delete;
    SSBO& operator=(SSBO&& other) = delete;

    /** Create SSBO from CPU side data by uploading it to GPU, optionally create cache file */
    template<typename T>
    void create(std::vector<T> const& data, GLenum usage, std::string const& cacheFilename = "") {
        static_assert(std::is_trivially_copyable_v<T>, "SSBO data must be trivially copyable");
        
        if(cacheFilename.length() > 0) {   
            std::string const fullCachePath = newstar::static_config::CACHE_BASE_PATH + cacheFilename;
            std::ofstream out(fullCachePath, std::ios::binary);
            if (!out) throw std::runtime_error("Failed to open file for writing: " + fullCachePath);
            out.write(reinterpret_cast<const char*>(data.data()), data.size() * sizeof(T));
        }

        mCount = data.size();
        cleanup();
        GL.GenBuffers(1, &mHandle);
        bind();
        GL.BufferData(GL_SHADER_STORAGE_BUFFER, data.size() * sizeof(T), data.data(), usage);
    }

    size_t count() const {
        return mCount;
    }

    // bind to a specific shader storage buffer slot
    void bind(GLuint index = 0) const {
        GL.BindBufferBase(GL_SHADER_STORAGE_BUFFER, index, mHandle);
    }

    // unbind
    void unbind() const {
        GL.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
    }

    // check if buffer/gl object handle is valid
    bool isValid() const {
        return mHandle != 0;
    }

    // get gl object handle
    GLuint getID() const {
        return mHandle;
    }

    /** Initialize SSBO from serialized cache file */
    template<typename T>
    void loadFromFile(std::string filename, GLenum usage) {
        static_assert(std::is_trivially_copyable_v<T>, "SSBO data must be trivially copyable");

        std::string const fullFilePath = newstar::static_config::CACHE_BASE_PATH + filename;
        ::newstar::log() << fullFilePath << std::endl;

        std::ifstream in(fullFilePath, std::ios::binary | std::ios::ate);
        if (!in) {
            throw std::runtime_error("Failed to open file for reading: " + fullFilePath);
        }

        std::streamsize size = in.tellg();
        if (size % sizeof(T) != 0) throw std::runtime_error("File size does not match type size");
        in.seekg(0, std::ios::beg);

        std::vector<T> data(size / sizeof(T));
        if (!in.read(reinterpret_cast<char*>(data.data()), size)) {
            throw std::runtime_error("Failed to read file data");
        }

        create(data, usage);
    }

private:
    void cleanup() {
        if (mHandle != 0) {
            GL.DeleteBuffers(1, &mHandle);
            mHandle = 0;
            mCount = 0;
        }
    }

    size_t mCount; // buffer array size
    GLuint mHandle; //gl object handle
};
