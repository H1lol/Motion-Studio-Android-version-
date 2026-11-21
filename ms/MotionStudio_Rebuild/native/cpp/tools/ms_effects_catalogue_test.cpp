// Verifies that every catalogue effect is reachable through cpu_effect_apply
// and actually changes pixels (a silent passthrough would count as failure).
#include "ms_cpu_effects.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

using namespace ms;

static ImageBuffer make_source(int w, int h) {
  ImageBuffer img;
  img.allocate(w, h, PixelFormat::RGBA8);
  uint8_t* p = img.data();
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      uint8_t* o = p + (static_cast<size_t>(y) * w + x) * 4;
      o[0] = static_cast<uint8_t>(x * 255 / (w - 1));
      o[1] = static_cast<uint8_t>(y * 255 / (h - 1));
      o[2] = static_cast<uint8_t>((x + y) * 255 / (w + h - 2));
      o[3] = 255;
    }
  }
  // A saturated green block so chroma keying has something to remove.
  for (int y = 8; y < 24; ++y)
    for (int x = 8; x < 24; ++x) {
      uint8_t* o = p + (static_cast<size_t>(y) * w + x) * 4;
      o[0] = 26; o[1] = 204; o[2] = 26; o[3] = 255;
    }
  return img;
}

static double mean_abs_diff(const ImageBuffer& a, const ImageBuffer& b) {
  const int n = a.width() * a.height() * 4;
  const uint8_t* pa = a.data();
  const uint8_t* pb = b.data();
  long long sum = 0;
  for (int i = 0; i < n; ++i) sum += std::abs(int(pa[i]) - int(pb[i]));
  return double(sum) / n;
}

