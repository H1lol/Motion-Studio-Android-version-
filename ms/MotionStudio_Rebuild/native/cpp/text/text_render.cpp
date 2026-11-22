#include "ms_text.hpp"
#define STB_TRUETYPE_IMPLEMENTATION
#include "../third_party/stb_truetype.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>

namespace ms {
namespace {

struct Font {
  std::vector<unsigned char> bytes;
  stbtt_fontinfo info{};
  bool ok = false;
  float ascent = 0.f, descent = 0.f, line_gap = 0.f; // in em units
};

std::mutex g_mutex;
std::map<std::string, std::shared_ptr<Font>> g_cache;
std::string g_font_dir;

bool file_exists(const std::string& p) {
  if (p.empty()) return false;
  std::ifstream f(p, std::ios::binary);
  return f.good();
}

const char* env_dir() {
  const char* d = std::getenv("MS_FONT_DIR");
  return d ? d : "";
}

std::shared_ptr<Font> load_font(const std::string& path) {
  std::lock_guard<std::mutex> lock(g_mutex);
  auto it = g_cache.find(path);
  if (it != g_cache.end()) return it->second;

  auto font = std::make_shared<Font>();
  std::ifstream f(path, std::ios::binary);
  if (f) {
    std::ostringstream ss;
    ss << f.rdbuf();
    const std::string s = ss.str();
    font->bytes.assign(s.begin(), s.end());
  }
  if (font->bytes.size() > 12) {
    const int offset = stbtt_GetFontOffsetForIndex(font->bytes.data(), 0);
    if (offset >= 0 && stbtt_InitFont(&font->info, font->bytes.data(), offset)) {
      int a = 0, d = 0, lg = 0;
      stbtt_GetFontVMetrics(&font->info, &a, &d, &lg);
      font->ascent = static_cast<float>(a);
      font->descent = static_cast<float>(d);
      font->line_gap = static_cast<float>(lg);
      font->ok = true;
    }
  }
  g_cache[path] = font;
  return font;
}

// Decodes one UTF-8 sequence; advances `i`.
char32_t next_codepoint(const std::string& s, size_t& i) {
  if (i >= s.size()) return 0;
  const unsigned char c = static_cast<unsigned char>(s[i]);
  if (c < 0x80) { ++i; return c; }
  int extra = 0;
  char32_t cp = 0;
  if ((c & 0xE0) == 0xC0) { extra = 1; cp = c & 0x1F; }
  else if ((c & 0xF0) == 0xE0) { extra = 2; cp = c & 0x0F; }
  else if ((c & 0xF8) == 0xF0) { extra = 3; cp = c & 0x07; }
  else { ++i; return '?'; }
  if (i + extra >= s.size()) { i = s.size(); return '?'; }
  for (int k = 1; k <= extra; ++k) {
    const unsigned char cc = static_cast<unsigned char>(s[i + k]);
    if ((cc & 0xC0) != 0x80) { i += 1; return '?'; }
    cp = (cp << 6) | (cc & 0x3F);
  }
  i += extra + 1;
  return cp;
}

struct Line {
  std::u32string text;
  float width = 0.f;
};

float glyph_advance(const Font& font, char32_t cp, float scale, float tracking) {
  int adv = 0, lsb = 0;
  stbtt_GetCodepointHMetrics(&font.info, static_cast<int>(cp), &adv, &lsb);
  return adv * scale + tracking;
}

float line_width(const Font& font, const std::u32string& line, float scale, float tracking) {
  float w = 0.f;
  char32_t prev = 0;
  for (char32_t cp : line) {
    if (prev) {
      const int kern = stbtt_GetCodepointKernAdvance(&font.info, static_cast<int>(prev),
                                                     static_cast<int>(cp));
      w += kern * scale;
    }
    w += glyph_advance(font, cp, scale, tracking);
    prev = cp;
  }
  return w;
}

std::u32string to_u32(const std::string& text, bool upper) {
  std::u32string out;
  size_t i = 0;
  while (i < text.size()) {
    char32_t cp = next_codepoint(text, i);
    if (upper && cp >= U'a' && cp <= U'z') cp = cp - U'a' + U'A';
    out.push_back(cp);
  }
  return out;
}

/** Splits into lines, wrapping on spaces when box_width > 0. */
void layout_lines(const Font& font, const TextStyle& style, const std::string& text,
                  float scale, std::vector<Line>& out, float& max_w) {
  const std::u32string src = to_u32(text, style.uppercase);
  max_w = 0.f;
  std::u32string cur;
  auto flush = [&](std::u32string line) {
    // trim trailing spaces
    while (!line.empty() && line.back() == U' ') line.pop_back();
    Line l;
    l.text = line;
    l.width = line_width(font, line, scale, style.tracking);
    max_w = std::max(max_w, l.width);
    out.push_back(std::move(l));
  };

  const bool wrap = style.box_width > 0.f;
  if (!wrap) {
    std::u32string line;
    for (char32_t cp : src) {
      if (cp == U'\n') { flush(line); line.clear(); continue; }
      line.push_back(cp);
    }
    flush(line);
    return;
  }

  // Greedy word wrap against the box.
  std::u32string word;
  auto space_width = glyph_advance(font, U' ', scale, style.tracking);
  auto current_width = [&]() { return line_width(font, cur, scale, style.tracking); };
  for (size_t i = 0; i <= src.size(); ++i) {
    const char32_t cp = i < src.size() ? src[i] : U'\n';
    if (cp != U' ' && cp != U'\n') { word.push_back(cp); continue; }
    if (!word.empty()) {
      const float w = line_width(font, word, scale, style.tracking);
      const float joined = cur.empty() ? w : current_width() + space_width + w;
      if (!cur.empty() && joined > style.box_width) {
        flush(cur);
        cur.clear();
      }
      if (!cur.empty()) cur.push_back(U' ');
      cur += word;
      word.clear();
    }
    if (cp == U'\n') { flush(cur); cur.clear(); }
  }
  if (!cur.empty()) flush(cur);
  if (out.empty()) flush(std::u32string());
}

void paint_alpha(std::vector<float>& alpha, int w, int h, const unsigned char* bmp, int bw, int bh,
                 int ox, int oy) {
  for (int y = 0; y < bh; ++y) {
    const int dy = oy + y;
    if (dy < 0 || dy >= h) continue;
    for (int x = 0; x < bw; ++x) {
      const int dx = ox + x;
      if (dx < 0 || dx >= w) continue;
      const float a = bmp[y * bw + x] / 255.f;
      if (a <= 0.f) continue;
      float& dst = alpha[static_cast<size_t>(dy) * w + dx];
      dst = std::max(dst, a);
    }
  }
}

} // namespace

void set_text_font_dir(const std::string& dir) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_font_dir = dir;
}
const std::string& text_font_dir() { return g_font_dir; }

