// Looks that change the images a phone edit usually wants at the end of a
// stack: a radial zoom smear, a two-colour duotone and a pencil/ink sketch.
//
// Work happens on premultiplied coverage, like the motion/screen families, so a
// resampled pixel cannot pull colour out of a transparent neighbour and a zero
// setting is a byte-for-byte passthrough rather than a lossy round trip.
//
// Every routine is an original implementation of a standard image operation. No
// shader, source file, parameter text or asset from any third-party editor was
// copied; Nova Editor's published feature list was used only to decide which
// operations a phone editor is expected to have.
#include "ms_cpu_effects.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace ms {
namespace {

constexpr int kMaxZoomTaps = 64;

inline float clampf(float v, float lo, float hi) { return std::min(std::max(v, lo), hi); }

struct Plane {
  int w = 0;
  int h = 0;
  std::vector<uint8_t> p;
};

bool plane_from(const ImageBuffer& in, Plane& plane) {
  if (!in.data() || in.width() <= 0 || in.height() <= 0) return false;
  plane.w = in.width();
  plane.h = in.height();
  premultiply_rgba(in, plane.p);
  return true;
}

inline const uint8_t* at(const Plane& s, int x, int y) {
  return s.p.data() + (static_cast<size_t>(y) * s.w + x) * 4;
}

inline void sample_clamped(const Plane& s, float x, float y, float out[4]) {
  const float cx = clampf(x, 0.f, static_cast<float>(s.w - 1));
  const float cy = clampf(y, 0.f, static_cast<float>(s.h - 1));
  const int x0 = static_cast<int>(cx);
  const int y0 = static_cast<int>(cy);
  const int x1 = std::min(x0 + 1, s.w - 1);
  const int y1 = std::min(y0 + 1, s.h - 1);
  const float fx = cx - static_cast<float>(x0);
  const float fy = cy - static_cast<float>(y0);
  const uint8_t* a = at(s, x0, y0);
  const uint8_t* b = at(s, x1, y0);
  const uint8_t* c = at(s, x0, y1);
  const uint8_t* d = at(s, x1, y1);
  for (int i = 0; i < 4; ++i) {
    const float top = a[i] + (b[i] - a[i]) * fx;
    const float bot = c[i] + (d[i] - c[i]) * fx;
    out[i] = top + (bot - top) * fy;
  }
}

inline void put(Plane& dst, int x, int y, const float v[4]) {
  uint8_t* q = dst.p.data() + (static_cast<size_t>(y) * dst.w + x) * 4;
  for (int i = 0; i < 4; ++i)
    q[i] = static_cast<uint8_t>(clampf(v[i], 0.f, 255.f) + 0.5f);
}

Error write_plane(Plane& dst, ImageBuffer& out) {
  out.allocate(dst.w, dst.h, PixelFormat::RGBA8);
  if (!out.data()) return Error::make(ErrorCode::InvalidArgument, "alloc failed");
  std::memcpy(out.data(), dst.p.data(), dst.p.size());
  unpremultiply_rgba(out.data(), static_cast<size_t>(dst.w) * dst.h);
  return Error::success();
}

Error copy_through(const ImageBuffer& in, ImageBuffer& out) {
  if (!in.data() || in.width() <= 0 || in.height() <= 0)
    return Error::make(ErrorCode::InvalidArgument, "empty input");
  out.allocate(in.width(), in.height(), PixelFormat::RGBA8);
  if (!out.data()) return Error::make(ErrorCode::InvalidArgument, "alloc failed");
  std::memcpy(out.data(), in.data(), static_cast<size_t>(in.width()) * in.height() * 4);
  return Error::success();
}

} // namespace

// --------------------------------------------------------------- zoom blur ---

