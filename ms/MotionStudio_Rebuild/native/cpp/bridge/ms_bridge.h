#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Called from Flutter via FFI / Platform Channel

typedef struct {
    double time_seconds;
    int    width;
    int    height;
    int    is_playing;
} MSPreviewState;

// Initialize the native engine
int ms_engine_init(int width, int height);

// Replace the current document with a fresh project (name / size / fps).
int ms_project_new(const char* name, int width, int height, double fps);

// Shutdown
void ms_engine_shutdown();

// Load a project from JSON string
int ms_project_load(const char* json);

// Get current project as JSON (caller must free)
char* ms_project_to_json();

// Seek
void ms_seek(double time_seconds);

// Play / Pause
void ms_play();
void ms_pause();
int ms_project_save(const char* path);
int ms_project_autosave(const char* path);
double ms_perf_frame_ms();
double ms_perf_fps();
unsigned long long ms_perf_cache_hits();
unsigned long long ms_perf_cache_misses();
void ms_free_string(char* p);
size_t ms_frame_bytes();
int ms_copy_frame_rgba(void* rgba, size_t bytes);

// Fingerprint of the preview state. Unchanged between polls means the next
// render would produce exactly the pixels already on screen.
unsigned long long ms_render_signature();
int ms_import_media(const char* path);
int ms_add_clip(int track_id, int media_id, double start, double duration);
int ms_split_clip(int track_id, int clip_id, double at);
int ms_undo();
int ms_redo();

// Get current preview state
MSPreviewState ms_get_preview_state();

// Render one frame into the given texture / surface (platform specific)
int ms_render_frame();

// Timeline audio. The host audio thread pulls interleaved f32 PCM for the
// current playhead; the master insert chain is already applied.
typedef struct {
    int    sample_rate;
    int    channels;
    double duration;
    double time;
    int    is_playing;
} MSAudioState;

MSAudioState ms_audio_state();
int ms_audio_read_pcm(float* interleaved, int frames, int channels);
// Sets the master gain stage of the timeline insert chain (dB, 0 = unity).
int ms_audio_set_master_gain(float gain_db);

// ---- Master insert chain (the timeline's final mix bus) ----
// Kinds: 0 Gain, 1 Parametric EQ, 2 Compressor, 3 Delay, 4 Reverb, 5 Limiter.
// Inserts run in list order on the mixed timeline audio, for playback and
// export alike. The chain is engine state, not document state.
int ms_audio_fx_count(void);
/** Appends an insert and returns its index, or -1 on a bad kind. */
int ms_audio_fx_add(int kind);
int ms_audio_fx_remove(int index);
int ms_audio_fx_clear(void);
/** Kind of the insert at `index`, or -1 when the index is out of range. */
int ms_audio_fx_kind(int index);
int ms_audio_fx_set_enabled(int index, int enabled);
int ms_audio_fx_enabled(int index);
/** Parameter names: wet, gain_db, low_gain_db, low_freq_hz, mid_gain_db,
 *  mid_freq_hz, mid_q, high_gain_db, high_freq_hz, threshold_db, ratio,
 *  attack_ms, release_ms, makeup_db, delay_ms, feedback, room_size, damping,
 *  ceiling_db. Unknown names are rejected (0 / fallback) so a typo cannot
 *  silently do nothing. */
int ms_audio_fx_set_param(int index, const char* name, float value);
float ms_audio_fx_get_param(int index, const char* name, float fallback);

/** Track fader, linear gain (1 = unity). Applies to the timeline audio mix. */
int ms_track_gain(int track_id, float gain);

// ---- Per-track insert chain ----------------------------------------------
// Same insert kinds and parameter names as the master chain, but applied to one
// track's clips before the master bus. Saved with the project document, so a
// mixed session reopens with its track processing.
int ms_track_fx_count(int track_id);
/** Appends an insert and returns its index, or -1 on a bad track or kind. */
int ms_track_fx_add(int track_id, int kind);
int ms_track_fx_remove(int track_id, int index);
int ms_track_fx_clear(int track_id);
int ms_track_fx_kind(int track_id, int index);
int ms_track_fx_set_enabled(int track_id, int index, int enabled);
int ms_track_fx_enabled(int track_id, int index);
/** Parameter names are the same as ms_audio_fx_set_param. */
int ms_track_fx_set_param(int track_id, int index, const char* name, float value);
float ms_track_fx_get_param(int track_id, int index, const char* name, float fallback);

