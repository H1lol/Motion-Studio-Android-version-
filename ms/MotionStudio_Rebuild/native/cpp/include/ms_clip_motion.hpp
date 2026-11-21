#pragma once
/**
 * Phase 3 — Motion on clips: keyframed opacity + string effect stack
 */
#include "ms_keyframe.hpp"
#include "ms_types.hpp"
#include "ms_image.hpp"
#include "ms_error.hpp"
#include <string>
#include <vector>
#include <unordered_map>

namespace ms {

/** Lightweight effect op for CPU stack (Phase 3) — avoids clashing with EffectInstance id type */
struct EffectOp {
  std::string id; // "gaussian_blur", "invert", "brightness_contrast"
  std::unordered_map<std::string, double> params;
  bool enabled = true;
  /**
   * Per-parameter automation, keyed by parameter name. Times are clip-local
   * seconds, the same clock the transform keys use. While a curve holds keys it
   * overrides the resting value in `params`, so dragging a slider still changes
   * what an un-keyed parameter does without fighting the animation.
   */
  std::unordered_map<std::string, AnimCurve> curves;

  bool animated(const std::string& name) const {
    const auto it = curves.find(name);
    return it != curves.end() && !it->second.empty();
  }

  /** Curve value when animated, else the static parameter, else `fallback`. */
  double param_at(const std::string& name, double clip_local, double fallback = 0.0) const {
    if (animated(name)) return curves.find(name)->second.eval(clip_local);
    const auto it = params.find(name);
    return it != params.end() ? it->second : fallback;
  }

  /** Parameters as they are at `clip_local`, curves resolved over the statics. */
  std::unordered_map<std::string, double> params_at(double clip_local) const {
    std::unordered_map<std::string, double> out = params;
    for (const auto& kv : curves) {
      if (!kv.second.empty()) out[kv.first] = kv.second.eval(clip_local);
    }
    return out;
  }
};

struct ClipMotion {
  AnimCurve opacity;
  AnimCurve pos_x;
  AnimCurve pos_y;
  AnimCurve scale;
  std::vector<EffectOp> effects;

  void ensure_defaults() {
    if (opacity.empty()) {
      opacity.add(0, 1.0);
      opacity.set_seeded(true);
    }
    if (scale.empty()) {
      scale.add(0, 1.0);
      scale.set_seeded(true);
    }
    if (pos_x.empty()) {
      pos_x.add(0, 0.0);
      pos_x.set_seeded(true);
    }
    if (pos_y.empty()) {
      pos_y.add(0, 0.0);
      pos_y.set_seeded(true);
    }
  }

  double opacity_at(TimeSec t) const {
    return opacity.empty() ? 1.0 : opacity.eval(t);
  }
};

class EffectStack {
public:
  /**
   * Applies the enabled effects in stack order. `clip_local` is the clip-local
   * time in seconds used to evaluate parameter curves; a negative value means
   * "no automation", which keeps the stack usable from still-frame callers.
   */
  static Error apply(const std::vector<EffectOp>& effects,
                     const ImageBuffer& in, ImageBuffer& out,
                     double clip_local = -1.0);
};

} // namespace ms
