#include "ms_project.hpp"
#include "ms_media.hpp"
#include "ms_ffmpeg_io.hpp"
#include <fstream>
#include <sstream>
#include <atomic>
#include <cstdlib>
#include <mutex>
#include <algorithm>
#include <filesystem>
#include <cmath>

namespace ms {

namespace {

std::string esc(const std::string& s) {
  std::string o;
  o.reserve(s.size());
  for (char c : s) {
    if (c == '\\' || c == '"') o.push_back('\\');
    if (c == '\n') { o += "\\n"; continue; }
    o.push_back(c);
  }
  return o;
}

// Reads a keyframe array written as "keys_x": [[time, value], ...].
void parse_motion_curve(const std::string& obj, const std::string& key, AnimCurve& curve) {
  const std::string pat = "\"" + key + "\"";
  size_t pos = obj.find(pat);
  if (pos == std::string::npos) return;
  pos = obj.find('[', pos);
  if (pos == std::string::npos) return;
  int depth = 0;
  size_t end = pos;
  for (; end < obj.size(); ++end) {
    if (obj[end] == '[') ++depth;
    else if (obj[end] == ']') { --depth; if (depth == 0) break; }
  }
  const std::string body = obj.substr(pos + 1, end - pos - 1);
  size_t i = 0;
  while ((i = body.find('[', i)) != std::string::npos) {
    const size_t close = body.find(']', i);
    if (close == std::string::npos) break;
    double t = 0.0, v = 0.0;
    int interp = 0;
    const int fields =
        std::sscanf(body.substr(i + 1, close - i - 1).c_str(), "%lf , %lf , %d", &t, &v, &interp);
    if (fields >= 2) {
      curve.add(t, v, fields >= 3 ? static_cast<CurveInterp>(std::clamp(interp, 0, 5))
                                  : CurveInterp::Linear);
    }
    i = close + 1;
  }
}

std::string extract_string(const std::string& j, const std::string& key) {
  std::string pat = "\"" + key + "\"";
  auto p = j.find(pat);
  if (p == std::string::npos) return {};
  p = j.find(':', p);
  if (p == std::string::npos) return {};
  p = j.find('"', p);
  if (p == std::string::npos) return {};
  ++p;
  std::string out;
  while (p < j.size() && j[p] != '"') {
    if (j[p] == '\\' && p + 1 < j.size()) { out.push_back(j[p + 1]); p += 2; continue; }
    out.push_back(j[p++]);
  }
  return out;
}

double extract_number(const std::string& j, const std::string& key, double def = 0) {
  std::string pat = "\"" + key + "\"";
  auto p = j.find(pat);
  if (p == std::string::npos) return def;
  p = j.find(':', p);
  if (p == std::string::npos) return def;
  ++p;
  while (p < j.size() && (j[p] == ' ' || j[p] == '\t')) ++p;
  try { return std::stod(j.substr(p)); } catch (...) { return def; }
}


// One row per writable AudioFxParams field, shared by the writer and the reader
// so a new knob cannot be persisted under one name and parsed under another.
struct FxField {
  const char* key;
  float AudioFxParams::*member;
};
const FxField kFxFields[] = {
    {"wet", &AudioFxParams::wet},
    {"gain_db", &AudioFxParams::gain_db},
    {"low_gain_db", &AudioFxParams::low_gain_db},
    {"low_freq_hz", &AudioFxParams::low_freq_hz},
    {"mid_gain_db", &AudioFxParams::mid_gain_db},
    {"mid_freq_hz", &AudioFxParams::mid_freq_hz},
    {"mid_q", &AudioFxParams::mid_q},
    {"high_gain_db", &AudioFxParams::high_gain_db},
    {"high_freq_hz", &AudioFxParams::high_freq_hz},
    {"threshold_db", &AudioFxParams::threshold_db},
    {"ratio", &AudioFxParams::ratio},
    {"attack_ms", &AudioFxParams::attack_ms},
    {"release_ms", &AudioFxParams::release_ms},
    {"makeup_db", &AudioFxParams::makeup_db},
    {"delay_ms", &AudioFxParams::delay_ms},
    {"feedback", &AudioFxParams::feedback},
    {"room_size", &AudioFxParams::room_size},
    {"damping", &AudioFxParams::damping},
    {"ceiling_db", &AudioFxParams::ceiling_db},
};

// Reads the balanced object that follows `"key":`, skipping braces inside
// strings. Returns the object including its braces, or an empty string.
std::string extract_object(const std::string& j, const std::string& key) {
  const auto k = j.find("\"" + key + "\"");
  if (k == std::string::npos) return {};
  const auto open = j.find('{', k);
  if (open == std::string::npos) return {};
  int depth = 0;
  bool in_string = false, escaped = false;
  for (size_t i = open; i < j.size(); ++i) {
    const char c = j[i];
    if (in_string) {
      if (escaped) escaped = false;
      else if (c == '\\') escaped = true;
      else if (c == '"') in_string = false;
      continue;
    }
    if (c == '"') { in_string = true; continue; }
    if (c == '{') ++depth;
    else if (c == '}') { if (--depth == 0) return j.substr(open, i - open + 1); }
  }
  return {};
}

// Reads a flat number array written as "key": [a, b, c].
int parse_float_array(const std::string& obj, const std::string& key, float* out, int n) {
  if (!out || n <= 0) return 0;
  const auto p = obj.find("\"" + key + "\"");
  if (p == std::string::npos) return 0;
  const auto open = obj.find('[', p);
  if (open == std::string::npos) return 0;
  const auto close = obj.find(']', open);
  if (close == std::string::npos) return 0;
  std::string nums = obj.substr(open + 1, close - open - 1);
  int slot = 0;
  std::string cur;
  nums += ',';
  for (char ch : nums) {
    if (ch == ',') {
      if (slot < n) out[slot++] = static_cast<float>(std::atof(cur.c_str()));
      cur.clear();
    } else if (ch != ' ' && ch != '\n' && ch != '\t') {
      cur.push_back(ch);
    }
  }
  return slot;
}

std::vector<std::string> array_objects(const std::string& j, const std::string& key) {
  std::vector<std::string> out;
  auto k = j.find("\"" + key + "\""); if (k == std::string::npos) return out;
  auto a = j.find('[', k); if (a == std::string::npos) return out;
  int depth = 0; bool in_string = false, escp = false; size_t start = std::string::npos;
  for (size_t i=a+1;i<j.size();++i) {
    char c=j[i];
    if (in_string) { if (escp) escp=false; else if(c=='\\') escp=true; else if(c=='"') in_string=false; continue; }
    if(c=='"'){in_string=true;continue;}
    if(c=='{'){ if(depth==0) start=i; ++depth; }
    else if(c=='}'){ if(depth>0) --depth; if(depth==0 && start!=std::string::npos){out.push_back(j.substr(start,i-start+1));start=std::string::npos;} }
    else if(c==']' && depth==0) break;
  }
  return out;
}

} // namespace

struct ProjectSession::Impl {
  std::mutex mutex;
  ProjectMeta meta;
  std::vector<MediaInfo> media_list;
  std::vector<TrackInfo> track_list;
  std::atomic<TimeSec> current_time{0.0};
  std::atomic<bool> playing{false};
  bool dirty = false;
  MediaId next_media = 1;
  TrackId next_track = 1;
  ClipId next_clip = 1;
  uint64_t next_marker = 1;
  std::vector<TimelineMarker> marker_list;
  MasterState master;
  static constexpr int kMaxUndo = 50;
  std::vector<std::string> undo_stack;
  std::vector<std::string> redo_stack;

