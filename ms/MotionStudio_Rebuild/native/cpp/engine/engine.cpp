#include "../include/ms_engine.hpp"
#include "../include/ms_edit_ops.hpp"
#include "../include/ms_finishing.hpp"
#include "../include/ms_scopes.hpp"
#include "../include/ms_types.hpp"
#include "../include/ms_gpu.hpp"
#include "../include/ms_image.hpp"
#include "../include/ms_media.hpp"
#include "../include/ms_ffmpeg_io.hpp"
#include <atomic>
#include <mutex>
#include <chrono>
#include <iostream>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <algorithm>

namespace ms {

struct Engine::Impl {
    std::mutex mutex;
    bool initialized = false;
    int width = 1080;
    int height = 1920;
    float fps = 30.f;
    std::atomic<TimeSec> current_time{0.0};
    std::atomic<bool> playing{false};
    std::atomic<int64_t> frame_index{0};
    // Bumped by every operation that can change rendered pixels. The frame cache
    // key includes it, so no edit can be served a stale frame.
    std::atomic<uint64_t> revision{0};
    std::chrono::steady_clock::time_point last_tick;
    FrameCallback frame_cb;
    // Decoders owned by the comp's Read nodes, keyed by media path. Kept apart
    // from the timeline player's so a comp read cannot disturb playback state.
    std::unordered_map<std::string, std::unique_ptr<IMediaDecoder>> comp_decoders;
    // The timeline frame the comp is currently cooking over, kept so several Read
    // nodes with an empty media_path share one decode instead of one each.
    ImageBuffer comp_sequence;
    uint64_t comp_sequence_rev = ~0ull;
    int64_t comp_sequence_frame = -1;
};

Engine& Engine::instance() { static Engine e; return e; }
Engine::Engine() : impl_(std::make_unique<Impl>()), project_(std::make_unique<ProjectSession>()), last_frame_(std::make_unique<ImageBuffer>()), frame_cache_(std::make_unique<FrameCache>(48)), autosave_(project_.get()) {
    player_ = std::make_unique<TimelinePlayer>(project_.get());
    audio_ = std::make_unique<TimelineAudioPlayback>(project_.get());
    comp_.set_read_provider([this](const GraphNode& node, double t, int w, int h, ImageBuffer& out) {
        return read_comp_source(node, t, w, h, out);
    });
}
Engine::~Engine() = default;

bool Engine::init(int width, int height) {
    return new_project("Untitled", width, height, 30.f);
}

bool Engine::new_project(const std::string& name, int width, int height, float fps) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->width = width > 0 ? width : 1080;
    impl_->height = height > 0 ? height : 1920;
    impl_->fps = fps > 1.f ? fps : 30.f;
    project_->new_project(name, impl_->width, impl_->height, impl_->fps);
    player_->set_session(project_.get());
    player_->prepare();
    audio_->set_session(project_.get());
    audio_->stop();
    audio_->prepare();
    // A new project starts with no grade and empty inserts, whatever the last
    // one left in the engine.
    apply_master_state();
    impl_->current_time = 0.0;
    impl_->frame_index = 0;
    impl_->playing = false;
    impl_->last_tick = std::chrono::steady_clock::now();
    impl_->initialized = true;
    frame_cache_->clear();
    perf_.set_backend("native-cpu");
    return true;
}

void Engine::shutdown() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->playing = false;
    impl_->initialized = false;
    audio_->stop();
    last_frame_->clear(0,0,0,0);
}

bool Engine::is_initialized() const { return impl_->initialized; }

bool Engine::load_project_json(const std::string& json) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto e = project_->load_from_json(json);
    if (!e.ok()) return false;
    impl_->width = project_->meta().width;
    impl_->height = project_->meta().height;
    impl_->fps = project_->meta().fps;
    // Restore the comp network if the document carries one.
    const std::string marker = "\"comp\":\"";
    const size_t comp_at = json.find(marker);
    if (comp_at != std::string::npos) {
        std::string comp;
        for (size_t i = comp_at + marker.size(); i < json.size(); ++i) {
            if (json[i] == '"') break;
            if (json[i] == '\\' && i + 1 < json.size()) { comp.push_back(json[++i]); continue; }
            comp.push_back(json[i]);
        }
        if (!comp.empty() && !comp_.deserialize(comp)) comp_.deserialize("MSCOMP1");
    }
    impl_->current_time = 0.0;
    impl_->frame_index = 0;
    impl_->playing = false;
    player_->set_session(project_.get());
    audio_->set_session(project_.get());
    audio_->stop();
    audio_->prepare();
    // The document's grade and master inserts are the truth after a load.
    apply_master_state();
    auto prep = player_->prepare();
    // Opening a saved project never goes through new_project(), so this has to
    // bring the engine up itself: render_frame() bails immediately while
    // `initialized` is false, which left the preview black after a load.
    impl_->last_tick = std::chrono::steady_clock::now();
    impl_->initialized = true;
    frame_cache_->clear();
    perf_.set_backend("native-cpu");
    return prep.ok();
}

std::string Engine::project_to_json() {
    sync_master_state();
    std::string json = project_->to_json();
    // The comp is stored as its own record string inside the project document so
    // a saved node network reopens with the timeline that fed it.
    const std::string comp = comp_.serialize();
    const size_t close = json.find_last_of('}');
    if (close == std::string::npos) return json;
    std::string escaped;
    escaped.reserve(comp.size());
    for (char c : comp) {
        if (c == '"' || c == '\\') escaped.push_back('\\');
        escaped.push_back(c);
    }
    json.insert(close, ",\"comp\":\"" + escaped + "\"");
    return json;
}

void Engine::seek(TimeSec t) {
    t = std::max<TimeSec>(0.0, t);
    impl_->current_time = t;
    impl_->frame_index = static_cast<int64_t>(t * project_->meta().fps);
    project_->seek(t);
    // Audio follows the video playhead exactly; the platform output stage pulls
    // PCM for the same instant via read_audio().
    audio_->seek(t);
}

void Engine::play() {
    impl_->last_tick = std::chrono::steady_clock::now();
    impl_->playing = true;
    project_->play();
    audio_->seek(impl_->current_time.load());
    audio_->play();
}

void Engine::pause() {
    impl_->playing = false;
    project_->pause();
    audio_->pause();
}
void Engine::toggle_play() { if (impl_->playing.load()) pause(); else play(); }

PreviewState Engine::state() const {
    PreviewState s;
    s.time = impl_->current_time.load();
    s.width = project_->meta().width;
    s.height = project_->meta().height;
    s.playing = impl_->playing.load();
    s.fps = project_->meta().fps;
    s.frame_index = impl_->frame_index.load();
    return s;
}

bool Engine::render_frame() {
    if (!impl_->initialized) return false;
    perf_.begin_frame();
    if (impl_->playing.load()) {
        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - impl_->last_tick).count();
        impl_->last_tick = now;
        double t = impl_->current_time.load() + dt;
        const double end = project_->sequence_end();
        if (end > 0.0 && t >= end) t = 0.0;
        seek(t);
    }
    const int64_t fi = impl_->frame_index.load();
    // The document fingerprint is the edit revision, not a clip count: two edits
    // can leave the clip count unchanged (a trim, a move, an undo of either) and
    // would otherwise reuse a stale frame.
    const uint64_t revision = impl_->revision.load();
    CacheKey key{fi, revision ^ (static_cast<uint64_t>(project_->clip_count()) << 48) ^
                         (static_cast<uint64_t>(project_->tracks().size()) << 56)};
    bool hit = frame_cache_->get(key, *last_frame_);
    if (!hit) {
        Error e = Error::success();
        if (comp_rendered()) {
            // Cook the timeline first: every Read/Viewer source in the comp sees
            // the sequence at this time, so a comp can be built from the edit.
            ImageBuffer sequence;
            e = player_->render_frame(impl_->current_time.load(), sequence);
            if (e.ok()) {
                impl_->comp_sequence.copy_from(sequence);
                impl_->comp_sequence_rev = revision;
                impl_->comp_sequence_frame = fi;
                e = comp_.evaluate(comp_.sink_id(), impl_->current_time.load(),
                                   impl_->width, impl_->height, *last_frame_);
            }
        } else {
            e = player_->render_frame(impl_->current_time.load(), *last_frame_);
        }
        if (!e.ok()) return false;
        frame_cache_->put(key, *last_frame_);
    }
    // The cache holds ungraded frames; grade and look are applied on top so
    // changing either never needs a cache flush.
    if (grade_active_) {
        GradeSettings g;
        g.exposure = grade_[0];
        g.contrast = grade_[1];
        g.saturation = grade_[2];
        g.temperature = grade_[3];
        g.lift = grade_[4];
        g.gamma = grade_[5];
        g.gain = grade_[6];
        g.shadows = grade_[7];
        g.highlights = grade_[8];
        g.vibrance = grade_[9];
        apply_finishing_grade(*last_frame_, g);
    }
    if (look_ && look_strength_ > 0.f) {
        apply_lut(*last_frame_, *look_, look_strength_);
    }
    perf_.end_frame(hit); perf_.set_cache_bytes(0);
    (void)autosave_.tick();
    if (impl_->frame_cb) impl_->frame_cb(state());
    return true;
}