Error cpu_zoom_blur(const ImageBuffer& in, ImageBuffer& out, float amount, int samples,
                    float cx, float cy) {
  if (amount <= 1e-6f) return copy_through(in, out);
  samples = std::clamp(samples, 2, kMaxZoomTaps);
  amount = clampf(amount, 0.f, 0.9f);

  Plane src, dst;
  if (!plane_from(in, src)) return Error::make(ErrorCode::InvalidArgument, "empty input");
  dst.w = src.w;
  dst.h = src.h;
  dst.p.assign(src.p.size(), 0);

  const float ox = clampf(cx, 0.f, 1.f) * static_cast<float>(src.w);
  const float oy = clampf(cy, 0.f, 1.f) * static_cast<float>(src.h);
  const float span = amount * 0.5f;
  const float inv = 1.f / static_cast<float>(samples);

  for (int y = 0; y < src.h; ++y) {
    for (int x = 0; x < src.w; ++x) {
      // Taps run along the ray from the centre through the pixel, centred on it,
      // so a symmetric zoom smear leaves the framing where it was.
      float acc[4] = {0.f, 0.f, 0.f, 0.f};
      for (int s = 0; s < samples; ++s) {
        const float u = static_cast<float>(s) / static_cast<float>(samples) - 0.5f;
        const float scale = 1.f + u * 2.f * span;
        float v[4];
        sample_clamped(src, ox + (static_cast<float>(x) - ox) * scale,
                       oy + (static_cast<float>(y) - oy) * scale, v);
        for (int i = 0; i < 4; ++i) acc[i] += v[i];
      }
      float avg[4] = {acc[0] * inv, acc[1] * inv, acc[2] * inv, acc[3] * inv};
      put(dst, x, y, avg);
    }
  }
  return write_plane(dst, out);
}

// ------------------------------------------------------------------ duotone --

Error cpu_duotone(const ImageBuffer& in, ImageBuffer& out, float dark_r, float dark_g,
                  float dark_b, float light_r, float light_g, float light_b, float mix) {
  mix = clampf(mix, 0.f, 1.f);
  if (mix <= 0.f) return copy_through(in, out);

  Plane src, dst;
  if (!plane_from(in, src)) return Error::make(ErrorCode::InvalidArgument, "empty input");
  dst.w = src.w;
  dst.h = src.h;
  dst.p.assign(src.p.size(), 0);

  const float dr = clampf(dark_r, 0.f, 1.f), dg = clampf(dark_g, 0.f, 1.f),
              db = clampf(dark_b, 0.f, 1.f);
  const float lr = clampf(light_r, 0.f, 1.f), lg = clampf(light_g, 0.f, 1.f),
              lb = clampf(light_b, 0.f, 1.f);

  for (int y = 0; y < src.h; ++y) {
    for (int x = 0; x < src.w; ++x) {
      const uint8_t* sp = at(src, x, y);
      const float a = sp[3] / 255.f;
      // Luma is measured on straight colour: measuring premultiplied values
      // would map a half-covered edge towards the dark end of the ramp.
      const float sr = a > 1e-4f ? sp[0] / a : 0.f;
      const float sg = a > 1e-4f ? sp[1] / a : 0.f;
      const float sb = a > 1e-4f ? sp[2] / a : 0.f;
      const float luma = clampf((0.2126f * sr + 0.7152f * sg + 0.0722f * sb) / 255.f, 0.f, 1.f);
      const float tr = (dr + (lr - dr) * luma) * 255.f;
      const float tg = (dg + (lg - dg) * luma) * 255.f;
      const float tb = (db + (lb - db) * luma) * 255.f;
      // Store premultiplied; write_plane converts back to straight.
      const float v[4] = {sp[0] * (1.f - mix) + tr * a * mix,
                          sp[1] * (1.f - mix) + tg * a * mix,
                          sp[2] * (1.f - mix) + tb * a * mix,
                          static_cast<float>(sp[3])};
      put(dst, x, y, v);
    }
  }
  return write_plane(dst, out);
}

// ------------------------------------------------------------------- sketch --