const std::vector<std::string>& bundled_text_fonts() {
  static const std::vector<std::string> fonts = {"anton",   "bebas",  "archivo",
                                                 "oswald",  "playfair", "lobster"};
  return fonts;
}

std::string resolve_font_path(const TextStyle& style) {
  if (!style.font_path.empty() && file_exists(style.font_path)) return style.font_path;
  std::string family = style.font.empty() ? "anton" : style.font;
  std::string lower;
  for (char c : family) lower.push_back(static_cast<char>(std::tolower(c)));
  auto in_dir = [&](const std::string& dir) -> std::string {
    if (dir.empty()) return {};
    for (const char* ext : {".ttf", ".otf"}) {
      const std::string p = dir + "/" + lower + ext;
      if (file_exists(p)) return p;
    }
    return {};
  };
  if (std::string p = in_dir(g_font_dir); !p.empty()) return p;
  if (std::string p = in_dir(env_dir()); !p.empty()) return p;
  for (const char* base : {"android/app/src/main/assets/text",
                           "../android/app/src/main/assets/text",
                           "../../android/app/src/main/assets/text"}) {
    if (std::string p = in_dir(base); !p.empty()) return p;
  }
  return {};
}

bool measure_text(const TextStyle& style, const std::string& text, TextMetrics& out) {
  out = {};
  const std::string path = resolve_font_path(style);
  out.resolved_path = path;
  if (path.empty()) return false;
  auto font = load_font(path);
  if (!font || !font->ok) return false;

  const float size = std::max(1.f, style.size);
  const float scale = stbtt_ScaleForPixelHeight(&font->info, size);
  std::vector<Line> lines;
  float max_w = 0.f;
  layout_lines(*font, style, text, scale, lines, max_w);

  out.lines = static_cast<int>(lines.size());
  out.width = max_w;
  out.height = lines.size() * size * std::max(0.4f, style.line_spacing);
  return true;
}

void set_text_color(TextStyle& style, const std::string& hex) {
  std::string s = hex;
  if (!s.empty() && s[0] == '#') s.erase(0, 1);
  if (s.size() != 6 && s.size() != 8) return;
  auto hexv = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
  };
  try {
    style.r = ((hexv(s[0]) << 4) | hexv(s[1])) / 255.f;
    style.g = ((hexv(s[2]) << 4) | hexv(s[3])) / 255.f;
    style.b = ((hexv(s[4]) << 4) | hexv(s[5])) / 255.f;
    if (s.size() == 8) style.a = ((hexv(s[6]) << 4) | hexv(s[7])) / 255.f;
  } catch (...) {
  }
}