// Display look (3D .cube LUT) applied to the rendered preview frame.
// Returns 0 on success, -1 if the file is missing or malformed.
int ms_look_load(const char* path);
void ms_look_clear();
// strength: 0 = no effect, 1 = full look.
void ms_look_set_strength(float strength);
float ms_look_strength();

// Per-clip look (3D .cube LUT) graded over one clip after its grade. Returns 0
// on success, -1 if the file is missing, malformed or the clip was not found.
int ms_clip_look_set(int track_id, int clip_id, const char* path);
int ms_clip_look_set_strength(int track_id, int clip_id, float strength);
int ms_clip_look_clear(int track_id, int clip_id);
// Returns 0 and fills buf (NUL-terminated) plus the strength, -1 when the clip
// carries no look.
int ms_clip_look_get(int track_id, int clip_id, char* buf, int size, float* strength);

// ---- Timeline editing (each edit is undoable) ----
int ms_clip_remove(int track_id, int clip_id);
int ms_clip_move(int track_id, int clip_id, double start);
int ms_clip_trim(int track_id, int clip_id, double start, double duration);
int ms_clip_ripple_delete(int track_id, int clip_id);
int ms_track_add(const char* name, int type);
int ms_track_remove(int track_id);
int ms_track_flags(int track_id, int muted, int locked, int visible, int solo);

// ---- Master grade + scopes ----
void ms_grade_set(float exposure, float contrast, float saturation, float temperature,
                  float lift, float gamma, float gain);
void ms_grade_clear();
/** Fills 7 floats (exposure, contrast, saturation, temperature, lift, gamma,
 *  gain, shadows, highlights, vibrance). Returns 1 when a master grade is
 *  active, 0 otherwise. */
int ms_grade_get(float* out10);
/** Shadow / highlight split and vibrance, on top of the seven base moves. */
void ms_grade_tonal_set(float shadows, float highlights, float vibrance);
// Fills 4*bins ints (R, G, B, luma). Returns 0 on success.
int ms_scopes_histogram(int* out, int bins);

// ---- Transitions ----
// Returns the new transition id, or 0 on failure.
int ms_transition_add(int track_id, int outgoing, int incoming, int type, double duration);

// ---- Comp network (node graph) ----
// Node ops: 0 Read, 1 Write, 2 Merge, 3 Blur, 4 Grade, 5 Transform,
//           6 Constant, 7 Viewer, 8 Saturation, 9 Invert, 10 CompositeOver.
int ms_comp_node_count(void);
int ms_comp_node_at(int index, int* out_id, int* out_op, int* out_enabled,
                    float* out_x, float* out_y);
int ms_comp_edge_count(void);
int ms_comp_edge_at(int index, int* out_from, int* out_to, int* out_slot);
int ms_comp_add_node(int op, float x, float y);
int ms_comp_remove_node(int id);
int ms_comp_connect(int from, int to, int slot);
int ms_comp_disconnect(int from, int to, int slot);
int ms_comp_set_param(int id, const char* name, double value);
int ms_comp_get_param(int id, const char* name, double* out);
/** Empty path makes the Read node sample the edited timeline instead of a file. */
int ms_comp_set_media(int id, const char* path);
/** Knob link: `param` on `node` follows `src_param` on `src`. Refused on a cycle. */
int ms_comp_link_param(int node, const char* param, int src, const char* src_param);
int ms_comp_unlink_param(int node, const char* param);
int ms_comp_link_count(void);
/** "node|param|src|src_param", or "" when the index is out of range. */
int ms_comp_link_at(int index, char* buf, int size);
int ms_comp_get_media(int id, char* buf, int size);
int ms_comp_set_enabled(int id, int enabled);
int ms_comp_set_position(int id, float x, float y);
int ms_comp_set_sink(int id);
int ms_comp_sink(void);
int ms_comp_set_active(int active);
int ms_comp_active(void);

// Which decoder backend is live: "mediacodec", "ffmpeg" or "slate".
// "slate" means imported video renders as a placeholder — surface it in the UI.
const char* ms_decoder_backend();

// Timeline markers. Returns the new marker id, or 0.
int ms_marker_add(double time, const char* name);
int ms_marker_remove(int id);
// Project size / frame rate without discarding edits. 0 on success.
int ms_project_settings(int width, int height, double fps);

// Shape mask on a clip. shape: 0 rectangle, 1 ellipse, 2 full frame. x/y/w/h are
// normalized 0..1 layer coordinates, feather is in output pixels.
int ms_clip_mask_set(int track_id, int clip_id, int shape, float x, float y, float w,
                     float h, float feather, int inverted);