  void ensure_default_tracks() {
    if (!track_list.empty()) return;
    // One picture track is the whole default project. The old version also laid
    // down an empty Overlay and Audio lane at creation, so every new project
    // opened with three layers the user never asked for. Titles and audio land
    // on this track; a second lane is only created when one is requested.
    TrackInfo v;
    v.id = next_track++; v.name = "Video 1"; v.type = static_cast<int>(TrackType::Video);
    track_list = {v};
  }

  TrackInfo* track_mut(TrackId id) {
    for (auto& t : track_list) if (t.id == id) return &t;
    return nullptr;
  }

  void snapshot_to(std::vector<std::string>& stack, const std::string& json) {
    stack.push_back(json);
    if (static_cast<int>(stack.size()) > kMaxUndo)
      stack.erase(stack.begin(), stack.begin() + static_cast<long>(stack.size() - kMaxUndo));
  }
};

ProjectSession::ProjectSession() : impl_(std::make_unique<Impl>()) {
  impl_->ensure_default_tracks();
}
ProjectSession::~ProjectSession() = default;

void ProjectSession::push_undo() {
  // caller should hold lock OR we lock — take lock
  std::string snap = to_json();
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->snapshot_to(impl_->undo_stack, snap);
  impl_->redo_stack.clear();
}

uint64_t ProjectSession::add_marker(TimeSec time, const std::string& name) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  TimelineMarker mk;
  mk.id = impl_->next_marker++;
  mk.time = std::max(0.0, time);
  mk.name = name;
  impl_->marker_list.push_back(mk);
  impl_->dirty = true;
  return mk.id;
}

bool ProjectSession::remove_marker(uint64_t id) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  for (auto it = impl_->marker_list.begin(); it != impl_->marker_list.end(); ++it) {
    if (it->id != id) continue;
    impl_->marker_list.erase(it);
    impl_->dirty = true;
    return true;
  }
  return false;
}

const std::vector<TimelineMarker>& ProjectSession::markers() const {
  return impl_->marker_list;
}

const MasterState& ProjectSession::master() const { return impl_->master; }

void ProjectSession::set_master(const MasterState& state) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->master = state;
}

Error ProjectSession::new_project(const std::string& name, int width, int height, float fps) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->meta = {};
  impl_->meta.name = name.empty() ? "Untitled" : name;
  impl_->meta.width = width > 0 ? width : 1920;
  impl_->meta.height = height > 0 ? height : 1080;
  impl_->meta.fps = fps > 0 ? fps : 24.f;
  impl_->meta.duration = 15.0;
  impl_->meta.version = 1;
  impl_->media_list.clear();
  impl_->track_list.clear();
  impl_->next_media = 1;
  impl_->next_track = 1;
  impl_->next_clip = 1;
  impl_->next_marker = 1;
  impl_->marker_list.clear();
  impl_->master = MasterState{};
  impl_->ensure_default_tracks();
  impl_->current_time = 0.0;
  impl_->playing = false;
  impl_->dirty = false;
  impl_->undo_stack.clear();
  impl_->redo_stack.clear();
  return Error::success();
}

