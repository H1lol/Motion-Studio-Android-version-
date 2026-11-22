#pragma once
/**
 * Text / title rendering.
 *
 * Real glyph rasterisation through stb_truetype (public domain, v1.21). The
 * clip renderer asks this module for a transparent RGBA tile and composites it
 * like any other clip, so titles inherit keyframed transform, opacity, effects,
 * grade and transitions for free.
 *
 * Font resolution order for a family name ("anton"):
 *   1. explicit font_path, if set
 *   2. <font_dir>/<family>.ttf   (set from the host once assets are extracted)
 *   3. <source tree>/android/app/src/main/assets/text/<family>.ttf
 *   4. $MS_FONT_DIR/<family>.ttf
 */
#include "ms_types.hpp"
#include "ms_error.hpp"
#include "ms_image.hpp"
#include <string>
#include <vector>

namespace ms {

enum class TextAlign : uint8_t { Left = 0, Center = 1, Right = 2 };
enum class TextAnchor : uint8_t { Top = 0, Middle = 1, Bottom = 2 };

struct TextStyle {
  std::string font = "anton";   // asset family name, no extension
  std::string font_path;        // overrides `font` when non-empty
  float size = 120.f;           // px, relative to the project height
  float r = 1.f, g = 1.f, b = 1.f, a = 1.f;
  TextAlign  align  = TextAlign::Center;
  TextAnchor anchor = TextAnchor::Middle;
  float line_spacing = 1.12f;   // multiple of the font size
  float tracking = 0.f;         // extra px between glyphs
  float outline  = 0.f;         // px, 0 = off
  float outline_r = 0.f, outline_g = 0.f, outline_b = 0.f;
  float shadow = 0.f;           // px offset down/right, 0 = off
  float shadow_alpha = 0.65f;
  float box_width = 0.f;        // px; wraps text when > 0
  float rise = 0.f;             // px above centre (baseline nudge for titles)
  bool  uppercase = false;
  /**
   * Title reveal: 0 none, 1 type-on, 2 word-by-word, 3 line-by-line. The reveal
   * progresses over `reveal_sec` seconds from the clip's start; a value <= 0
   * spreads the reveal across the whole clip. The text is truncated, not
   * faded, so the finished frame is byte-identical to no reveal at all.
   */
  int   reveal = 0;
  float reveal_sec = 1.f;
};

struct TextMetrics {
  float width = 0.f;
  float height = 0.f;
  int   lines = 0;
  std::string resolved_path;    // font file actually used
};

/** Directory holding <family>.ttf files; set from the host once. */
void set_text_font_dir(const std::string& dir);
const std::string& text_font_dir();

/** Resolves a style's family to a font path (empty when not found). */
std::string resolve_font_path(const TextStyle& style);

/** Fonts bundled with the app, for menus. */
const std::vector<std::string>& bundled_text_fonts();

/** Lays the text out and reports its pixel extent. Never rasterises. */
bool measure_text(const TextStyle& style, const std::string& text, TextMetrics& out);

/**
 * Rasterises `text` into `out` (w×h, RGBA8, transparent background) honouring
 * align/anchor/outline/shadow. Alpha-weighted colour, straight (not premultiplied)
 * because the clip compositor divides by 255 as it blends.
 *
 * `reveal_progress` (0..1) drives `style.reveal`; 1 draws the whole title.
 */
Error render_text(ImageBuffer& out, int w, int h, const TextStyle& style,
                  const std::string& text, double reveal_progress = 1.0);

/** Parses "#rrggbb" / "#rrggbbaa" into the style colour. */
void set_text_color(TextStyle& style, const std::string& hex);

} // namespace ms