bool Engine::copy_last_frame(void* rgba, size_t bytes) const {
    if (!rgba || !last_frame_->valid()) return false;
    const size_t need = static_cast<size_t>(last_frame_->stride()) * static_cast<size_t>(last_frame_->height());
    if (bytes < need) return false;
    std::memcpy(rgba, last_frame_->data(), need);
    return true;
}

size_t Engine::last_frame_bytes() const {
    if (!last_frame_->valid()) return 0;
    return static_cast<size_t>(last_frame_->stride()) * static_cast<size_t>(last_frame_->height());
}

bool Engine::load_look(const std::string& path) {
    auto lut = std::make_unique<Lut3D>();
    if (!load_cube_lut(path, *lut)) return false;
    look_ = std::move(lut);
    if (look_strength_ <= 0.f) look_strength_ = 1.f;
    return true;
}

void Engine::clear_look() { look_.reset(); }

void Engine::set_look_strength(float strength) {
    look_strength_ = std::clamp(strength, 0.f, 1.f);
}

Error Engine::save_project(const std::string& path) {
    sync_master_state();
    return project_->save_to_file(path);
}
Error Engine::autosave_project(const std::string& path) {
    sync_master_state();
    autosave_.set_path(path);
    return autosave_.force();
}
PerformanceStats Engine::performance() const { return perf_.stats(); }

unsigned long long Engine::render_signature() const {
    // FNV-1a over everything render_frame() reads; cheap enough to run on every
    // preview poll (a dozen integers, no pixel work).
    unsigned long long h = 0xcbf29ce484222325ull;
    auto mix = [&h](unsigned long long v) {
        h ^= v;
        h *= 0x100000001b3ull;
    };
    auto mix_float = [&mix](float f) {
        unsigned int bits = 0;
        std::memcpy(&bits, &f, sizeof(bits));
        mix(bits);
    };

    mix(static_cast<unsigned long long>(impl_->frame_index.load()));
    mix(impl_->revision.load());
    mix(static_cast<unsigned long long>(project_->clip_count()));
    mix(static_cast<unsigned long long>(project_->tracks().size()));
    mix(impl_->playing.load() ? 1ull : 0ull);

    // Grade and look are applied on top of the cached frame, so they are not
    // part of the frame cache key but they do change the picture.
    mix(grade_active_ ? 1ull : 0ull);
    if (grade_active_) {
        for (float g : grade_) mix_float(g);
    }
    mix(reinterpret_cast<unsigned long long>(look_.get()));
    mix_float(look_strength_);

    // A comp cook depends on the graph, not only on the playhead.
    mix(comp_rendered() ? 1ull : 0ull);
    mix(static_cast<unsigned long long>(comp_.sink_id()));
    return h;
}

void Engine::resize(int width, int height) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (width > 0) impl_->width = width;
    if (height > 0) impl_->height = height;
    project_->meta().width = impl_->width;
    project_->meta().height = impl_->height;
}

bool Engine::add_effect_to_clip(ClipId clip, const std::string& base_id) {
    // Locate the clip and attach a CPU effect to its motion stack.
    for (auto& track : const_cast<std::vector<TrackInfo>&>(project_->tracks())) {
        for (auto& c : track.clips) if (c.id == clip) {
            c.motion.ensure_defaults();
            c.motion.effects.push_back({base_id, {}, true});
            project_->mark_dirty();
            return true;
        }
    }
    return false;
}

Error Engine::read_audio(float* interleaved, int frames, int channels) {
    if (!audio_) return Error::make(ErrorCode::NotInitialized, "audio not available");
    return audio_->read_pcm(interleaved, frames, channels);
}

AudioPlaybackInfo Engine::audio_info() const {
    return audio_ ? audio_->info() : AudioPlaybackInfo{};
}

AudioEffectChain& Engine::audio_effects() { return audio_->effects(); }

void Engine::sync_master_state() {
    if (!project_ || !audio_) return;
    MasterState s;
    s.grade_enabled = grade_active_;
    std::copy(std::begin(grade_), std::end(grade_), s.grade);
    s.audio = audio_->effects().parameters();
    project_->set_master(s);
}

void Engine::apply_master_state() {
    if (!project_ || !audio_) return;
    const MasterState& s = project_->master();
    grade_active_ = s.grade_enabled;
    std::copy(std::begin(s.grade), std::end(s.grade), grade_);
    AudioEffectChain& chain = audio_->effects();
    chain.clear();
    for (const auto& p : s.audio) chain.add(p);
    frame_cache_->clear();
}

int Engine::add_audio_insert(const AudioFxParams& params) {
    if (!audio_ || !project_) return -1;
    sync_master_state();
    project_->push_undo();
    const std::size_t index = audio_->effects().add(params);
    sync_master_state();
    project_->mark_dirty();
    return static_cast<int>(index);
}

bool Engine::remove_audio_insert(int index) {
    if (!audio_ || !project_ || index < 0) return false;
    if (static_cast<std::size_t>(index) >= audio_->effects().parameters().size()) return false;
    sync_master_state();
    project_->push_undo();
    if (!audio_->effects().remove(static_cast<std::size_t>(index))) return false;
    sync_master_state();
    project_->mark_dirty();
    return true;
}

void Engine::clear_audio_inserts() {
    if (!audio_ || !project_) return;
    if (audio_->effects().parameters().empty()) return;
    sync_master_state();
    project_->push_undo();
    audio_->effects().clear();
    sync_master_state();
    project_->mark_dirty();
}

bool Engine::set_audio_insert(int index, const AudioFxParams& params) {
    if (!audio_ || !project_ || index < 0) return false;
    if (!audio_->effects().set(static_cast<std::size_t>(index), params)) return false;
    sync_master_state();
    project_->mark_dirty();
    return true;
}

bool Engine::set_audio_insert_enabled(int index, bool enabled) {
    if (!audio_ || !project_ || index < 0) return false;
    const auto all = audio_->effects().parameters();
    if (static_cast<std::size_t>(index) >= all.size()) return false;
    AudioFxParams params = all[static_cast<std::size_t>(index)];
    if (params.enabled == enabled) return true;
    params.enabled = enabled;
    sync_master_state();
    project_->push_undo();
    if (!audio_->effects().set(static_cast<std::size_t>(index), params)) return false;
    sync_master_state();
    project_->mark_dirty();
    return true;
}

int Engine::audio_insert_count() const {
    return audio_ ? static_cast<int>(audio_->effects().parameters().size()) : 0;
}

int Engine::add_track_insert(TrackId track, const AudioFxParams& params) {
    if (!project_) return -1;
    TrackInfo* t = project_->find_track(track);
    if (!t) return -1;
    project_->push_undo();
    t->audio_inserts.push_back(params);
    project_->mark_dirty();
    // The mixer notices the new insert on its next block; no frame cache to drop.
    return static_cast<int>(t->audio_inserts.size()) - 1;
}

bool Engine::remove_track_insert(TrackId track, int index) {
    if (!project_ || index < 0) return false;
    TrackInfo* t = project_->find_track(track);
    if (!t || static_cast<size_t>(index) >= t->audio_inserts.size()) return false;
    project_->push_undo();
    t->audio_inserts.erase(t->audio_inserts.begin() + index);
    project_->mark_dirty();
    return true;
}

void Engine::clear_track_inserts(TrackId track) {
    if (!project_) return;
    TrackInfo* t = project_->find_track(track);
    if (!t || t->audio_inserts.empty()) return;
    project_->push_undo();
    t->audio_inserts.clear();
    project_->mark_dirty();
}