std::string ProjectSession::to_json() const {
  std::ostringstream ss;
  ss << "{\n  \"format\": \"msproj\",\n  \"version\": " << impl_->meta.version
     << ",\n  \"name\": \"" << esc(impl_->meta.name) << "\",\n"
     << "  \"width\": " << impl_->meta.width << ",\n"
     << "  \"height\": " << impl_->meta.height << ",\n"
     << "  \"fps\": " << impl_->meta.fps << ",\n"
     << "  \"duration\": " << impl_->meta.duration << ",\n"
     << "  \"markers\": [";
  for (size_t i = 0; i < impl_->marker_list.size(); ++i) {
    const auto& mk = impl_->marker_list[i];
    ss << "{\"id\": " << mk.id << ", \"time\": " << mk.time << ", \"name\": \""
       << esc(mk.name) << "\"}";
    if (i + 1 < impl_->marker_list.size()) ss << ", ";
  }
  ss << "],\n  \"media\": [\n";
  for (size_t i = 0; i < impl_->media_list.size(); ++i) {
    const auto& m = impl_->media_list[i];
    ss << "    {\"id\": " << m.id << ", \"name\": \"" << esc(m.name)
       << "\", \"path\": \"" << esc(m.path) << "\", \"width\": " << m.width
       << ", \"height\": " << m.height << ", \"duration\": " << m.duration
       << ", \"fps\": " << m.fps << ", \"has_audio\": " << (m.has_audio ? "true" : "false")
       << ", \"type\": " << m.type << "}";
    if (i + 1 < impl_->media_list.size()) ss << ",";
    ss << "\n";
  }
  ss << "  ],\n  \"tracks\": [\n";
  for (size_t ti = 0; ti < impl_->track_list.size(); ++ti) {
    const auto& t = impl_->track_list[ti];
    ss << "    {\"id\": " << t.id << ", \"name\": \"" << esc(t.name)
       << "\", \"type\": " << t.type
       << ", \"muted\": " << (t.muted ? "true" : "false")
       << ", \"locked\": " << (t.locked ? "true" : "false")
       << ", \"visible\": " << (t.visible ? "true" : "false")
       << ", \"gain\": " << t.gain
       << ", \"solo\": " << (t.solo ? "true" : "false")
       << ", \"clips\": [\n";
    for (size_t ci = 0; ci < t.clips.size(); ++ci) {
      const auto& c = t.clips[ci];
      ss << "      {\"id\": " << c.id << ", \"media_id\": " << c.media_id
         << ", \"name\": \"" << esc(c.name) << "\", \"start\": " << c.start
         << ", \"duration\": " << c.duration
         << ", \"source_in\": " << c.source_in
         << ", \"source_out\": " << c.source_out
         << ", \"speed\": " << c.speed
         << ", \"opacity\": " << c.opacity
         << ", \"gain\": " << c.gain
         << ", \"pan\": " << c.pan
         << ", \"mute\": " << (c.mute ? "true" : "false")
         << ", \"adj\": " << (c.is_adjustment ? "true" : "false")
         << ", \"null\": " << (c.is_null ? "true" : "false")
         << ", \"parent\": " << c.parent_id
         << ", \"mask_enabled\": " << (c.mask_enabled ? "true" : "false")
         // Legacy single-mask keys mirror the first mask, so an older build
         // still shows something after this document is saved by a newer one.
         << ", \"mk_shape\": " << static_cast<int>(c.masks.empty() ? MaskShape::Rect : c.masks[0].shape)
         << ", \"mk_mode\": " << static_cast<int>(c.masks.empty() ? MaskMode::Alpha : c.masks[0].mode)
         << ", \"mk_x\": " << (c.masks.empty() ? 0.1f : c.masks[0].x)
         << ", \"mk_y\": " << (c.masks.empty() ? 0.1f : c.masks[0].y)
         << ", \"mk_w\": " << (c.masks.empty() ? 0.8f : c.masks[0].w)
         << ", \"mk_h\": " << (c.masks.empty() ? 0.8f : c.masks[0].h)
         << ", \"mk_feather\": " << (c.masks.empty() ? 0.f : c.masks[0].feather)
         << ", \"mk_inv\": " << ((!c.masks.empty() && c.masks[0].inverted) ? "true" : "false")
         << ", \"masks\": [";
      for (size_t mi = 0; mi < c.masks.size(); ++mi) {
        const Mask& mm = c.masks[mi];
        if (mi) ss << ", ";
        ss << "{\"shape\": " << static_cast<int>(mm.shape)
           << ", \"op\": " << static_cast<int>(mm.op)
           << ", \"mode\": " << static_cast<int>(mm.mode)
           << ", \"x\": " << mm.x << ", \"y\": " << mm.y
           << ", \"w\": " << mm.w << ", \"h\": " << mm.h
           << ", \"feather\": " << mm.feather
           << ", \"inv\": " << (mm.inverted ? "true" : "false")
           << ", \"on\": " << (mm.enabled ? "true" : "false");
        // Mask geometry curves, written only when keyed so an un-animated mask
        // keeps the exact record older builds wrote.
        auto mask_curve = [&](const char* name, const AnimCurve& curve, bool last) {
          if (curve.empty()) {
            if (last) ss << "}";
            return;
          }
          ss << ", \"" << name << "\": [";
          const auto& ks = curve.keys();
          for (size_t ki = 0; ki < ks.size(); ++ki) {
            ss << "[" << ks[ki].time << ", " << ks[ki].value << ", "
               << static_cast<int>(ks[ki].interp) << "]";
            if (ki + 1 < ks.size()) ss << ", ";
          }
          ss << "]" << (last ? "}" : "");
        };
        mask_curve("keys_x", mm.x_curve, false);
        mask_curve("keys_y", mm.y_curve, false);
        mask_curve("keys_w", mm.w_curve, false);
        mask_curve("keys_h", mm.h_curve, false);
        mask_curve("keys_feather", mm.feather_curve, true);
      }
      ss << "]"
         << ", \"look_path\": \"" << esc(c.look_path) << "\""
         << ", \"look_strength\": " << c.look_strength
         << ", \"grade_enabled\": " << (c.grade_enabled ? "true" : "false")
         << ", \"grade\": [" << c.grade[0] << ", " << c.grade[1] << ", " << c.grade[2]
         << ", " << c.grade[3] << ", " << c.grade[4] << ", " << c.grade[5]
         << ", " << c.grade[6] << ", " << c.grade[7] << ", " << c.grade[8]
         << ", " << c.grade[9] << "]"
         << ", \"effects\": [";
      if (c.is_text) {
        const TextStyle& ts = c.text_style;
        ss << ", \"is_text\": true, \"tx_text\": \"" << esc(c.text) << "\""
           << ", \"tx_font\": \"" << esc(ts.font) << "\""
           << ", \"tx_path\": \"" << esc(ts.font_path) << "\""
           << ", \"tx_size\": " << ts.size
           << ", \"tx_r\": " << ts.r << ", \"tx_g\": " << ts.g
           << ", \"tx_b\": " << ts.b << ", \"tx_a\": " << ts.a
           << ", \"tx_align\": " << static_cast<int>(ts.align)
           << ", \"tx_anchor\": " << static_cast<int>(ts.anchor)
           << ", \"tx_line\": " << ts.line_spacing
           << ", \"tx_track\": " << ts.tracking
           << ", \"tx_outline\": " << ts.outline
           << ", \"tx_or\": " << ts.outline_r << ", \"tx_og\": " << ts.outline_g
           << ", \"tx_ob\": " << ts.outline_b
           << ", \"tx_shadow\": " << ts.shadow
           << ", \"tx_shadow_alpha\": " << ts.shadow_alpha
           << ", \"tx_box\": " << ts.box_width
           << ", \"tx_rise\": " << ts.rise
           << ", \"tx_reveal\": " << ts.reveal
           << ", \"tx_reveal_sec\": " << ts.reveal_sec
           << ", \"tx_upper\": " << (ts.uppercase ? "true" : "false");
      }
      for (size_t ei = 0; ei < c.motion.effects.size(); ++ei) {
        const auto& e = c.motion.effects[ei];
        ss << "{\"id\": \"" << esc(e.id) << "\", \"enabled\": "
           << (e.enabled ? "true" : "false") << ", \"params\": [";
        size_t pi = 0;
        for (const auto& kv : e.params) {
          ss << "{\"name\": \"" << esc(kv.first) << "\", \"value\": " << kv.second << "}";
          if (++pi < e.params.size()) ss << ", ";
        }
        ss << "]";
        // Per-parameter automation. Only curves with keys are written: an empty
        // entry is the same as no curve, and an older build reads the rest fine.
        ss << ", \"curves\": [";
        bool wrote_curve = false;
        for (const auto& kv : e.curves) {
          if (kv.second.empty()) continue;
          if (wrote_curve) ss << ", ";
          ss << "{\"name\": \"" << esc(kv.first) << "\", \"keys\": [";
          const auto& ks = kv.second.keys();
          for (size_t ki = 0; ki < ks.size(); ++ki) {
            ss << "[" << ks[ki].time << ", " << ks[ki].value << ", "
               << static_cast<int>(ks[ki].interp) << "]";
            if (ki + 1 < ks.size()) ss << ", ";
          }
          ss << "]}";
          wrote_curve = true;
        }
        ss << "]}";
        if (ei + 1 < c.motion.effects.size()) ss << ", ";
      }
      ss << "]";
      ss << ", \"motion\": {";
      auto write_curve = [&](const char* name, const AnimCurve& curve, bool last) {
        ss << "\"" << name << "\": [";
        // A curve that only holds the static placeholder is written empty: the
        // placeholder is a UI default, not user data, and saving it turned into a
        // phantom key on every clip after a reload.
        if (curve.seeded()) {
          ss << "]" << (last ? "" : ", ");
          return;
        }
        const auto& ks = curve.keys();
        for (size_t ki = 0; ki < ks.size(); ++ki) {
          // [time, value, interp]; a two-element key from an older file reads
          // back as Linear.
          ss << "[" << ks[ki].time << ", " << ks[ki].value << ", "
             << static_cast<int>(ks[ki].interp) << "]";
          if (ki + 1 < ks.size()) ss << ", ";
        }
        ss << "]" << (last ? "" : ", ");
      };
      // Clip audio automation rides in the clip record next to the transform
      // curves; older builds ignore the two keys.
      write_curve("keys_gain", c.gain_curve, false);
      write_curve("keys_pan", c.pan_curve, false);
      write_curve("keys_opacity", c.motion.opacity, false);
      write_curve("keys_scale", c.motion.scale, false);
      write_curve("keys_pos_x", c.motion.pos_x, false);
      write_curve("keys_pos_y", c.motion.pos_y, true);
      ss << "}}";
      if (ci + 1 < t.clips.size()) ss << ",";
      ss << "\n";
    }
    ss << "    ]";
    ss << ", \"transitions\": [";
    for (size_t xi = 0; xi < t.transitions.size(); ++xi) {
      const auto& x = t.transitions[xi];
      ss << "{\"id\": " << x.id << ", \"outgoing\": " << x.outgoing
         << ", \"incoming\": " << x.incoming << ", \"type\": " << x.type
         << ", \"duration\": " << x.duration << ", \"softness\": " << x.softness << "}";
      if (xi + 1 < t.transitions.size()) ss << ", ";
    }
    ss << "]";
    // Track-local insert chain, written with the same field table as the master
    // bus so a knob name never drifts between the two.
    ss << ", \"audio_inserts\": [";
    for (size_t ai = 0; ai < t.audio_inserts.size(); ++ai) {
      const AudioFxParams& p = t.audio_inserts[ai];
      ss << "{\"kind\": " << static_cast<int>(p.kind)
         << ", \"enabled\": " << (p.enabled ? "true" : "false");
      for (const auto& f : kFxFields) ss << ", \"" << f.key << "\": " << p.*(f.member);
      ss << "}";
      if (ai + 1 < t.audio_inserts.size()) ss << ", ";
    }
    ss << "]}";
    if (ti + 1 < impl_->track_list.size()) ss << ",";
    ss << "\n";
  }
  ss << "  ],\n  \"master\": {";
  {
    const MasterState& m = impl_->master;
    ss << "\"grade_enabled\": " << (m.grade_enabled ? "true" : "false") << ", \"grade\": [";
    for (int i = 0; i < 10; ++i) {
      ss << m.grade[i];
      if (i < 9) ss << ", ";
    }
    ss << "],\n    \"audio\": [";
    for (size_t i = 0; i < m.audio.size(); ++i) {
      const AudioFxParams& p = m.audio[i];
      ss << "{\"kind\": " << static_cast<int>(p.kind)
         << ", \"enabled\": " << (p.enabled ? "true" : "false");
      for (const auto& f : kFxFields) ss << ", \"" << f.key << "\": " << p.*(f.member);
      ss << "}";
      if (i + 1 < m.audio.size()) ss << ", ";
    }
    ss << "]}\n}\n";
  }
  return ss.str();
}

