#pragma once
/**
 * Phase 1 — Project foundation
 * .msproj JSON · media bin · multi-track timeline · undo/redo · autosave
 */
#include "ms_types.hpp"
#include "ms_error.hpp"
#include "ms_clip_motion.hpp"
#include "ms_text.hpp"
#include "ms_mask.hpp"
#include "ms_audio_fx.hpp"
#include <string>
#include <vector>
#include <memory>

namespace ms {

struct MediaInfo {
  MediaId id = 0;
  std::string name;
  std::string path;
  int width = 0;
  int height = 0;
  TimeSec duration = 0.0;
  float fps = 0.f;
  bool has_audio = false;
  int type = 0; // 0=video 1=image 2=audio
};

struct ClipInfo {
  ClipId id = 0;
  MediaId media_id = 0;
  std::string name;
  TimeSec start = 0.0;       // timeline position
  TimeSec duration = 0.0;    // length on timeline
  TimeSec source_in = 0.0;   // media in-point
  TimeSec source_out = 0.0;  // media out-point
  float speed = 1.f;
  float opacity = 1.f;
  float gain = 1.f;
  float pan = 0.f;
  bool mute = false;
  // Clip audio automation, in clip-local seconds. `gain_curve` is a multiplier on
  // the clip's own fader (1 = no change), so a fade is a curve from 0 to 1 and the
  // slider still trims the whole move. `pan_curve` is an absolute pan position
  // (-1 left .. +1 right). An empty curve leaves the static value in charge.
  AnimCurve gain_curve;
  AnimCurve pan_curve;
  ClipMotion motion;
  // Per-clip grade: exposure, contrast, saturation, temperature, lift, gamma,
  // gain, shadows, highlights, vibrance. Applied per clip before compositing,
  // so shots can be matched.
  bool grade_enabled = false;
  float grade[10] = {0.f, 1.f, 1.f, 0.f, 0.f, 1.f, 1.f, 0.f, 0.f, 0.f};
  // Shape masks applied to the clip before its transform is composited. Add
  // masks union together, Subtract masks are cut out of that union, so a clip
  // can be matted to several shapes at once. `mask_enabled` is the clip-level
  // switch; empty or all-disabled masks leave the clip untouched.
  std::vector<Mask> masks;
  bool mask_enabled = false;
  // Per-clip LUT look: a resolved `.cube` path plus its blend strength. Applied
  // after the clip's grade and before its transform, so one shot can carry a
  // look the rest of the sequence does not. Empty path or 0 strength = no look.
  std::string look_path;
  float look_strength = 0.f;
  // Adjustment layer: has no source of its own. Its effect stack and grade are
  // applied to everything the tracks below have already composited.
  bool is_adjustment = false;
  // Text / title clip (rendered by ms::render_text instead of a decoder).
  bool is_text = false;
  std::string text;
  TextStyle text_style;
};

/** A transition centred on the cut between two adjacent clips on a track. */
struct TimelineTransition {
  uint64_t id = 0;
  ClipId   outgoing = 0;
  ClipId   incoming = 0;
  int      type = 1;          // ms::TransitionType (1 = Dissolve)
  TimeSec  duration = 0.5;
  float    softness = 0.05f;
};

struct TrackInfo {
  TrackId id = 0;
  std::string name;
  int type = 0; // TrackType
  bool muted = false;
  bool locked = false;
  bool visible = true;
  float gain = 1.f;
  bool solo = false;
  // Insert chain for this track alone, run over the track's clips before they
  // reach the master bus. Saved with the track, so a mixed project reopens with
  // its track processing intact. Empty means the track passes through dry.
  std::vector<AudioFxParams> audio_inserts;
  std::vector<ClipInfo> clips;
  std::vector<TimelineTransition> transitions;
};

/** A named point on the timeline. */
struct TimelineMarker {
  uint64_t id = 0;
  TimeSec time = 0.0;
  std::string name;
  std::string color = "#2D8CEB";
};

struct ProjectMeta {
  std::string name = "Untitled";
  std::string path; // last save path
  int width = 1080;
  int height = 1920;
  float fps = 30.f;
  TimeSec duration = 15.0;
  int version = 1;
};

/**
 * Finish-stage state that the document owns: the master preview grade and the
 * master audio insert chain. The engine holds the live copies (a grade is not a
 * clip property, and the DSP chain keeps running state), so it hands them to the
 * document on every change and takes them back on load/undo.
 */
struct MasterState {
  bool grade_enabled = false;
  float grade[10] = {0.f, 1.f, 1.f, 0.f, 0.f, 1.f, 1.f, 0.f, 0.f, 0.f};
  std::vector<AudioFxParams> audio;
};

class ProjectSession {
public:
  ProjectSession();
  ~ProjectSession();

  // Lifecycle
  Error new_project(const std::string& name, int width = 1920, int height = 1080, float fps = 24.f);
  Error load_from_json(const std::string& json);
  std::string to_json() const;
  Error save_to_file(const std::string& path);
  Error load_from_file(const std::string& path);
  Error autosave(const std::string& path);

  const ProjectMeta& meta() const;
  ProjectMeta& meta();

  // Media bin
  MediaId import_media(const std::string& path);
  bool remove_media(MediaId id);
  const std::vector<MediaInfo>& media() const;
  const MediaInfo* find_media(MediaId id) const;

  // Tracks
  TrackId add_track(const std::string& name, int type);
  bool remove_track(TrackId id);
  const std::vector<TrackInfo>& tracks() const;
  TrackInfo* find_track(TrackId id);

  // Timeline markers
  uint64_t add_marker(TimeSec time, const std::string& name = "");
  bool remove_marker(uint64_t id);
  const std::vector<TimelineMarker>& markers() const;

  // Master grade + master audio inserts, serialized with the document.
  const MasterState& master() const;
  void set_master(const MasterState& state);

  // Clips / timeline edit
  ClipId add_clip(TrackId track, const ClipInfo& clip);
  bool remove_clip(TrackId track, ClipId clip);
  bool move_clip(TrackId track, ClipId clip, TimeSec new_start);
  bool trim_clip(TrackId track, ClipId clip, TimeSec new_start, TimeSec new_duration);
  bool split_clip(TrackId track, ClipId clip, TimeSec at_time);
  const ClipInfo* find_clip(TrackId track, ClipId clip) const;

  // Playback
  void seek(TimeSec t);
  void play();
  void pause();
  void toggle_play();
  PreviewState state() const;

  // Undo / redo (snapshot based)
  bool can_undo() const;
  bool can_redo() const;
  bool undo();
  bool redo();
  void push_undo(); // call before mutating edits
  void mark_dirty();
  bool is_dirty() const;

  // Helpers
  TimeSec sequence_end() const; // max of clip ends
  int clip_count() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace ms