int ms_clip_mask_clear(int track_id, int clip_id);
/** Writes shape/x/y/w/h/feather to out6 and the invert flag to inverted. */
int ms_clip_mask_get(int track_id, int clip_id, float* out6, int* inverted);

// Multi-mask: a clip can carry several shape masks. op is 0 add (union) or 1
// subtract (cut out of the union). At most 8 per clip.
/** Appends a mask; returns the new mask count, or -1 on failure. */
int ms_clip_mask_add(int track_id, int clip_id, int shape, float x, float y, float w,
                     float h, float feather, int inverted, int op);
int ms_clip_mask_update(int track_id, int clip_id, int index, int shape, float x, float y,
                        float w, float h, float feather, int inverted, int op);
int ms_clip_mask_remove(int track_id, int clip_id, int index);
int ms_clip_mask_count(int track_id, int clip_id);
/** Writes shape/x/y/w/h/feather to out6, plus inverted and op. 0 on success. */
int ms_clip_mask_read(int track_id, int clip_id, int index, float* out6, int* inverted,
                      int* op);

// Retime. The same slice of source plays at `speed` (0.1..8) and the clip's
// timeline length follows it. Text and adjustment clips are rejected. 0 on
// success, -1 when the clip cannot be retimed.
int ms_clip_speed_set(int track_id, int clip_id, float speed);
int ms_clip_speed_get(int track_id, int clip_id, float* out);

// Adjustment layer: no media of its own. Its effect stack and grade run over
// everything the tracks below have already composited. Returns the new clip id
// or 0.
int ms_adjustment_add(int track_id, double start, double duration);

// Per-clip grade. Applied to the clip before compositing, independent of the
// master grade. Returns 0 on success.
int ms_clip_grade_set(int track_id, int clip_id, float exposure, float contrast,
                      float saturation, float temperature, float lift, float gamma,
                      float gain);
int ms_clip_grade_clear(int track_id, int clip_id);
/** Reads a clip's grade; writes 10 floats and returns 0, or -1 when ungraded. */
int ms_clip_grade_get(int track_id, int clip_id, float* out10);
/** Per-clip shadow / highlight split and vibrance. 0 on success. */
int ms_clip_grade_tonal_set(int track_id, int clip_id, float shadows, float highlights,
                            float vibrance);

// Peak envelope for a clip's audio: writes `bins` floats in 0..1. Returns 0 on
// success, -1 when the clip has no decodable audio (text clips, silent media).
int ms_clip_peaks(int track_id, int clip_id, float* out, int bins);

// ---- Clip audio ----------------------------------------------------------
// The clip fader multiplies the track fader in the timeline mix; pan is -1 left
// to +1 right. Both are saved with the clip.
int ms_clip_audio_set(int track_id, int clip_id, float gain, float pan);
int ms_clip_audio_get(int track_id, int clip_id, float* out_gain, float* out_pan);

// ---- Clip audio automation ----------------------------------------------
// prop: 0 gain (a multiplier on the fader), 1 pan (absolute). Times and samples
// are clip-local seconds. Times may be written as [time, value, interp].
int ms_audio_keyframe_add(int track_id, int clip_id, int prop, double time, double value,
                          int interp);
int ms_audio_keyframe_clear(int track_id, int clip_id, int prop);
int ms_audio_keyframe_count(int track_id, int clip_id, int prop);
/** Fills out3 with time, value, interpolation. */
int ms_audio_keyframe_get(int track_id, int clip_id, int prop, int key, double* out3);
int ms_audio_keyframe_set(int track_id, int clip_id, int prop, int key, double time,
                          double value, int interp);
int ms_audio_keyframe_sample(int track_id, int clip_id, int prop, double t0, double t1,
                             float* out, int bins);

// ---- Text / titles ----
// Points the native rasteriser at the directory holding <family>.ttf files
// (host extracts them from assets once). Returns 0 on success.
int ms_text_set_font_dir(const char* dir);
/** Adds a title clip. font is a family name ("anton", "bebas", ...).
 *  align/anchor are 0=left/top, 1=center/middle, 2=right/bottom.
 *  color_hex/outline_hex are "#rrggbb". Returns the new clip id, or 0. */
int ms_text_add_clip(int track_id, double start, double duration, const char* text,
                     const char* font, double size, const char* color_hex,
                     int align, int anchor, double outline, const char* outline_hex,
                     double shadow, double line_spacing, double tracking,
                     double box_width, int uppercase);
