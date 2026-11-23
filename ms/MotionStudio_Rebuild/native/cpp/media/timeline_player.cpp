#include "ms_timeline_player.hpp"
#include "ms_transitions.hpp"
#include "ms_ffmpeg_io.hpp"
#include "ms_clip_motion.hpp"
#include "ms_animation.hpp"
#include "ms_text.hpp"
#include "ms_finishing.hpp"
#include "ms_mask.hpp"
#include "ms_proxy_registry.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>

namespace ms {

TimelinePlayer::TimelinePlayer(ProjectSession* session) : session_(session) {}

void TimelinePlayer::set_session(ProjectSession* session) {
  session_ = session;
  decoders_.clear();
}

int TimelinePlayer::output_width() const {
  return session_ ? session_->meta().width : 1920;
}
int TimelinePlayer::output_height() const {
  return session_ ? session_->meta().height : 1080;
}
float TimelinePlayer::output_fps() const {
  return session_ ? session_->meta().fps : 24.f;
}

void TimelinePlayer::reopen_after_proxy_change() {
  const int generation = proxy_registry().generation();
  if (generation == proxy_generation_) return;
  proxy_generation_ = generation;
  // The decoders still point at the files they were opened with, which may now
  // be the wrong ones. Dropping them makes the next lookup reopen.
  decoders_.clear();
}

Error TimelinePlayer::prepare() {
  if (!session_) return Error::make(ErrorCode::NotInitialized, "no session");
  decoders_.clear();
  proxy_generation_ = proxy_registry().generation();
  for (const auto& m : session_->media()) {
    auto dec = create_media_decoder(m.path);
    Error e = Error::success();
    if (!dec) e = Error::make(ErrorCode::MediaOpenFailed, "decoder");
    if (!e.ok()) {
      // still register empty-capable decoder
    }
    decoders_[m.id] = std::move(dec);
  }
  return Error::success();
}

IMediaDecoder* TimelinePlayer::decoder_for(MediaId id) {
  reopen_after_proxy_change();
  auto it = decoders_.find(id);
  if (it != decoders_.end()) return it->second.get();
  if (!session_) return nullptr;
  const MediaInfo* mi = session_->find_media(id);
  if (!mi) return nullptr;
  auto dec = create_media_decoder(mi->path);
  if (!dec) return nullptr;
  IMediaDecoder* p = dec.get();
  decoders_[id] = std::move(dec);
  return p;
}

void TimelinePlayer::blit_scaled(const ImageBuffer& src, ImageBuffer& dst,
                                 float opacity, int /*dst_x*/, int /*dst_y*/) {
  if (!src.valid() || !dst.valid()) return;
  opacity = std::clamp(opacity, 0.f, 1.f);
  const int w = std::min(src.width(), dst.width());
  const int h = std::min(src.height(), dst.height());
  for (int y = 0; y < h; ++y) {
    const uint8_t* srow = src.data() + y * src.stride();
    uint8_t* drow = dst.data() + y * dst.stride();
    for (int x = 0; x < w; ++x) {
      const uint8_t* s = srow + x * 4;
      uint8_t* d = drow + x * 4;
      float a = (s[3] / 255.f) * opacity;
      float ia = 1.f - a;
      d[0] = static_cast<uint8_t>(std::clamp(s[0] * a + d[0] * ia, 0.f, 255.f));
      d[1] = static_cast<uint8_t>(std::clamp(s[1] * a + d[1] * ia, 0.f, 255.f));
      d[2] = static_cast<uint8_t>(std::clamp(s[2] * a + d[2] * ia, 0.f, 255.f));
      d[3] = static_cast<uint8_t>(std::clamp(a * 255.f + d[3] * ia, 0.f, 255.f));
    }
  }
}

void TimelinePlayer::blit_transformed(const ImageBuffer& src, ImageBuffer& dst,
                                      float opacity, float dx, float dy, float scale) {
  if (!src.valid() || !dst.valid()) return;
  if (std::fabs(scale - 1.f) < 1e-4f && std::fabs(dx) < 0.5f && std::fabs(dy) < 0.5f) {
    blit_scaled(src, dst, opacity, 0, 0);
    return;
  }
  opacity = std::clamp(opacity, 0.f, 1.f);
  if (opacity <= 0.f) return;
  scale = std::clamp(scale, 0.05f, 8.f);

  const int dw = dst.width(), dh = dst.height();
  const int sw = src.width(), sh = src.height();
  const float w = sw * scale;
  const float h = sh * scale;
  const float ox = (dw - w) * 0.5f + dx;
  const float oy = (dh - h) * 0.5f + dy;
  const int x0 = std::max(0, static_cast<int>(std::floor(ox)));
  const int y0 = std::max(0, static_cast<int>(std::floor(oy)));
  const int x1 = std::min(dw, static_cast<int>(std::ceil(ox + w)));
  const int y1 = std::min(dh, static_cast<int>(std::ceil(oy + h)));

  for (int y = y0; y < y1; ++y) {
    const int sy = std::clamp(static_cast<int>((y - oy) / scale), 0, sh - 1);
    const uint8_t* srow = src.data() + static_cast<size_t>(sy) * src.stride();
    uint8_t* drow = dst.data() + static_cast<size_t>(y) * dst.stride();
    for (int x = x0; x < x1; ++x) {
      const int sx = std::clamp(static_cast<int>((x - ox) / scale), 0, sw - 1);
      const uint8_t* s = srow + static_cast<size_t>(sx) * 4;
      uint8_t* d = drow + static_cast<size_t>(x) * 4;
      const float a = (s[3] / 255.f) * opacity;
      const float ia = 1.f - a;
      d[0] = static_cast<uint8_t>(std::clamp(s[0] * a + d[0] * ia, 0.f, 255.f));
      d[1] = static_cast<uint8_t>(std::clamp(s[1] * a + d[1] * ia, 0.f, 255.f));
      d[2] = static_cast<uint8_t>(std::clamp(s[2] * a + d[2] * ia, 0.f, 255.f));
      d[3] = static_cast<uint8_t>(std::clamp(a * 255.f + d[3] * ia, 0.f, 255.f));
    }
  }
}

// Composites a premultiplied buffer over another (over-operator).
static void composite_premul(const ImageBuffer& src, ImageBuffer& dst) {
  if (!src.valid() || !dst.valid()) return;
  const int w = std::min(src.width(), dst.width());
  const int h = std::min(src.height(), dst.height());
  for (int y = 0; y < h; ++y) {
    const uint8_t* s = src.data() + static_cast<size_t>(y) * src.stride();
    uint8_t* d = dst.data() + static_cast<size_t>(y) * dst.stride();
    for (int x = 0; x < w; ++x) {
      const float ia = 1.f - s[x * 4 + 3] / 255.f;
      for (int c = 0; c < 3; ++c) {
        d[x * 4 + c] = static_cast<uint8_t>(
            std::clamp(s[x * 4 + c] + d[x * 4 + c] * ia, 0.f, 255.f));
      }
      d[x * 4 + 3] = static_cast<uint8_t>(
          std::clamp(s[x * 4 + 3] + d[x * 4 + 3] * ia, 0.f, 255.f));
    }
  }
}

// Runs a clip's own effect stack and its per-clip grade on pixels that are
// already in `buf`, using `tmp` as the effect stack's destination. Called for a
// single clip's frame, and for an adjustment layer where `buf` is everything the
// tracks below have composited so far.
void TimelinePlayer::apply_clip_look(const ClipInfo& clip, TimeSec clip_local, ImageBuffer& buf,
                                     ImageBuffer& tmp) {
  // An empty stack used to copy the whole frame into a fresh buffer for no
  // reason; with no enabled effect there is nothing to do at all.
  if (!clip.motion.effects.empty()) {
    Error fx = EffectStack::apply(clip.motion.effects, buf, tmp, clip_local);
    if (fx.ok() && tmp.valid()) buf.swap(tmp);
  }

  // The grade runs after the clip's own effects, before compositing, so one
  // shot can be matched to another without touching the master grade.
  if (clip.grade_enabled) {
    GradeSettings g;
    g.exposure = clip.grade[0];
    g.contrast = clip.grade[1];
    g.saturation = clip.grade[2];
    g.temperature = clip.grade[3];
    g.lift = clip.grade[4];
    g.gamma = clip.grade[5];
    g.gain = clip.grade[6];
    g.shadows = clip.grade[7];
    g.highlights = clip.grade[8];
    g.vibrance = clip.grade[9];
    apply_finishing_grade(buf, g);
  }

  // A per-clip look runs last, after the grade: one shot can carry a LUT while
  // the rest of the sequence does not, which a display-wide look cannot express.
  // The parsed cube is cached by path, so this is a trilinear sample per pixel.
  if (!clip.look_path.empty() && clip.look_strength > 0.f) {
    if (const Lut3D* lut = lut_for(clip.look_path))
      apply_lut(buf, *lut, std::clamp(clip.look_strength, 0.f, 1.f));
  }
}

const Lut3D* TimelinePlayer::lut_for(const std::string& path) {
  auto it = lut_cache_.find(path);
  if (it != lut_cache_.end()) return it->second.get();
  auto lut = std::make_shared<Lut3D>();
  if (!load_cube_lut(path, *lut)) {
    // Cache the failure too: a clip pointing at a missing file must not retry
    // the file open on every frame of every render.
    lut_cache_[path] = nullptr;
    return nullptr;
  }
  lut_cache_[path] = lut;
  return lut.get();
}

void TimelinePlayer::render_clip_into(const ClipInfo& clip, TimeSec time, int W, int H,
                                      ImageBuffer& dst) {
  // Two rotating working images: the clip's own pixels and the destination of
  // whatever stage runs next (mask, effects). Both stay allocated between
  // frames; nothing here constructs an ImageBuffer.
  ImageBuffer& frame = scratch_[0];
  ImageBuffer& alt = scratch_[1];

  if (!dst.allocate(W, H, PixelFormat::RGBA8)) return;
  dst.clear(0.f, 0.f, 0.f, 0.f);

  const double max_local = std::max(0.0, clip.duration * clip.speed - 1e-3);
  const double local = std::clamp((time - clip.start) * clip.speed, 0.0, max_local);
  const double src_t = clip.source_in + local;
  const int64_t ms = static_cast<int64_t>(src_t * 1000.0);

  IMediaDecoder* dec = decoder_for(clip.media_id);
  if (clip.is_text) {
    // A reveal runs on the timeline clock (not the retimed source clock), so a
    // sped-up or slowed title still types on in the time you see on the ruler.
    double reveal = 1.0;
    if (clip.text_style.reveal > 0) {
      const double span = clip.text_style.reveal_sec > 0.f
                              ? static_cast<double>(clip.text_style.reveal_sec)
                              : std::max(0.05, clip.duration);
      reveal = std::clamp((time - clip.start) / span, 0.0, 1.0);
    }
    if (!render_text(alt, W, H, clip.text_style, clip.text, reveal).ok() || !alt.valid()) {
      alt.allocate(W, H);
      alt.clear(0.f, 0.f, 0.f, 0.f);
    }
    frame.swap(alt);
  } else if (dec) {
    // Streaming read: a step forward decodes one frame instead of seeking
    // back to the keyframe and re-decoding the whole GOP.
    int64_t pts = 0;
    Error e = dec->read_video_at(ms, frame, pts);
    if (!e.ok() || !frame.valid()) {
      frame.allocate(W, H);
      frame.clear(0.2f, 0.25f, 0.35f, 1.f);
    }
  } else {
    frame.allocate(W, H);
    frame.clear(0.3f, 0.15f, 0.15f, 1.f);
  }

  // Shape masks: rasterise the clip's mask list at output size and multiply the
  // clip's alpha, so the transform (and transitions) see the matted image. The
  // matte is applied in place: this used to copy the whole frame into a second
  // buffer per clip per frame for a multiplication of one channel.
  if (clip.mask_enabled && !clip.masks.empty()) {
    ImageBuffer& mask_img = scratch_[4];
    // Keyed geometry is resolved once per frame; an un-keyed clip rasterises its
    // stored masks directly and pays nothing for the animation support.
    bool animated = false;
    for (const auto& m : clip.masks) {
      if (!m.animated()) continue;
      animated = true;
      break;
    }
    std::vector<Mask> evaluated;
    if (animated) {
      evaluated.reserve(clip.masks.size());
      for (const auto& m : clip.masks) evaluated.push_back(m.at(local));
    }
    if (rasterize_masks(animated ? evaluated : clip.masks, W, H, mask_img).ok() &&
        mask_img.valid())
      apply_mask_inplace(frame, mask_img, MaskMode::Alpha);
  }

  apply_clip_look(clip, local, frame, alt);

  const float op = static_cast<float>(
      clip.opacity * (clip.motion.opacity.empty() ? 1.0 : clip.motion.opacity_at(local)));
  float sc = static_cast<float>(
      clip.motion.scale.empty() ? 1.0 : clip.motion.scale.eval(local));
  float px = static_cast<float>(
      clip.motion.pos_x.empty() ? 0.0 : clip.motion.pos_x.eval(local));
  float py = static_cast<float>(
      clip.motion.pos_y.empty() ? 0.0 : clip.motion.pos_y.eval(local));

  // Layer parenting: a child inherits its parent's animated translation and
  // scale, and chains compose from the nearest parent outwards. Parenting does
  // not carry opacity, matching how a rig's control layer is meant to work.
  // Each ancestor is sampled on its own clip-local clock and clamped to its
  // own range, so a parent that ends holds its last pose instead of snapping.
  if (clip.parent_id != 0 && session_) {
    float pdx = 0.f, pdy = 0.f, pscale = 1.f;
    std::vector<ClipId> seen;
    ClipId pid = clip.parent_id;
    for (int depth = 0; depth < 16 && pid != 0; ++depth) {
      bool loop = false;
      for (ClipId s : seen) loop = loop || s == pid;
      if (loop) break;
      seen.push_back(pid);
      const ClipInfo* p = nullptr;
      for (const auto& tr : session_->tracks()) {
        for (const auto& pc : tr.clips) {
          if (pc.id == pid) p = &pc;
        }
      }
      if (!p) break;
      const double pmax = std::max(0.0, p->duration * p->speed - 1e-3);
      const double pl = std::clamp((time - p->start) * p->speed, 0.0, pmax);
      const float ax = static_cast<float>(
          p->motion.pos_x.empty() ? 0.0 : p->motion.pos_x.eval(pl));
      const float ay = static_cast<float>(
          p->motion.pos_y.empty() ? 0.0 : p->motion.pos_y.eval(pl));
      const float as = static_cast<float>(
          p->motion.scale.empty() ? 1.0 : p->motion.scale.eval(pl));
      pdx = ax + pdx * as;
      pdy = ay + pdy * as;
      pscale *= as;
      pid = p->parent_id;
    }
    px = pdx + px * pscale;
    py = pdy + py * pscale;
    sc *= pscale;
  }
  blit_transformed(frame, dst, op, px, py, sc);
}

Error TimelinePlayer::render_frame(TimeSec t, ImageBuffer& out) {
  if (!session_) return Error::make(ErrorCode::NotInitialized, "no session");
  reopen_after_proxy_change();
  const int W = output_width();
  const int H = output_height();
  if (!out.allocate(W, H, PixelFormat::RGBA8))
    return Error::make(ErrorCode::OutOfMemory, "frame buffer");
  out.clear(0.08f, 0.08f, 0.10f, 1.f);

  // Clip images come from the player's scratch pool: see render_clip_into().

  // Bottom track first (video under overlay)
  const auto& tracks = session_->tracks();
  for (const auto& track : tracks) {
    if (!track.visible || track.muted) continue;
    if (track.type == static_cast<int>(TrackType::Audio)) continue;

    const TimelineTransition* tr = nullptr;
    const ClipInfo* tr_a = nullptr;
    const ClipInfo* tr_b = nullptr;
    for (const auto& x : track.transitions) {
      const ClipInfo* a = nullptr;
      const ClipInfo* b = nullptr;
      for (const auto& c : track.clips) {
        if (c.id == x.outgoing) a = &c;
        if (c.id == x.incoming) b = &c;
      }
      if (!a || !b || x.duration <= 1e-6) continue;
      const TimeSec cut = (b->start > a->start) ? b->start : (a->start + a->duration);
      const TimeSec half = x.duration * 0.5;
      if (t >= cut - half && t <= cut + half) {
        tr = &x;
        tr_a = a;
        tr_b = b;
        break;
      }
    }

    if (tr && tr_a && tr_b) {
      const TimeSec cut = (tr_b->start > tr_a->start) ? tr_b->start : (tr_a->start + tr_a->duration);
      const float u = static_cast<float>(
          std::clamp((t - (cut - tr->duration * 0.5)) / tr->duration, 0.0, 1.0));
      ImageBuffer& a_buf = scratch_[2];
      ImageBuffer& b_buf = scratch_[3];
      ImageBuffer& blended = scratch_[5];
      render_clip_into(*tr_a, t, W, H, a_buf);
      render_clip_into(*tr_b, t, W, H, b_buf);
      if (!blended.allocate(W, H, PixelFormat::RGBA8)) continue;
      const Error te = apply_transition(a_buf, b_buf, static_cast<TransitionType>(tr->type),
                                        u, tr->softness, blended);
      if (te.ok() && blended.valid()) composite_premul(blended, out);
      continue;
    }

    for (const auto& clip : track.clips) {
      if (t < clip.start || t >= clip.start + clip.duration) continue;
      // A null object is a control layer only: it exists for parenting, and it
      // must never reach the composite, the frame cache or an export.
      if (clip.is_null) continue;
      // An adjustment layer owns no pixels: its effect stack and grade run on
      // the frame the tracks below have already built, so there is nothing to
      // blend and it is not composited over `out`. scratch_[4] is free here —
      // render_clip_into has finished with it by the time this loop advances.
      if (clip.is_adjustment) {
        // Its curves follow the same clip-local clock as the clips below it.
        const double max_local = std::max(0.0, clip.duration * clip.speed - 1e-3);
        const double local = std::clamp((t - clip.start) * clip.speed, 0.0, max_local);
        apply_clip_look(clip, local, out, scratch_[4]);
        continue;
      }
      ImageBuffer& clip_buf = scratch_[2];
      render_clip_into(clip, t, W, H, clip_buf);
      composite_premul(clip_buf, out);
    }
  }
  return Error::success();
}

Error SequenceExporter::export_png_sequence(TimelinePlayer& player, const Options& opt) {
  if (!player.session()) return Error::make(ErrorCode::NotInitialized, "no session");
  namespace fs = std::filesystem;
  fs::create_directories(opt.out_dir);
  float fps = opt.fps > 0 ? opt.fps : player.output_fps();
  if (fps < 1.f) fps = 24.f;
  TimeSec end = opt.end > 0 ? opt.end : player.session()->sequence_end();
  if (end <= opt.start) end = opt.start + 1.0;

  Error pe = player.prepare();
  if (!pe.ok()) return pe;

  int frame_i = 0;
  for (TimeSec t = opt.start; t < end - 1e-9; t += 1.0 / fps, ++frame_i) {
    ImageBuffer buf;
    Error e = player.render_frame(t, buf);
    if (!e.ok()) return e;
    // Minimal PPM (no libpng required) — portable Phase 2 proof
    std::ostringstream name;
    name << opt.out_dir << "/frame_" << std::setw(6) << std::setfill('0') << frame_i << ".ppm";
    std::ofstream f(name.str(), std::ios::binary);
    if (!f) return Error::make(ErrorCode::FilePermission, "cannot write", name.str());
    f << "P6\n" << buf.width() << " " << buf.height() << "\n255\n";
    for (int y = 0; y < buf.height(); ++y) {
      const uint8_t* row = buf.data() + y * buf.stride();
      for (int x = 0; x < buf.width(); ++x) {
        f.put(static_cast<char>(row[x * 4 + 0]));
        f.put(static_cast<char>(row[x * 4 + 1]));
        f.put(static_cast<char>(row[x * 4 + 2]));
      }
    }
  }
  // marker file
  std::ofstream done(opt.out_dir + "/export_ok.txt");
  done << "frames=" << frame_i << " fps=" << fps << " end=" << end << "\n";
  return Error::success();
}

} // namespace ms