Error ProjectSession::load_from_json(const std::string& json) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (json.find("\"name\"") == std::string::npos || json.find("\"tracks\"") == std::string::npos)
    return Error::make(ErrorCode::ProjectLoadFailed, "invalid project json");

  impl_->meta.name = extract_string(json, "name");
  if (impl_->meta.name.empty()) impl_->meta.name = "Untitled";
  impl_->meta.width = static_cast<int>(extract_number(json, "width", 1920));
  impl_->meta.height = static_cast<int>(extract_number(json, "height", 1080));
  impl_->meta.fps = static_cast<float>(extract_number(json, "fps", 24));
  impl_->meta.duration = extract_number(json, "duration", 15);
  impl_->meta.version = static_cast<int>(extract_number(json, "version", 1));
  impl_->marker_list.clear();
  impl_->next_marker = 1;
  for (const auto& mo : array_objects(json, "markers")) {
    TimelineMarker mk;
    mk.id = static_cast<uint64_t>(extract_number(mo, "id"));
    if (mk.id == 0) mk.id = impl_->next_marker;
    impl_->next_marker = std::max(impl_->next_marker, mk.id + 1);
    mk.time = extract_number(mo, "time", 0);
    mk.name = extract_string(mo, "name");
    impl_->marker_list.push_back(mk);
  }
  impl_->media_list.clear(); impl_->track_list.clear();
  impl_->next_media=1; impl_->next_track=1; impl_->next_clip=1;

  for (const auto& obj : array_objects(json, "media")) {
    MediaInfo m;
    m.id=(MediaId)extract_number(obj,"id"); m.name=extract_string(obj,"name"); m.path=extract_string(obj,"path");
    m.width=(int)extract_number(obj,"width"); m.height=(int)extract_number(obj,"height"); m.duration=extract_number(obj,"duration");
    m.fps=(float)extract_number(obj,"fps"); m.type=(int)extract_number(obj,"type"); m.has_audio=obj.find("\"has_audio\": true")!=std::string::npos;
    if(!m.path.empty()){ if(!m.id)m.id=impl_->next_media++; else impl_->next_media=std::max(impl_->next_media,m.id+1); impl_->media_list.push_back(std::move(m)); }
  }

  for (const auto& obj : array_objects(json, "tracks")) {
    TrackInfo t;
    t.id=(TrackId)extract_number(obj,"id"); if(!t.id)t.id=impl_->next_track++; else impl_->next_track=std::max(impl_->next_track,t.id+1);
    t.name=extract_string(obj,"name"); t.type=(int)extract_number(obj,"type");
    t.muted=obj.find("\"muted\": true")!=std::string::npos; t.locked=obj.find("\"locked\": true")!=std::string::npos; t.visible=obj.find("\"visible\": false")==std::string::npos; t.gain=(float)extract_number(obj,"gain",1); t.solo=obj.find("\"solo\": true")!=std::string::npos;
    for (const auto& co : array_objects(obj, "clips")) {
      ClipInfo c; c.id=(ClipId)extract_number(co,"id"); c.media_id=(MediaId)extract_number(co,"media_id"); c.name=extract_string(co,"name");
      c.start=extract_number(co,"start"); c.duration=extract_number(co,"duration"); c.source_in=extract_number(co,"source_in"); c.source_out=extract_number(co,"source_out");
      c.speed=(float)extract_number(co,"speed",1); c.opacity=(float)extract_number(co,"opacity",1); c.gain=(float)extract_number(co,"gain",1); c.pan=(float)extract_number(co,"pan",0);       c.mute=co.find("\"mute\": true")!=std::string::npos;
      c.is_adjustment = co.find("\"adj\": true") != std::string::npos;
      c.is_null = co.find("\"null\": true") != std::string::npos;
      c.parent_id = static_cast<ClipId>(extract_number(co, "parent", 0));
      c.mask_enabled = co.find("\"mask_enabled\": true") != std::string::npos;
      {
        c.masks.clear();
        for (const auto& mo : array_objects(co, "masks")) {
          Mask mm;
          mm.shape = mask_shape_from_index(static_cast<int>(extract_number(mo, "shape", 0)));
          int mode = static_cast<int>(extract_number(mo, "mode", 1));
          if (mode < 0) mode = 0;
          if (mode > 4) mode = 4;
          mm.mode = static_cast<MaskMode>(mode);
          mm.op = static_cast<int>(extract_number(mo, "op", 0)) == 1 ? MaskOp::Subtract
                                                                    : MaskOp::Add;
          mm.x = static_cast<float>(extract_number(mo, "x", 0.1));
          mm.y = static_cast<float>(extract_number(mo, "y", 0.1));
          mm.w = static_cast<float>(extract_number(mo, "w", 0.8));
          mm.h = static_cast<float>(extract_number(mo, "h", 0.8));
          mm.feather = static_cast<float>(extract_number(mo, "feather", 0));
          mm.inverted = mo.find("\"inv\": true") != std::string::npos;
          mm.enabled = mo.find("\"on\": false") == std::string::npos;
          parse_motion_curve(mo, "keys_x", mm.x_curve);
          parse_motion_curve(mo, "keys_y", mm.y_curve);
          parse_motion_curve(mo, "keys_w", mm.w_curve);
          parse_motion_curve(mo, "keys_h", mm.h_curve);
          parse_motion_curve(mo, "keys_feather", mm.feather_curve);
          mm.id = c.masks.size() + 1;
          mm.name = "Mask " + std::to_string(c.masks.size() + 1);
          c.masks.push_back(mm);
        }
        // Documents written before multi-mask carry one mask in mk_* keys.
        if (c.masks.empty() && c.mask_enabled) {
          Mask mm;
          mm.shape = mask_shape_from_index(static_cast<int>(extract_number(co, "mk_shape", 0)));
          mm.mode = static_cast<MaskMode>(static_cast<int>(extract_number(co, "mk_mode", 1)));
          mm.x = static_cast<float>(extract_number(co, "mk_x", 0.1));
          mm.y = static_cast<float>(extract_number(co, "mk_y", 0.1));
          mm.w = static_cast<float>(extract_number(co, "mk_w", 0.8));
          mm.h = static_cast<float>(extract_number(co, "mk_h", 0.8));
          mm.feather = static_cast<float>(extract_number(co, "mk_feather", 0));
          mm.inverted = co.find("\"mk_inv\": true") != std::string::npos;
          mm.id = 1;
          mm.name = "Mask 1";
          c.masks.push_back(mm);
        }
        c.mask_enabled = !c.masks.empty();
      }
      c.look_path = extract_string(co, "look_path");
      c.look_strength = static_cast<float>(extract_number(co, "look_strength", 0));
      c.grade_enabled = co.find("\"grade_enabled\": true") != std::string::npos;
      {
        const size_t gp = co.find("\"grade\": [");
        if (gp != std::string::npos) {
          const size_t end = co.find(']', gp);
          std::string nums = co.substr(gp + 10, end == std::string::npos ? 0 : end - gp - 10);
          int slot = 0;
          std::string cur;
          nums += ',';
          for (char ch : nums) {
            if (ch == ',') {
              if (slot < 10) c.grade[slot++] = static_cast<float>(std::atof(cur.c_str()));
              cur.clear();
            } else if (ch != ' ') {
              cur.push_back(ch);
            }
          }
        }
      }
      c.is_text = co.find("\"is_text\": true") != std::string::npos;
      if (c.is_text) {
        TextStyle& ts = c.text_style;
        c.text = extract_string(co, "tx_text");
        ts.font = extract_string(co, "tx_font");
        if (ts.font.empty()) ts.font = "anton";
        ts.font_path = extract_string(co, "tx_path");
        ts.size = static_cast<float>(extract_number(co, "tx_size", 120));
        ts.r = static_cast<float>(extract_number(co, "tx_r", 1));
        ts.g = static_cast<float>(extract_number(co, "tx_g", 1));
        ts.b = static_cast<float>(extract_number(co, "tx_b", 1));
        ts.a = static_cast<float>(extract_number(co, "tx_a", 1));
        ts.align = static_cast<TextAlign>(static_cast<int>(extract_number(co, "tx_align", 1)));
        ts.anchor = static_cast<TextAnchor>(static_cast<int>(extract_number(co, "tx_anchor", 1)));
        ts.line_spacing = static_cast<float>(extract_number(co, "tx_line", 1.12));
        ts.tracking = static_cast<float>(extract_number(co, "tx_track", 0));
        ts.outline = static_cast<float>(extract_number(co, "tx_outline", 0));
        ts.outline_r = static_cast<float>(extract_number(co, "tx_or", 0));
        ts.outline_g = static_cast<float>(extract_number(co, "tx_og", 0));
        ts.outline_b = static_cast<float>(extract_number(co, "tx_ob", 0));
        ts.shadow = static_cast<float>(extract_number(co, "tx_shadow", 0));
        ts.shadow_alpha = static_cast<float>(extract_number(co, "tx_shadow_alpha", 0.65));
        ts.box_width = static_cast<float>(extract_number(co, "tx_box", 0));
        ts.rise = static_cast<float>(extract_number(co, "tx_rise", 0));
        ts.reveal = std::clamp(static_cast<int>(extract_number(co, "tx_reveal", 0)), 0, 3);
        ts.reveal_sec = static_cast<float>(extract_number(co, "tx_reveal_sec", 1));
        ts.uppercase = co.find("\"tx_upper\": true") != std::string::npos;
      }
      for (const auto& eo : array_objects(co, "effects")) {
        EffectOp op;
        op.id = extract_string(eo, "id");
        op.enabled = eo.find("\"enabled\": false") == std::string::npos;
        for (const auto& po : array_objects(eo, "params")) {
          const std::string pname = extract_string(po, "name");
          if (!pname.empty()) op.params[pname] = extract_number(po, "value");
        }
        for (const auto& co2 : array_objects(eo, "curves")) {
          const std::string cname = extract_string(co2, "name");
          if (cname.empty()) continue;
          parse_motion_curve(co2, "keys", op.curves[cname]);
        }
        if (!op.id.empty()) c.motion.effects.push_back(std::move(op));
      }
      parse_motion_curve(co, "keys_gain", c.gain_curve);
      parse_motion_curve(co, "keys_pan", c.pan_curve);
      parse_motion_curve(co, "keys_opacity", c.motion.opacity);
      parse_motion_curve(co, "keys_scale", c.motion.scale);
      parse_motion_curve(co, "keys_pos_x", c.motion.pos_x);
      parse_motion_curve(co, "keys_pos_y", c.motion.pos_y);
      // After the curves, not before: seeding first added a phantom key to every
      // loaded curve and left it flagged as "seeded", so the next key authored on
      // an opened project cleared the animation that had just been loaded.
      c.motion.ensure_defaults();
      if(!c.id)c.id=impl_->next_clip++; else impl_->next_clip=std::max(impl_->next_clip,c.id+1); t.clips.push_back(std::move(c));
    }
    for (const auto& ao : array_objects(obj, "audio_inserts")) {
      AudioFxParams p;
      const int kind = static_cast<int>(extract_number(ao, "kind", 0));
      if (kind < 0 || kind > static_cast<int>(AudioFxKind::Limiter)) continue;
      p.kind = static_cast<AudioFxKind>(kind);
      p.enabled = ao.find("\"enabled\": false") == std::string::npos;
      for (const auto& f : kFxFields)
        p.*(f.member) = static_cast<float>(extract_number(ao, f.key, p.*(f.member)));
      t.audio_inserts.push_back(p);
    }
    for (const auto& xo : array_objects(obj, "transitions")) {
      TimelineTransition x;
      x.id = static_cast<uint64_t>(extract_number(xo, "id"));
      x.outgoing = static_cast<ClipId>(extract_number(xo, "outgoing"));
      x.incoming = static_cast<ClipId>(extract_number(xo, "incoming"));
      x.type = static_cast<int>(extract_number(xo, "type", 1));
      x.duration = extract_number(xo, "duration", 0.5);
      x.softness = static_cast<float>(extract_number(xo, "softness", 0.05));
      if (x.outgoing && x.incoming) t.transitions.push_back(x);
    }
    if(t.name.empty())t.name="Track"; impl_->track_list.push_back(std::move(t));
  }
  // Master finish state: absent in older documents, which means "no grade and
  // no inserts" rather than an error.
  impl_->master = MasterState{};
  {
    const std::string mobj = extract_object(json, "master");
    if (!mobj.empty()) {
      impl_->master.grade_enabled = mobj.find("\"grade_enabled\": true") != std::string::npos;
      parse_float_array(mobj, "grade", impl_->master.grade, 10);
      for (const auto& ao : array_objects(mobj, "audio")) {
        AudioFxParams p;
        const int kind = static_cast<int>(extract_number(ao, "kind", 0));
        if (kind < 0 || kind > static_cast<int>(AudioFxKind::Limiter)) continue;
        p.kind = static_cast<AudioFxKind>(kind);
        p.enabled = ao.find("\"enabled\": false") == std::string::npos;
        for (const auto& f : kFxFields)
          p.*(f.member) = static_cast<float>(extract_number(ao, f.key, p.*(f.member)));
        impl_->master.audio.push_back(p);
      }
    }
  }
  impl_->ensure_default_tracks();
  impl_->dirty=false; impl_->current_time=0; impl_->playing=false;
  return Error::success();
}