// Updates an existing title clip (same argument layout). Returns 0 on success.
int ms_text_update_clip(int track_id, int clip_id, const char* text, const char* font,
                        double size, const char* color_hex, int align, int anchor,
                        double outline, const char* outline_hex, double shadow,
                        double line_spacing, double tracking, double box_width,
                        int uppercase);
// Text metrics for hit-testing/layout: returns 0 when the font resolves,
// writing width/height/lines into the out pointers.
// Title reveal: 0 none, 1 type-on, 2 word-by-word, 3 line-by-line. `seconds`
// <= 0 spreads the reveal across the whole clip.
int ms_text_reveal_set(int track_id, int clip_id, int mode, double seconds);
/** Fills out2 with mode and seconds; -1 when the clip is not a title. */
int ms_text_reveal_get(int track_id, int clip_id, float* out2);

int ms_text_measure(const char* text, const char* font, double size, double box_width, int uppercase, double* out_w, double* out_h,
                    int* out_lines);
int ms_transition_remove(int track_id, int id);

// ---- Keyframes (prop: 0 opacity, 1 scale, 2 pos_x, 3 pos_y; time clip-local) ----
int ms_keyframe_add(int track_id, int clip_id, int prop, double time, double value);
int ms_keyframe_clear(int track_id, int clip_id, int prop);
/* Curve editing: keys are addressed by their index in time order. */
int ms_keyframe_count(int track_id, int clip_id, int prop);
int ms_keyframe_get(int track_id, int clip_id, int prop, int index, double* out_time,
                    double* out_value, int* out_interp);
int ms_keyframe_set(int track_id, int clip_id, int prop, int index, double time, double value,
                    int interp);
int ms_keyframe_set_interp(int track_id, int clip_id, int prop, int index, int interp);
int ms_keyframe_sample(int track_id, int clip_id, int prop, double t0, double t1, float* out,
                       int bins);

// ---- Proxy media ---------------------------------------------------------
// A proxy is a small, already-encoded stand-in for a large source file. The app
// generates it with the platform codec and registers the file it wrote here;
// preview then decodes the proxy instead of the source. Export calls
// ms_proxy_enabled(0) first so the delivered master still comes from the source.
// Registered proxies are per-process state, not project data: they are rebuilt
// from disk by registering whatever files exist when a project opens.
int ms_proxy_register(const char* media_path, const char* proxy_path);
int ms_proxy_unregister(const char* media_path);
void ms_proxy_clear(void);
int ms_proxy_count(void);
// Writes the registered proxy path into buf (empty when none). Returns its length.
int ms_proxy_for(const char* media_path, char* buf, int size);
// Returns the previous state. Changing it reopens cached decoders.
int ms_proxy_enabled(int on);
int ms_proxy_is_enabled(void);

// ---- Clip effect stack ----
// Returns the new effect's stack index, or -1 on failure.
int ms_effect_add(int track_id, int clip_id, const char* base_id);
int ms_effect_remove(int track_id, int clip_id, int index);
// Reorders the stack: the effect at `from` lands at `to`. 0 on success.
int ms_effect_move(int track_id, int clip_id, int from, int to);
int ms_effect_set_param(int track_id, int clip_id, int index, const char* name, float value);
int ms_effect_set_enabled(int track_id, int clip_id, int index, int enabled);

// Effect parameter automation. Times are clip-local seconds; interp follows
// ms::CurveInterp (0 linear, 1 hold, 2-4 ease, 5 smooth). A parameter with at
// least one key is driven by its curve at render time.
int ms_effect_keyframe_add(int track_id, int clip_id, int index, const char* param,
                           double time, double value, int interp);
int ms_effect_keyframe_clear(int track_id, int clip_id, int index, const char* param);
int ms_effect_keyframe_count(int track_id, int clip_id, int index, const char* param);
// Reads a key into [time, value, interp]; returns 0 on success.
int ms_effect_keyframe_get(int track_id, int clip_id, int index, const char* param,
                           int key, double* out3);
int ms_effect_keyframe_set(int track_id, int clip_id, int index, const char* param,
                           int key, double time, double value, int interp);
// Fills `bins` samples of the curve over [t0, t1]; returns 0 on success.
int ms_effect_keyframe_sample(int track_id, int clip_id, int index, const char* param,
                              double t0, double t1, float* out, int bins);

// Effect stack clipboard (session-scoped). copy returns the number of effects,
// paste returns the clip's new stack size; both return -1 on failure.
int ms_effect_copy(int track_id, int clip_id);
int ms_effect_paste(int track_id, int clip_id, int append);
int ms_effect_clipboard_count(void);

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
}
#endif
