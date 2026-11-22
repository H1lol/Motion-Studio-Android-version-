#pragma once
/**
 * Pass 3 — Masks / mattes for compositing
 */
#include "ms_image.hpp"
#include "ms_error.hpp"
#include "ms_keyframe.hpp"
#include "ms_types.hpp"
#include <algorithm>
#include <string>
#include <vector>
#include <cstdint>

namespace ms {

enum class MaskMode : uint8_t {
  None = 0,
  Alpha,       // use mask alpha
  Luma,        // use mask luminance
  InvertedAlpha,
  InvertedLuma,
};

enum class MaskShape : uint8_t {
  Rect = 0,
  Ellipse,
  Full,        // full frame white
};

/**
 * How a mask combines with the other masks on the same clip.
 *
 * Add masks union together. Subtract masks are carved out of that union. A clip
 * whose masks are all Subtract starts from a full frame, so one subtract mask
 * alone punches a hole in an otherwise unmatted clip instead of hiding it all.
 */
enum class MaskOp : uint8_t {
  Add = 0,
  Subtract = 1,
};

/** Hard cap on masks per clip, so a document cannot carry an unbounded list. */
constexpr size_t kMaxClipMasks = 8;

struct Mask {
  uint64_t id = 0;
  std::string name = "Mask";
  MaskShape shape = MaskShape::Rect;
  MaskMode mode = MaskMode::Alpha;
  MaskOp op = MaskOp::Add;
  // normalized 0..1 in layer space
  float x = 0.1f, y = 0.1f, w = 0.8f, h = 0.8f;
  float feather = 0.f; // pixels
  bool inverted = false;
  bool enabled = true;
  // Geometry automation, in clip-local seconds. Values are the same units as the
  // static fields (normalized x/y/w/h, feather in pixels). An empty curve leaves
  // its field in charge, so a mask can animate width alone and still be dragged
  // by hand when it is not keyed.
  AnimCurve x_curve, y_curve, w_curve, h_curve, feather_curve;

  bool animated() const {
    return !x_curve.empty() || !y_curve.empty() || !w_curve.empty() ||
           !h_curve.empty() || !feather_curve.empty();
  }

  /** Copy of this mask with every keyed field resolved at `clip_local`. */
  Mask at(double clip_local) const {
    Mask m = *this;
    // Clamp the resolved values: curves interpolate and can overshoot, and a
    // mask outside the frame would silently vanish or cover everything.
    if (!x_curve.empty()) m.x = std::min(1.f, std::max(0.f, static_cast<float>(x_curve.eval(clip_local))));
    if (!y_curve.empty()) m.y = std::min(1.f, std::max(0.f, static_cast<float>(y_curve.eval(clip_local))));
    if (!w_curve.empty()) m.w = std::min(1.f, std::max(0.01f, static_cast<float>(w_curve.eval(clip_local))));
    if (!h_curve.empty()) m.h = std::min(1.f, std::max(0.01f, static_cast<float>(h_curve.eval(clip_local))));
    if (!feather_curve.empty())
      m.feather = std::max(0.f, static_cast<float>(feather_curve.eval(clip_local)));
    return m;
  }
};

/** 0 rect, 1 ellipse, anything else full frame. */
MaskShape mask_shape_from_index(int index);

/**
 * Strength of one mask shape at one pixel, 0..1, with its own invert applied.
 * Exposed so the single- and multi-mask paths cannot drift apart.
 */
float mask_strength_at(const Mask& mask, int x, int y, int width, int height);

/**
 * Rasterize one mask to RGBA with the strength in every channel (including
 * alpha, which is what MaskMode::Alpha reads).
 */
Error rasterize_mask(const Mask& mask, int width, int height, ImageBuffer& out);

/**
 * Combine a clip's masks into one matte. Add masks union, Subtract masks cut,
 * disabled masks are skipped.
 */
Error rasterize_masks(const std::vector<Mask>& masks, int width, int height,
                      ImageBuffer& out);

/** Multiply source alpha by mask strength */
Error apply_mask_to_image(const ImageBuffer& src, const ImageBuffer& mask_img,
                          MaskMode mode, ImageBuffer& out);

/**
 * Same as apply_mask_to_image, in place, so matting a clip costs no second
 * full-size frame buffer.
 */
Error apply_mask_inplace(ImageBuffer& img, const ImageBuffer& mask_img, MaskMode mode);

} // namespace ms