namespace {

/** Byte offset just past `count` UTF-8 codepoints. */
size_t utf8_after(const std::string& s, size_t count) {
  size_t i = 0;
  size_t seen = 0;
  while (i < s.size() && seen < count) {
    size_t j = i;
    next_codepoint(s, j);
    if (j <= i) break;
    i = j;
    ++seen;
  }
  return i;
}

/**
 * The part of `text` a reveal shows at `progress`. Cutting on codepoint, word
 * and line boundaries keeps multibyte characters whole, so a type-on never
 * paints half a glyph.
 */
std::string revealed_text(const std::string& text, int mode, double progress) {
  if (mode <= 0 || progress >= 1.0) return text;
  if (progress <= 0.0) return {};

  size_t codepoints = 0;
  for (size_t i = 0; i < text.size();) {
    size_t j = i;
    next_codepoint(text, j);
    if (j <= i) break;
    i = j;
    ++codepoints;
  }

  if (mode == 1) {
    const size_t show = static_cast<size_t>(std::ceil(progress * static_cast<double>(codepoints)));
    return text.substr(0, utf8_after(text, show));
  }

  if (mode == 2) {
    // Words are maximal runs of non-space codepoints; a newline also ends one.
    std::vector<size_t> word_ends; // byte offset just past each word
    bool in_word = false;
    size_t i = 0;
    while (i < text.size()) {
      size_t j = i;
      const char32_t cp = next_codepoint(text, j);
      if (j <= i) break;
      const bool space = cp == U' ' || cp == U'\t' || cp == U'\n' || cp == U'\r';
      if (space && in_word) {
        word_ends.push_back(i);
        in_word = false;
      } else if (!space) {
        in_word = true;
      }
      i = j;
    }
    if (in_word) word_ends.push_back(text.size());
    if (word_ends.empty()) return {};
    const size_t show = static_cast<size_t>(
        std::ceil(progress * static_cast<double>(word_ends.size())));
    const size_t at = word_ends[std::min(show, word_ends.size()) - 1];
    return text.substr(0, at);
  }

  // Line-by-line: keep whole explicit lines.
  std::vector<size_t> line_ends;
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '\n') line_ends.push_back(i + 1);
  }
  if (line_ends.empty() || line_ends.back() != text.size()) line_ends.push_back(text.size());
  const size_t show = static_cast<size_t>(
      std::ceil(progress * static_cast<double>(line_ends.size())));
  return text.substr(0, line_ends[std::min(show, line_ends.size()) - 1]);
}

} // namespace

