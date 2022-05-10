#pragma once


#define JSON_USE_IMPLICIT_CONVERSIONS 0

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#endif

#include "include/json/json.hpp"
#include "pixel_helpers.h"


using json = nlohmann::json;

// one sample of audio pixel data, which stores max, min, and rms values
// avoid making these when the pixel-to-sample ratio is 1:1, since we can 
// just store the raw sample value in that case
class audio_pixel_t {
public: 
    audio_pixel_t() {};
    audio_pixel_t(double max, double min, double rms) : m_max(max), m_min(min), m_rms(rms) {};

    // linearly interpolate between two pixels
    static audio_pixel_t linear_interpolation(double t, double t0, double t1, audio_pixel_t p0, audio_pixel_t p1){
        audio_pixel_t p;
        p.m_max = linear_interp(t, t0, t1, p0.m_max, p1.m_max);
        p.m_min = linear_interp(t, t0, t1, p0.m_min, p1.m_min);
        p.m_rms = linear_interp(t, t0, t1, p0.m_rms, p1.m_rms);
        return p;
    }

    double m_max{ std::numeric_limits<double>::lowest() };
    double m_min { std::numeric_limits<double>::max() };
    double m_rms { 0 };

public:
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(audio_pixel_t, m_max, m_min, m_rms);
};

// this is the pixel format that the iOS client expects
class haptic_pixel_t {
public: 
    haptic_pixel_t() {};
    haptic_pixel_t(int idx, audio_pixel_t pixel) : 
        id(idx) 
    {
        value = abs(pixel.m_max) + abs(pixel.m_min) / 2; 
    };

    int id { 0 };
    double value { 0 };

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(haptic_pixel_t, id, value);
};

using haptic_pixel_block_t = vec<haptic_pixel_t>;
haptic_pixel_block_t from(const vec<audio_pixel_t>& pixels, int start, int end){
    haptic_pixel_block_t block;
    start = std::clamp(start, 0, (int)pixels.size());
    end = std::clamp(end, start, (int)pixels.size());
    for (int i = start; i < end; i++) {
        block.push_back(haptic_pixel_t(i, pixels.at(i)));
    }
    return block;
}


class audio_pixel_transform_t {
public:
    audio_pixel_transform_t() {};
    
    void normalize(std::shared_ptr<vec<vec<audio_pixel_t>>> block){
        std::vector<double> max_max_field;
        std::vector<double> min_min_field;
        std::vector<double> max_rms_field;

        for (vec<audio_pixel_t>& curr_channel : (*block)) {
            double channel_max_max = std::numeric_limits<double>::lowest();
            double channel_max_min = std::numeric_limits<double>::max();
            double channel_max_rms = std::numeric_limits<double>::lowest();

            for (audio_pixel_t& curr_pixel : curr_channel) {
                channel_max_max = std::max(channel_max_max, curr_pixel.m_max);
                channel_max_min = std::min(channel_max_min, curr_pixel.m_min);
                channel_max_rms = std::max(channel_max_max, curr_pixel.m_rms);
            }

            max_max_field.push_back(channel_max_max);
            min_min_field.push_back(channel_max_min);
            max_rms_field.push_back(channel_max_rms);
        }

        for (int channel_idx = 0; channel_idx < block->size(); channel_idx++) {
            for (audio_pixel_t& curr_pixel : block->at(channel_idx)) {
                curr_pixel.m_max = curr_pixel.m_max/max_max_field.at(channel_idx);
                curr_pixel.m_min = curr_pixel.m_min/min_min_field.at(channel_idx);
                curr_pixel.m_rms = curr_pixel.m_rms/max_rms_field.at(channel_idx);
            } 
        }
    }
};
