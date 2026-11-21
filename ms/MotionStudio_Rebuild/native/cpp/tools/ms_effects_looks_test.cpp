// Properties of the finishing looks: zero settings are identities, resampling
// never invents colour under zero coverage, the duotone ramp lands on the two
// colours at the ends of luma, and the sketch keeps flat paper clean while edges
// go dark.
#include "ms_cpu_effects.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>

using namespace ms;
using P = std::unordered_map<std::string, double>;

static int failures = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL %s\n", what);
    ++failures;
  }
}

static ImageBuffer make_solid(int w, int h, int r, int g, int b, int a) {
  ImageBuffer img;
  img.allocate(w, h, PixelFormat::RGBA8);
  uint8_t* p = img.data();
  for (int i = 0; i < w * h; ++i) {
    p[i * 4 + 0] = static_cast<uint8_t>(r);
    p[i * 4 + 1] = static_cast<uint8_t>(g);
    p[i * 4 + 2] = static_cast<uint8_t>(b);
    p[i * 4 + 3] = static_cast<uint8_t>(a);
  }
  return img;
}

// Left half white, right half black, plus a bright-garbage transparent corner.
static ImageBuffer make_edge_frame(int w, int h) {
  ImageBuffer img;
  img.allocate(w, h, PixelFormat::RGBA8);
  uint8_t* p = img.data();
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      uint8_t* o = p + (static_cast<size_t>(y) * w + x) * 4;
      const bool left = x < w / 2;
      o[0] = o[1] = o[2] = left ? 255 : 0;
      o[3] = 255;
      if (x >= w - 4 && y >= h - 4) {  // transparent, but bright underneath
        o[0] = o[1] = o[2] = 255;
        o[3] = 0;
      }
    }
  }
  return img;
}

static bool identical(const ImageBuffer& a, const ImageBuffer& b) {
  if (a.width() != b.width() || a.height() != b.height() || !a.data() || !b.data()) return false;
  return std::memcmp(a.data(), b.data(),
                     static_cast<size_t>(a.width()) * a.height() * 4) == 0;
}