Error ProjectSession::save_to_file(const std::string& path) {
  const std::string tmp = path + ".tmp";
  const std::string backup = path + ".bak";
  const std::string json = to_json();
  { std::ofstream f(tmp, std::ios::binary | std::ios::trunc); if(!f) return Error::make(ErrorCode::ProjectSaveFailed,"cannot open temp project",tmp); f<<json; if(!f.good()) return Error::make(ErrorCode::ProjectSaveFailed,"temp project write failed",tmp); }
  std::error_code ec;
  if(std::filesystem::exists(path,ec)) { std::filesystem::copy_file(path,backup,std::filesystem::copy_options::overwrite_existing,ec); ec.clear(); }
  std::filesystem::rename(tmp,path,ec);
  if(ec){std::filesystem::remove(tmp); return Error::make(ErrorCode::ProjectSaveFailed,"atomic project commit failed",ec.message());}
  impl_->meta.path=path; impl_->dirty=false; return Error::success();
}

Error ProjectSession::load_from_file(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return Error::make(ErrorCode::FileNotFound, "cannot open", path);
  std::ostringstream ss;
  ss << f.rdbuf();
  Error e = load_from_json(ss.str());
  if (e.ok()) impl_->meta.path = path;
  return e;
}

Error ProjectSession::autosave(const std::string& path) {
  if (!impl_->dirty) return Error::success();
  return save_to_file(path);
}