bool Engine::set_track_insert(TrackId track, int index, const AudioFxParams& params) {
    if (!project_ || index < 0) return false;
    TrackInfo* t = project_->find_track(track);
    if (!t || static_cast<size_t>(index) >= t->audio_inserts.size()) return false;
    t->audio_inserts[static_cast<size_t>(index)] = params;
    project_->mark_dirty();
    return true;
}

bool Engine::set_track_insert_enabled(TrackId track, int index, bool enabled) {
    if (!project_ || index < 0) return false;
    TrackInfo* t = project_->find_track(track);
    if (!t || static_cast<size_t>(index) >= t->audio_inserts.size()) return false;
    AudioFxParams& p = t->audio_inserts[static_cast<size_t>(index)];
    if (p.enabled == enabled) return true;
    project_->push_undo();
    p.enabled = enabled;
    project_->mark_dirty();
    return true;
}

int Engine::track_insert_count(TrackId track) const {
    if (!project_) return 0;
    TrackInfo* t = project_->find_track(track);
    return t ? static_cast<int>(t->audio_inserts.size()) : 0;
}

bool Engine::track_insert_params(TrackId track, int index, AudioFxParams* out) const {
    if (!project_ || index < 0) return false;
    TrackInfo* t = project_->find_track(track);
    if (!t || static_cast<size_t>(index) >= t->audio_inserts.size()) return false;
    if (out) *out = t->audio_inserts[static_cast<size_t>(index)];
    return true;
}

namespace {

/** Curve slot for a clip audio property: 0 gain, 1 pan. */
AnimCurve* audio_curve_for(ClipInfo& c, int prop) {
    switch (prop) {
        case 0: return &c.gain_curve;
        case 1: return &c.pan_curve;
        default: return nullptr;
    }
}

const AnimCurve* audio_curve_for(const ClipInfo& c, int prop) {
    switch (prop) {
        case 0: return &c.gain_curve;
        case 1: return &c.pan_curve;
        default: return nullptr;
    }
}

} // namespace

bool Engine::set_clip_audio(TrackId track, ClipId clip, float gain, float pan) {
    if (!project_) return false;
    TrackInfo* t = project_->find_track(track);
    if (!t) return false;
    for (auto& c : t->clips) {
        if (c.id != clip) continue;
        project_->push_undo();
        c.gain = std::clamp(gain, 0.f, 4.f);
        c.pan = std::clamp(pan, -1.f, 1.f);
        project_->mark_dirty();
        return true;
    }
    return false;
}

bool Engine::clip_audio(TrackId track, ClipId clip, float* gain, float* pan) const {
    if (!project_) return false;
    for (const auto& t : project_->tracks()) {
        if (t.id != track) continue;
        for (const auto& c : t.clips) {
            if (c.id != clip) continue;
            if (gain) *gain = c.gain;
            if (pan) *pan = c.pan;
            return true;
        }
    }
    return false;
}

bool Engine::add_audio_keyframe(TrackId track, ClipId clip, int prop, TimeSec time,
                                double value, int interp) {
    if (!project_ || prop < 0 || prop > 1 || interp < 0 || interp > 5) return false;
    TrackInfo* t = project_->find_track(track);
    if (!t) return false;
    for (auto& c : t->clips) {
        if (c.id != clip) continue;
        AnimCurve* curve = audio_curve_for(c, prop);
        if (!curve) return false;
        project_->push_undo();
        const double at = std::max<TimeSec>(0.0, time);
        // Same instant: retime/revalue instead of stacking a duplicate key.
        const auto& keys = curve->keys();
        bool replaced = false;
        for (size_t i = 0; i < keys.size(); ++i) {
            if (std::fabs(keys[i].time - at) > 1e-6) continue;
            curve->set_key(i, at, value);
            curve->set_interp(i, static_cast<CurveInterp>(interp));
            replaced = true;
            break;
        }
        if (!replaced) curve->add(at, value, static_cast<CurveInterp>(interp));
        project_->mark_dirty();
        return true;
    }
    return false;
}

bool Engine::clear_audio_keyframes(TrackId track, ClipId clip, int prop) {
    if (!project_ || prop < 0 || prop > 1) return false;
    TrackInfo* t = project_->find_track(track);
    if (!t) return false;
    for (auto& c : t->clips) {
        if (c.id != clip) continue;
        AnimCurve* curve = audio_curve_for(c, prop);
        if (!curve) return false;
        project_->push_undo();
        curve->clear();
        project_->mark_dirty();
        return true;
    }
    return false;
}

int Engine::audio_keyframe_count(TrackId track, ClipId clip, int prop) const {
    if (!project_ || prop < 0 || prop > 1) return 0;
    for (const auto& t : project_->tracks()) {
        if (t.id != track) continue;
        for (const auto& c : t.clips) {
            if (c.id != clip) continue;
            const AnimCurve* curve = audio_curve_for(c, prop);
            return curve ? static_cast<int>(curve->keys().size()) : 0;
        }
    }
    return 0;
}

bool Engine::audio_keyframe_at(TrackId track, ClipId clip, int prop, int key, double* time,
                               double* value, int* interp) const {
    if (!project_ || prop < 0 || prop > 1 || key < 0) return false;
    for (const auto& t : project_->tracks()) {
        if (t.id != track) continue;
        for (const auto& c : t.clips) {
            if (c.id != clip) continue;
            const AnimCurve* curve = audio_curve_for(c, prop);
            if (!curve) return false;
            const AnimKeyframe* k = curve->at(static_cast<size_t>(key));
            if (!k) return false;
            if (time) *time = k->time;
            if (value) *value = k->value;
            if (interp) *interp = static_cast<int>(k->interp);
            return true;
        }
    }
    return false;
}

bool Engine::set_audio_keyframe(TrackId track, ClipId clip, int prop, int key, double time,
                                double value, int interp) {
    if (!project_ || prop < 0 || prop > 1 || key < 0 || interp < 0 || interp > 5) return false;
    TrackInfo* t = project_->find_track(track);
    if (!t) return false;
    for (auto& c : t->clips) {
        if (c.id != clip) continue;
        AnimCurve* curve = audio_curve_for(c, prop);
        if (!curve) return false;
        project_->push_undo();
        if (!curve->set_key(static_cast<size_t>(key), time, value)) return false;
        curve->set_interp(static_cast<size_t>(key), static_cast<CurveInterp>(interp));
        project_->mark_dirty();
        return true;
    }
    return false;
}

bool Engine::sample_audio_param(TrackId track, ClipId clip, int prop, double t0, double t1,
                                float* out, int bins) const {
    if (!project_ || prop < 0 || prop > 1 || !out || bins < 2) return false;
    for (const auto& t : project_->tracks()) {
        if (t.id != track) continue;
        for (const auto& c : t.clips) {
            if (c.id != clip) continue;
            const AnimCurve* curve = audio_curve_for(c, prop);
            if (!curve) return false;
            const double span = (t1 > t0) ? (t1 - t0) : 1e-6;
            for (int i = 0; i < bins; ++i) {
                const double tt = t0 + span * static_cast<double>(i) /
                                           static_cast<double>(bins - 1);
                out[i] = static_cast<float>(curve->eval(tt));
            }
            return true;
        }
    }
    return false;
}

bool Engine::set_effect_param(EffectId effect, const std::string& name, float value) {
    (void)effect; (void)name; (void)value;
    // Effect IDs are intentionally lightweight in the first native API. Parameters are stored by name in EffectOp.
    return true;
}

void Engine::set_grade(float exposure, float contrast, float saturation, float temperature,
                       float lift, float gamma, float gain) {
    grade_[0] = exposure;
    grade_[1] = contrast > 0.f ? contrast : 1.f;
    grade_[2] = saturation < 0.f ? 0.f : saturation;
    grade_[3] = temperature;
    grade_[4] = lift;
    grade_[5] = gamma > 0.01f ? gamma : 1.f;
    grade_[6] = gain < 0.f ? 0.f : gain;
    grade_active_ = true;
    // Persisted with the document, but not undoable per slider frame.
    sync_master_state();
    project_->mark_dirty();
}

void Engine::set_grade_tonal(float shadows, float highlights, float vibrance) {
    grade_[7] = std::clamp(shadows, -1.f, 1.f);
    grade_[8] = std::clamp(highlights, -1.f, 1.f);
    grade_[9] = std::clamp(vibrance, -1.f, 1.f);
    // A tonal move alone still has to run the grade pass, so keep it active.
    grade_active_ = true;
    sync_master_state();
    project_->mark_dirty();
}

void Engine::clear_grade() {
    grade_active_ = false;
    sync_master_state();
    project_->mark_dirty();
}

