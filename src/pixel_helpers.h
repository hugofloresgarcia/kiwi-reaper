#pragma once

template<typename T> 
using vec = std::vector<T>;

// helpers!
int time_to_pixel_idx(double time, double pix_per_s) {
    return floor((int)(time * pix_per_s));
}

double pixel_idx_to_time(int pixel_idx, double pix_per_s) {
    return (double)pixel_idx / pix_per_s;
}

int pps_to_samples_per_pix(double pix_per_s, int sample_rate) {
    return ceil((double)sample_rate / pix_per_s);
}

double samples_per_pix_to_pps(int samples_per_pix, int sample_rate) {
    return sample_rate / samples_per_pix;
}

double linear_interp(double x, double x1, double x2, double y1, double y2) {
    // edge cases 
    if ((x2-x1) == 0)
        return x1;
    // interpolation in time domain
    return (((x2 - x) / (x2 - x1)) * y1) + ((x - x1) / (x2 - x1)) * y2;
}

template<typename T>
const vec<T> get_view(const vec<T>& parent, int start, int end)
{
    end = std::min(end, (int)parent.size()); 
    start = std::max(start, 0);   
    // todo: make sure start and  end indices are valid
    auto startptr = parent.cbegin() + start;
    auto endptr = parent.cbegin() + end;
 
    vec<T> vec(startptr, endptr);
    return vec;
}