Error cpu_sketch(const ImageBuffer& in, ImageBuffer& out, float strength, float paper,
                 float mix) {
  mix = clampf(mix, 0.f, 1.f);
  if (mix <= 0.f) return copy_through(in, out);
  strength = clampf(strength, 0.f, 8.f);
  paper = clampf(paper, 0.2f, 1.f);

  Plane src, dst;
  if (!plane_from(in, src)) return Error::make(ErrorCode::InvalidArgument, "empty input");
  dst.w = src.w;
  dst.h = src.h;
  dst.p.assign(src.p.size(), 0);

  const size_t n = static_cast<size_t>(src.w) * src.h;
  std::vector<float> luma(n, 0.f);
  for (size_t i = 0; i < n; ++i) {
    const uint8_t* p = src.p.data() + i * 4;
    luma[i] = 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
  }
  auto lum_at = [&](int x, int y) {
    x = std::clamp(x, 0, src.w - 1);
    y = std::clamp(y, 0, src.h - 1);
    return luma[static_cast<size_t>(y) * src.w + x];
  };

  const float paper_level = paper * 255.f;
  for (int y = 0; y < src.h; ++y) {
    for (int x = 0; x < src.w; ++x) {
      // Sobel gradient. The pencil drawing is the paper with ink where the
      // gradient is strong, so flat areas stay clean and edges go dark.
      const float gx = (lum_at(x + 1, y - 1) + 2.f * lum_at(x + 1, y) + lum_at(x + 1, y + 1)) -
                       (lum_at(x - 1, y - 1) + 2.f * lum_at(x - 1, y) + lum_at(x - 1, y + 1));
      const float gy = (lum_at(x - 1, y + 1) + 2.f * lum_at(x, y + 1) + lum_at(x + 1, y + 1)) -
                       (lum_at(x - 1, y - 1) + 2.f * lum_at(x, y - 1) + lum_at(x + 1, y - 1));
      const float edge = std::sqrt(gx * gx + gy * gy) * 0.125f * strength;
      const float ink = clampf(edge, 0.f, 255.f);
      const float level = clampf(paper_level - ink, 0.f, 255.f);

      const uint8_t* sp = at(src, x, y);
      const float a = sp[3] / 255.f;
      const float v[4] = {sp[0] * (1.f - mix) + level * a * mix,
                          sp[1] * (1.f - mix) + level * a * mix,
                          sp[2] * (1.f - mix) + level * a * mix,
                          static_cast<float>(sp[3])};
      put(dst, x, y, v);
    }
  }
  return write_plane(dst, out);
}

// ----------------------------------------------------------------- dispatch --

bool cpu_effect_looks_try(const std::string& id,
                          const ImageBuffer& in,
                          ImageBuffer& out,
                          const std::unordered_map<std::string, double>& params) {
  auto get = [&](const char* k, double def) {
    auto it = params.find(k);
    return it == params.end() ? def : it->second;
  };
  auto is = [&](const char* bare) {
    return id == bare || id == std::string("org.motionstudio.") + bare;
  };

  if (is("zoom_blur") || is("radial_blur")) {
    cpu_zoom_blur(in, out, static_cast<float>(get("amount", 0.25)),
                  static_cast<int>(get("samples", 16.0)),
                  static_cast<float>(get("center_x", 0.5)),
                  static_cast<float>(get("center_y", 0.5)));
    return true;
  }
  if (is("duotone") || is("two_tone")) {
    cpu_duotone(in, out, static_cast<float>(get("dark_r", 0.06)),
                static_cast<float>(get("dark_g", 0.09)),
                static_cast<float>(get("dark_b", 0.16)),
                static_cast<float>(get("light_r", 1.0)),
                static_cast<float>(get("light_g", 0.86)),
                static_cast<float>(get("light_b", 0.64)),
                static_cast<float>(get("mix", 1.0)));
    return true;
  }
  if (is("sketch") || is("pencil")) {
    cpu_sketch(in, out, static_cast<float>(get("strength", 1.6)),
               static_cast<float>(get("paper", 1.0)),
               static_cast<float>(get("mix", 1.0)));
    return true;
  }
  return false;
}

} // namespace ms