bool Engine::get_grade(float* out7) const {
    if (!out7) return false;
    std::copy(std::begin(grade_), std::end(grade_), out7);
    return grade_active_;
}

bool Engine::scopes_histogram(int* out, int bins) const {
    if (!out || bins < 2 || bins > 256 || !last_frame_ || !last_frame_->valid()) return false;
    ScopeHistogram h;
    if (!Scopes::histogram(*last_frame_, h).ok()) return false;
    for (int i = 0; i < bins; ++i) {
        const int src = i * 256 / bins;
        out[0 * bins + i] = h.bins_r[src];
        out[1 * bins + i] = h.bins_g[src];
        out[2 * bins + i] = h.bins_b[src];
        out[3 * bins + i] = h.bins_y[src];
    }
    return true;
}

uint64_t Engine::add_transition(TrackId track, ClipId outgoing, ClipId incoming, int type,
                                TimeSec duration) {
    if (!project_ || !outgoing || !incoming || outgoing == incoming) return 0;
    TrackInfo* t = project_->find_track(track);
    if (!t) return 0;
    project_->push_undo();
    // One transition per clip pair: replace rather than stack.
    t->transitions.erase(
        std::remove_if(t->transitions.begin(), t->transitions.end(),
                       [&](const TimelineTransition& x) {
                           return x.outgoing == outgoing && x.incoming == incoming;
                       }),
        t->transitions.end());
    uint64_t next_id = 1;
    for (const auto& x : t->transitions) next_id = std::max(next_id, x.id + 1);
    TimelineTransition x;
    x.id = next_id;
    x.outgoing = outgoing;
    x.incoming = incoming;
    x.type = type;
    x.duration = duration > 0.05 ? duration : 0.05;
    t->transitions.push_back(x);
    project_->mark_dirty();
    invalidate_frames();
    return x.id;
}

bool Engine::remove_transition(TrackId track, uint64_t id) {
    if (!project_) return false;
    TrackInfo* t = project_->find_track(track);
    if (!t) return false;
    const auto before = t->transitions.size();
    project_->push_undo();
    t->transitions.erase(
        std::remove_if(t->transitions.begin(), t->transitions.end(),
                       [id](const TimelineTransition& x) { return x.id == id; }),
        t->transitions.end());
    if (t->transitions.size() == before) return false;
    project_->mark_dirty();
    invalidate_frames();
    return true;
}

bool Engine::add_keyframe(TrackId track, ClipId clip, int prop, TimeSec time, double value) {
    if (!project_ || prop < 0 || prop > 3) return false;
    TrackInfo* t = project_->find_track(track);
    if (!t) return false;
    for (auto& c : t->clips) {
        if (c.id != clip) continue;
        project_->push_undo();
        c.motion.ensure_defaults();
        AnimCurve* curve = prop == 0   ? &c.motion.opacity
                           : prop == 1 ? &c.motion.scale
                           : prop == 2 ? &c.motion.pos_x
                                       : &c.motion.pos_y;
        // The defaults seed a single static key; the first authored key replaces
        // it so the curve animates instead of holding a constant. Only the first:
        // the old `size() == 1` test also fired on a curve holding the one key you
        // had just made, so every second keyframe deleted the first.
        if (curve->seeded()) curve->clear();
        curve->add(std::max<TimeSec>(0.0, time), value);
        project_->mark_dirty();
        invalidate_frames();
        return true;
    }
    return false;
}

namespace {

AnimCurve* curve_for(ClipInfo& c, int prop) {
  switch (prop) {
    case 0: return &c.motion.opacity;
    case 1: return &c.motion.scale;
    case 2: return &c.motion.pos_x;
    case 3: return &c.motion.pos_y;
    default: return nullptr;
  }
}

} // namespace

int Engine::keyframe_count(TrackId track, ClipId clip, int prop) {
  if (!project_ || prop < 0 || prop > 3) return 0;
  TrackInfo* t = project_->find_track(track);
  if (!t) return 0;
  for (auto& c : t->clips) {
    if (c.id != clip) continue;
    AnimCurve* curve = curve_for(c, prop);
    return curve ? static_cast<int>(curve->keys().size()) : 0;
  }
  return 0;
}

bool Engine::keyframe_at(TrackId track, ClipId clip, int prop, int index, double* time,
                         double* value, int* interp) {
  if (!project_ || prop < 0 || prop > 3 || index < 0) return false;
  TrackInfo* t = project_->find_track(track);
  if (!t) return false;
  for (auto& c : t->clips) {
    if (c.id != clip) continue;
    AnimCurve* curve = curve_for(c, prop);
    if (!curve) return false;
    const AnimKeyframe* key = curve->at(static_cast<size_t>(index));
    if (!key) return false;
    if (time) *time = key->time;
    if (value) *value = key->value;
    if (interp) *interp = static_cast<int>(key->interp);
    return true;
  }
  return false;
}

bool Engine::set_keyframe(TrackId track, ClipId clip, int prop, int index, double time,
                          double value, int interp) {
  if (!project_ || prop < 0 || prop > 3 || index < 0 || interp < 0 || interp > 5) return false;
  TrackInfo* t = project_->find_track(track);
  if (!t) return false;
  for (auto& c : t->clips) {
    if (c.id != clip) continue;
    AnimCurve* curve = curve_for(c, prop);
    if (!curve) return false;
    project_->push_undo();
    if (!curve->set_key(static_cast<size_t>(index), time, value)) return false;
    curve->set_interp(static_cast<size_t>(index), static_cast<CurveInterp>(interp));
    project_->mark_dirty();
    invalidate_frames();
    return true;
  }
  return false;
}

bool Engine::set_keyframe_interp(TrackId track, ClipId clip, int prop, int index, int interp) {
  if (!project_ || prop < 0 || prop > 3 || index < 0 || interp < 0 || interp > 5) return false;
  TrackInfo* t = project_->find_track(track);
  if (!t) return false;
  for (auto& c : t->clips) {
    if (c.id != clip) continue;
    AnimCurve* curve = curve_for(c, prop);
    if (!curve) return false;
    project_->push_undo();
    if (!curve->set_interp(static_cast<size_t>(index), static_cast<CurveInterp>(interp)))
      return false;
    project_->mark_dirty();
    invalidate_frames();
    return true;
  }
  return false;
}

bool Engine::sample_keyframe(TrackId track, ClipId clip, int prop, double t0, double t1,
                             float* out, int bins) {
  if (!project_ || prop < 0 || prop > 3 || !out || bins < 2) return false;
  TrackInfo* t = project_->find_track(track);
  if (!t) return false;
  for (auto& c : t->clips) {
    if (c.id != clip) continue;
    AnimCurve* curve = curve_for(c, prop);
    if (!curve) return false;
    const double span = (t1 > t0) ? (t1 - t0) : 1e-6;
    for (int i = 0; i < bins; ++i) {
      const double tt = t0 + span * static_cast<double>(i) / static_cast<double>(bins - 1);
      out[i] = static_cast<float>(curve->eval(tt));
    }
    return true;
  }
  return false;
}

bool Engine::clear_keyframes(TrackId track, ClipId clip, int prop) {
    if (!project_ || prop < 0 || prop > 3) return false;
    TrackInfo* t = project_->find_track(track);
    if (!t) return false;
    for (auto& c : t->clips) {
        if (c.id != clip) continue;
        project_->push_undo();
        AnimCurve* curve = prop == 0   ? &c.motion.opacity
                           : prop == 1 ? &c.motion.scale
                           : prop == 2 ? &c.motion.pos_x
                                       : &c.motion.pos_y;
        curve->clear();
        project_->mark_dirty();
        invalidate_frames();
        return true;
    }
    return false;
}

uint64_t Engine::add_marker(TimeSec time, const std::string& name) {
    project_->push_undo();
    const uint64_t id = project_->add_marker(time, name);
    project_->mark_dirty();
    return id;
}

bool Engine::remove_marker(uint64_t id) {
    project_->push_undo();
    if (!project_->remove_marker(id)) return false;
    project_->mark_dirty();
    return true;
}

bool Engine::set_project_settings(int width, int height, float fps) {
    if (width <= 0 || height <= 0 || fps <= 0.f) return false;
    resize(width, height);
    project_->meta().fps = fps;
    impl_->fps = fps;
    project_->mark_dirty();
    invalidate_frames();
    return true;
}

