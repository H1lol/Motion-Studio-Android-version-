// Motion, lens and light-break effects: a directional blur, a barrel lens warp,
// a gait/tracking glitch and an additive particle field.
//
// Work happens on a premultiplied coverage plane so that a displaced sample
// cannot drag the colour of a transparent neighbour into the edge of a glyph,
// and every "zero" setting is a byte-for-byte passthrough instead of a lossy
// premultiply/unpremultiply round trip.
//
// Every routine here is an original implementation of a standard image
// operation. No shader, source file, parameter text or asset from any
// third-party editor was copied; Nova Editor's published feature list was used
// only to decide which operations a phone editor is expected to have.
#include "ms_cpu_effects.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace ms {
namespace {

constexpr float kPi = 3.14159265358979f;

// A particle field never needs more than this many emitters, and a blur never
// needs more than 64 taps; both ceilings are picked to keep one 1080p frame
// inside a phone's frame budget.
constexpr int kMaxParticles = 512;
constexpr int kMaxBlurTaps = 64;

inline float clampf(float v, float lo, float hi) { return std::min(std::max(v, lo), hi); }

// Deterministic per-coordinate noise so a frame renders identically on every
// run (a glitch that re-rolls each frame is a different effect entirely).
inline float hash01(int x, int y, int seed) {
  uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u +
               static_cast<uint32_t>(seed) * 2246822519u;
  h = (h ^ (h >> 13)) * 1274126177u;
  h ^= h >> 16;
  return static_cast<float>(h & 0xffffff) / 16777215.f;
}

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

// Bilinear sample with edge clamping: a blur must not treat the frame border as
// transparent, or every clip loses a soft dark rim when it is blurred.
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

// Bilinear sample that reads outside the frame as empty. A lens warp pulls
// pixels in from beyond the edge, and an invented edge pixel would smear.
inline void sample_inside(const Plane& s, float x, float y, float out[4]) {
  for (int i = 0; i < 4; ++i) out[i] = 0.f;
  if (x < 0.f || y < 0.f || x > static_cast<float>(s.w - 1) || y > static_cast<float>(s.h - 1))
    return;
  const int x0 = static_cast<int>(x);
  const int y0 = static_cast<int>(y);
  const int x1 = std::min(x0 + 1, s.w - 1);
  const int y1 = std::min(y0 + 1, s.h - 1);
  const float fx = x - static_cast<float>(x0);
  const float fy = y - static_cast<float>(y0);
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

// ------------------------------------------------------- directional blur ----

Error cpu_motion_blur(const ImageBuffer& in, ImageBuffer& out,
                      float distance, float angle_deg, int samples) {
  if (distance <= 0.f) return copy_through(in, out);
  samples = std::clamp(samples, 1, kMaxBlurTaps);
  distance = clampf(distance, 0.f, 256.f);

  Plane src, dst;
  if (!plane_from(in, src)) return Error::make(ErrorCode::InvalidArgument, "empty input");
  dst.w = src.w;
  dst.h = src.h;
  dst.p.assign(src.p.size(), 0);

  const float rad = angle_deg * kPi / 180.f;
  const float dx = std::cos(rad);
  const float dy = std::sin(rad);
  const float inv = 1.f / static_cast<float>(samples);

  for (int y = 0; y < src.h; ++y) {
    for (int x = 0; x < src.w; ++x) {
      float acc[4] = {0.f, 0.f, 0.f, 0.f};
      for (int s = 0; s < samples; ++s) {
        // Taps are spread over the whole shutter interval, centred on the
        // pixel, so a symmetric blur leaves a static frame in place.
        const float t = (static_cast<float>(s) / static_cast<float>(samples) - 0.5f) * distance;
        float v[4];
        sample_clamped(src, static_cast<float>(x) + dx * t, static_cast<float>(y) + dy * t, v);
        for (int i = 0; i < 4; ++i) acc[i] += v[i];
      }
      float avg[4] = {acc[0] * inv, acc[1] * inv, acc[2] * inv, acc[3] * inv};
      put(dst, x, y, avg);
    }
  }
  return write_plane(dst, out);
}

// ---------------------------------------------------------------- fisheye ----

Error cpu_fisheye(const ImageBuffer& in, ImageBuffer& out,
                  float amount, float zoom, float cx, float cy) {
  if (std::fabs(amount) < 1e-6f && std::fabs(zoom - 1.f) < 1e-6f) return copy_through(in, out);

  Plane src, dst;
  if (!plane_from(in, src)) return Error::make(ErrorCode::InvalidArgument, "empty input");
  dst.w = src.w;
  dst.h = src.h;
  dst.p.assign(src.p.size(), 0);

  amount = clampf(amount, -0.9f, 1.f);
  zoom = clampf(zoom, 0.1f, 4.f);
  const float half = 0.5f * static_cast<float>(std::min(src.w, src.h));
  const float cxp = clampf(cx, 0.f, 1.f) * static_cast<float>(src.w);
  const float cyp = clampf(cy, 0.f, 1.f) * static_cast<float>(src.h);

  for (int y = 0; y < src.h; ++y) {
    for (int x = 0; x < src.w; ++x) {
      // Inverse map: for each destination pixel ask where it came from. A
      // positive amount expands the centre (barrel glass), negative pinches it.
      const float nx = (static_cast<float>(x) + 0.5f - cxp) / half;
      const float ny = (static_cast<float>(y) + 0.5f - cyp) / half;
      const float r2 = nx * nx + ny * ny;
      const float scale = (1.f + amount * r2) / zoom;
      float v[4];
      sample_inside(src, cxp + nx * scale * half, cyp + ny * scale * half, v);
      put(dst, x, y, v);
    }
  }
  return write_plane(dst, out);
}

// ----------------------------------------------------------------- glitch ----

Error cpu_glitch(const ImageBuffer& in, ImageBuffer& out,
                 float amount, int bands, int seed) {
  if (amount <= 0.f) return copy_through(in, out);

  Plane src, dst;
  if (!plane_from(in, src)) return Error::make(ErrorCode::InvalidArgument, "empty input");
  dst.w = src.w;
  dst.h = src.h;
  dst.p.assign(src.p.size(), 0);

  amount = clampf(amount, 0.f, 1.f);
  bands = std::clamp(bands, 1, 128);
  const int band_h = std::max(1, src.h / bands);
  // Two scales of defect: whole bands slip sideways, and the channels separate
  // so a hard edge fringes. Both grow from the same amount knob.
  const float shift_px = amount * 0.12f * static_cast<float>(src.w);
  const float fringe_px = amount * 8.f;

  for (int y = 0; y < src.h; ++y) {
    const int band = y / band_h;
    const float drift = (hash01(0, band, seed) * 2.f - 1.f) * shift_px;
    // A minority of bands tear instead of drifting, which reads as a dropped
    // row rather than a smooth wobble.
    const bool torn = hash01(1, band, seed) > 0.86f;
    const float tear = torn ? (hash01(2, band, seed) * 2.f - 1.f) * shift_px * 1.8f : 0.f;
    const float fx = drift + tear;

    for (int x = 0; x < src.w; ++x) {
      float r[4], g[4], b[4], base[4];
      sample_clamped(src, static_cast<float>(x) + fx + fringe_px, static_cast<float>(y), r);
      sample_clamped(src, static_cast<float>(x) + fx, static_cast<float>(y), g);
      sample_clamped(src, static_cast<float>(x) + fx - fringe_px, static_cast<float>(y), b);
      sample_clamped(src, static_cast<float>(x) + fx, static_cast<float>(y), base);
      // Coverage comes from the undispersed sample: a channel split moves
      // colour, never the silhouette.
      float v[4] = {r[0], g[1], b[2], base[3]};
      put(dst, x, y, v);
    }
  }
  return write_plane(dst, out);
}

// -------------------------------------------------------------- particles ----

Error cpu_particles(const ImageBuffer& in, ImageBuffer& out,
                    int count, float size, float speed, float time, int seed) {
  count = std::clamp(count, 0, kMaxParticles);
  if (count == 0) return copy_through(in, out);

  Plane src, dst;
  if (!plane_from(in, src)) return Error::make(ErrorCode::InvalidArgument, "empty input");
  dst.w = src.w;
  dst.h = src.h;
  dst.p.assign(src.p.size(), 0);

  size = clampf(size, 0.5f, 64.f);
  speed = clampf(speed, 0.f, 4.f);
  time = clampf(time, 0.f, 3600.f);

  // Light is accumulated separately and added once: two particles crossing must
  // sum like light, not overwrite each other like paint.
  std::vector<float> light(static_cast<size_t>(src.w) * src.h * 3, 0.f);
  const float radius = size;
  const float inv_r2 = 1.f / (radius * radius);

  for (int i = 0; i < count; ++i) {
    const float phase_x = hash01(i, 11, seed);
    const float phase_y = hash01(i, 23, seed);
    const float drift_x = (hash01(i, 31, seed) * 2.f - 1.f) * speed * 0.12f;
    const float drift_y = (hash01(i, 41, seed) * 2.f - 1.f) * speed * 0.12f - speed * 0.05f;
    float px = phase_x + drift_x * time;
    float py = phase_y + drift_y * time;
    px -= std::floor(px);
    py -= std::floor(py);
    const float cx = px * static_cast<float>(src.w);
    const float cy = py * static_cast<float>(src.h);

    // Three warm/cool light temperatures distributed over the field.
    const float pick = hash01(i, 53, seed);
    const float cr = pick < 0.5f ? 1.f : 0.72f;
    const float cg = pick < 0.5f ? 0.86f : 0.84f;
    const float cb = pick < 0.5f ? 0.55f : 1.f;
    const float gain = 0.55f + hash01(i, 61, seed) * 0.45f;

    const int x0 = std::max(0, static_cast<int>(cx - radius));
    const int x1 = std::min(src.w - 1, static_cast<int>(cx + radius) + 1);
    const int y0 = std::max(0, static_cast<int>(cy - radius));
    const int y1 = std::min(src.h - 1, static_cast<int>(cy + radius) + 1);
    for (int y = y0; y <= y1; ++y) {
      for (int x = x0; x <= x1; ++x) {
        const float dx = static_cast<float>(x) + 0.5f - cx;
        const float dy = static_cast<float>(y) + 0.5f - cy;
        const float d2 = dx * dx + dy * dy;
        if (d2 > radius * radius) continue;
        const float falloff = 1.f - d2 * inv_r2;
        const float w = falloff * falloff * gain;
        float* l = light.data() + (static_cast<size_t>(y) * src.w + x) * 3;
        l[0] += cr * w;
        l[1] += cg * w;
        l[2] += cb * w;
      }
    }
  }

  constexpr float kLightGain = 0.85f;  // additive upper bound per channel
  for (int y = 0; y < src.h; ++y) {
    for (int x = 0; x < src.w; ++x) {
      const float* l = light.data() + (static_cast<size_t>(y) * src.w + x) * 3;
      const uint8_t* sp = at(src, x, y);
      const float peak = std::max({l[0], l[1], l[2]});
      if (peak <= 0.f) {
        float v[4] = {static_cast<float>(sp[0]), static_cast<float>(sp[1]),
                      static_cast<float>(sp[2]), static_cast<float>(sp[3])};
        put(dst, x, y, v);
        continue;
      }
      // The field is light: it raises coverage too, otherwise the compositor
      // would multiply the added colour back down by an unchanged alpha.
      float v[4] = {
          std::min(255.f, static_cast<float>(sp[0]) + l[0] * 255.f * kLightGain),
          std::min(255.f, static_cast<float>(sp[1]) + l[1] * 255.f * kLightGain),
          std::min(255.f, static_cast<float>(sp[2]) + l[2] * 255.f * kLightGain),
          std::min(255.f, static_cast<float>(sp[3]) + peak * 255.f * kLightGain),
      };
      put(dst, x, y, v);
    }
  }
  return write_plane(dst, out);
}

// ---------------------------------------------------------------- dispatch ---

bool cpu_effect_motion_try(const std::string& id,
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

  if (is("motion_blur") || is("direction_blur")) {
    cpu_motion_blur(in, out, static_cast<float>(get("distance", 12.0)),
                    static_cast<float>(get("angle", 0.0)),
                    static_cast<int>(get("samples", 16.0)));
    return true;
  }
  if (is("fisheye") || is("lens_distortion")) {
    cpu_fisheye(in, out, static_cast<float>(get("amount", 0.5)),
                static_cast<float>(get("zoom", 1.0)),
                static_cast<float>(get("center_x", 0.5)),
                static_cast<float>(get("center_y", 0.5)));
    return true;
  }
  if (is("glitch") || is("datamosh")) {
    cpu_glitch(in, out, static_cast<float>(get("amount", 0.5)),
               static_cast<int>(get("bands", 24.0)),
               static_cast<int>(get("seed", 1.0)));
    return true;
  }
  if (is("particles") || is("sparks")) {
    cpu_particles(in, out, static_cast<int>(get("count", 64.0)),
                  static_cast<float>(get("size", 6.0)),
                  static_cast<float>(get("speed", 0.6)),
                  static_cast<float>(get("time", 0.0)),
                  static_cast<int>(get("seed", 1.0)));
    return true;
  }
  return false;
}

} // namespace ms
