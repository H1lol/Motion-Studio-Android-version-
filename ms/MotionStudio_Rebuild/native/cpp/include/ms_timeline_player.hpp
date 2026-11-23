#pragma once
/**
 * Phase 2 — Timeline player
 * Seek time → sample active clips → decode/slate → composite to frame
 */
#include "ms_project.hpp"
#include "ms_media.hpp"
#include "ms_image.hpp"
#include "ms_error.hpp"
#include "ms_lut.hpp"
#include <memory>
#include <unordered_map>
#include <string>

namespace ms {

class TimelinePlayer {
public:
  explicit TimelinePlayer(ProjectSession* session);

  void set_session(ProjectSession* session);
  ProjectSession* session() const { return session_; }

  /** Open / cache decoders for all media in the project */
  Error prepare();

  /** Render one output frame at timeline time (seconds) */
  Error render_frame(TimeSec t, ImageBuffer& out);

  /**
   * Render one frame at an explicit size, or the session's output size when
   * `width`/`height` are <= 0. The preview uses this to composite at a reduced
   * quality; export always goes through render_frame() at full size.
   */
  Error render_frame_at(TimeSec t, ImageBuffer& out, int width, int height);

  /**
   * Picks the layer under a point on the composite, for tapping in the preview.
   * `nx`/`ny` are normalized 0..1 across the output frame at timeline time `t`,
   * so the caller does not have to know the preview scale or the view size.
   * Fills the owning track and the clip; returns false when the point misses
   * every visible layer. The box is derived from the same transform the
   * renderer applies, so what is composited is what can be tapped.
   */
  bool hit_test(TimeSec t, float nx, float ny, TrackId* track, ClipId* clip) const;

  /** Current playhead helpers */
  Error scrub(TimeSec t, ImageBuffer& out) { return render_frame(t, out); }

  int output_width() const;
  int output_height() const;
  float output_fps() const;

private:
  ProjectSession* session_ = nullptr;
  std::unordered_map<MediaId, std::unique_ptr<IMediaDecoder>> decoders_;

  /**
   * Proxy registry generation the cached decoders were opened under. Turning
   * proxies on or off, or registering a freshly generated one, changes the file
   * a decoder should be reading, so the cache is reopened when it moves.
   */
  int proxy_generation_ = 0;
  void reopen_after_proxy_change();

  IMediaDecoder* decoder_for(MediaId id);

  /**
   * Renders one clip (decode -> mask -> effects -> transform) into `dst`.
   * Buffers come from [scratch_], never from freshly constructed images.
   */
  void render_clip_into(const ClipInfo& clip, TimeSec time, int w, int h, ImageBuffer& dst);

  /**
   * A clip's effect stack, grade and per-clip LUT, on pixels already in `buf`.
   * `clip_local` drives the effect parameter curves; pass the clip-local time of
   * the frame being built.
   */
  void apply_clip_look(const ClipInfo& clip, TimeSec clip_local, ImageBuffer& buf,
                       ImageBuffer& tmp);

  /**
   * Resolved compositing transform of a clip at timeline time `time`: opacity,
   * translation in output pixels and scale, with the parent chain folded in.
   * Shared by the compositor and hit_test() so a tap can never disagree with
   * the picture.
   */
  void clip_transform(const ClipInfo& clip, TimeSec time, float* opacity, float* dx,
                      float* dy, float* scale) const;

  /** Parsed `.cube` cache: a look is read from disk once, not once per frame. */
  const Lut3D* lut_for(const std::string& path);
  std::unordered_map<std::string, std::shared_ptr<const Lut3D>> lut_cache_;

  /**
   * Frame scratch, reused for the lifetime of the player.
   *
   * Every clip used to build four to six full-size ImageBuffers per frame, and
   * each of those allocated and zeroed its own pixel store, so a single 1080p
   * frame churned through tens of megabytes of heap before it could be shown.
   * That allocation traffic, not the compositing maths, is what made playback
   * on a phone stutter.
   */
  ImageBuffer scratch_[6];

  static void blit_scaled(const ImageBuffer& src, ImageBuffer& dst,
                          float opacity, int dst_x, int dst_y);

  /** Opacity + keyframed translate/scale blit into the output frame. */
  static void blit_transformed(const ImageBuffer& src, ImageBuffer& dst, float opacity,
                               float dx, float dy, float scale);
};

/** Phase 2 — simple sequential export (frame sequence / stub mp4 path) */
class SequenceExporter {
public:
  struct Options {
    std::string out_dir = "/tmp/ms_export";
    TimeSec start = 0;
    TimeSec end = 0; // 0 = sequence_end
    int width = 0;    // 0 = project
    int height = 0;
    float fps = 0;
  };

  Error export_png_sequence(TimelinePlayer& player, const Options& opt);
};

} // namespace ms