bool Engine::set_clip_mask(TrackId track, ClipId clip, int shape, float x, float y, float w,
                           float h, float feather, bool inverted) {
    TrackInfo* t = project_->find_track(track);
    if (!t) return false;
    for (auto& c : t->clips) {
        if (c.id != clip) continue;
        project_->push_undo();
        c.mask_enabled = true;
        // Single-mask entry point: keeps the first mask's slot, leaves any
        // extra masks the clip already carries alone.
        if (c.masks.empty()) c.masks.emplace_back();
        Mask& m = c.masks.front();
        m.shape = mask_shape_from_index(shape);
        m.x = std::clamp(x, 0.f, 1.f);
        m.y = std::clamp(y, 0.f, 1.f);
        m.w = std::clamp(w, 0.01f, 1.f);
        m.h = std::clamp(h, 0.01f, 1.f);
        m.feather = std::max(0.f, feather);
        m.inverted = inverted;
        project_->mark_dirty();
        invalidate_frames();
        return true;
    }
    return false;
}

bool Engine::clear_clip_mask(TrackId track, ClipId clip) {
    TrackInfo* t = project_->find_track(track);
    if (!t) return false;
    for (auto& c : t->clips) {
        if (c.id != clip) continue;
        project_->push_undo();
        c.masks.clear();
        c.mask_enabled = false;
        project_->mark_dirty();
        invalidate_frames();
        return true;
    }
    return false;
}

bool Engine::clip_mask(TrackId track, ClipId clip, float* out6, bool* inverted) const {
    for (const auto& t : project_->tracks()) {
        if (t.id != track) continue;
        for (const auto& c : t.clips) {
            if (c.id != clip) continue;
            if (!c.mask_enabled || c.masks.empty()) return false;
            const Mask& m = c.masks.front();
            if (out6) {
                out6[0] = static_cast<float>(static_cast<int>(m.shape));
                out6[1] = m.x;
                out6[2] = m.y;
                out6[3] = m.w;
                out6[4] = m.h;
                out6[5] = m.feather;
            }
            if (inverted) *inverted = m.inverted;
            return true;
        }
    }
    return false;
}

static Mask make_mask(const ClipInfo& c, int shape, float x, float y, float w, float h,
                      float feather, bool inverted, int op) {
    Mask m;
    // Ids come from the largest id already on the clip, not the list length:
    // removing a mask and adding another made the new one share an id with the
    // mask that followed the removed one.
    uint64_t next_id = 1;
    for (const auto& existing : c.masks) next_id = std::max(next_id, existing.id + 1);
    m.id = next_id;
    m.name = "Mask " + std::to_string(c.masks.size() + 1);
    m.shape = mask_shape_from_index(shape);
    m.mode = MaskMode::Alpha;
    m.op = op == 1 ? MaskOp::Subtract : MaskOp::Add;
    m.x = std::clamp(x, 0.f, 1.f);
    m.y = std::clamp(y, 0.f, 1.f);
    m.w = std::clamp(w, 0.01f, 1.f);
    m.h = std::clamp(h, 0.01f, 1.f);
    m.feather = std::max(0.f, feather);
    m.inverted = inverted;
    m.enabled = true;
    return m;
}

int Engine::add_clip_mask(TrackId track, ClipId clip, int shape, float x, float y, float w,
                          float h, float feather, bool inverted, int op) {
    TrackInfo* t = project_->find_track(track);
    if (!t) return -1;
    for (auto& c : t->clips) {
        if (c.id != clip) continue;
        if (c.masks.size() >= kMaxClipMasks) return -1;
        project_->push_undo();
        c.masks.push_back(make_mask(c, shape, x, y, w, h, feather, inverted, op));
        c.mask_enabled = true;
        project_->mark_dirty();
        invalidate_frames();
        return static_cast<int>(c.masks.size());
    }
    return -1;
}

bool Engine::update_clip_mask_at(TrackId track, ClipId clip, int index, int shape, float x,
                                 float y, float w, float h, float feather, bool inverted,
                                 int op) {
    TrackInfo* t = project_->find_track(track);
    if (!t || index < 0) return false;
    for (auto& c : t->clips) {
        if (c.id != clip) continue;
        if (index >= static_cast<int>(c.masks.size())) return false;
        project_->push_undo();
        Mask& m = c.masks[static_cast<size_t>(index)];
        m.shape = mask_shape_from_index(shape);
        m.op = op == 1 ? MaskOp::Subtract : MaskOp::Add;
        m.x = std::clamp(x, 0.f, 1.f);
        m.y = std::clamp(y, 0.f, 1.f);
        m.w = std::clamp(w, 0.01f, 1.f);
        m.h = std::clamp(h, 0.01f, 1.f);
        m.feather = std::max(0.f, feather);
        m.inverted = inverted;
        c.mask_enabled = true;
        project_->mark_dirty();
        invalidate_frames();
        return true;
    }
    return false;
}

bool Engine::remove_clip_mask_at(TrackId track, ClipId clip, int index) {
    TrackInfo* t = project_->find_track(track);
    if (!t || index < 0) return false;
    for (auto& c : t->clips) {
        if (c.id != clip) continue;
        if (index >= static_cast<int>(c.masks.size())) return false;
        project_->push_undo();
        c.masks.erase(c.masks.begin() + index);
        c.mask_enabled = !c.masks.empty();
        project_->mark_dirty();
        invalidate_frames();
        return true;
    }
    return false;
}

int Engine::clip_mask_count(TrackId track, ClipId clip) const {
    for (const auto& t : project_->tracks()) {
        if (t.id != track) continue;
        for (const auto& c : t.clips)
            if (c.id == clip) return static_cast<int>(c.masks.size());
    }
    return 0;
}

bool Engine::clip_mask_at(TrackId track, ClipId clip, int index, float* out6, bool* inverted,
                          int* op) const {
    if (index < 0) return false;
    for (const auto& t : project_->tracks()) {
        if (t.id != track) continue;
        for (const auto& c : t.clips) {
            if (c.id != clip) continue;
            if (index >= static_cast<int>(c.masks.size())) return false;
            const Mask& m = c.masks[static_cast<size_t>(index)];
            if (out6) {
                out6[0] = static_cast<float>(static_cast<int>(m.shape));
                out6[1] = m.x;
                out6[2] = m.y;
                out6[3] = m.w;
                out6[4] = m.h;
                out6[5] = m.feather;
            }
            if (inverted) *inverted = m.inverted;
            if (op) *op = m.op == MaskOp::Subtract ? 1 : 0;
            return true;
        }
    }
    return false;
}

bool Engine::set_clip_look(TrackId track, ClipId clip, const std::string& path,
                           float strength) {
    if (path.empty()) return false;
    // Parse here so a missing or malformed cube is reported when it is picked,
    // instead of silently grading nothing on every frame afterwards.
    Lut3D probe;
    if (!load_cube_lut(path, probe)) return false;
    TrackInfo* t = project_->find_track(track);
    if (!t) return false;
    for (auto& c : t->clips) {
        if (c.id != clip) continue;
        project_->push_undo();
        c.look_path = path;
        c.look_strength = std::clamp(strength <= 0.f ? 1.f : strength, 0.f, 1.f);
        project_->mark_dirty();
        invalidate_frames();
        return true;
    }
    return false;
}

bool Engine::set_clip_look_strength(TrackId track, ClipId clip, float strength) {
    TrackInfo* t = project_->find_track(track);
    if (!t) return false;
    for (auto& c : t->clips) {
        if (c.id != clip) continue;
        if (c.look_path.empty()) return false;
        project_->push_undo();
        c.look_strength = std::clamp(strength, 0.f, 1.f);
        project_->mark_dirty();
        invalidate_frames();
        return true;
    }
    return false;
}

bool Engine::clear_clip_look(TrackId track, ClipId clip) {
    TrackInfo* t = project_->find_track(track);
    if (!t) return false;
    for (auto& c : t->clips) {
        if (c.id != clip) continue;
        project_->push_undo();
        c.look_path.clear();
        c.look_strength = 0.f;
        project_->mark_dirty();
        invalidate_frames();
        return true;
    }
    return false;
}

bool Engine::clip_look(TrackId track, ClipId clip, std::string* out_path,
                       float* out_strength) const {
    for (const auto& t : project_->tracks()) {
        if (t.id != track) continue;
        for (const auto& c : t.clips) {
            if (c.id != clip) continue;
            if (c.look_path.empty() || c.look_strength <= 0.f) return false;
            if (out_path) *out_path = c.look_path;
            if (out_strength) *out_strength = c.look_strength;
            return true;
        }
    }
    return false;
}

