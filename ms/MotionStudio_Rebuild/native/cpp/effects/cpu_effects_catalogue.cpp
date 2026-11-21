// Additional CPU effects.
//
// Every routine here is an original implementation of a standard image
// operation (levels curve, Sobel gradient, screen blend, …). No code, XML or
// parameter text is taken from Kdenlive/MLT/frei0r, which are GPL-3.0.
#include "ms_cpu_effects.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace ms {
namespace {

inline const uint8_t* px(const ImageBuffer& img, int x, int y) {
  x = std::clamp(x, 0, img.width() - 1);
  y = std::clamp(y, 0, img.height() - 1);
  return img.data() + (static_cast<size_t>(y) * img.width() + x) * 4;
}

inline float luma709(const uint8_t* p) {
  return (0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2]) / 255.f;
}

inline uint8_t to8(float v) {
  return static_cast<uint8_t>(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f);
}

// Cheap deterministic noise so a given frame always renders identically.
inline float hash01(int x, int y, int seed) {
  uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u +
               static_cast<uint32_t>(seed) * 2246822519u;
  h = (h ^ (h >> 13)) * 1274126177u;
  h ^= h >> 16;
  return (h & 0xffffff) / 16777215.f;
}

bool need_input(const ImageBuffer& in, ImageBuffer& out) {
  if (!in.data() || in.width() <= 0 || in.height() <= 0) return false;
  out.allocate(in.width(), in.height(), PixelFormat::RGBA8);
  return out.data() != nullptr;
}

} // namespace

// ---------------------------------------------------------------- colour ----

Error cpu_saturation(const ImageBuffer& in, ImageBuffer& out, float amount) {
  if (!need_input(in, out)) return Error::make(ErrorCode::InvalidArgument, "empty input");
  const int n = in.width() * in.height();
  const uint8_t* s = in.data();
  uint8_t* d = out.data();
  amount = std::max(amount, 0.f);
  for (int i = 0; i < n; ++i) {
    float l = luma709(s);
    for (int c = 0; c < 3; ++c) {
      float v = s[c] / 255.f;
      d[c] = to8(l + (v - l) * amount);
    }
    d[3] = s[3];
    s += 4; d += 4;
  }
  return Error::success();
}

Error cpu_hue_shift(const ImageBuffer& in, ImageBuffer& out, float degrees) {
  if (!need_input(in, out)) return Error::make(ErrorCode::InvalidArgument, "empty input");
  const int n = in.width() * in.height();
  const uint8_t* s = in.data();
  uint8_t* d = out.data();
  const float rad = degrees * 3.14159265358979f / 180.f;
  const float cs = std::cos(rad), sn = std::sin(rad);
  // Rotation about the grey axis derived from the two chroma planes.
  const float m[3][3] = {
      {0.213f + cs * 0.787f - sn * 0.213f, 0.715f - cs * 0.715f - sn * 0.715f, 0.072f - cs * 0.072f + sn * 0.928f},
      {0.213f - cs * 0.213f + sn * 0.143f, 0.715f + cs * 0.285f + sn * 0.140f, 0.072f - cs * 0.072f - sn * 0.283f},
      {0.213f - cs * 0.213f - sn * 0.787f, 0.715f - cs * 0.715f + sn * 0.715f, 0.072f + cs * 0.928f + sn * 0.072f}};
  for (int i = 0; i < n; ++i) {
    float r = s[0] / 255.f, g = s[1] / 255.f, b = s[2] / 255.f;
    d[0] = to8(m[0][0] * r + m[0][1] * g + m[0][2] * b);
    d[1] = to8(m[1][0] * r + m[1][1] * g + m[1][2] * b);
    d[2] = to8(m[2][0] * r + m[2][1] * g + m[2][2] * b);
    d[3] = s[3];
    s += 4; d += 4;
  }
  return Error::success();
}

