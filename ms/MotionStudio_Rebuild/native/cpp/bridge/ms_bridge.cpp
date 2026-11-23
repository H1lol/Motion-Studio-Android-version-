#include "ms_bridge.h"
#include "../include/ms_engine.hpp"
#include "../include/ms_android_decode.hpp"
#include "../include/ms_proxy_registry.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>

extern "C" {
// ---------------------------------------------------------------- comp ------
namespace {
ms::Engine& eng() { return ms::Engine::instance(); }
}

int ms_comp_node_count(void) {
    return static_cast<int>(eng().comp_graph().nodes().size());
}

int ms_comp_node_at(int index, int* out_id, int* out_op, int* out_enabled,
                    float* out_x, float* out_y) {
    const auto& nodes = eng().comp_graph().nodes();
    if (index < 0 || index >= static_cast<int>(nodes.size())) return -1;
    const auto& n = nodes[static_cast<size_t>(index)];
    if (out_id) *out_id = static_cast<int>(n.id);
    if (out_op) *out_op = static_cast<int>(n.op);
    if (out_enabled) *out_enabled = n.enabled ? 1 : 0;
    if (out_x) *out_x = n.x;
    if (out_y) *out_y = n.y;
    return 0;
}

int ms_comp_edge_count(void) {
    return static_cast<int>(eng().comp_graph().edges().size());
}

int ms_comp_edge_at(int index, int* out_from, int* out_to, int* out_slot) {
    const auto& edges = eng().comp_graph().edges();
    if (index < 0 || index >= static_cast<int>(edges.size())) return -1;
    const auto& e = edges[static_cast<size_t>(index)];
    if (out_from) *out_from = static_cast<int>(e.from);
    if (out_to) *out_to = static_cast<int>(e.to);
    if (out_slot) *out_slot = e.to_in;
    return 0;
}

int ms_comp_add_node(int op, float x, float y) {
    if (op < 0 || op > 10) return 0;
    ms::GraphNode n;
    n.op = static_cast<ms::NodeOp>(op);
    n.x = x;
    n.y = y;
    switch (n.op) {
        case ms::NodeOp::Constant: n.solid_r = 0.12f; n.solid_g = 0.12f; n.solid_b = 0.14f; break;
        case ms::NodeOp::Grade: n.params = {{"exposure", 0.0}, {"contrast", 1.0}}; break;
        case ms::NodeOp::Blur: n.params = {{"radius", 6.0}}; break;
        case ms::NodeOp::Saturation: n.params = {{"amount", 1.4}}; break;
        case ms::NodeOp::Merge: n.params = {{"mix", 0.5}}; break;
        default: break;
    }
    const uint64_t id = eng().comp_graph().add(n);
    eng().invalidate_frames();
    return static_cast<int>(id);
}

int ms_comp_remove_node(int id) {
    const bool ok = eng().comp_graph().remove(static_cast<uint64_t>(id));
    if (ok) eng().invalidate_frames();
    return ok ? 0 : -1;
}

int ms_comp_connect(int from, int to, int slot) {
    const bool ok = eng().comp_graph().connect(static_cast<uint64_t>(from),
                                               static_cast<uint64_t>(to), 0, slot);
    if (ok) eng().invalidate_frames();
    return ok ? 0 : -1;
}

int ms_comp_disconnect(int from, int to, int slot) {
    const bool ok = eng().comp_graph().disconnect(static_cast<uint64_t>(from),
                                                  static_cast<uint64_t>(to), slot);
    if (ok) eng().invalidate_frames();
    return ok ? 0 : -1;
}

int ms_comp_set_param(int id, const char* name, double value) {
    const bool ok = name && eng().comp_graph().set_parameter(static_cast<uint64_t>(id), name, value);
    if (ok) eng().invalidate_frames();
    return ok ? 0 : -1;
}

int ms_comp_get_param(int id, const char* name, double* out) {
    if (!name || !out) return -1;
    const ms::GraphNode* n = eng().comp_graph().find(static_cast<uint64_t>(id));
    if (!n) return -1;
    for (const auto& p : n->params) {
        if (p.name == name) { *out = p.value; return 0; }
    }
    return -1;
}

int ms_comp_set_media(int id, const char* path) {
    ms::GraphNode* n = eng().comp_graph().find(static_cast<uint64_t>(id));
    if (!n) return -1;
    n->media_path = path ? path : "";
    eng().invalidate_frames();
    return 0;
}

int ms_comp_link_param(int node, const char* param, int src, const char* src_param) {
    if (!param || !src_param) return -1;
    return eng().comp_graph().link_parameter(static_cast<uint64_t>(node), param,
                                             static_cast<uint64_t>(src), src_param)
               ? 0 : -1;
}

int ms_comp_unlink_param(int node, const char* param) {
    if (!param) return -1;
    return eng().comp_graph().unlink_parameter(static_cast<uint64_t>(node), param) ? 0 : -1;
}

int ms_comp_link_count(void) {
    return static_cast<int>(eng().comp_graph().links().size());
}

int ms_comp_link_at(int index, char* buf, int size) {
    const auto& links = eng().comp_graph().links();
    if (index < 0 || index >= static_cast<int>(links.size())) return -1;
    const auto& l = links[static_cast<std::size_t>(index)];
    const std::string text = std::to_string(l.node) + "|" + l.param + "|" +
                             std::to_string(l.src) + "|" + l.src_param;
    if (buf && size > 0) {
        const int copy = static_cast<int>(text.size()) < size - 1 ? static_cast<int>(text.size()) : size - 1;
        std::memcpy(buf, text.data(), static_cast<size_t>(copy));
        buf[copy] = '\0';
    }
    return static_cast<int>(text.size());
}

int ms_comp_get_media(int id, char* buf, int size) {
    const ms::GraphNode* n = eng().comp_graph().find(static_cast<uint64_t>(id));
    if (!n) return -1;
    const int need = static_cast<int>(n->media_path.size());
    if (buf && size > 0) {
        const int copy = need < size - 1 ? need : size - 1;
        std::memcpy(buf, n->media_path.data(), static_cast<size_t>(copy));
        buf[copy] = '\0';
    }
    return need;
}

int ms_comp_set_enabled(int id, int enabled) {
    ms::GraphNode* n = eng().comp_graph().find(static_cast<uint64_t>(id));
    if (!n) return -1;
    n->enabled = enabled != 0;
    eng().invalidate_frames();
    return 0;
}

int ms_comp_set_position(int id, float x, float y) {
    ms::GraphNode* n = eng().comp_graph().find(static_cast<uint64_t>(id));
    if (!n) return -1;
    n->x = x;
    n->y = y;
    return 0; // layout only: no pixel change, so the frame cache stays warm
}

int ms_comp_set_sink(int id) {
    // The cooked node is the first Viewer/Write in the network, so designating a
    // sink means promoting the chosen node and demoting the previous one.
    auto& graph = eng().comp_graph();
    ms::GraphNode* target = graph.find(static_cast<uint64_t>(id));
    if (!target) return -1;
    // nodes() is const-only, so ids are collected first and each node is reached
    // through the mutable find() overload.
    std::vector<uint64_t> ids;
    for (const auto& n : graph.nodes()) ids.push_back(n.id);
    for (uint64_t nid : ids) {
        ms::GraphNode* n = graph.find(nid);
        if (n && nid != static_cast<uint64_t>(id) && n->op == ms::NodeOp::Viewer)
            n->op = ms::NodeOp::Write;
    }
    target->op = ms::NodeOp::Viewer;
    eng().invalidate_frames();
    return 0;
}

int ms_comp_sink(void) { return static_cast<int>(eng().comp_graph().sink_id()); }

int ms_comp_set_active(int active) {
    eng().set_comp_active(active != 0);
    return 0;
}

int ms_comp_active(void) { return eng().comp_active() ? 1 : 0; }


int ms_engine_init(int width, int height) { return ms::Engine::instance().init(width, height) ? 0 : -1; }
int ms_project_new(const char* name, int width, int height, double fps) {
    return ms::Engine::instance().new_project(name ? name : "Untitled", width, height,
                                              static_cast<float>(fps)) ? 0 : -1;
}
void ms_engine_shutdown() { ms::Engine::instance().shutdown(); }
void ms_seek(double t) { ms::Engine::instance().seek(t); }
void ms_play() { ms::Engine::instance().play(); }
void ms_pause() { ms::Engine::instance().pause(); }
MSPreviewState ms_get_preview_state() {
    auto s = ms::Engine::instance().state();
    return {s.time, s.width, s.height, s.playing ? 1 : 0};
}
int ms_render_frame() { return ms::Engine::instance().render_frame() ? 0 : -1; }
float ms_preview_scale() { return ms::Engine::instance().preview_scale(); }
int ms_preview_scale_set(float scale) {
    ms::Engine::instance().set_preview_scale(scale);
    return 0;
}
int ms_preview_frame_size(int* width, int* height) {
    const auto& e = ms::Engine::instance();
    if (!e.is_initialized()) return -1;
    e.preview_frame_size(width, height);
    return 0;
}
int ms_project_load(const char* json) { return json && ms::Engine::instance().load_project_json(json) ? 0 : -1; }
int ms_project_save(const char* path) { return path && ms::Engine::instance().save_project(path).ok() ? 0 : -1; }
int ms_project_autosave(const char* path) { return path && ms::Engine::instance().autosave_project(path).ok() ? 0 : -1; }
double ms_perf_frame_ms() { return ms::Engine::instance().performance().frame_ms; }
double ms_perf_fps() { return ms::Engine::instance().performance().fps; }
unsigned long long ms_perf_cache_hits() { return (unsigned long long)ms::Engine::instance().performance().cache_hits; }
unsigned long long ms_perf_cache_misses() { return (unsigned long long)ms::Engine::instance().performance().cache_misses; }
char* ms_project_to_json() {
    std::string s = ms::Engine::instance().project_to_json();
    char* p = static_cast<char*>(std::malloc(s.size() + 1));
    if (p) std::memcpy(p, s.c_str(), s.size() + 1);
    return p;
}
void ms_free_string(char* p) { std::free(p); }
size_t ms_frame_bytes() { return ms::Engine::instance().last_frame_bytes(); }
unsigned long long ms_render_signature() {
  return ms::Engine::instance().render_signature();
}
int ms_copy_frame_rgba(void* rgba, size_t bytes) { return ms::Engine::instance().copy_last_frame(rgba, bytes) ? 0 : -1; }
int ms_import_media(const char* path) { return path ? static_cast<int>(ms::Engine::instance().project().import_media(path)) : -1; }
int ms_add_clip(int track_id, int media_id, double start, double duration) {
    // Goes through Engine so the edit is undoable and the frame cache is dropped.
    return static_cast<int>(ms::Engine::instance().add_clip(
        static_cast<ms::TrackId>(track_id), static_cast<ms::MediaId>(media_id), start, duration));
}
int ms_adjustment_add(int track_id, double start, double duration) {
    return static_cast<int>(ms::Engine::instance().add_adjustment_clip(
        static_cast<ms::TrackId>(track_id), start, duration));
}
int ms_null_add(int track_id, double start, double duration) {
    return static_cast<int>(ms::Engine::instance().add_null_clip(
        static_cast<ms::TrackId>(track_id), start, duration));
}
int ms_clip_parent_set(int track_id, int clip_id, int parent_clip_id) {
    return ms::Engine::instance().set_clip_parent(static_cast<ms::TrackId>(track_id),
                                                  static_cast<ms::ClipId>(clip_id),
                                                  static_cast<ms::ClipId>(parent_clip_id))
               ? 0 : -1;
}
int ms_clip_parent_get(int track_id, int clip_id, int* out_parent) {
    ms::ClipId parent = 0;
    if (!ms::Engine::instance().clip_parent(static_cast<ms::TrackId>(track_id),
                                            static_cast<ms::ClipId>(clip_id), &parent))
        return -1;
    if (out_parent) *out_parent = static_cast<int>(parent);
    return 0;
}
int ms_clip_speed_set(int track_id, int clip_id, float speed) {
    return ms::Engine::instance().set_clip_speed(static_cast<ms::TrackId>(track_id),
                                                 static_cast<ms::ClipId>(clip_id), speed)
               ? 0 : -1;
}
int ms_clip_speed_get(int track_id, int clip_id, float* out) {
    return ms::Engine::instance().clip_speed(static_cast<ms::TrackId>(track_id),
                                             static_cast<ms::ClipId>(clip_id), out)
               ? 0 : -1;
}
int ms_split_clip(int track_id, int clip_id, double at) {
    auto& e = ms::Engine::instance();
    const bool ok = e.project().split_clip(static_cast<ms::TrackId>(track_id),
                                          static_cast<ms::ClipId>(clip_id), at);
    if (ok) e.invalidate_frames();
    return ok ? 0 : -1;
}
int ms_undo() { return ms::Engine::instance().undo() ? 0 : -1; }
int ms_redo() { return ms::Engine::instance().redo() ? 0 : -1; }
MSAudioState ms_audio_state() {
    auto i = ms::Engine::instance().audio_info();
    return {i.sample_rate, i.channels, i.duration, i.time, i.playing ? 1 : 0};
}
int ms_audio_read_pcm(float* interleaved, int frames, int channels) {
    return ms::Engine::instance().read_audio(interleaved, frames, channels).ok() ? 0 : -1;
}
int ms_audio_meter(float* out5) {
    return ms::Engine::instance().audio_meter(out5) ? 0 : -1;
}
int ms_look_load(const char* path) {
    return (path && ms::Engine::instance().load_look(path)) ? 0 : -1;
}
void ms_look_clear() { ms::Engine::instance().clear_look(); }
void ms_look_set_strength(float strength) { ms::Engine::instance().set_look_strength(strength); }
float ms_look_strength() { return ms::Engine::instance().look_strength(); }

int ms_clip_look_set(int track_id, int clip_id, const char* path) {
    if (!path || !*path) return -1;
    return ms::Engine::instance().set_clip_look(track_id, clip_id, path, 1.f) ? 0 : -1;
}

int ms_clip_look_set_strength(int track_id, int clip_id, float strength) {
    return ms::Engine::instance().set_clip_look_strength(track_id, clip_id, strength) ? 0 : -1;
}

int ms_clip_look_clear(int track_id, int clip_id) {
    return ms::Engine::instance().clear_clip_look(track_id, clip_id) ? 0 : -1;
}

int ms_clip_look_get(int track_id, int clip_id, char* buf, int size, float* strength) {
    std::string path;
    float s = 0.f;
    if (!ms::Engine::instance().clip_look(track_id, clip_id, &path, &s)) return -1;
    if (buf && size > 0) {
        const int need = static_cast<int>(path.size());
        const int copy = need < size - 1 ? need : size - 1;
        std::memcpy(buf, path.data(), static_cast<size_t>(copy));
        buf[copy] = '\0';
    }
    if (strength) *strength = s;
    return 0;
}
void ms_grade_set(float exposure, float contrast, float saturation, float temperature,
                  float lift, float gamma, float gain) {
    ms::Engine::instance().set_grade(exposure, contrast, saturation, temperature, lift, gamma, gain);
}
void ms_grade_clear() { ms::Engine::instance().clear_grade(); }
int ms_grade_get(float* out10) { return ms::Engine::instance().get_grade(out10) ? 1 : 0; }

void ms_grade_tonal_set(float shadows, float highlights, float vibrance) {
    ms::Engine::instance().set_grade_tonal(shadows, highlights, vibrance);
}
int ms_scopes_histogram(int* out, int bins) {
    return ms::Engine::instance().scopes_histogram(out, bins) ? 0 : -1;
}
int ms_transition_add(int track_id, int outgoing, int incoming, int type, double duration) {
    return static_cast<int>(ms::Engine::instance().add_transition(track_id, outgoing, incoming,
                                                                 type, duration));
}
int ms_transition_remove(int track_id, int id) {
    return ms::Engine::instance().remove_transition(track_id, static_cast<uint64_t>(id)) ? 0 : -1;
}
static ms::TextStyle make_text_style(const char* font, double size, const char* color_hex,
                                     int align, int anchor, double outline,
                                     const char* outline_hex, double shadow,
                                     double line_spacing, double tracking,
                                     double box_width, int uppercase) {
    ms::TextStyle s;
    if (font && *font) s.font = font;
    s.size = size > 0 ? static_cast<float>(size) : 120.f;
    if (color_hex && *color_hex) ms::set_text_color(s, color_hex);
    s.align = align == 0 ? ms::TextAlign::Left
              : align == 2 ? ms::TextAlign::Right : ms::TextAlign::Center;
    s.anchor = anchor == 0 ? ms::TextAnchor::Top
               : anchor == 2 ? ms::TextAnchor::Bottom : ms::TextAnchor::Middle;
    s.outline = outline > 0 ? static_cast<float>(outline) : 0.f;
    if (outline_hex && *outline_hex) {
        ms::TextStyle oc = s;
        ms::set_text_color(oc, outline_hex);
        s.outline_r = oc.r;
        s.outline_g = oc.g;
        s.outline_b = oc.b;
    }
    s.shadow = shadow > 0 ? static_cast<float>(shadow) : 0.f;
    s.line_spacing = line_spacing > 0 ? static_cast<float>(line_spacing) : 1.12f;
    s.tracking = static_cast<float>(tracking);
    s.box_width = box_width > 0 ? static_cast<float>(box_width) : 0.f;
    s.uppercase = uppercase != 0;
    return s;
}

const char* ms_decoder_backend() {
    static const std::string name = ms::decoder_backend_name();
    return name.c_str();
}

int ms_marker_add(double time, const char* name) {
    return static_cast<int>(ms::Engine::instance().add_marker(time, name ? name : ""));
}
int ms_marker_remove(int id) {
    return ms::Engine::instance().remove_marker(static_cast<uint64_t>(id)) ? 0 : -1;
}
int ms_project_settings(int width, int height, double fps) {
    return ms::Engine::instance().set_project_settings(width, height, static_cast<float>(fps))
               ? 0 : -1;
}

int ms_clip_mask_set(int track_id, int clip_id, int shape, float x, float y, float w,
                     float h, float feather, int inverted) {
    return ms::Engine::instance().set_clip_mask(track_id, clip_id, shape, x, y, w, h, feather,
                                                inverted != 0) ? 0 : -1;
}
int ms_clip_mask_clear(int track_id, int clip_id) {
    return ms::Engine::instance().clear_clip_mask(track_id, clip_id) ? 0 : -1;
}
int ms_clip_mask_add(int track_id, int clip_id, int shape, float x, float y, float w,
                     float h, float feather, int inverted, int op) {
    return ms::Engine::instance().add_clip_mask(track_id, clip_id, shape, x, y, w, h, feather,
                                                inverted != 0, op);
}

int ms_clip_mask_update(int track_id, int clip_id, int index, int shape, float x, float y,
                        float w, float h, float feather, int inverted, int op) {
    return ms::Engine::instance().update_clip_mask_at(track_id, clip_id, index, shape, x, y, w,
                                                      h, feather, inverted != 0, op)
               ? 0 : -1;
}

int ms_clip_mask_remove(int track_id, int clip_id, int index) {
    return ms::Engine::instance().remove_clip_mask_at(track_id, clip_id, index) ? 0 : -1;
}

int ms_clip_mask_count(int track_id, int clip_id) {
    return ms::Engine::instance().clip_mask_count(track_id, clip_id);
}

int ms_clip_mask_read(int track_id, int clip_id, int index, float* out6, int* inverted,
                      int* op) {
    bool inv = false;
    int o = 0;
    if (!ms::Engine::instance().clip_mask_at(track_id, clip_id, index, out6, &inv, &o)) return -1;
    if (inverted) *inverted = inv ? 1 : 0;
    if (op) *op = o;
    return 0;
}

int ms_clip_mask_get(int track_id, int clip_id, float* out6, int* inverted) {
    bool inv = false;
    const bool ok = ms::Engine::instance().clip_mask(track_id, clip_id, out6, &inv);
    if (inverted) *inverted = inv ? 1 : 0;
    return ok ? 0 : -1;
}

int ms_mask_keyframe_add(int track_id, int clip_id, int mask, int prop, double time,
                         double value, int interp) {
    return ms::Engine::instance().add_mask_keyframe(track_id, clip_id, mask, prop, time, value,
                                                     interp) ? 0 : -1;
}

int ms_mask_keyframe_clear(int track_id, int clip_id, int mask, int prop) {
    return ms::Engine::instance().clear_mask_keyframes(track_id, clip_id, mask, prop) ? 0 : -1;
}

int ms_mask_keyframe_count(int track_id, int clip_id, int mask, int prop) {
    return ms::Engine::instance().mask_keyframe_count(track_id, clip_id, mask, prop);
}

int ms_mask_keyframe_get(int track_id, int clip_id, int mask, int prop, int key,
                         double* out3) {
    if (!out3) return -1;
    double time = 0.0, value = 0.0;
    int interp = 0;
    if (!ms::Engine::instance().mask_keyframe_at(track_id, clip_id, mask, prop, key, &time,
                                                 &value, &interp))
        return -1;
    out3[0] = time;
    out3[1] = value;
    out3[2] = static_cast<double>(interp);
    return 0;
}

int ms_mask_keyframe_set(int track_id, int clip_id, int mask, int prop, int key, double time,
                         double value, int interp) {
    return ms::Engine::instance().set_mask_keyframe(track_id, clip_id, mask, prop, key, time,
                                                    value, interp) ? 0 : -1;
}

int ms_mask_keyframe_sample(int track_id, int clip_id, int mask, int prop, double t0,
                            double t1, float* out, int bins) {
    if (!out) return -1;
    return ms::Engine::instance().sample_mask_param(track_id, clip_id, mask, prop, t0, t1, out,
                                                    bins) ? 0 : -1;
}

int ms_clip_grade_set(int track_id, int clip_id, float exposure, float contrast,
                      float saturation, float temperature, float lift, float gamma,
                      float gain) {
    return ms::Engine::instance().set_clip_grade(track_id, clip_id, exposure, contrast,
                                                 saturation, temperature, lift, gamma,
                                                 gain) ? 0 : -1;
}
int ms_clip_grade_clear(int track_id, int clip_id) {
    return ms::Engine::instance().clear_clip_grade(track_id, clip_id) ? 0 : -1;
}
int ms_clip_grade_tonal_set(int track_id, int clip_id, float shadows, float highlights,
                            float vibrance) {
    return ms::Engine::instance().set_clip_grade_tonal(track_id, clip_id, shadows, highlights,
                                                       vibrance) ? 0 : -1;
}

int ms_clip_grade_get(int track_id, int clip_id, float* out10) {
    return ms::Engine::instance().clip_grade(track_id, clip_id, out10) ? 0 : -1;
}

int ms_clip_peaks(int track_id, int clip_id, float* out, int bins) {
    return ms::Engine::instance().clip_peaks(track_id, clip_id, bins, out) ? 0 : -1;
}

// ---- Clip audio ----------------------------------------------------------
int ms_clip_audio_set(int track_id, int clip_id, float gain, float pan) {
    return ms::Engine::instance().set_clip_audio(track_id, clip_id, gain, pan) ? 0 : -1;
}

int ms_clip_audio_get(int track_id, int clip_id, float* out_gain, float* out_pan) {
    return ms::Engine::instance().clip_audio(track_id, clip_id, out_gain, out_pan) ? 0 : -1;
}

int ms_audio_keyframe_add(int track_id, int clip_id, int prop, double time, double value,
                          int interp) {
    return ms::Engine::instance().add_audio_keyframe(track_id, clip_id, prop, time, value,
                                                     interp) ? 0 : -1;
}

int ms_audio_keyframe_clear(int track_id, int clip_id, int prop) {
    return ms::Engine::instance().clear_audio_keyframes(track_id, clip_id, prop) ? 0 : -1;
}

int ms_audio_keyframe_count(int track_id, int clip_id, int prop) {
    return ms::Engine::instance().audio_keyframe_count(track_id, clip_id, prop);
}

int ms_audio_keyframe_get(int track_id, int clip_id, int prop, int key, double* out3) {
    if (!out3) return -1;
    double time = 0.0, value = 0.0;
    int interp = 0;
    if (!ms::Engine::instance().audio_keyframe_at(track_id, clip_id, prop, key, &time, &value,
                                                  &interp))
        return -1;
    out3[0] = time;
    out3[1] = value;
    out3[2] = static_cast<double>(interp);
    return 0;
}

int ms_audio_keyframe_set(int track_id, int clip_id, int prop, int key, double time,
                          double value, int interp) {
    return ms::Engine::instance().set_audio_keyframe(track_id, clip_id, prop, key, time, value,
                                                     interp) ? 0 : -1;
}

int ms_audio_keyframe_sample(int track_id, int clip_id, int prop, double t0, double t1,
                             float* out, int bins) {
    if (!out) return -1;
    return ms::Engine::instance().sample_audio_param(track_id, clip_id, prop, t0, t1, out, bins)
               ? 0 : -1;
}

int ms_text_set_font_dir(const char* dir) {
    ms::set_text_font_dir(dir ? dir : "");
    return 0;
}

int ms_text_add_clip(int track_id, double start, double duration, const char* text,
                     const char* font, double size, const char* color_hex,
                     int align, int anchor, double outline, const char* outline_hex,
                     double shadow, double line_spacing, double tracking,
                     double box_width, int uppercase) {
    const ms::TextStyle style = make_text_style(font, size, color_hex, align, anchor, outline,
                                                outline_hex, shadow, line_spacing, tracking,
                                                box_width, uppercase);
    return static_cast<int>(ms::Engine::instance().add_text_clip(
        track_id, start, duration, text ? text : "", style));
}

int ms_text_update_clip(int track_id, int clip_id, const char* text, const char* font,
                        double size, const char* color_hex, int align, int anchor,
                        double outline, const char* outline_hex, double shadow,
                        double line_spacing, double tracking, double box_width,
                        int uppercase) {
    const ms::TextStyle style = make_text_style(font, size, color_hex, align, anchor, outline,
                                                outline_hex, shadow, line_spacing, tracking,
                                                box_width, uppercase);
    return ms::Engine::instance().set_clip_text(track_id, clip_id, text ? text : "", style)
               ? 0 : -1;
}

int ms_text_reveal_set(int track_id, int clip_id, int mode, double seconds) {
    return ms::Engine::instance().set_clip_text_reveal(track_id, clip_id, mode,
                                                       static_cast<float>(seconds))
               ? 0 : -1;
}

int ms_text_reveal_get(int track_id, int clip_id, float* out2) {
    int mode = 0;
    float seconds = 0.f;
    if (!ms::Engine::instance().clip_text_reveal(track_id, clip_id, &mode, &seconds)) return -1;
    if (out2) {
        out2[0] = static_cast<float>(mode);
        out2[1] = seconds;
    }
    return 0;
}

int ms_text_measure(const char* text, const char* font, double size, double box_width,
                    int uppercase, double* out_w, double* out_h, int* out_lines) {
    ms::TextStyle style;
    if (font && *font) style.font = font;
    style.size = size > 0 ? static_cast<float>(size) : 120.f;
    style.box_width = box_width > 0 ? static_cast<float>(box_width) : 0.f;
    style.uppercase = uppercase != 0;
    ms::TextMetrics m;
    if (!ms::measure_text(style, text ? text : "", m)) return -1;
    if (out_w) *out_w = m.width;
    if (out_h) *out_h = m.height;
    if (out_lines) *out_lines = m.lines;
    return 0;
}

int ms_keyframe_add(int track_id, int clip_id, int prop, double time, double value) {
    return ms::Engine::instance().add_keyframe(track_id, clip_id, prop, time, value) ? 0 : -1;
}
int ms_keyframe_clear(int track_id, int clip_id, int prop) {
    return ms::Engine::instance().clear_keyframes(track_id, clip_id, prop) ? 0 : -1;
}
int ms_keyframe_count(int track_id, int clip_id, int prop) {
    return ms::Engine::instance().keyframe_count(track_id, clip_id, prop);
}
int ms_keyframe_get(int track_id, int clip_id, int prop, int index, double* out_time,
                    double* out_value, int* out_interp) {
    return ms::Engine::instance().keyframe_at(track_id, clip_id, prop, index, out_time,
                                              out_value, out_interp)
               ? 0
               : -1;
}
int ms_keyframe_set(int track_id, int clip_id, int prop, int index, double time, double value,
                    int interp) {
    return ms::Engine::instance().set_keyframe(track_id, clip_id, prop, index, time, value,
                                               interp)
               ? 0
               : -1;
}
int ms_keyframe_set_interp(int track_id, int clip_id, int prop, int index, int interp) {
    return ms::Engine::instance().set_keyframe_interp(track_id, clip_id, prop, index, interp)
               ? 0
               : -1;
}
int ms_keyframe_sample(int track_id, int clip_id, int prop, double t0, double t1, float* out,
                       int bins) {
    return ms::Engine::instance().sample_keyframe(track_id, clip_id, prop, t0, t1, out, bins)
               ? 0
               : -1;
}
int ms_clip_remove(int track_id, int clip_id) {
    return ms::Engine::instance().remove_clip(track_id, clip_id) ? 0 : -1;
}
int ms_clip_move(int track_id, int clip_id, double start) {
    return ms::Engine::instance().move_clip(track_id, clip_id, start) ? 0 : -1;
}
int ms_clip_trim(int track_id, int clip_id, double start, double duration) {
    return ms::Engine::instance().trim_clip(track_id, clip_id, start, duration) ? 0 : -1;
}
int ms_clip_ripple_delete(int track_id, int clip_id) {
    return ms::Engine::instance().ripple_delete(track_id, clip_id) ? 0 : -1;
}
int ms_track_add(const char* name, int type) {
    return static_cast<int>(ms::Engine::instance().add_track(name ? name : "Track", type));
}
int ms_track_remove(int track_id) {
    return ms::Engine::instance().remove_track(track_id) ? 0 : -1;
}
int ms_track_flags(int track_id, int muted, int locked, int visible, int solo) {
    return ms::Engine::instance().set_track_flags(track_id, muted != 0, locked != 0,
                                                  visible != 0, solo != 0) ? 0 : -1;
}
int ms_effect_add(int track_id, int clip_id, const char* base_id) {
    return base_id ? ms::Engine::instance().add_effect(track_id, clip_id, base_id) : -1;
}
int ms_effect_remove(int track_id, int clip_id, int index) {
    return ms::Engine::instance().remove_effect(track_id, clip_id, index) ? 0 : -1;
}
int ms_effect_move(int track_id, int clip_id, int from, int to) {
    return ms::Engine::instance().move_effect(track_id, clip_id, from, to) ? 0 : -1;
}
int ms_effect_set_param(int track_id, int clip_id, int index, const char* name, float value) {
    return (name && ms::Engine::instance().set_effect_param(track_id, clip_id, index, name, value))
               ? 0
               : -1;
}
int ms_effect_set_enabled(int track_id, int clip_id, int index, int enabled) {
    return ms::Engine::instance().set_effect_enabled(track_id, clip_id, index, enabled != 0) ? 0 : -1;
}
// ---- Effect parameter automation ------------------------------------------
int ms_effect_keyframe_add(int track_id, int clip_id, int index, const char* param,
                           double time, double value, int interp) {
    if (!param) return -1;
    return ms::Engine::instance()
                   .add_effect_keyframe(track_id, clip_id, index, param, time, value, interp)
               ? 0
               : -1;
}

int ms_effect_keyframe_clear(int track_id, int clip_id, int index, const char* param) {
    if (!param) return -1;
    return ms::Engine::instance().clear_effect_keyframes(track_id, clip_id, index, param) ? 0
                                                                                          : -1;
}

int ms_effect_keyframe_count(int track_id, int clip_id, int index, const char* param) {
    if (!param) return 0;
    return ms::Engine::instance().effect_keyframe_count(track_id, clip_id, index, param);
}

int ms_effect_keyframe_get(int track_id, int clip_id, int index, const char* param,
                           int key, double* out3) {
    if (!param || !out3) return -1;
    double time = 0.0, value = 0.0;
    int interp = 0;
    if (!ms::Engine::instance().effect_keyframe_at(track_id, clip_id, index, param, key, &time,
                                                  &value, &interp))
        return -1;
    out3[0] = time;
    out3[1] = value;
    out3[2] = static_cast<double>(interp);
    return 0;
}

int ms_effect_keyframe_set(int track_id, int clip_id, int index, const char* param,
                           int key, double time, double value, int interp) {
    if (!param) return -1;
    return ms::Engine::instance()
                   .set_effect_keyframe(track_id, clip_id, index, param, key, time, value, interp)
               ? 0
               : -1;
}

int ms_effect_keyframe_sample(int track_id, int clip_id, int index, const char* param,
                              double t0, double t1, float* out, int bins) {
    if (!param || !out) return -1;
    return ms::Engine::instance()
                   .sample_effect_param(track_id, clip_id, index, param, t0, t1, out, bins)
               ? 0
               : -1;
}

int ms_effect_copy(int track_id, int clip_id) {
    return ms::Engine::instance().copy_effects(track_id, clip_id);
}

int ms_effect_paste(int track_id, int clip_id, int append) {
    return ms::Engine::instance().paste_effects(track_id, clip_id, append != 0);
}

int ms_effect_clipboard_count(void) {
    return ms::Engine::instance().effect_clipboard_count();
}

int ms_audio_set_master_gain(float gain_db) {
    ms::Engine& engine = ms::Engine::instance();
    ms::AudioFxParams gain;
    gain.kind = ms::AudioFxKind::Gain;
    gain.gain_db = gain_db;
    gain.enabled = true;
    if (engine.audio_insert_count() == 0) return engine.add_audio_insert(gain) >= 0 ? 0 : -1;
    return engine.set_audio_insert(0, gain) ? 0 : -1;
}

// ---- Master insert chain -------------------------------------------------
namespace {
// One row per writable AudioFxParams field. The names match the knob table the
// UI builds, so a parameter added here is reachable from Kotlin immediately.
struct AudioFxField {
    const char* name;
    float ms::AudioFxParams::*member;
};
const AudioFxField kAudioFxFields[] = {
    {"wet", &ms::AudioFxParams::wet},
    {"gain_db", &ms::AudioFxParams::gain_db},
    {"low_gain_db", &ms::AudioFxParams::low_gain_db},
    {"low_freq_hz", &ms::AudioFxParams::low_freq_hz},
    {"mid_gain_db", &ms::AudioFxParams::mid_gain_db},
    {"mid_freq_hz", &ms::AudioFxParams::mid_freq_hz},
    {"mid_q", &ms::AudioFxParams::mid_q},
    {"high_gain_db", &ms::AudioFxParams::high_gain_db},
    {"high_freq_hz", &ms::AudioFxParams::high_freq_hz},
    {"threshold_db", &ms::AudioFxParams::threshold_db},
    {"ratio", &ms::AudioFxParams::ratio},
    {"attack_ms", &ms::AudioFxParams::attack_ms},
    {"release_ms", &ms::AudioFxParams::release_ms},
    {"makeup_db", &ms::AudioFxParams::makeup_db},
    {"delay_ms", &ms::AudioFxParams::delay_ms},
    {"feedback", &ms::AudioFxParams::feedback},
    {"room_size", &ms::AudioFxParams::room_size},
    {"damping", &ms::AudioFxParams::damping},
    {"ceiling_db", &ms::AudioFxParams::ceiling_db},
};
const AudioFxField* find_audio_fx_field(const char* name) {
    if (!name) return nullptr;
    for (const auto& f : kAudioFxFields)
        if (std::strcmp(f.name, name) == 0) return &f;
    return nullptr;
}
} // namespace

int ms_audio_fx_count(void) {
    return static_cast<int>(eng().audio_effects().parameters().size());
}

int ms_audio_fx_add(int kind) {
    if (kind < 0 || kind > static_cast<int>(ms::AudioFxKind::Limiter)) return -1;
    ms::AudioFxParams params;
    params.kind = static_cast<ms::AudioFxKind>(kind);
    // Through the engine so the insert lands in the project document and on the
    // undo stack.
    return eng().add_audio_insert(params);
}

int ms_audio_fx_remove(int index) {
    if (index < 0) return -1;
    return eng().remove_audio_insert(index) ? 0 : -1;
}

int ms_audio_fx_clear(void) {
    eng().clear_audio_inserts();
    return 0;
}

int ms_audio_fx_kind(int index) {
    if (index < 0) return -1;
    const auto all = eng().audio_effects().parameters();
    if (static_cast<std::size_t>(index) >= all.size()) return -1;
    return static_cast<int>(all[static_cast<std::size_t>(index)].kind);
}

int ms_audio_fx_set_enabled(int index, int enabled) {
    if (index < 0) return -1;
    auto all = eng().audio_effects().parameters();
    if (static_cast<std::size_t>(index) >= all.size()) return -1;
    ms::AudioFxParams params = all[static_cast<std::size_t>(index)];
    params.enabled = enabled != 0;
    // set() drops the cached DSP state, so a bypass takes effect on the next block.
    return eng().set_audio_insert_enabled(index, params.enabled) ? 0 : -1;
}

int ms_audio_fx_enabled(int index) {
    if (index < 0) return -1;
    const auto all = eng().audio_effects().parameters();
    if (static_cast<std::size_t>(index) >= all.size()) return -1;
    return all[static_cast<std::size_t>(index)].enabled ? 1 : 0;
}

int ms_audio_fx_set_param(int index, const char* name, float value) {
    if (index < 0 || !name) return -1;
    const AudioFxField* field = find_audio_fx_field(name);
    if (!field) return -1;
    auto all = eng().audio_effects().parameters();
    if (static_cast<std::size_t>(index) >= all.size()) return -1;
    ms::AudioFxParams params = all[static_cast<std::size_t>(index)];
    params.*(field->member) = value;
    return eng().set_audio_insert(index, params) ? 0 : -1;
}

float ms_audio_fx_get_param(int index, const char* name, float fallback) {
    if (index < 0 || !name) return fallback;
    const AudioFxField* field = find_audio_fx_field(name);
    if (!field) return fallback;
    const auto all = eng().audio_effects().parameters();
    if (static_cast<std::size_t>(index) >= all.size()) return fallback;
    return all[static_cast<std::size_t>(index)].*(field->member);
}

int ms_track_gain(int track_id, float gain) {
    return eng().set_track_gain(static_cast<ms::TrackId>(track_id), gain) ? 0 : -1;
}

// ---- Per-track insert chain ----------------------------------------------
int ms_track_fx_count(int track_id) {
    return eng().track_insert_count(static_cast<ms::TrackId>(track_id));
}

int ms_track_fx_add(int track_id, int kind) {
    if (kind < 0 || kind > static_cast<int>(ms::AudioFxKind::Limiter)) return -1;
    ms::AudioFxParams params;
    params.kind = static_cast<ms::AudioFxKind>(kind);
    return eng().add_track_insert(static_cast<ms::TrackId>(track_id), params);
}

int ms_track_fx_remove(int track_id, int index) {
    return eng().remove_track_insert(static_cast<ms::TrackId>(track_id), index) ? 0 : -1;
}

int ms_track_fx_clear(int track_id) {
    eng().clear_track_inserts(static_cast<ms::TrackId>(track_id));
    return 0;
}

int ms_track_fx_kind(int track_id, int index) {
    ms::AudioFxParams p;
    if (!eng().track_insert_params(static_cast<ms::TrackId>(track_id), index, &p)) return -1;
    return static_cast<int>(p.kind);
}

int ms_track_fx_set_enabled(int track_id, int index, int enabled) {
    return eng().set_track_insert_enabled(static_cast<ms::TrackId>(track_id), index,
                                          enabled != 0) ? 0 : -1;
}

int ms_track_fx_enabled(int track_id, int index) {
    ms::AudioFxParams p;
    if (!eng().track_insert_params(static_cast<ms::TrackId>(track_id), index, &p)) return 0;
    return p.enabled ? 1 : 0;
}

int ms_track_fx_set_param(int track_id, int index, const char* name, float value) {
    if (!name) return -1;
    const AudioFxField* field = find_audio_fx_field(name);
    if (!field) return -1;
    ms::AudioFxParams p;
    if (!eng().track_insert_params(static_cast<ms::TrackId>(track_id), index, &p)) return -1;
    p.*(field->member) = value;
    return eng().set_track_insert(static_cast<ms::TrackId>(track_id), index, p) ? 0 : -1;
}

float ms_track_fx_get_param(int track_id, int index, const char* name, float fallback) {
    if (!name) return fallback;
    const AudioFxField* field = find_audio_fx_field(name);
    if (!field) return fallback;
    ms::AudioFxParams p;
    if (!eng().track_insert_params(static_cast<ms::TrackId>(track_id), index, &p)) return fallback;
    return p.*(field->member);
}

// ------------------------------------------------------------- proxies ------
int ms_proxy_register(const char* media_path, const char* proxy_path) {
    if (!media_path || !proxy_path) return -1;
    if (!std::filesystem::exists(proxy_path)) return -1;
    ms::proxy_registry().set(media_path, proxy_path);
    return 0;
}

int ms_proxy_unregister(const char* media_path) {
    if (!media_path) return -1;
    ms::proxy_registry().erase(media_path);
    return 0;
}

void ms_proxy_clear(void) { ms::proxy_registry().clear(); }

int ms_proxy_count(void) { return static_cast<int>(ms::proxy_registry().size()); }

int ms_proxy_for(const char* media_path, char* buf, int size) {
    if (!media_path || !buf || size <= 0) return 0;
    const std::string p = ms::proxy_registry().proxy_for(media_path);
    const int n = static_cast<int>(std::min<size_t>(p.size(), static_cast<size_t>(size - 1)));
    std::memcpy(buf, p.data(), static_cast<size_t>(n));
    buf[n] = '\0';
    return n;
}

int ms_proxy_enabled(int on) {
    const bool previous = ms::proxy_registry().enabled();
    ms::proxy_registry().set_enabled(on != 0);
    return previous ? 1 : 0;
}

int ms_proxy_is_enabled(void) { return ms::proxy_registry().enabled() ? 1 : 0; }
}