bool Engine::set_clip_grade(TrackId track, ClipId clip, float exposure, float contrast,
                            float saturation, float temperature, float lift, float gamma,
                            float gain) {
    TrackInfo* t = project_->find_track(track);
    if (!t) return false;
    for (auto& c : t->clips) {
        if (c.id != clip) continue;
        project_->push_undo();
        c.grade_enabled = true;
        c.grade[0] = exposure;
        c.grade[1] = contrast > 0.f ? contrast : 1.f;
        c.grade[2] = saturation < 0.f ? 0.f : saturation;
        c.grade[3] = temperature;
        c.grade[4] = lift;
        c.grade[5] = gamma > 0.01f ? gamma : 1.f;
        c.grade[6] = gain < 0.f ? 0.f : gain;
        project_->mark_dirty();
        invalidate_frames();
        return true;
    }
    return false;
}

bool Engine::set_clip_grade_tonal(TrackId track, ClipId clip, float shadows, float highlights,
                                  float vibrance) {
    TrackInfo* t = project_->find_track(track);
    if (!t) return false;
    for (auto& c : t->clips) {
        if (c.id != clip) continue;
        project_->push_undo();
        c.grade_enabled = true;
        c.grade[7] = std::clamp(shadows, -1.f, 1.f);
        c.grade[8] = std::clamp(highlights, -1.f, 1.f);
        c.grade[9] = std::clamp(vibrance, -1.f, 1.f);
        project_->mark_dirty();
        invalidate_frames();
        return true;
    }
    return false;
}

bool Engine::clear_clip_grade(TrackId track, ClipId clip) {
    TrackInfo* t = project_->find_track(track);
    if (!t) return false;
    for (auto& c : t->clips) {
        if (c.id != clip) continue;
        project_->push_undo();
        c.grade_enabled = false;
        project_->mark_dirty();
        invalidate_frames();
        return true;
    }
    return false;
}

bool Engine::clip_grade(TrackId track, ClipId clip, float* out10) const {
    if (!out10) return false;
    const TrackInfo* t = nullptr;
    for (const auto& x : project_->tracks()) if (x.id == track) t = &x;
    if (!t) return false;
    for (const auto& c : t->clips) {
        if (c.id != clip) continue;
        if (!c.grade_enabled) return false;
        std::memcpy(out10, c.grade, sizeof(float) * 10);
        return true;
    }
    return false;
}

bool Engine::clip_peaks(TrackId track, ClipId clip, int bins, float* out) {
    if (!out || bins <= 0) return false;
    TrackInfo* t = project_->find_track(track);
    if (!t) return false;
    const ClipInfo* c = nullptr;
    for (const auto& x : t->clips) if (x.id == clip) c = &x;
    if (!c || c->is_text) return false;
    std::vector<float> peaks;
    if (!audio_->clip_peaks(*c, bins, peaks).ok()) return false;
    if (peaks.size() != static_cast<size_t>(bins)) return false;
    std::memcpy(out, peaks.data(), sizeof(float) * static_cast<size_t>(bins));
    return true;
}

ClipId Engine::add_text_clip(TrackId track, TimeSec start, TimeSec duration,
                             const std::string& text, const TextStyle& style) {
    if (text.empty()) return 0;
    TrackInfo* t = project_->find_track(track);
    if (!t) return 0;
    project_->push_undo();
    ClipInfo c;
    c.name = text.size() > 32 ? text.substr(0, 32) : text;
    c.start = start;
    c.duration = duration > 0.05 ? duration : 3.0;
    c.source_out = c.duration;
    c.is_text = true;
    c.text = text;
    c.text_style = style;
    c.motion.ensure_defaults();
    const ClipId id = project_->add_clip(track, c);
    invalidate_frames();
    return id;
}

bool Engine::set_clip_text(TrackId track, ClipId clip, const std::string& text,
                           const TextStyle& style) {
    TrackInfo* t = project_->find_track(track);
    if (!t) return false;
    for (auto& c : t->clips) {
        if (c.id != clip) continue;
        project_->push_undo();
        // The reveal is edited through its own call, and a style edit from the
        // title sheet carries no reveal, so preserve the current one unless the
        // incoming style asks for a mode explicitly.
        const int previous_reveal = c.text_style.reveal;
        const float previous_reveal_sec = c.text_style.reveal_sec;
        c.is_text = true;
        c.text = text;
        c.text_style = style;
        if (style.reveal == 0 && previous_reveal != 0) {
            c.text_style.reveal = previous_reveal;
            c.text_style.reveal_sec = previous_reveal_sec;
        }
        if (c.name.empty() || c.name == "Text") c.name = text.size() > 32 ? text.substr(0, 32) : text;
        project_->mark_dirty();
        invalidate_frames();
        return true;
    }
    return false;
}

bool Engine::set_clip_text_reveal(TrackId track, ClipId clip, int mode, float seconds) {
    if (!project_ || mode < 0 || mode > 3) return false;
    TrackInfo* t = project_->find_track(track);
    if (!t) return false;
    for (auto& c : t->clips) {
        if (c.id != clip) continue;
        if (!c.is_text) return false;
        project_->push_undo();
        c.text_style.reveal = mode;
        c.text_style.reveal_sec = seconds > 0.f ? seconds : 0.f;
        project_->mark_dirty();
        invalidate_frames();
        return true;
    }
    return false;
}

bool Engine::clip_text_reveal(TrackId track, ClipId clip, int* mode, float* seconds) const {
    if (!project_) return false;
    for (const auto& t : project_->tracks()) {
        if (t.id != track) continue;
        for (const auto& c : t.clips) {
            if (c.id != clip) continue;
            if (!c.is_text) return false;
            if (mode) *mode = c.text_style.reveal;
            if (seconds) *seconds = c.text_style.reveal_sec;
            return true;
        }
    }
    return false;
}

bool Engine::render_title_preview(const std::string& text, const TextStyle& style,
                                  ImageBuffer& out) {
    const int w = project_->meta().width;
    const int h = project_->meta().height;
    return render_text(out, w, h, style, text).ok();
}

bool Engine::undo() {
    if (!project_) return false;
    const bool ok = project_->undo();
    if (ok) {
        apply_master_state();
        invalidate_frames();
    }
    return ok;
}

bool Engine::redo() {
    if (!project_) return false;
    const bool ok = project_->redo();
    if (ok) {
        apply_master_state();
        invalidate_frames();
    }
    return ok;
}

ClipId Engine::add_clip(TrackId track, MediaId media, TimeSec start, TimeSec duration) {
    if (!project_) return 0;
    if (!project_->find_track(track)) return 0;
    project_->push_undo();
    ClipInfo c;
    c.media_id = media;
    c.start = start;
    c.duration = duration > 0.01 ? duration : 1.0;
    c.source_in = 0;
    c.source_out = c.duration;
    c.name = "Clip";
    c.motion.ensure_defaults();
    const ClipId id = project_->add_clip(track, c);
    if (!id) return 0;
    project_->mark_dirty();
    invalidate_frames();
    return id;
}

ClipId Engine::add_adjustment_clip(TrackId track, TimeSec start, TimeSec duration) {
    if (!project_) return 0;
    if (!project_->find_track(track)) return 0;
    project_->push_undo();
    ClipInfo c;
    c.name = "Adjustment";
    c.start = std::max(0.0, start);
    c.duration = duration > 0.05 ? duration : 3.0;
    c.source_out = c.duration;
    c.is_adjustment = true;
    c.motion.ensure_defaults();
    const ClipId id = project_->add_clip(track, c);
    if (!id) return 0;
    project_->mark_dirty();
    invalidate_frames();
    return id;
}

bool Engine::set_clip_speed(TrackId track, ClipId clip, float speed) {
    TrackInfo* t = project_->find_track(track);
    if (!t) return false;
    for (auto& c : t->clips) {
        if (c.id != clip) continue;
        // A title rasterises to an arbitrary length and an adjustment layer has
        // no source range at all, so neither has anything to stretch.
        if (c.is_adjustment || c.is_text) return false;
        const float s = std::clamp(speed, 0.1f, 8.f);
        if (std::fabs(s - c.speed) < 1e-4f) return true;
        project_->push_undo();
        // Keep the same slice of the source: only how fast it plays changes, so
        // the clip's length on the timeline has to move with it.
        const double source_len = (c.source_out > c.source_in)
                                      ? (c.source_out - c.source_in)
                                      : c.duration * c.speed;
        c.speed = s;
        c.duration = std::max(0.05, source_len / s);
        c.source_out = c.source_in + c.duration * c.speed;
        project_->mark_dirty();
        invalidate_frames();
        return true;
    }
    return false;
}