Error cpu_levels(const ImageBuffer& in, ImageBuffer& out, float black, float white, float gamma) {
  if (!need_input(in, out)) return Error::make(ErrorCode::InvalidArgument, "empty input");
  const int n = in.width() * in.height();
  const uint8_t* s = in.data();
  uint8_t* d = out.data();
  black = std::clamp(black, 0.f, 0.99f);
  white = std::clamp(white, black + 0.01f, 1.f);
  gamma = std::clamp(gamma, 0.05f, 5.f);
  const float inv_g = 1.f / gamma;
  for (int i = 0; i < n; ++i) {
    for (int c = 0; c < 3; ++c) {
      float v = (s[c] / 255.f - black) / (white - black);
      v = std::clamp(v, 0.f, 1.f);
      d[c] = to8(std::pow(v, inv_g));
    }
    d[3] = s[3];
    s += 4; d += 4;
  }
  return Error::success();
}

Error cpu_white_balance(const ImageBuffer& in, ImageBuffer& out, float temperature, float tint) {
  if (!need_input(in, out)) return Error::make(ErrorCode::InvalidArgument, "empty input");
  const int n = in.width() * in.height();
  const uint8_t* s = in.data();
  uint8_t* d = out.data();
  temperature = std::clamp(temperature, -1.f, 1.f);
  tint = std::clamp(tint, -1.f, 1.f);
  const float gr = 1.f + temperature * 0.30f;
  const float gg = 1.f + tint * 0.15f;
  const float gb = 1.f - temperature * 0.30f;
  for (int i = 0; i < n; ++i) {
    d[0] = to8(s[0] / 255.f * gr);
    d[1] = to8(s[1] / 255.f * gg);
    d[2] = to8(s[2] / 255.f * gb);
    d[3] = s[3];
    s += 4; d += 4;
  }
  return Error::success();
}

Error cpu_posterize(const ImageBuffer& in, ImageBuffer& out, int levels) {
  if (!need_input(in, out)) return Error::make(ErrorCode::InvalidArgument, "empty input");
  levels = std::clamp(levels, 2, 64);
  const int n = in.width() * in.height();
  const uint8_t* s = in.data();
  uint8_t* d = out.data();
  const float steps = static_cast<float>(levels - 1);
  for (int i = 0; i < n; ++i) {
    for (int c = 0; c < 3; ++c)
      d[c] = static_cast<uint8_t>(std::round(std::round(s[c] / 255.f * steps) / steps * 255.f));
    d[3] = s[3];
    s += 4; d += 4;
  }
  return Error::success();
}

// --------------------------------------------------------------- stylise ----

Error cpu_sharpen(const ImageBuffer& in, ImageBuffer& out, float amount) {
  if (!need_input(in, out)) return Error::make(ErrorCode::InvalidArgument, "empty input");
  const int w = in.width(), h = in.height();
  amount = std::clamp(amount, 0.f, 3.f);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const uint8_t* c8 = px(in, x, y);
      uint8_t* o = out.data() + (static_cast<size_t>(y) * w + x) * 4;
      for (int ch = 0; ch < 3; ++ch) {
        float c = c8[ch];
        float sum = px(in, x - 1, y)[ch] + px(in, x + 1, y)[ch] +
                    px(in, x, y - 1)[ch] + px(in, x, y + 1)[ch];
        float v = (c * (1.f + 4.f * amount) - sum * amount) / 255.f;
        o[ch] = to8(v);
      }
      o[3] = c8[3];
    }
  }
  return Error::success();
}

Error cpu_edge_detect(const ImageBuffer& in, ImageBuffer& out, float amount) {
  if (!need_input(in, out)) return Error::make(ErrorCode::InvalidArgument, "empty input");
  const int w = in.width(), h = in.height();
  amount = std::clamp(amount, 0.f, 3.f);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      float gx = -luma709(px(in, x - 1, y - 1)) - 2.f * luma709(px(in, x - 1, y)) - luma709(px(in, x - 1, y + 1))
                 + luma709(px(in, x + 1, y - 1)) + 2.f * luma709(px(in, x + 1, y)) + luma709(px(in, x + 1, y + 1));
      float gy = -luma709(px(in, x - 1, y - 1)) - 2.f * luma709(px(in, x, y - 1)) - luma709(px(in, x + 1, y - 1))
                 + luma709(px(in, x - 1, y + 1)) + 2.f * luma709(px(in, x, y + 1)) + luma709(px(in, x + 1, y + 1));
      float m = std::clamp(std::sqrt(gx * gx + gy * gy) * amount, 0.f, 1.f);
      uint8_t* o = out.data() + (static_cast<size_t>(y) * w + x) * 4;
      o[0] = o[1] = o[2] = to8(m);
      o[3] = px(in, x, y)[3];
    }
  }
  return Error::success();
}

