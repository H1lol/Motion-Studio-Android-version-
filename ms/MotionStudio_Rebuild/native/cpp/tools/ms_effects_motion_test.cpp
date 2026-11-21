// Properties of the motion/lens/light effects that a "did pixels change?" check
// cannot see: zero settings are true identities, a directional blur neither
// darkens an opaque frame nor drags colour out of transparent pixels, a glitch
// moves colour without touching coverage, a lens warp reads the outside as
// empty rather than smearing the border, and a particle field only ever adds.
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

static ImageBuffer make_alpha_frame(int w, int h) {
  ImageBuffer img;
  img.allocate(w, h, PixelFormat::RGBA8);
  uint8_t* p = img.data();
  const float cx = w * 0.5f, cy = h * 0.5f, r = std::min(w, h) * 0.30f;
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      uint8_t* o = p + (static_cast<size_t>(y) * w + x) * 4;
      const float d = std::hypot(x - cx, y - cy);
      const float a = d < r ? 1.f : (d < r + 2.f ? (r + 2.f - d) / 2.f : 0.f);
      o[0] = static_cast<uint8_t>(x * 255 / (w - 1));
      o[1] = static_cast<uint8_t>(y * 255 / (h - 1));
      o[2] = 200;
      o[3] = static_cast<uint8_t>(a * 255.f + 0.5f);
      if (a == 0.f) o[0] = o[1] = o[2] = 255;  // bright garbage under alpha 0
    }
  }
  return img;
}

static ImageBuffer make_opaque(int w, int h) {
  ImageBuffer img;
  img.allocate(w, h, PixelFormat::RGBA8);
  uint8_t* p = img.data();
  for (int i = 0; i < w * h; ++i) {
    p[i * 4 + 0] = 255;
    p[i * 4 + 1] = 255;
    p[i * 4 + 2] = 255;
    p[i * 4 + 3] = 255;
  }
  return img;
}

static bool identical(const ImageBuffer& a, const ImageBuffer& b) {
  if (a.width() != b.width() || a.height() != b.height() || !a.data() || !b.data()) return false;
  return std::memcmp(a.data(), b.data(),
                     static_cast<size_t>(a.width()) * a.height() * 4) == 0;
}