int main() {
  const int w = 64, h = 48;
  const ImageBuffer white = make_solid(w, h, 255, 255, 255, 255);
  const ImageBuffer black = make_solid(w, h, 0, 0, 0, 255);
  const ImageBuffer edge = make_edge_frame(w, h);

  // 1. Every look is exactly the input at its zero setting, with no round trip.
  {
    ImageBuffer out;
    check(cpu_effect_apply("zoom_blur", edge, out, P{{"amount", 0.0}}).ok(),
          "zoom_blur applies");
    check(identical(edge, out), "zoom_blur amount 0 is identity");

    check(cpu_effect_apply("duotone", edge, out, P{{"mix", 0.0}}).ok(), "duotone applies");
    check(identical(edge, out), "duotone mix 0 is identity");

    check(cpu_effect_apply("sketch", edge, out, P{{"mix", 0.0}}).ok(), "sketch applies");
    check(identical(edge, out), "sketch mix 0 is identity");
  }

  // 2. A zoom smear must not darken an opaque white frame or drag colour out of
  //    a transparent region.
  {
    ImageBuffer out;
    check(cpu_effect_apply("zoom_blur", white, out,
                           P{{"amount", 0.25}, {"samples", 12}}).ok(),
          "zoom_blur on opaque input applies");
    bool still_white = out.width() == w && out.height() == h;
    for (int i = 0; still_white && i < w * h; ++i)
      still_white = out.data()[i * 4 + 0] == 255 && out.data()[i * 4 + 3] == 255;
    check(still_white, "zoom_blur leaves an opaque white frame white");

    check(cpu_effect_apply("zoom_blur", edge, out,
                           P{{"amount", 0.35}, {"samples", 16}}).ok(),
          "zoom_blur on mixed input applies");
    // A blur legitimately spreads coverage, so the property to keep is that a
    // pixel with zero coverage never carries colour.
    bool clean = out.width() == w && out.height() == h;
    for (int i = 0; clean && i < w * h; ++i) {
      const uint8_t* o = out.data() + i * 4;
      if (o[3] == 0 && (o[0] || o[1] || o[2])) clean = false;
    }
    check(clean, "zoom_blur keeps colour out of transparent pixels");
    check(!identical(edge, out), "zoom_blur changes the frame");
  }

  // 3. The duotone ramp hits the dark colour at black and the light one at
  //    white, keeps coverage, and leaves transparent pixels empty.
  {
    ImageBuffer dark_out, light_out;
    const P p{{"dark_r", 0.06}, {"dark_g", 0.09}, {"dark_b", 0.16},
              {"light_r", 1.0},  {"light_g", 0.86}, {"light_b", 0.64},
              {"mix", 1.0}};
    check(cpu_effect_apply("duotone", black, dark_out, p).ok(), "duotone on black");
    check(cpu_effect_apply("duotone", white, light_out, p).ok(), "duotone on white");
    const uint8_t* d = dark_out.data();
    const uint8_t* l = light_out.data();
    check(d && std::abs(d[0] - 15) <= 2 && std::abs(d[1] - 23) <= 2 &&
              std::abs(d[2] - 41) <= 2,
          "black maps to the dark colour");
    check(l && std::abs(l[0] - 255) <= 2 && std::abs(l[1] - 219) <= 2 &&
              std::abs(l[2] - 163) <= 2,
          "white maps to the light colour");
    check(d && l && d[3] == 255 && l[3] == 255, "duotone keeps coverage");

    ImageBuffer alpha_out;
    check(cpu_effect_apply("duotone", edge, alpha_out, p).ok(), "duotone on mixed input");
    bool clean = alpha_out.width() == w && alpha_out.height() == h;
    for (int y = h - 4; clean && y < h; ++y)
      for (int x = w - 4; x < w; ++x) {
        const uint8_t* o = alpha_out.data() + (static_cast<size_t>(y) * w + x) * 4;
        if (o[3] != 0 || o[0] || o[1] || o[2]) clean = false;
      }
    check(clean, "duotone leaves transparent pixels empty");
  }

  // 4. The sketch keeps flat paper clean, darkens an edge, and never invents
  //    coverage.
  {
    ImageBuffer paper_out, edge_out;
    check(cpu_effect_apply("sketch", white, paper_out,
                           P{{"strength", 1.6}, {"paper", 1.0}, {"mix", 1.0}}).ok(),
          "sketch on flat paper");
    const uint8_t* p = paper_out.data();
    check(p && p[0] >= 250 && p[3] == 255, "flat paper stays clean");

    check(cpu_effect_apply("sketch", edge, edge_out,
                           P{{"strength", 1.6}, {"paper", 1.0}, {"mix", 1.0}}).ok(),
          "sketch on an edge");
    int darkest = 255;
    bool keeps_coverage = edge_out.width() == w && edge_out.height() == h;
    for (int i = 0; keeps_coverage && i < w * h; ++i) {
      const uint8_t* o = edge_out.data() + i * 4;
      const uint8_t* s = edge.data() + i * 4;
      darkest = std::min(darkest, static_cast<int>(o[0]));
      if (o[3] != s[3]) keeps_coverage = false;
    }
    check(keeps_coverage, "sketch preserves coverage");
    check(darkest < 140, "sketch inks the edge");

    ImageBuffer transparent_out;
    check(cpu_effect_apply("sketch", edge, transparent_out,
                           P{{"strength", 1.6}, {"mix", 1.0}}).ok(),
          "sketch on mixed input");
    bool clean = true;
    for (int y = h - 4; clean && y < h; ++y)
      for (int x = w - 4; x < w; ++x) {
        const uint8_t* o = transparent_out.data() + (static_cast<size_t>(y) * w + x) * 4;
        if (o[3] != 0 || o[0] || o[1] || o[2]) clean = false;
      }
    check(clean, "sketch keeps colour out of transparent pixels");
  }

  // 5. Every look is reachable through the catalogue dispatcher and repeats
  //    byte-for-byte.
  {
    ImageBuffer a, b;
    const P z{{"amount", 0.2}, {"samples", 10}};
    check(cpu_effect_apply("zoom_blur", edge, a, z).ok(), "zoom_blur dispatches");
    check(cpu_effect_apply("zoom_blur", edge, b, z).ok(), "zoom_blur dispatches again");
    check(identical(a, b), "zoom_blur is deterministic");

    const P d{{"mix", 0.5}};
    check(cpu_effect_apply("duotone", edge, a, d).ok(), "duotone dispatches");
    check(cpu_effect_apply("duotone", edge, b, d).ok(), "duotone dispatches again");
    check(identical(a, b), "duotone is deterministic");

    const P s{{"strength", 2.0}, {"mix", 1.0}};
    check(cpu_effect_apply("sketch", edge, a, s).ok(), "sketch dispatches");
    check(cpu_effect_apply("sketch", edge, b, s).ok(), "sketch dispatches again");
    check(identical(a, b), "sketch is deterministic");
  }

  if (failures == 0) std::printf("ms_effects_looks_test: all checks passed\n");
  return failures == 0 ? 0 : 1;
}