Error cpu_emboss(const ImageBuffer& in, ImageBuffer& out, float amount) {
  if (!need_input(in, out)) return Error::make(ErrorCode::InvalidArgument, "empty input");
  const int w = in.width(), h = in.height();
  amount = std::clamp(amount, 0.f, 2.f);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      float v = -2.f * luma709(px(in, x - 1, y - 1)) - luma709(px(in, x, y - 1)) - luma709(px(in, x - 1, y))
                + luma709(px(in, x, y))
                + luma709(px(in, x + 1, y)) + luma709(px(in, x, y + 1)) + 2.f * luma709(px(in, x + 1, y + 1));
      float g = std::clamp(0.5f + v * amount * 0.5f, 0.f, 1.f);
      uint8_t* o = out.data() + (static_cast<size_t>(y) * w + x) * 4;
      o[0] = o[1] = o[2] = to8(g);
      o[3] = px(in, x, y)[3];
    }
  }
  return Error::success();
}

Error cpu_pixelate(const ImageBuffer& in, ImageBuffer& out, int size) {
  if (!need_input(in, out)) return Error::make(ErrorCode::InvalidArgument, "empty input");
  const int w = in.width(), h = in.height();
  size = std::clamp(size, 1, 256);
  for (int by = 0; by < h; by += size) {
    for (int bx = 0; bx < w; bx += size) {
      const int x1 = std::min(bx + size, w), y1 = std::min(by + size, h);
      double sr = 0, sg = 0, sb = 0, sa = 0;
      int count = 0;
      for (int y = by; y < y1; ++y)
        for (int x = bx; x < x1; ++x) {
          const uint8_t* p = in.data() + (static_cast<size_t>(y) * w + x) * 4;
          sr += p[0]; sg += p[1]; sb += p[2]; sa += p[3];
          ++count;
        }
      if (count == 0) continue;
      const uint8_t r = static_cast<uint8_t>(sr / count), g = static_cast<uint8_t>(sg / count);
      const uint8_t b = static_cast<uint8_t>(sb / count), a = static_cast<uint8_t>(sa / count);
      for (int y = by; y < y1; ++y)
        for (int x = bx; x < x1; ++x) {
          uint8_t* o = out.data() + (static_cast<size_t>(y) * w + x) * 4;
          o[0] = r; o[1] = g; o[2] = b; o[3] = a;
        }
    }
  }
  return Error::success();
}

Error cpu_glow(const ImageBuffer& in, ImageBuffer& out, float amount, float radius) {
  if (!need_input(in, out)) return Error::make(ErrorCode::InvalidArgument, "empty input");
  ImageBuffer blurred;
  Error e = cpu_gaussian_blur(in, blurred, std::max(radius, 0.f));
  if (!e.ok()) return e;
  amount = std::clamp(amount, 0.f, 1.f);
  const int n = in.width() * in.height();
  const uint8_t* s = in.data();
  const uint8_t* b = blurred.data();
  uint8_t* d = out.data();
  for (int i = 0; i < n; ++i) {
    for (int c = 0; c < 3; ++c) {
      float base = s[c] / 255.f;
      float bloom = b[c] / 255.f * amount;
      d[c] = to8(1.f - (1.f - base) * (1.f - bloom)); // screen blend
    }
    d[3] = s[3];
    s += 4; b += 4; d += 4;
  }
  return Error::success();
}

