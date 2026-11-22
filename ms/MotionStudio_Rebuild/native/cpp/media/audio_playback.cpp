#include "ms_audio_playback.hpp"
#include "ms_android_decode.hpp"
#include "ms_project.hpp"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <sys/wait.h>

namespace ms {
namespace fs = std::filesystem;

namespace {

// Field-by-field so a document change is detected without touching padding, and
// so an untouched chain keeps its reverb/delay tails running across blocks.
bool same_fx(const AudioFxParams& a, const AudioFxParams& b) {
  return a.kind == b.kind && a.enabled == b.enabled && a.wet == b.wet &&
         a.gain_db == b.gain_db && a.low_gain_db == b.low_gain_db &&
         a.low_freq_hz == b.low_freq_hz && a.mid_gain_db == b.mid_gain_db &&
         a.mid_freq_hz == b.mid_freq_hz && a.mid_q == b.mid_q &&
         a.high_gain_db == b.high_gain_db && a.high_freq_hz == b.high_freq_hz &&
         a.threshold_db == b.threshold_db && a.ratio == b.ratio &&
         a.attack_ms == b.attack_ms && a.release_ms == b.release_ms &&
         a.makeup_db == b.makeup_db && a.delay_ms == b.delay_ms &&
         a.feedback == b.feedback && a.room_size == b.room_size &&
         a.damping == b.damping && a.ceiling_db == b.ceiling_db;
}

} // namespace

AudioDecoderFFmpeg::AudioDecoderFFmpeg(FFmpegPaths paths) : paths_(std::move(paths)) {}

Error AudioDecoderFFmpeg::open(const std::string& path, int target_rate, int target_ch) {
  close();
  path_ = path;
  info_ = {};
  info_.sample_rate = target_rate > 0 ? target_rate : 48000;
  info_.channels = target_ch > 0 ? target_ch : 2;

  auto probe = ffmpeg_probe(path, paths_);
  double dur = probe.duration_ms / 1000.0;
  if (dur <= 0) dur = 0;
  for (const auto& s : probe.streams) {
    if (s.type == "audio") {
      if (s.sample_rate > 0) {
        // keep target rate for mixability; note native rate in path
      }
      if (s.duration_ms > 0) dur = s.duration_ms / 1000.0;
      break;
    }
  }
  // also duration from format
  if (probe.duration_ms > 0) dur = std::max(dur, probe.duration_ms / 1000.0);
  info_.duration = dur > 0 ? dur : 0;
  open_ = true;
  return Error::success();
}

void AudioDecoderFFmpeg::close() {
  pcm_.clear();
  open_ = false;
  path_.clear();
}

Error AudioDecoderFFmpeg::decode_window(double start, double duration, AudioBuffer& dst) {
  if (!open_ || path_.empty())
    return Error::make(ErrorCode::NotInitialized, "audio decoder closed");
#if defined(__ANDROID__)
  // Android has no ffmpeg binary: decode through the platform extractor and
  // slice the window out of the decoded PCM.
  {
    AudioBuffer all;
    const int rate = info_.sample_rate > 0 ? info_.sample_rate : 48000;
    const int ch_req = info_.channels > 0 ? info_.channels : 2;
    Error ae = android_decode_audio(path_, all, rate, ch_req, start + duration + 1.0);
    if (!ae.ok()) return ae;
    const int ch = std::max(1, all.channels);
    const size_t s0 = static_cast<size_t>(std::max(0.0, start) * all.sample_rate) *
                      static_cast<size_t>(ch);
    const size_t frames = static_cast<size_t>(std::max(0.0, duration) * all.sample_rate);
    dst.sample_rate = all.sample_rate;
    dst.channels = ch;
    dst.samples.assign(frames * static_cast<size_t>(ch), 0.f);
    for (size_t i = 0; i < dst.samples.size(); ++i) {
      const size_t src = s0 + i;
      if (src < all.samples.size()) dst.samples[i] = all.samples[src];
    }
    return Error::success();
  }
#endif
  if (!ffmpeg_available(paths_))
    return Error::make(ErrorCode::NotSupported, "ffmpeg not found");
  if (duration <= 0)
    return Error::make(ErrorCode::InvalidArgument, "duration");

  int rate = info_.sample_rate;
  int ch = info_.channels;
  std::ostringstream cmd;
  cmd << paths_.ffmpeg << " -v error -ss " << start
      << " -i \"" << path_ << "\""
      << " -t " << duration
      << " -vn -ac " << ch << " -ar " << rate
      << " -f f32le -";

  FILE* pipe = popen(cmd.str().c_str(), "r");
  if (!pipe) return Error::make(ErrorCode::MediaDecodeFailed, "popen audio");

  std::vector<float> data;
  float buf[2048];
  while (true) {
    size_t n = fread(buf, sizeof(float), 2048, pipe);
    if (n == 0) break;
    data.insert(data.end(), buf, buf + n);
  }
  int st = pclose(pipe);
  (void)st;

  dst.sample_rate = rate;
  dst.channels = ch;
  dst.samples = std::move(data);
  // if empty (no audio stream), silence
  if (dst.samples.empty()) {
    int frames = std::max(1, static_cast<int>(duration * rate));
    dst.samples.assign(static_cast<size_t>(frames) * ch, 0.f);
  }
  return Error::success();
}

Error AudioDecoderFFmpeg::decode_all(double max_seconds) {
  if (!open_) return Error::make(ErrorCode::NotInitialized, "closed");
  double dur = info_.duration > 0 ? info_.duration : max_seconds;
  if (dur > max_seconds) dur = max_seconds;
  if (dur <= 0) dur = 1.0;
  Error e = decode_window(0, dur, pcm_);
  if (!e.ok()) return e;
  info_.total_samples = pcm_.samples.size() / std::max(1, pcm_.channels);
  info_.duration = static_cast<double>(info_.total_samples) / info_.sample_rate;
  return Error::success();
}

// ---- AudioPlayback ----

AudioPlayback::AudioPlayback(FFmpegPaths paths) : decoder_(std::move(paths)) {}

Error AudioPlayback::open(const std::string& path) {
  close();
  Error e = decoder_.open(path);
  if (!e.ok()) return e;
  e = decoder_.decode_all(180.0);
  if (!e.ok()) return e;
  info_ = decoder_.info();
  time_ = 0;
  playing_ = false;
  loaded_ = true;
  return Error::success();
}

void AudioPlayback::close() {
  decoder_.close();
  loaded_ = false;
  playing_ = false;
  time_ = 0;
}

Error AudioPlayback::play() {
  if (!loaded_) return Error::make(ErrorCode::NotInitialized, "no audio");
  playing_ = true;
  return Error::success();
}

void AudioPlayback::pause() { playing_ = false; }

void AudioPlayback::stop() {
  playing_ = false;
  time_ = 0;
}

Error AudioPlayback::seek(TimeSec t) {
  if (!loaded_) return Error::make(ErrorCode::NotInitialized, "no audio");
  time_ = std::clamp(t, 0.0, info_.duration);
  return Error::success();
}

void AudioPlayback::advance(double dt) {
  if (!playing_ || !loaded_) return;
  time_ += dt;
  if (time_ >= info_.duration) {
    if (looping_) time_ = std::fmod(time_, info_.duration);
    else {
      time_ = info_.duration;
      playing_ = false;
    }
  }
}

Error AudioPlayback::read_pcm(float* interleaved, int frames, int channels) {
  if (!interleaved || frames <= 0)
    return Error::make(ErrorCode::InvalidArgument, "pcm args");
  const int ch = channels > 0 ? channels : 2;
  std::memset(interleaved, 0, sizeof(float) * static_cast<size_t>(frames) * ch);
  if (!loaded_ || !playing_) return Error::success();

  const auto& pcm = decoder_.buffer();
  const int src_ch = pcm.channels > 0 ? pcm.channels : 2;
  const int rate = pcm.sample_rate > 0 ? pcm.sample_rate : 48000;
  size_t start = static_cast<size_t>(time_ * rate);
  for (int i = 0; i < frames; ++i) {
    size_t si = start + static_cast<size_t>(i);
    for (int c = 0; c < ch; ++c) {
      float s = 0.f;
      if (si * src_ch + static_cast<size_t>(std::min(c, src_ch - 1)) < pcm.samples.size()) {
        s = pcm.samples[si * src_ch + static_cast<size_t>(std::min(c, src_ch - 1))];
      }
      interleaved[i * ch + c] = s * volume_;
    }
  }
  return effects_.process(interleaved, static_cast<std::size_t>(frames), rate, ch);
}

Error AudioPlayback::read_buffer(AudioBuffer& out, int frames) {
  int ch = 2;
  out.sample_rate = info_.sample_rate;
  out.channels = ch;
  out.samples.assign(static_cast<size_t>(frames) * ch, 0.f);
  return read_pcm(out.samples.data(), frames, ch);
}

AudioPlaybackInfo AudioPlayback::info() const {
  AudioPlaybackInfo i = info_;
  i.time = time_;
  i.playing = playing_;
  return i;
}

// ---- TimelineAudioPlayback ----

TimelineAudioPlayback::TimelineAudioPlayback(ProjectSession* session, FFmpegPaths paths)
    : session_(session), paths_(std::move(paths)) {}

Error TimelineAudioPlayback::prepare() {
  if (!session_) return Error::make(ErrorCode::NotInitialized, "no session");
  fps_ = session_->meta().fps > 1 ? session_->meta().fps : 24.f;
  decoders_.clear();
  // The mix runs at a fixed 48 kHz here; tell the meter so its K-weighting is
  // computed for the rate the samples actually arrive at.
  meter_.set_sample_rate(48000);
  return Error::success();
}

Error TimelineAudioPlayback::play() {
  Error e = prepare();
  if (!e.ok()) return e;
  playing_ = true;
  return Error::success();
}

void TimelineAudioPlayback::pause() { playing_ = false; }

void TimelineAudioPlayback::stop() {
  playing_ = false;
  time_ = 0;
  meter_.reset();
}

Error TimelineAudioPlayback::seek(TimeSec t) {
  time_ = std::max(0.0, t);
  // A new position starts a new measurement; carrying an integrated reading
  // across a seek would report the loudness of two unrelated sections.
  meter_.reset();
  return Error::success();
}

void TimelineAudioPlayback::advance(double dt) {
  if (playing_) time_ += dt;
}

void TimelineAudioPlayback::sync_track_chains() {
  if (!session_) return;
  const auto& tracks = session_->tracks();
  for (const auto& track : tracks) {
    auto it = track_chains_.find(track.id);
    if (it == track_chains_.end()) {
      if (track.audio_inserts.empty()) continue;
      it = track_chains_.emplace(track.id, AudioEffectChain{}).first;
    }
    AudioEffectChain& chain = it->second;
    const auto current = chain.parameters();
    const auto& want = track.audio_inserts;
    // A structure change (or a kind swap) has to rebuild the chain; anything
    // else is an in-place parameter edit, which keeps the DSP state alive.
    bool rebuild = current.size() != want.size();
    for (size_t i = 0; !rebuild && i < want.size(); ++i)
      if (current[i].kind != want[i].kind) rebuild = true;
    if (rebuild) {
      chain.clear();
      for (const auto& p : want) chain.add(p);
      continue;
    }
    for (size_t i = 0; i < want.size(); ++i)
      if (!same_fx(current[i], want[i])) chain.set(i, want[i]);
  }
  // Drop chains whose track went away (or whose inserts were cleared), so a
  // deleted track cannot keep processing stale audio.
  for (auto it = track_chains_.begin(); it != track_chains_.end();) {
    bool live = false;
    for (const auto& track : tracks) {
      if (track.id != it->first) continue;
      live = !track.audio_inserts.empty();
      break;
    }
    if (live) ++it; else it = track_chains_.erase(it);
  }
}

Error TimelineAudioPlayback::read_pcm(float* interleaved, int frames, int channels) {
  if (!interleaved || frames <= 0)
    return Error::make(ErrorCode::InvalidArgument, "pcm");
  const int ch = channels > 0 ? channels : 2;
  std::memset(interleaved, 0, sizeof(float) * static_cast<size_t>(frames) * ch);
  if (!session_ || !playing_) return Error::success();

  const int rate = 48000;
  double win = static_cast<double>(frames) / rate;
  sync_track_chains();

  // Solo on any audible track mutes the rest, the way a mixing desk behaves.
  bool any_solo = false;
  for (const auto& t : session_->tracks()) if (t.solo && !t.muted) any_solo = true;

  for (const auto& track : session_->tracks()) {
    if (track.muted) continue;
    if (any_solo && !track.solo) continue;
    // A track with inserts is mixed into its own buffer first, processed, then
    // summed into the master bus. Tracks without inserts keep the direct path.
    auto chain_it = track.audio_inserts.empty() ? track_chains_.end()
                                                : track_chains_.find(track.id);
    const bool track_fx = chain_it != track_chains_.end();
    float* mix = interleaved;
    if (track_fx) {
      track_scratch_.assign(static_cast<size_t>(frames) * static_cast<size_t>(ch), 0.f);
      mix = track_scratch_.data();
    }
    for (const auto& clip : track.clips) {
      if (clip.mute || clip.is_text) continue;
      if (time_ + win < clip.start || time_ >= clip.start + clip.duration) continue;
      const MediaInfo* mi = session_->find_media(clip.media_id);
      if (!mi || !mi->has_audio) continue;

      auto it = decoders_.find(mi->path);
      if (it == decoders_.end()) {
        auto dec = std::make_unique<AudioDecoderFFmpeg>(paths_);
        if (!dec->open(mi->path).ok()) continue;
        // decode full short media for simplicity
        if (!dec->decode_all(120.0).ok()) continue;
        it = decoders_.emplace(mi->path, std::move(dec)).first;
      }
      const AudioBuffer& pcm = it->second->buffer();
      int src_ch = pcm.channels > 0 ? pcm.channels : 2;
      int src_rate = pcm.sample_rate > 0 ? pcm.sample_rate : 48000;

      // Clip fader × track fader, then a linear pan law. This used to read
      // clip.opacity, which meant the mixer never heard a clip's own gain.
      const float clip_gain = std::clamp(clip.gain * track.gain, 0.f, 4.f);
      const float pan = std::clamp(clip.pan, -1.f, 1.f);
      // A curve in either direction means the channel gains have to be worked out
      // per sample; a static clip keeps one set of gains for the whole block.
      const bool auto_gain = !clip.gain_curve.empty();
      const bool auto_pan = !clip.pan_curve.empty();
      const bool animated = auto_gain || auto_pan;
      float ch_gain[32];
      for (int c = 0; c < ch && c < 32; ++c) {
        if (ch == 1) ch_gain[c] = clip_gain;
        else ch_gain[c] = clip_gain * (c == 0 ? 1.f - std::max(0.f, pan)
                                             : 1.f + std::min(0.f, pan));
      }

      for (int i = 0; i < frames; ++i) {
        double t = time_ + static_cast<double>(i) / rate;
        if (t < clip.start || t >= clip.start + clip.duration) continue;
        const double clip_local = t - clip.start;
        double local = clip_local * clip.speed + clip.source_in;
        size_t si = static_cast<size_t>(local * src_rate);
        if (animated) {
          float g = clip_gain;
          float p = pan;
          if (auto_gain)
            g = std::clamp(clip_gain * static_cast<float>(clip.gain_curve.eval(clip_local)),
                           0.f, 4.f);
          if (auto_pan)
            p = std::clamp(static_cast<float>(clip.pan_curve.eval(clip_local)), -1.f, 1.f);
          for (int c = 0; c < ch && c < 32; ++c) {
            if (ch == 1) ch_gain[c] = g;
            else ch_gain[c] = g * (c == 0 ? 1.f - std::max(0.f, p) : 1.f + std::min(0.f, p));
          }
        }
        for (int c = 0; c < ch; ++c) {
          float s = 0.f;
          size_t idx = si * src_ch + static_cast<size_t>(std::min(c, src_ch - 1));
          if (idx < pcm.samples.size()) s = pcm.samples[idx];
          mix[i * ch + c] += s * ch_gain[c < 32 ? c : 0];
        }
      }
    }
    if (track_fx) {
      chain_it->second.process(track_scratch_.data(), static_cast<std::size_t>(frames), rate, ch);
      for (int i = 0; i < frames * ch; ++i) interleaved[i] += track_scratch_[i];
    }
  }
  // Keep the pre-insert mix bounded, then process the stateful timeline insert
  // chain. Float output may exceed unity after time-based effects; the host's
  // device/output stage is responsible for final conversion/limiting.
  for (int i = 0; i < frames * ch; ++i)
    interleaved[i] = std::clamp(interleaved[i], -1.f, 1.f);
  Error mixed = effects_.process(interleaved, static_cast<std::size_t>(frames), rate, ch);
  // Meter what the listener actually hears: post-fader, post-insert.
  if (mixed.ok()) meter_.process(interleaved, static_cast<std::size_t>(frames), ch);
  return mixed;
}

Error TimelineAudioPlayback::clip_peaks(const ClipInfo& clip, int bins, std::vector<float>& out) {
  if (bins <= 0) return Error::make(ErrorCode::InvalidArgument, "bins");
  out.assign(static_cast<size_t>(bins), 0.f);
  if (!session_) return Error::make(ErrorCode::NotInitialized, "no session");
  if (clip.is_text) return Error::make(ErrorCode::InvalidArgument, "text clip has no audio");

  const MediaInfo* mi = session_->find_media(clip.media_id);
  if (!mi) return Error::make(ErrorCode::FileNotFound, "clip media");

  auto it = decoders_.find(mi->path);
  if (it == decoders_.end()) {
    auto dec = std::make_unique<AudioDecoderFFmpeg>(paths_);
    Error e = dec->open(mi->path);
    if (!e.ok()) return e;
    e = dec->decode_all(600.0);
    if (!e.ok()) return e;
    it = decoders_.emplace(mi->path, std::move(dec)).first;
  }

  const AudioBuffer& pcm = it->second->buffer();
  const int src_ch = pcm.channels > 0 ? pcm.channels : 2;
  const int src_rate = pcm.sample_rate > 0 ? pcm.sample_rate : 48000;
  const double duration = clip.duration > 0.01 ? clip.duration : 0.01;

  for (int b = 0; b < bins; ++b) {
    const double t0 = clip.source_in + duration * static_cast<double>(b) / bins;
    const double t1 = clip.source_in + duration * static_cast<double>(b + 1) / bins;
    size_t s0 = static_cast<size_t>(std::max(0.0, t0) * src_rate);
    size_t s1 = static_cast<size_t>(std::max(0.0, t1) * src_rate);
    if (s1 <= s0) s1 = s0 + 1;
    // Bound the work per bin for long clips; a waveform does not need every sample.
    const size_t stride = std::max<size_t>(1, (s1 - s0) / 4096);
    float peak = 0.f;
    bool any = false;
    for (size_t s = s0; s < s1; s += stride) {
      for (int c = 0; c < src_ch; ++c) {
        const size_t idx = s * static_cast<size_t>(src_ch) + static_cast<size_t>(c);
        if (idx >= pcm.samples.size()) break;
        any = true;
        peak = std::max(peak, std::fabs(pcm.samples[idx]));
      }
      if (!any) break;
    }
    out[static_cast<size_t>(b)] = std::clamp(peak, 0.f, 1.f);
  }
  return Error::success();
}

AudioPlaybackInfo TimelineAudioPlayback::info() const {
  AudioPlaybackInfo i;
  i.sample_rate = 48000;
  i.channels = 2;
  i.time = time_;
  i.playing = playing_;
  if (session_) i.duration = session_->sequence_end();
  return i;
}

} // namespace ms