const ProjectMeta& ProjectSession::meta() const { return impl_->meta; }
ProjectMeta& ProjectSession::meta() { return impl_->meta; }

MediaId ProjectSession::import_media(const std::string& path) {
  std::string snap = to_json();
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->snapshot_to(impl_->undo_stack, snap);
  impl_->redo_stack.clear();

  MediaInfo m;
  m.id = impl_->next_media++;
  m.path = path;
  auto slash = path.find_last_of("/\\");
  m.name = (slash == std::string::npos) ? path : path.substr(slash + 1);

  MediaProbeResult pr = MediaProbe::probe(path);
  // The light probe only sniffs the container and guesses (fixed 10 s, 1920×1080,
  // "has audio"). Ask a real decoder when one is available so clips get the true
  // duration, size and audio presence.
  if (auto dec = create_media_decoder(path)) {
    const MediaProbeResult& real = dec->info();
    if (!real.streams.empty() || real.duration_ms > 0) {
      const int64_t estimate_ms = pr.duration_ms;
      pr = real;
      pr.ok = true;
      // A decoder that knows the streams but not their length must not erase
      // the estimate: that is what cut imported clips to five seconds.
      if (pr.duration_ms <= 0) pr.duration_ms = estimate_ms;
    }
  }
  if (pr.ok && !pr.streams.empty()) {
    for (const auto& s : pr.streams) {
      if (s.type == "video") {
        m.width = s.width; m.height = s.height; m.fps = static_cast<float>(s.fps);
        m.type = 0;
      }
      if (s.type == "audio") m.has_audio = true;
    }
    m.duration = pr.duration_ms / 1000.0;
  } else {
    m.width = 1920; m.height = 1080; m.fps = impl_->meta.fps; m.duration = 5.0;
  }
  if (m.duration <= 0) m.duration = 10.0;
  impl_->media_list.push_back(m);
  impl_->dirty = true;
  return m.id;
}

