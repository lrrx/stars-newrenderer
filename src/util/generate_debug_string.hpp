#pragma once

#include <cmath>
#include <iomanip>
#include <unordered_map>
#include <string>
#include <sstream>
#include <iostream>
#include <cstddef>

#include <glm/glm.hpp>

#include "preprocessing/gpu_datatypes.hpp"

namespace newstar {

constexpr size_t DEBUG_TEXT_LINE_CHARS = 32;
constexpr size_t DEBUG_TEXT_LINES = 32;
constexpr size_t DEBUG_TEXT_BUFFER_CHARS = DEBUG_TEXT_LINE_CHARS * DEBUG_TEXT_LINES;
constexpr size_t DEBUG_TEXT_BUFFER_WORDS = (DEBUG_TEXT_BUFFER_CHARS + 3) / 4; // 4 chars into one uint32_t/"word", round up

namespace {
/**
 * Creates the char buffer for the bitmap text debug overlay.
 * The buffer consists of 32 lines, each holding 32 chars, so 1024 chars in total.
 * 4 chars are packed into one uint32_t/"word".
 * ASCII newlines (char 10) in the input string are interpreted as a line break (moving to next line)
 *
 * @param s input string to be transformed into buffer format
 * @param buf target buffer for output
 */
void fillTextBufferPacked(std::string const&s, std::array<uint32_t, DEBUG_TEXT_BUFFER_WORDS>& buf) {
    assert(s.size() < DEBUG_TEXT_BUFFER_CHARS);

    buf.fill(0);
    size_t line = 0;
    size_t column = 0; //column of char within line, goes through 0..DEBUG_TEXT_LINE_CHARS
    for (uint8_t chr : s) {
        if (line >= DEBUG_TEXT_LINES) break;
        size_t wordIndex = line * 8 + (column >> 2); //which word of line this char should land on
        size_t shiftInWord = (column % 4) * 8; // byte offset (0 - 3) in uint32_t/"word"
        buf[wordIndex] |= (uint32_t)chr << shiftInWord; //apply char to word
        
        //handle newline
        if (chr == '\n') {
            ++line;
            column = 0;
            continue;
        }

        //automatic line break on line overflow
        if (column++ >= DEBUG_TEXT_LINE_CHARS) {
            line++;
            column = 0;
        }
    }
    // count used lines
    /*size_t used = 0;
    for (; used < 32; ++used) {
        bool any = false;
        for (size_t w = 0; w < 8; ++w) if (buf[used*8 + w]) { any = true; break; }
        if (!any) break;
    }*/
    return;
}

}

/**
 * Creates the char buffer for the bitmap text debug overlay.
 * The buffer consists of 32 lines, each holding 32 chars, so 1024 chars in total.
 * 4 chars are packed into one uint32_t/"word".
 * ASCII newlines (char 10) in the input string are interpreted as a line break (moving to next line)
 *
 * @param s input string to be transformed into buffer format
 * @param buf target buffer for output
 * @return number of lines used up.
 */
static inline double ema_smoothing(double x, std::string const& tag) {
    static std::unordered_map<std::string, double> smoothedValues;

    constexpr double alpha = 0.01;
    if (smoothedValues.find(tag) == smoothedValues.end()) {
        smoothedValues[tag] = x;
    } else {
        if(smoothedValues[tag] == INFINITY) smoothedValues[tag] = x;
        smoothedValues[tag] = (1.0 - alpha) * smoothedValues[tag] + alpha * x;
    }

    return smoothedValues[tag];
}

static inline std::unordered_map<std::string, size_t> ioBytesPerCategory{};

inline std::string memSizeMB(std::string ioCategory, size_t count, size_t instance_bytes) {
    size_t bytes = (count * instance_bytes);
    if(ioCategory != " ")  {
        if(ioBytesPerCategory.find(ioCategory) == ioBytesPerCategory.end()) ioBytesPerCategory[ioCategory] = 0;
        ioBytesPerCategory[ioCategory] += bytes;
    }
    std::stringstream ss;
    ss << " ~" << ioCategory << ": " << std::setw(6) << std::to_string(bytes / 1024 / 1024) + "MB";
    return ss.str();
}

inline void generateDebugString(
    std::unordered_map<std::string, uint32_t> const& us_timings,
    GpuProfilingStruct const& profilingStruct,
    glm::vec3 const& cameraPosParsec,
    std::string& out
) {
    ioBytesPerCategory.clear();

    std::stringstream ss;

    assert(us_timings.size() < 10);
    for(auto const& [name, time] : us_timings) {
        ss << std::setw(16) << std::left << name << " " << std::right << std::setw(6) << ema_smoothing(us_timings.at(name), "us_timing_" + name) << '\n';
    }

    ss << "rawStars: " << std::setw(9) << profilingStruct.starsDrawn  << '\n';
    ss << "s.total:  " << std::setw(9) << profilingStruct.starsTotal  <<'\n';
    ss << "chunks:   " << std::setw(5) << profilingStruct.chunksDrawn << '\n';
    ss << "tiles:    " << std::setw(5) << profilingStruct.tilesDrawn  << '\n';
    ss << "lds writes: " << std::setw(5) << profilingStruct.writesShared / 1'000'000u << std::setw(0) << "mil\n";
    ss << "glob writes:" << std::setw(5) << profilingStruct.writesGlobalDirect / 1'000'000u << std::setw(0) << "mil\n";
    double ratio = static_cast<double>(profilingStruct.writesShared) / static_cast<double>(profilingStruct.writesGlobalDirect);
    ss << "lds/glob:   " << std::setw(6) << std::setprecision(6) << std::setfill(' ') << ratio << '\n';

    assert(us_timings.find("raster") != us_timings.end());
    double microSecondsPer1M = static_cast<double>(us_timings.at("raster")) / (static_cast<double>(profilingStruct.starsTotal) / 1'000'000.0);
    ss << "per. 1m rawStars: " << std::setw(6) << ema_smoothing(microSecondsPer1M, "usPerStar") << '\n';
    ss << "job count: " << std::setw(6) << profilingStruct.jobCount << '\n';
    ss << "camera x: " << std::setw(10) << cameraPosParsec.x << '\n';
    ss << "camera y: " << std::setw(10) << cameraPosParsec.y << '\n';
    ss << "camera z: " << std::setw(10) << cameraPosParsec.z << '\n';

    ss << "rGlobalStars: " << memSizeMB("rG", profilingStruct.starsTotal, 8) << '\n';
    ss << "wSharedClear: " << memSizeMB("wS", profilingStruct.writesSharedClear, 8) << '\n';
    ss << "wShared:      " << memSizeMB("wS", profilingStruct.writesShared, 8) << '\n';
    ss << "wGlobalDirect:" << memSizeMB("wG", profilingStruct.writesGlobalDirect, 8) << '\n';
    ss << "wGlobalFlush: " << memSizeMB("wG", profilingStruct.writesGlobalFlush, 8) << '\n';

    double const rastertimeMS = static_cast<double>(us_timings.at("raster")) / 1000.0;
    double mbSum = 0.0;
    for(auto const& [category, bytes] : ioBytesPerCategory) {
        double const megabytes = bytes / (1024.0*1024.0);
        double const bytesPerStar = static_cast<double>(bytes) / profilingStruct.starsDrawn;
        mbSum += megabytes;
        ss << category << " " << std::setw(8) << bytesPerStar << '\n';
    }
    ss << "MB per 4ms:   " << std::setw(8) << ema_smoothing(mbSum / rastertimeMS, "mbp4ms") * 4.0 << '\n';
    ss << "bytes/star:  " << std::setw(8) << ema_smoothing(mbSum / profilingStruct.starsDrawn * 1024.0 * 1024.0, "bpstar") << '\n';
    ss << "us/megabyte: " << std::setw(8) << ema_smoothing(rastertimeMS * 1000.0 / mbSum, "uspmb") << '\n';

    std::string const result_str = ss.str();

    out = result_str;
}
}