int main() {
  const int w = 96, h = 64;
  const ImageBuffer src = make_alpha_frame(w, h);

  // 1. A zero shutter is not a blur.
  {
    ImageBuffer out;
    check(cpu_effect_apply("motion_blur", src, out, P{{"distance", 0.0}}).ok(),
          "motion_blur applies");
    check(identical(src, out), "motion_blur distance 0 is identity");
  }

  // 2. A blur must not darken an opaque frame: the classic straight-alpha bug.
  {
    const ImageBuffer white = make_opaque(w, h);
    ImageBuffer out;
    check(cpu_effect_apply("motion_blur", white, out, P{{"distance", 9.0}, {"samples", 8}}).ok(),
          "motion_blur on opaque input applies");
    bool still_white = out.width() == w && out.height() == h;
    for (int i = 0; still_white && i < w * h; ++i)
      still_white = out.data()[i * 4 + 0] == 255 && out.data()[i * 4 + 3] == 255;
    check(still_white, "motion_blur leaves an opaque white frame white");
  }

  // 3. A blur must not pull the colour out of a transparent region.
  {
    ImageBuffer out;
    check(cpu_effect_apply("motion_blur", src, out, P{{"distance", 6.0}, {"samples", 8}}).ok(),
          "motion_blur on alpha input applies");
    const int margin = 14;  // beyond the shutter reach (6 px)
    bool clean = out.width() == w && out.height() == h;
    for (int y = 0; clean && y < h; ++y)
      for (int x = 0; x < w; ++x) {
        bool clear_source = true;
        for (int yy = std::max(0, y - margin); yy <= std::min(h - 1, y + margin) && clear_source; ++yy)
          for (int xx = std::max(0, x - margin); xx <= std::min(w - 1, x + margin); ++xx)
            if (src.data()[(static_cast<size_t>(yy) * w + xx) * 4 + 3] != 0) {
              clear_source = false;
              break;
            }
        if (!clear_source) continue;
        const uint8_t* o = out.data() + (static_cast<size_t>(y) * w + x) * 4;
        if (o[3] != 0 || o[0] != 0 || o[1] != 0 || o[2] != 0) clean = false;
      }
    check(clean, "motion_blur keeps transparent areas empty");
  }

  // 4. A lens warp with no amount and no zoom is the identity.
  {
    ImageBuffer out;
    check(cpu_effect_apply("fisheye", src, out,
                           P{{"amount", 0.0}, {"zoom", 1.0}}).ok(),
          "fisheye applies");
    check(identical(src, out), "fisheye amount 0 / zoom 1 is identity");
  }

  // 5. The warp reads beyond the frame as empty, not as a smeared border.
  {
    ImageBuffer out;
    check(cpu_effect_apply("fisheye", src, out,
                           P{{"amount", 0.8}, {"zoom", 1.0}}).ok(),
          "fisheye warp applies");
    check(!identical(src, out), "fisheye warp changes the frame");
    bool corners_empty = true;
    const int pts[4][2] = {{0, 0}, {w - 1, 0}, {0, h - 1}, {w - 1, h - 1}};
    for (auto& pt : pts) {
      const uint8_t* o = out.data() + (static_cast<size_t>(pt[1]) * w + pt[0]) * 4;
      if (o[3] != 0 || o[0] != 0 || o[1] != 0 || o[2] != 0) corners_empty = false;
    }
    check(corners_empty, "fisheye corners are empty, not smeared");
  }

  // 6. A glitch tears whole rows, so coverage moves with the tear, but a
  //    straight-alpha result must never carry colour under zero coverage.
  {
    ImageBuffer out;
    check(cpu_effect_apply("glitch", src, out, P{{"amount", 0.0}}).ok(), "glitch applies");
    check(identical(src, out), "glitch amount 0 is identity");

    check(cpu_effect_apply("glitch", src, out,
                           P{{"amount", 0.7}, {"bands", 16}, {"seed", 2}}).ok(),
          "glitch displacement applies");
    bool clean_alpha = out.width() == w && out.height() == h;
    for (int i = 0; clean_alpha && i < w * h; ++i) {
      const uint8_t* o = out.data() + i * 4;
      if (o[3] == 0 && (o[0] || o[1] || o[2])) clean_alpha = false;
    }
    check(clean_alpha, "glitch keeps colour out of transparent pixels");
    check(!identical(src, out), "glitch changes colour");
  }

  // 7. A particle field with no emitters is the identity.
  {
    ImageBuffer out;
    check(cpu_effect_apply("particles", src, out, P{{"count", 0.0}}).ok(), "particles applies");
    check(identical(src, out), "particles count 0 is identity");
  }

  // 8. The field adds light and coverage, never removes either, and is stable.
  {
    ImageBuffer a, b;
    const P params{{"count", 40}, {"size", 7.0}, {"speed", 0.5}, {"time", 1.5}, {"seed", 4}};
    check(cpu_effect_apply("particles", src, a, params).ok(), "particle field applies");
    check(cpu_effect_apply("particles", src, b, params).ok(), "particle field repeats");
    check(identical(a, b), "particles render deterministically");

    // The field is light, so it may tint a pixel (a warm spark over blue does
    // lower the blue value), but it must never take coverage away.
    bool adds = false, keeps_coverage = true;
    for (int i = 0; i < w * h; ++i) {
      const uint8_t* o = a.data() + i * 4;
      const uint8_t* s = src.data() + i * 4;
      if (o[3] > s[3]) adds = true;
      if (o[3] < s[3]) keeps_coverage = false;
    }
    check(adds, "particles raise coverage somewhere");
    check(keeps_coverage, "particles never remove coverage");
  }

  if (failures == 0) std::printf("ms_effects_motion_test: all checks passed\n");
  return failures == 0 ? 0 : 1;
}