bool ProjectSession::remove_media(MediaId id) {
  std::string snap = to_json();
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->snapshot_to(impl_->undo_stack, snap);
  impl_->redo_stack.clear();
  auto& v = impl_->media_list;
  auto n = v.size();
  v.erase(std::remove_if(v.begin(), v.end(), [id](const MediaInfo& m) { return m.id == id; }), v.end());
  if (v.size() != n) { impl_->dirty = true; return true; }
  return false;
}

const std::vector<MediaInfo>& ProjectSession::media() const { return impl_->media_list; }

const MediaInfo* ProjectSession::find_media(MediaId id) const {
  for (const auto& m : impl_->media_list) if (m.id == id) return &m;
  return nullptr;
}

TrackId ProjectSession::add_track(const std::string& name, int type) {
  std::string snap = to_json();
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->snapshot_to(impl_->undo_stack, snap);
  impl_->redo_stack.clear();
  TrackInfo t;
  t.id = impl_->next_track++;
  t.name = name.empty() ? "Track" : name;
  t.type = type;
  impl_->track_list.push_back(t);
  impl_->dirty = true;
  return t.id;
}

bool ProjectSession::remove_track(TrackId id) {
  std::string snap = to_json();
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->snapshot_to(impl_->undo_stack, snap);
  auto& v = impl_->track_list;
  auto n = v.size();
  v.erase(std::remove_if(v.begin(), v.end(), [id](const TrackInfo& t) { return t.id == id; }), v.end());
  if (v.size() != n) { impl_->dirty = true; return true; }
  return false;
}

const std::vector<TrackInfo>& ProjectSession::tracks() const { return impl_->track_list; }

TrackInfo* ProjectSession::find_track(TrackId id) {
  return impl_->track_mut(id);
}