Error cpu_noise(const ImageBuffer& in, ImageBuffer& out, float amount, int seed) {
  if (!need_input(in, out)) return Error::make(ErrorCode::InvalidArgument, "empty input");
  const int w = in.width(), h = in.height();
  amount = std::clamp(amount, 0.f, 1.f);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const uint8_t* s = in.data() + (static_cast<size_t>(y) * w + x) * 4;
      uint8_t* o = out.data() + (static_cast<size_t>(y) * w + x) * 4;
      float nse = (hash01(x, y, seed) - 0.5f) * 2.f * amount;
      for (int c = 0; c < 3; ++c) o[c] = to8(s[c] / 255.f + nse);
      o[3] = s[3];
    }
  }
  return Error::success();
}

Error cpu_box_blur(const ImageBuffer& in, ImageBuffer& out, int radius) {
  if (!need_input(in, out)) return Error::make(ErrorCode::InvalidArgument, "empty input");
  const int w = in.width(), h = in.height();
  radius = std::clamp(radius, 0, 64);
  if (radius == 0) {
    std::memcpy(out.data(), in.data(), static_cast<size_t>(w) * h * 4);
    return Error::success();
  }
  // The average runs on premultiplied colour, like the gaussian blur: on a
  // straight-alpha buffer a transparent neighbour would average in as black and
  // darken every soft edge.
  std::vector<uint8_t> pre;
  premultiply_rgba(in, pre);
  std::vector<uint8_t> tmp(static_cast<size_t>(w) * h * 4, 0);
  auto at = [&](const std::vector<uint8_t>& buf, int x, int y) {
    x = std::clamp(x, 0, w - 1);
    y = std::clamp(y, 0, h - 1);
    return buf.data() + (static_cast<size_t>(y) * w + x) * 4;
  };
  // Horizontal pass: running sum keeps this O(n) instead of O(n·r).
  const int span = radius * 2 + 1;
  for (int y = 0; y < h; ++y) {
    long sum[4] = {0, 0, 0, 0};
    for (int k = -radius; k <= radius; ++k) {
      const uint8_t* p = at(pre, k, y);
      for (int c = 0; c < 4; ++c) sum[c] += p[c];
    }
    for (int x = 0; x < w; ++x) {
      uint8_t* o = tmp.data() + (static_cast<size_t>(y) * w + x) * 4;
      for (int c = 0; c < 4; ++c) o[c] = static_cast<uint8_t>(sum[c] / span);
      const uint8_t* add = at(pre, x + radius + 1, y);
      const uint8_t* sub = at(pre, x - radius, y);
      for (int c = 0; c < 4; ++c) sum[c] += add[c] - sub[c];
    }
  }
  // Vertical pass over the horizontal result.
  for (int x = 0; x < w; ++x) {
    long sum[4] = {0, 0, 0, 0};
    for (int k = -radius; k <= radius; ++k) {
      const uint8_t* p = at(tmp, x, k);
      for (int c = 0; c < 4; ++c) sum[c] += p[c];
    }
    for (int y = 0; y < h; ++y) {
      uint8_t* o = out.data() + (static_cast<size_t>(y) * w + x) * 4;
      for (int c = 0; c < 4; ++c) o[c] = static_cast<uint8_t>(sum[c] / span);
      const uint8_t* add = at(tmp, x, y + radius + 1);
      const uint8_t* sub = at(tmp, x, y - radius);
      for (int c = 0; c < 4; ++c) sum[c] += add[c] - sub[c];
    }
  }
  unpremultiply_rgba(out.data(), static_cast<size_t>(w) * h);
  return Error::success();
}

Error cpu_crop(const ImageBuffer& in, ImageBuffer& out,
               float left, float right, float top, float bottom) {
  if (!need_input(in, out)) return Error::make(ErrorCode::InvalidArgument, "empty input");
  const int w = in.width(), h = in.height();
  // Fractions of each edge; the outside is cleared so the effect works as a
  // framing tool without needing a second input layer.
  const int x0 = static_cast<int>(std::clamp(left, 0.f, 0.98f) * w);
  const int x1 = w - static_cast<int>(std::clamp(right, 0.f, 0.98f) * w);
  const int y0 = static_cast<int>(std::clamp(top, 0.f, 0.98f) * h);
  const int y1 = h - static_cast<int>(std::clamp(bottom, 0.f, 0.98f) * h);
  std::memset(out.data(), 0, static_cast<size_t>(w) * h * 4);
  for (int y = std::max(y0, 0); y < std::min(y1, h); ++y) {
    const size_t row = static_cast<size_t>(std::min(std::max(x1 - x0, 0), w)) * 4;
    if (row > 0)
      std::memcpy(out.data() + (static_cast<size_t>(y) * w + x0) * 4,
                  in.data() + (static_cast<size_t>(y) * w + x0) * 4, row);
  }
  return Error::success();
}