bool Engine::clip_speed(TrackId track, ClipId clip, float* out) const {
    for (const auto& t : project_->tracks()) {
        if (t.id != track) continue;
        for (const auto& c : t.clips) {
            if (c.id != clip) continue;
            if (out) *out = c.speed;
            return true;
        }
    }
    return false;
}


namespace {

/** Bilinear RGBA8 resize used to normalise comp sources to the cook size. */
void scale_rgba(const ImageBuffer& src, int w, int h, ImageBuffer& out) {
    if (!src.valid() || w <= 0 || h <= 0) return;
    if (src.width() == w && src.height() == h) { out.copy_from(src); return; }
    if (!out.allocate(w, h, PixelFormat::RGBA8)) return;
    const int sw = src.width(), sh = src.height();
    const float sx = static_cast<float>(sw) / static_cast<float>(w);
    const float sy = static_cast<float>(sh) / static_cast<float>(h);
    for (int y = 0; y < h; ++y) {
        const float fy = std::clamp((static_cast<float>(y) + 0.5f) * sy - 0.5f, 0.f, static_cast<float>(sh - 1));
        const int y0 = static_cast<int>(fy);
        const int y1 = std::min(y0 + 1, sh - 1);
        const float ty = fy - static_cast<float>(y0);
        const uint8_t* r0 = src.data() + static_cast<size_t>(y0) * src.stride();
        const uint8_t* r1 = src.data() + static_cast<size_t>(y1) * src.stride();
        uint8_t* dst = out.data() + static_cast<size_t>(y) * out.stride();
        for (int x = 0; x < w; ++x) {
            const float fx = std::clamp((static_cast<float>(x) + 0.5f) * sx - 0.5f, 0.f, static_cast<float>(sw - 1));
            const int x0 = static_cast<int>(fx);
            const int x1 = std::min(x0 + 1, sw - 1);
            const float tx = fx - static_cast<float>(x0);
            for (int c = 0; c < 4; ++c) {
                const float a = r0[x0 * 4 + c] * (1.f - tx) + r0[x1 * 4 + c] * tx;
                const float b = r1[x0 * 4 + c] * (1.f - tx) + r1[x1 * 4 + c] * tx;
                dst[x * 4 + c] = static_cast<uint8_t>(std::clamp(a * (1.f - ty) + b * ty, 0.f, 255.f) + 0.5f);
            }
        }
    }
}

} // namespace

Error Engine::read_comp_source(const GraphNode& node, double time_sec, int w, int h,
                               ImageBuffer& out) {
    if (w <= 0 || h <= 0) return Error::make(ErrorCode::InvalidArgument, "invalid comp resolution");

    // No media path: the node reads the edited sequence at this time, so a comp
    // can be built on top of the timeline instead of only on raw files.
    if (node.media_path.empty()) {
        const int64_t frame = impl_->frame_index.load();
        const uint64_t revision = impl_->revision.load();
        if (impl_->comp_sequence.valid() && impl_->comp_sequence_frame == frame &&
            impl_->comp_sequence_rev == revision) {
            scale_rgba(impl_->comp_sequence, w, h, out);
            return out.valid() ? Error::success() : Error::make(ErrorCode::OutOfMemory, "comp read output");
        }
        ImageBuffer sequence;
        Error e = player_->render_frame(time_sec, sequence);
        if (!e.ok() || !sequence.valid()) return e.ok() ? Error::make(ErrorCode::MediaDecodeFailed, "empty sequence frame") : e;
        scale_rgba(sequence, w, h, out);
        return Error::success();
    }

    IMediaDecoder* dec = nullptr;
    auto it = impl_->comp_decoders.find(node.media_path);
    if (it != impl_->comp_decoders.end()) {
        dec = it->second.get();
    } else {
        const std::string path = node.media_path;
        auto created = create_media_decoder(path);
        if (!created) return Error::make(ErrorCode::MediaOpenFailed, "comp read cannot open " + path);
        dec = created.get();
        impl_->comp_decoders[path] = std::move(created);
    }
    if (!dec) return Error::make(ErrorCode::NotInitialized, "comp read has no decoder");

    double offset = 0.0;
    for (const auto& p : node.params) if (p.name == "offset") offset = p.value;
    const double local = std::max(0.0, time_sec + offset);
    const double duration = dec->info().duration_ms / 1000.0;
    const double clamped = duration > 0.0 ? std::min(local, duration) : local;

    dec->seek_ms(static_cast<int64_t>(clamped * 1000.0));
    ImageBuffer frame;
    int64_t pts = 0;
    Error e = dec->read_video(frame, pts);
    if (!e.ok() || !frame.valid()) return Error::make(ErrorCode::MediaDecodeFailed, "comp read decoded no frame");
    scale_rgba(frame, w, h, out);
    return out.valid() ? Error::success() : Error::make(ErrorCode::OutOfMemory, "comp read output");
}

bool Engine::comp_rendered() const {
    return comp_active_ && !comp_.nodes().empty() && comp_.sink_id() != 0;
}

void Engine::set_comp_active(bool on) {
    if (comp_active_ == on) return;
    comp_active_ = on;
    invalidate_frames();
}

void Engine::invalidate_frames() {
    if (frame_cache_) frame_cache_->clear();
    impl_->revision++;
}

bool Engine::remove_clip(TrackId track, ClipId clip) {
    if (!project_) return false;
    project_->push_undo();
    const bool ok = project_->remove_clip(track, clip);
    if (ok) invalidate_frames();
    return ok;
}

bool Engine::move_clip(TrackId track, ClipId clip, TimeSec start) {
    if (!project_) return false;
    project_->push_undo();
    const bool ok = project_->move_clip(track, clip, start < 0.0 ? 0.0 : start);
    if (ok) invalidate_frames();
    return ok;
}

bool Engine::trim_clip(TrackId track, ClipId clip, TimeSec start, TimeSec duration) {
    if (!project_) return false;
    project_->push_undo();
    const bool ok = project_->trim_clip(track, clip, start < 0.0 ? 0.0 : start,
                                        duration < 0.05 ? 0.05 : duration);
    if (ok) invalidate_frames();
    return ok;
}

bool Engine::ripple_delete(TrackId track, ClipId clip) {
    if (!project_) return false;
    project_->push_undo();
    const Error e = ripple_delete_clip(*project_, track, clip);
    if (e.ok()) invalidate_frames();
    return e.ok();
}

TrackId Engine::add_track(const std::string& name, int type) {
    if (!project_) return 0;
    project_->push_undo();
    const TrackId id = project_->add_track(name.empty() ? "Track" : name, type);
    if (id) invalidate_frames();
    return id;
}

bool Engine::remove_track(TrackId track) {
    if (!project_) return false;
    project_->push_undo();
    const bool ok = project_->remove_track(track);
    if (ok) invalidate_frames();
    return ok;
}

bool Engine::set_track_flags(TrackId track, bool muted, bool locked, bool visible, bool solo) {
    if (!project_) return false;
    TrackInfo* t = project_->find_track(track);
    if (!t) return false;
    project_->push_undo();
    t->muted = muted;
    t->locked = locked;
    t->visible = visible;
    t->solo = solo;
    project_->mark_dirty();
    invalidate_frames();
    return true;
}

bool Engine::set_track_gain(TrackId track, float gain) {
    if (!project_) return false;
    TrackInfo* t = project_->find_track(track);
    if (!t) return false;
    const float safe = std::clamp(gain, 0.f, 4.f);
    if (std::abs(safe - t->gain) < 1e-6f) return true;
    project_->push_undo();
    t->gain = safe;
    project_->mark_dirty();
    // Only the audio mix changes; the picture cache stays valid.
    return true;
}

int Engine::add_effect(TrackId track, ClipId clip, const std::string& base_id) {
    if (!project_ || base_id.empty()) return -1;
    TrackInfo* t = project_->find_track(track);
    if (!t) return -1;
    for (auto& c : t->clips) {
        if (c.id != clip) continue;
        project_->push_undo();
        c.motion.ensure_defaults();
        c.motion.effects.push_back(EffectOp{base_id, {}, true});
        project_->mark_dirty();
        invalidate_frames();
        return static_cast<int>(c.motion.effects.size()) - 1;
    }
    return -1;
}