ClipId ProjectSession::add_clip(TrackId track, const ClipInfo& clip_in) {
  std::string snap = to_json();
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->snapshot_to(impl_->undo_stack, snap);
  impl_->redo_stack.clear();
  TrackInfo* t = impl_->track_mut(track);
  if (!t || t->locked) return 0;
  ClipInfo c = clip_in;
  if (c.id == 0) c.id = impl_->next_clip++;
  else if (c.id >= impl_->next_clip) impl_->next_clip = c.id + 1;
  if (c.duration <= 0) c.duration = 1.0;
  if (c.source_out <= c.source_in) c.source_out = c.source_in + c.duration;
  t->clips.push_back(c);
  impl_->dirty = true;
  return c.id;
}

bool ProjectSession::remove_clip(TrackId track, ClipId clip) {
  std::string snap = to_json();
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->snapshot_to(impl_->undo_stack, snap);
  TrackInfo* t = impl_->track_mut(track);
  if (!t || t->locked) return false;
  auto n = t->clips.size();
  t->clips.erase(std::remove_if(t->clips.begin(), t->clips.end(),
                 [clip](const ClipInfo& c) { return c.id == clip; }), t->clips.end());
  if (t->clips.size() != n) { impl_->dirty = true; return true; }
  return false;
}

bool ProjectSession::move_clip(TrackId track, ClipId clip, TimeSec new_start) {
  std::string snap = to_json();
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->snapshot_to(impl_->undo_stack, snap);
  TrackInfo* t = impl_->track_mut(track);
  if (!t || t->locked) return false;
  for (auto& c : t->clips) {
    if (c.id == clip) {
      c.start = std::max(0.0, new_start);
      impl_->dirty = true;
      return true;
    }
  }
  return false;
}

bool ProjectSession::trim_clip(TrackId track, ClipId clip, TimeSec new_start, TimeSec new_duration) {
  std::string snap = to_json();
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->snapshot_to(impl_->undo_stack, snap);
  TrackInfo* t = impl_->track_mut(track);
  if (!t || t->locked) return false;
  for (auto& c : t->clips) {
    if (c.id == clip) {
      c.start = std::max(0.0, new_start);
      c.duration = std::max(0.01, new_duration);
      c.source_out = c.source_in + c.duration * c.speed;
      impl_->dirty = true;
      return true;
    }
  }
  return false;
}

bool ProjectSession::split_clip(TrackId track, ClipId clip, TimeSec at_time) {
  std::string snap = to_json();
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->snapshot_to(impl_->undo_stack, snap);
  TrackInfo* t = impl_->track_mut(track);
  if (!t || t->locked) return false;
  for (size_t i = 0; i < t->clips.size(); ++i) {
    ClipInfo& c = t->clips[i];
    if (c.id != clip) continue;
    if (at_time <= c.start + 1e-6 || at_time >= c.start + c.duration - 1e-6) return false;
    double left = at_time - c.start;
    double right = c.duration - left;
    ClipInfo b = c;
    b.id = impl_->next_clip++;
    b.start = at_time;
    b.duration = right;
    b.source_in = c.source_in + left * c.speed;
    c.duration = left;
    c.source_out = c.source_in + c.duration * c.speed;
    t->clips.insert(t->clips.begin() + static_cast<long>(i) + 1, b);
    impl_->dirty = true;
    return true;
  }
  return false;
}

const ClipInfo* ProjectSession::find_clip(TrackId track, ClipId clip) const {
  for (const auto& t : impl_->track_list) {
    if (t.id != track) continue;
    for (const auto& c : t.clips) if (c.id == clip) return &c;
  }
  return nullptr;
}

void ProjectSession::seek(TimeSec t) {
  impl_->current_time = std::max(0.0, t);
}
void ProjectSession::play() { impl_->playing = true; }
void ProjectSession::pause() { impl_->playing = false; }
void ProjectSession::toggle_play() { impl_->playing = !impl_->playing.load(); }

PreviewState ProjectSession::state() const {
  PreviewState s;
  s.time = impl_->current_time.load();
  s.width = impl_->meta.width;
  s.height = impl_->meta.height;
  s.playing = impl_->playing.load();
  s.fps = impl_->meta.fps;
  return s;
}

bool ProjectSession::can_undo() const { return !impl_->undo_stack.empty(); }
bool ProjectSession::can_redo() const { return !impl_->redo_stack.empty(); }

bool ProjectSession::undo() {
  if (impl_->undo_stack.empty()) return false;
  std::string current = to_json();
  std::string prev = impl_->undo_stack.back();
  impl_->undo_stack.pop_back();
  impl_->redo_stack.push_back(current);
  auto undo_keep = impl_->undo_stack; auto redo_keep = impl_->redo_stack;
  Error e = load_from_json(prev);
  impl_->undo_stack = std::move(undo_keep); impl_->redo_stack = std::move(redo_keep);
  impl_->dirty = true;
  return e.ok();
}

bool ProjectSession::redo() {
  if (impl_->redo_stack.empty()) return false;
  std::string current = to_json();
  std::string next = impl_->redo_stack.back();
  impl_->redo_stack.pop_back();
  impl_->undo_stack.push_back(current);
  auto undo_keep = impl_->undo_stack; auto redo_keep = impl_->redo_stack;
  Error e = load_from_json(next);
  impl_->undo_stack = std::move(undo_keep); impl_->redo_stack = std::move(redo_keep);
  impl_->dirty = true;
  return e.ok();
}

void ProjectSession::mark_dirty() { impl_->dirty = true; }
bool ProjectSession::is_dirty() const { return impl_->dirty; }

TimeSec ProjectSession::sequence_end() const {
  TimeSec end = 0;
  for (const auto& t : impl_->track_list)
    for (const auto& c : t.clips)
      end = std::max(end, c.start + c.duration);
  return std::max(end, impl_->meta.duration);
}

int ProjectSession::clip_count() const {
  int n = 0;
  for (const auto& t : impl_->track_list) n += static_cast<int>(t.clips.size());
  return n;
}

} // namespace ms