// ------------------------------------------------------------- dispatch -----

bool cpu_effect_try(const std::string& id,
                    const ImageBuffer& in,
                    ImageBuffer& out,
                    const std::unordered_map<std::string, double>& params) {
  auto get = [&](const char* k, double def) {
    auto it = params.find(k);
    return it == params.end() ? def : it->second;
  };

  // Accept both the bare id and the reverse-DNS form the plugin host uses.
  auto is = [&](const char* bare) {
    return id == bare || id == std::string("org.motionstudio.") + bare;
  };

  if (is("saturation")) {
    cpu_saturation(in, out, static_cast<float>(get("saturation", get("amount", 1.0))));
    return true;
  }
  if (is("hue_shift") || is("hue")) {
    cpu_hue_shift(in, out, static_cast<float>(get("degrees", 0.0)));
    return true;
  }
  if (is("levels")) {
    cpu_levels(in, out, static_cast<float>(get("black", 0.0)),
               static_cast<float>(get("white", 1.0)), static_cast<float>(get("gamma", 1.0)));
    return true;
  }
  if (is("white_balance")) {
    cpu_white_balance(in, out, static_cast<float>(get("temperature", 0.0)),
                      static_cast<float>(get("tint", 0.0)));
    return true;
  }
  if (is("posterize")) {
    cpu_posterize(in, out, static_cast<int>(get("levels", 8.0)));
    return true;
  }
  if (is("sharpen")) {
    cpu_sharpen(in, out, static_cast<float>(get("amount", 0.6)));
    return true;
  }
  if (is("edge_detect") || is("edges")) {
    cpu_edge_detect(in, out, static_cast<float>(get("amount", 1.0)));
    return true;
  }
  if (is("emboss")) {
    cpu_emboss(in, out, static_cast<float>(get("amount", 0.5)));
    return true;
  }
  if (is("pixelate") || is("mosaic")) {
    cpu_pixelate(in, out, static_cast<int>(get("size", 8.0)));
    return true;
  }
  if (is("glow")) {
    cpu_glow(in, out, static_cast<float>(get("amount", 0.5)),
             static_cast<float>(get("radius", 8.0)));
    return true;
  }
  if (is("noise") || is("grain")) {
    cpu_noise(in, out, static_cast<float>(get("amount", 0.15)),
              static_cast<int>(get("seed", 1.0)));
    return true;
  }
  if (is("box_blur")) {
    cpu_box_blur(in, out, static_cast<int>(get("radius", 4.0)));
    return true;
  }
  if (is("crop")) {
    cpu_crop(in, out, static_cast<float>(get("left", 0.0)), static_cast<float>(get("right", 0.0)),
             static_cast<float>(get("top", 0.0)), static_cast<float>(get("bottom", 0.0)));
    return true;
  }
  // These have real implementations in cpu_effects_extra.cpp but were never
  // reachable from the dispatcher.
  if (is("threshold") || is("binarize")) {
    cpu_threshold(in, out, static_cast<float>(get("level", 0.5)));
    return true;
  }
  if (is("chromakey_simple")) {
    cpu_chromakey_simple(in, out,
                         static_cast<float>(get("key_r", 0.1)),
                         static_cast<float>(get("key_g", 0.8)),
                         static_cast<float>(get("key_b", 0.1)),
                         static_cast<float>(get("tolerance", 0.2)));
    return true;
  }
  // Warps and screens live in cpu_effects_screen.cpp so this dispatcher stays
  // a list of names rather than a second implementation file.
  if (cpu_effect_screen_try(id, in, out, params)) return true;
  if (cpu_effect_motion_try(id, in, out, params)) return true;
  if (cpu_effect_looks_try(id, in, out, params)) return true;
  return false;
}

} // namespace ms