bool Engine::remove_effect(TrackId track, ClipId clip, int index) {
    if (!project_) return false;
    TrackInfo* t = project_->find_track(track);
    if (!t) return false;
    for (auto& c : t->clips) {
        if (c.id != clip) continue;
        if (index < 0 || index >= static_cast<int>(c.motion.effects.size())) return false;
        project_->push_undo();
        c.motion.effects.erase(c.motion.effects.begin() + index);
        project_->mark_dirty();
        invalidate_frames();
        return true;
    }
    return false;
}

bool Engine::move_effect(TrackId track, ClipId clip, int from, int to) {
    if (!project_) return false;
    TrackInfo* t = project_->find_track(track);
    if (!t) return false;
    for (auto& c : t->clips) {
        if (c.id != clip) continue;
        const int count = static_cast<int>(c.motion.effects.size());
        if (from < 0 || from >= count) return false;
        if (to < 0 || to >= count) return false;
        if (from == to) return true;
        project_->push_undo();
        EffectOp moved = std::move(c.motion.effects[from]);
        c.motion.effects.erase(c.motion.effects.begin() + from);
        c.motion.effects.insert(c.motion.effects.begin() + to, std::move(moved));
        project_->mark_dirty();
        invalidate_frames();
        return true;
    }
    return false;
}

bool Engine::set_effect_param(TrackId track, ClipId clip, int index, const std::string& name, float value) {
    if (!project_ || name.empty()) return false;
    TrackInfo* t = project_->find_track(track);
    if (!t) return false;
    for (auto& c : t->clips) {
        if (c.id != clip) continue;
        if (index < 0 || index >= static_cast<int>(c.motion.effects.size())) return false;
        c.motion.effects[index].params[name] = static_cast<double>(value);
        project_->mark_dirty();
        invalidate_frames();
        return true;
    }
    return false;
}

bool Engine::set_effect_enabled(TrackId track, ClipId clip, int index, bool enabled) {
    if (!project_) return false;
    TrackInfo* t = project_->find_track(track);
    if (!t) return false;
    for (auto& c : t->clips) {
        if (c.id != clip) continue;
        if (index < 0 || index >= static_cast<int>(c.motion.effects.size())) return false;
        c.motion.effects[index].enabled = enabled;
        project_->mark_dirty();
        invalidate_frames();
        return true;
    }
    return false;
}

namespace {

EffectOp* effect_op_at(TrackInfo* track_info, ClipId clip, int index) {
  if (!track_info || index < 0) return nullptr;
  for (auto& c : track_info->clips) {
    if (c.id != clip) continue;
    if (index >= static_cast<int>(c.motion.effects.size())) return nullptr;
    return &c.motion.effects[index];
  }
  return nullptr;
}

const EffectOp* effect_op_at(const TrackInfo* track_info, ClipId clip, int index) {
  if (!track_info || index < 0) return nullptr;
  for (const auto& c : track_info->clips) {
    if (c.id != clip) continue;
    if (index >= static_cast<int>(c.motion.effects.size())) return nullptr;
    return &c.motion.effects[index];
  }
  return nullptr;
}

} // namespace

bool Engine::add_effect_keyframe(TrackId track, ClipId clip, int index,
                                 const std::string& param, TimeSec time, double value,
                                 int interp) {
  if (!project_ || param.empty()) return false;
  EffectOp* fx = effect_op_at(project_->find_track(track), clip, index);
  if (!fx) return false;
  project_->push_undo();
  AnimCurve& curve = fx->curves[param];
  const TimeSec at = std::max<TimeSec>(0.0, time);
  const auto& keys = curve.keys();
  int existing = -1;
  for (size_t i = 0; i < keys.size(); ++i) {
    if (std::fabs(keys[i].time - at) < 1e-6) { existing = static_cast<int>(i); break; }
  }
  const auto mode = static_cast<CurveInterp>(std::clamp(interp, 0, 5));
  if (existing >= 0) {
    // Same instant twice: the newer value wins, the curve keeps one key per
    // time. The UI adds a key on every tap, so duplicates were easy to make.
    curve.set_key(static_cast<size_t>(existing), at, value);
    curve.set_interp(static_cast<size_t>(existing), mode);
  } else {
    curve.add(at, value, mode);
  }
  project_->mark_dirty();
  invalidate_frames();
  return true;
}

bool Engine::clear_effect_keyframes(TrackId track, ClipId clip, int index,
                                    const std::string& param) {
  if (!project_ || param.empty()) return false;
  EffectOp* fx = effect_op_at(project_->find_track(track), clip, index);
  if (!fx) return false;
  auto it = fx->curves.find(param);
  if (it == fx->curves.end() || it->second.empty()) return false;
  project_->push_undo();
  fx->curves.erase(it);
  project_->mark_dirty();
  invalidate_frames();
  return true;
}

int Engine::effect_keyframe_count(TrackId track, ClipId clip, int index,
                                  const std::string& param) const {
  if (!project_ || param.empty()) return 0;
  const EffectOp* fx = effect_op_at(project_->find_track(track), clip, index);
  if (!fx) return 0;
  const auto it = fx->curves.find(param);
  return it == fx->curves.end() ? 0 : static_cast<int>(it->second.keys().size());
}

bool Engine::effect_keyframe_at(TrackId track, ClipId clip, int index,
                                const std::string& param, int key, double* time,
                                double* value, int* interp) const {
  if (!project_ || param.empty() || key < 0) return false;
  const EffectOp* fx = effect_op_at(project_->find_track(track), clip, index);
  if (!fx) return false;
  const auto it = fx->curves.find(param);
  if (it == fx->curves.end()) return false;
  const AnimKeyframe* k = it->second.at(static_cast<size_t>(key));
  if (!k) return false;
  if (time) *time = k->time;
  if (value) *value = k->value;
  if (interp) *interp = static_cast<int>(k->interp);
  return true;
}

bool Engine::set_effect_keyframe(TrackId track, ClipId clip, int index,
                                 const std::string& param, int key, double time,
                                 double value, int interp) {
  if (!project_ || param.empty() || key < 0) return false;
  EffectOp* fx = effect_op_at(project_->find_track(track), clip, index);
  if (!fx) return false;
  auto it = fx->curves.find(param);
  if (it == fx->curves.end() || static_cast<size_t>(key) >= it->second.keys().size())
    return false;
  project_->push_undo();
  it->second.set_key(static_cast<size_t>(key), time, value);
  it->second.set_interp(static_cast<size_t>(key), static_cast<CurveInterp>(std::clamp(interp, 0, 5)));
  project_->mark_dirty();
  invalidate_frames();
  return true;
}

bool Engine::sample_effect_param(TrackId track, ClipId clip, int index,
                                 const std::string& param, double t0, double t1,
                                 float* out, int bins) const {
  if (!project_ || param.empty() || !out || bins < 2) return false;
  const EffectOp* fx = effect_op_at(project_->find_track(track), clip, index);
  if (!fx) return false;
  const double step = (t1 - t0) / static_cast<double>(bins - 1);
  for (int i = 0; i < bins; ++i) {
    out[i] = static_cast<float>(fx->param_at(param, t0 + step * i, 0.0));
  }
  return true;
}

int Engine::copy_effects(TrackId track, ClipId clip) {
  if (!project_) return -1;
  TrackInfo* t = project_->find_track(track);
  if (!t) return -1;
  for (const auto& c : t->clips) {
    if (c.id != clip) continue;
    effect_clipboard_ = c.motion.effects;
    return static_cast<int>(effect_clipboard_.size());
  }
  return -1;
}

int Engine::paste_effects(TrackId track, ClipId clip, bool append) {
  if (!project_ || effect_clipboard_.empty()) return -1;
  TrackInfo* t = project_->find_track(track);
  if (!t) return -1;
  for (auto& c : t->clips) {
    if (c.id != clip) continue;
    project_->push_undo();
    c.motion.ensure_defaults();
    if (!append) c.motion.effects.clear();
    c.motion.effects.insert(c.motion.effects.end(), effect_clipboard_.begin(),
                            effect_clipboard_.end());
    project_->mark_dirty();
    invalidate_frames();
    return static_cast<int>(c.motion.effects.size());
  }
  return -1;
}

int Engine::effect_clipboard_count() const {
  return static_cast<int>(effect_clipboard_.size());
}

void Engine::set_frame_callback(FrameCallback cb) { impl_->frame_cb = std::move(cb); }

} // namespace ms
