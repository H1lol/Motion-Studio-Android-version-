#pragma once
#include "ms_error.hpp"
#include "ms_image.hpp"
#include <string>
#include <unordered_map>
#include <vector>

namespace ms {

/**
 * Alpha helpers for effects that average pixels across neighbours.
 *
 * Buffers reaching an effect hold straight (not premultiplied) colour, and a
 * pixel with alpha 0 can still carry a colour. Averaging straight values lets a
 * transparent black neighbour drag a glyph's edge down, so a blur on a title
 * came out both soft and dark. These two convert around such an average.
 */
void premultiply_rgba(const ImageBuffer& in, std::vector<uint8_t>& out);
/** Converts `pixels` premultiplied RGBA8 values back to straight, in place. */
void unpremultiply_rgba(uint8_t* data, size_t pixels);

/** CPU reference implementations — real pixel work, no GPU required */
Error cpu_effect_apply(const std::string& effect_id,
                       const ImageBuffer& in,
                       ImageBuffer& out,
                       const std::unordered_map<std::string, double>& params);

Error cpu_gaussian_blur(const ImageBuffer& in, ImageBuffer& out, float radius);
Error cpu_brightness_contrast(const ImageBuffer& in, ImageBuffer& out,
                              float brightness, float contrast);
Error cpu_opacity(const ImageBuffer& in, ImageBuffer& out, float opacity);
Error cpu_grayscale(const ImageBuffer& in, ImageBuffer& out);
Error cpu_sepia(const ImageBuffer& in, ImageBuffer& out, float amount);
Error cpu_vignette(const ImageBuffer& in, ImageBuffer& out, float strength, float radius);
Error cpu_mirror(const ImageBuffer& in, ImageBuffer& out, int horizontal);
Error cpu_solid(ImageBuffer& out, int w, int h,
                float r, float g, float b, float a);

// Implemented in cpu_effects_extra.cpp
Error cpu_threshold(const ImageBuffer& in, ImageBuffer& out, float level);
Error cpu_chromakey_simple(const ImageBuffer& in, ImageBuffer& out,
                           float kr, float kg, float kb, float tol);

// Implemented in cpu_effects_catalogue.cpp
Error cpu_saturation(const ImageBuffer& in, ImageBuffer& out, float amount);
Error cpu_hue_shift(const ImageBuffer& in, ImageBuffer& out, float degrees);
Error cpu_levels(const ImageBuffer& in, ImageBuffer& out, float black, float white, float gamma);
Error cpu_white_balance(const ImageBuffer& in, ImageBuffer& out, float temperature, float tint);
Error cpu_posterize(const ImageBuffer& in, ImageBuffer& out, int levels);
Error cpu_sharpen(const ImageBuffer& in, ImageBuffer& out, float amount);
Error cpu_edge_detect(const ImageBuffer& in, ImageBuffer& out, float amount);
Error cpu_emboss(const ImageBuffer& in, ImageBuffer& out, float amount);
Error cpu_pixelate(const ImageBuffer& in, ImageBuffer& out, int size);
Error cpu_glow(const ImageBuffer& in, ImageBuffer& out, float amount, float radius);
Error cpu_noise(const ImageBuffer& in, ImageBuffer& out, float amount, int seed);
Error cpu_box_blur(const ImageBuffer& in, ImageBuffer& out, int radius);
Error cpu_crop(const ImageBuffer& in, ImageBuffer& out,
               float left, float right, float top, float bottom);

// Implemented in cpu_effects_screen.cpp: warps and screens that read the frame
// through a displaced coordinate.
Error cpu_chromatic_aberration(const ImageBuffer& in, ImageBuffer& out,
                               float amount, float cx, float cy);
Error cpu_rgb_split(const ImageBuffer& in, ImageBuffer& out,
                    float distance, float angle_deg);
Error cpu_kaleidoscope(const ImageBuffer& in, ImageBuffer& out,
                       int segments, float rotation_deg, float zoom);
Error cpu_twirl(const ImageBuffer& in, ImageBuffer& out,
                float angle_deg, float radius, float cx, float cy);
Error cpu_wave_warp(const ImageBuffer& in, ImageBuffer& out,
                    float amplitude, float frequency, float phase_deg);
Error cpu_halftone(const ImageBuffer& in, ImageBuffer& out,
                   float cell, float angle_deg, float mix);
Error cpu_scanlines(const ImageBuffer& in, ImageBuffer& out,
                    float spacing, float strength, float phase_deg);
Error cpu_vhs(const ImageBuffer& in, ImageBuffer& out,
              float amount, float tracking, int seed);

// Implemented in cpu_effects_motion.cpp: motion, lens and light-break effects.
Error cpu_motion_blur(const ImageBuffer& in, ImageBuffer& out,
                      float distance, float angle_deg, int samples);
Error cpu_fisheye(const ImageBuffer& in, ImageBuffer& out,
                  float amount, float zoom, float cx, float cy);
Error cpu_glitch(const ImageBuffer& in, ImageBuffer& out,
                 float amount, int bands, int seed);
Error cpu_particles(const ImageBuffer& in, ImageBuffer& out,
                    int count, float size, float speed, float time, int seed);

// Implemented in cpu_effects_looks.cpp: finishing looks.
Error cpu_zoom_blur(const ImageBuffer& in, ImageBuffer& out,
                    float amount, int samples, float cx, float cy);
Error cpu_duotone(const ImageBuffer& in, ImageBuffer& out,
                  float dark_r, float dark_g, float dark_b,
                  float light_r, float light_g, float light_b, float mix);
Error cpu_sketch(const ImageBuffer& in, ImageBuffer& out,
                 float strength, float paper, float mix);

/** Tries the finishing looks; returns true when it wrote `out`. */
bool cpu_effect_looks_try(const std::string& effect_id,
                          const ImageBuffer& in,
                          ImageBuffer& out,
                          const std::unordered_map<std::string, double>& params);

/** Tries the motion/lens/light effects; returns true when it wrote `out`. */
bool cpu_effect_motion_try(const std::string& effect_id,
                           const ImageBuffer& in,
                           ImageBuffer& out,
                           const std::unordered_map<std::string, double>& params);

/** Tries the warp/screen effects; returns true when it wrote `out`. */
bool cpu_effect_screen_try(const std::string& effect_id,
                           const ImageBuffer& in,
                           ImageBuffer& out,
                           const std::unordered_map<std::string, double>& params);

/** Tries the catalogue effects; returns true when it wrote `out`. */
// Defined in cpu_effects_catalogue.cpp; falls through to
// cpu_effect_screen_try for the warp/screen effects.
bool cpu_effect_try(const std::string& effect_id,
                    const ImageBuffer& in,
                    ImageBuffer& out,
                    const std::unordered_map<std::string, double>& params);

} // namespace ms