Error render_text(ImageBuffer& out, int w, int h, const TextStyle& style,
                  const std::string& text, double reveal_progress) {
  if (!out.allocate(w, h, PixelFormat::RGBA8))
    return Error::make(ErrorCode::OutOfMemory, "text buffer");
  out.clear(0.f, 0.f, 0.f, 0.f);
  if (text.empty()) return Error::success();
  const std::string shown = revealed_text(text, style.reveal, reveal_progress);
  if (shown.empty()) return Error::success();

  const std::string path = resolve_font_path(style);
  if (path.empty())
    return Error::make(ErrorCode::FileNotFound, "font not found", style.font);
  auto font = load_font(path);
  if (!font || !font->ok)
    return Error::make(ErrorCode::InvalidArgument, "unreadable font", path);

  const float size = std::max(1.f, style.size);
  const float scale = stbtt_ScaleForPixelHeight(&font->info, size);
  std::vector<Line> lines;
  float max_w = 0.f;
  layout_lines(*font, style, shown, scale, lines, max_w);
  if (lines.empty()) return Error::success();

  const float line_h = size * std::max(0.4f, style.line_spacing);
  const float block_h = line_h * static_cast<float>(lines.size());
  const float block_w = style.box_width > 0.f ? style.box_width : max_w;

  float top = 0.f;
  switch (style.anchor) {
    case TextAnchor::Top:    top = 0.f; break;
    case TextAnchor::Bottom: top = static_cast<float>(h) - block_h; break;
    case TextAnchor::Middle:
    default:                 top = (static_cast<float>(h) - block_h) * 0.5f; break;
  }
  top -= style.rise;

  std::vector<float> alpha(static_cast<size_t>(w) * h, 0.f);
  const float ascent_px = font->ascent * scale;

  for (size_t li = 0; li < lines.size(); ++li) {
    const Line& line = lines[li];
    const float base_y = top + static_cast<float>(li) * line_h + ascent_px;
    float pen_x = 0.f;
    switch (style.align) {
      case TextAlign::Left:   pen_x = (static_cast<float>(w) - block_w) * 0.5f; break;
      case TextAlign::Right:  pen_x = (static_cast<float>(w) + block_w) * 0.5f - line.width; break;
      case TextAlign::Center:
      default:                pen_x = (static_cast<float>(w) - line.width) * 0.5f; break;
    }
    char32_t prev = 0;
    for (char32_t cp : line.text) {
      if (prev) {
        const int kern = stbtt_GetCodepointKernAdvance(&font->info, static_cast<int>(prev),
                                                       static_cast<int>(cp));
        pen_x += kern * scale;
      }
      int gw = 0, gh = 0, gx = 0, gy = 0;
      unsigned char* bmp = stbtt_GetCodepointBitmap(&font->info, scale, scale,
                                                    static_cast<int>(cp), &gw, &gh, &gx, &gy);
      if (bmp && gw > 0 && gh > 0) {
        paint_alpha(alpha, w, h, bmp, gw, gh, static_cast<int>(std::lround(pen_x)) + gx,
                    static_cast<int>(std::lround(base_y)) + gy);
        stbtt_FreeBitmap(bmp, nullptr);
      } else if (bmp) {
        stbtt_FreeBitmap(bmp, nullptr);
      }
      pen_x += glyph_advance(*font, cp, scale, style.tracking);
      prev = cp;
    }
  }

  // Paint order: shadow, outline, fill. Straight alpha, source-over.
  std::vector<float> cr(alpha.size(), 0.f), cg(alpha.size(), 0.f), cb(alpha.size(), 0.f),
      ca(alpha.size(), 0.f);

  auto over = [&](size_t idx, float sr, float sg, float sb, float sa) {
    if (sa <= 0.f) return;
    const float da = ca[idx];
    const float oa = sa + da * (1.f - sa);
    if (oa <= 0.f) return;
    cr[idx] = (sr * sa + cr[idx] * da * (1.f - sa)) / oa;
    cg[idx] = (sg * sa + cg[idx] * da * (1.f - sa)) / oa;
    cb[idx] = (sb * sa + cb[idx] * da * (1.f - sa)) / oa;
    ca[idx] = oa;
  };

  const int shadow = static_cast<int>(std::lround(style.shadow));
  if (shadow > 0) {
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        const int sy = y - shadow, sx = x - shadow;
        if (sy < 0 || sy >= h || sx < 0 || sx >= w) continue;
        const float a = alpha[static_cast<size_t>(sy) * w + sx] * style.shadow_alpha;
        over(static_cast<size_t>(y) * w + x, 0.f, 0.f, 0.f, a);
      }
    }
  }

  const int ring = static_cast<int>(std::lround(style.outline));
  if (ring > 0) {
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        float m = 0.f;
        for (int oy = -ring; oy <= ring; ++oy) {
          const int yy = y + oy;
          if (yy < 0 || yy >= h) continue;
          for (int ox = -ring; ox <= ring; ++ox) {
            const int xx = x + ox;
            if (xx < 0 || xx >= w) continue;
            const float a = alpha[static_cast<size_t>(yy) * w + xx];
            if (a > m) m = a;
          }
        }
        if (m > 0.f)
          over(static_cast<size_t>(y) * w + x, style.outline_r, style.outline_g,
               style.outline_b, m);
      }
    }
  }

  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const size_t idx = static_cast<size_t>(y) * w + x;
      if (alpha[idx] > 0.f) over(idx, style.r, style.g, style.b, alpha[idx] * style.a);
      if (ca[idx] <= 0.f) continue;
      uint8_t* p = out.data() + static_cast<size_t>(y) * out.stride() + static_cast<size_t>(x) * 4;
      p[0] = static_cast<uint8_t>(std::clamp(cr[idx], 0.f, 1.f) * 255.f + 0.5f);
      p[1] = static_cast<uint8_t>(std::clamp(cg[idx], 0.f, 1.f) * 255.f + 0.5f);
      p[2] = static_cast<uint8_t>(std::clamp(cb[idx], 0.f, 1.f) * 255.f + 0.5f);
      p[3] = static_cast<uint8_t>(std::clamp(ca[idx], 0.f, 1.f) * 255.f + 0.5f);
    }
  }
  return Error::success();
}

} // namespace ms