int main() {
  const int w = 64, h = 64;
  ImageBuffer src = make_source(w, h);
  int failures = 0;

  // Some effects are deliberately identity at their defaults (saturation 1.0,
  // hue 0°, crop 0). Push them off-default so the test proves real work.
  struct Case { const char* id; bool must_change; std::unordered_map<std::string, double> params; };
  using P = std::unordered_map<std::string, double>;
  const Case cases[] = {
      {"saturation", true, P{{"saturation", 1.8}}},
      {"hue_shift", true, P{{"degrees", 60.0}}},
      {"levels", true, P{{"gamma", 2.2}}},
      {"white_balance", true, P{{"temperature", 0.6}}},
      {"posterize", true, P{{"levels", 4.0}}},
      {"sharpen", true, P{{"amount", 1.0}}},
      {"edge_detect", true, P{}},
      {"emboss", true, P{}},
      {"pixelate", true, P{{"size", 8.0}}},
      {"glow", true, P{{"amount", 0.7}}},
      {"noise", true, P{{"amount", 0.3}}},
      {"box_blur", true, P{{"radius", 4.0}}},
      {"crop", true, P{{"left", 0.25}}},
      {"threshold", true, P{{"level", 0.45}}},
      {"chromakey_simple", true, P{}},
      {"chromatic_aberration", true, P{{"amount", 0.8}}},
      {"rgb_split", true, P{{"distance", 6.0}}},
      {"kaleidoscope", true, P{{"segments", 6.0}}},
      {"twirl", true, P{{"angle", 200.0}}},
      {"wave_warp", true, P{{"amplitude", 0.04}}},
      {"halftone", true, P{{"cell", 6.0}}},
      {"scanlines", true, P{{"strength", 0.6}}},
      {"vhs", true, P{{"amount", 0.7}}},
  };

  for (const auto& c : cases) {
    ImageBuffer out;
    const std::unordered_map<std::string, double>& params = c.params;
    Error e = cpu_effect_apply(c.id, src, out, params);
    if (!e.ok()) {
      std::printf("FAIL %-18s apply returned an error: %s\n", c.id, e.message.c_str());
      ++failures;
      continue;
    }
    if (out.width() != w || out.height() != h) {
      std::printf("FAIL %-18s wrong output size %dx%d\n", c.id, out.width(), out.height());
      ++failures;
      continue;
    }
    const double diff = mean_abs_diff(src, out);
    if (c.must_change && diff < 0.5) {
      std::printf("FAIL %-18s passthrough (mean diff %.3f)\n", c.id, diff);
      ++failures;
      continue;
    }
    std::printf("ok   %-18s mean diff %6.2f\n", c.id, diff);
  }

  // The parameters the picker actually ships with (EffectsSheet.kt). Adding an
  // effect must change the picture; several used to be neutral at their defaults
  // (opacity 1.0, levels gamma 1.0, crop 0, white balance 0), so they looked
  // broken until a slider was touched. Keep this table in step with the sheet.
  struct ShipCase { const char* id; P params; };
  const ShipCase shipped[] = {
      {"gaussian_blur", P{{"radius", 4.0}}},
      {"box_blur", P{{"radius", 4.0}}},
      {"sharpen", P{{"amount", 0.6}}},
      {"brightness_contrast", P{{"brightness", 0.06}, {"contrast", 1.1}}},
      {"levels", P{{"black", 0.0}, {"white", 1.0}, {"gamma", 1.2}}},
      {"opacity", P{{"opacity", 0.8}}},
      {"grayscale", P{}},
      {"sepia", P{{"amount", 1.0}}},
      {"invert", P{}},
      {"saturation", P{{"saturation", 1.4}}},
      {"hue_shift", P{{"degrees", 30.0}}},
      {"white_balance", P{{"temperature", 0.2}, {"tint", 0.0}}},
      {"posterize", P{{"levels", 8.0}}},
      {"threshold", P{{"level", 0.5}}},
      {"chromakey", P{{"tolerance", 0.2}}},
      {"duotone", P{{"dark_r", 0.06}, {"dark_g", 0.09}, {"dark_b", 0.16},
                     {"light_r", 1.0}, {"light_g", 0.86}, {"light_b", 0.64},
                     {"mix", 1.0}}},
      {"glow", P{{"amount", 0.5}, {"radius", 8.0}}},
      {"noise", P{{"amount", 0.15}}},
      {"emboss", P{{"amount", 0.5}}},
      {"edge_detect", P{{"amount", 1.0}}},
      {"sketch", P{{"strength", 1.6}, {"paper", 1.0}, {"mix", 1.0}}},
      {"pixelate", P{{"size", 8.0}}},
      {"zoom_blur", P{{"amount", 0.25}, {"samples", 16.0},
                       {"center_x", 0.5}, {"center_y", 0.5}}},
      {"vignette", P{{"strength", 0.6}, {"radius", 1.0}}},
      {"mirror", P{{"horizontal", 1.0}}},
      {"crop", P{{"left", 0.08}, {"right", 0.08}, {"top", 0.08}, {"bottom", 0.08}}},
  };

  std::printf("\n-- shipped defaults must be visible --\n");
  for (const auto& c : shipped) {
    ImageBuffer out;
    Error e = cpu_effect_apply(c.id, src, out, c.params);
    if (!e.ok()) {
      std::printf("FAIL %-20s error %s\n", c.id, e.message.c_str());
      ++failures;
      continue;
    }
    const double diff = mean_abs_diff(src, out);
    if (diff < 0.5) {
      std::printf("FAIL %-20s invisible at its default settings (%.3f)\n", c.id, diff);
      ++failures;
      continue;
    }
    std::printf("ok   %-20s %6.2f\n", c.id, diff);
  }

  // Unknown ids must report EffectNotFound rather than pretend success.
  ImageBuffer dummy;
  Error unknown = cpu_effect_apply("definitely_not_an_effect", src, dummy,
                                   std::unordered_map<std::string, double>{});
  if (unknown.ok()) {
    std::printf("FAIL unknown effect reported success\n");
    ++failures;
  }

  std::printf("\n%s (%d failures)\n", failures == 0 ? "PASS" : "FAIL", failures);
  return failures == 0 ? 0 : 1;
}
