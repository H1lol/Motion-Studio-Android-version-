#pragma once
/**
 * FFmpeg audio decode + playback buffer
 * - Decode via ffmpeg CLI to f32le PCM
 * - Ring/playhead streaming for MediaPlayer sync
 * - Optional SDL/PortAudio later; here we deliver PCM for host to output
 */
#include "ms_media.hpp"
#include "ms_audio_fx.hpp"
#include "ms_ffmpeg_io.hpp"
#include "ms_error.hpp"
#include "ms_types.hpp"
#include <string>
#include <vector>
#include <cstdint>
#include <mutex>
#include <memory>
#include <unordered_map>
#include <algorithm>
#include "ms_project.hpp"

namespace ms {

struct AudioPlaybackInfo {
  int sample_rate = 48000;
  int channels = 2;
  TimeSec duration = 0;
  TimeSec time = 0;
  bool playing = false;
  size_t total_samples = 0; // frames (not interleaved count)
};

/**
 * Loads full (or windowed) PCM from file using ffmpeg.
 * For short clips; long media can use streaming decode_window.
 */
class AudioDecoderFFmpeg {
public:
  explicit AudioDecoderFFmpeg(FFmpegPaths paths = {});

  Error open(const std::string& path, int target_rate = 48000, int target_ch = 2);
  void close();

  /** Decode entire file to internal buffer (cap max_seconds) */
  Error decode_all(double max_seconds = 120.0);

  /**
   * Decode a time window [start, start+duration] into dst (interleaved float)
   * dst is cleared and filled.
   */
  Error decode_window(double start, double duration, AudioBuffer& dst);

  const AudioBuffer& buffer() const { return pcm_; }
  const AudioPlaybackInfo& info() const { return info_; }
  bool is_open() const { return open_; }

private:
  FFmpegPaths paths_;
  std::string path_;
  AudioBuffer pcm_;
  AudioPlaybackInfo info_;
  bool open_ = false;
};

/**
 * Audio transport synced to a playhead (same clock as video)
 * Pull model: host asks for nsamples at current time.
 */
class AudioPlayback {
public:
  explicit AudioPlayback(FFmpegPaths paths = {});

  Error open(const std::string& path);
  void close();

  Error play();
  void pause();
  void stop();
  Error seek(TimeSec t);

  /** Advance time by dt (seconds); same as video player clock */
  void advance(double dt);

  /**
   * Fill interleaved float PCM for `frames` sample frames at current playhead.
   * Silence if not playing or past end.
   */
  Error read_pcm(float* interleaved, int frames, int channels = 2);

  /** Convenience: fill AudioBuffer */
  Error read_buffer(AudioBuffer& out, int frames);

  AudioPlaybackInfo info() const;
  void set_looping(bool v) { looping_ = v; }
  void set_volume(float v) { volume_ = std::clamp(v, 0.f, 2.f); }
  AudioEffectChain& effects() { return effects_; }
  const AudioEffectChain& effects() const { return effects_; }

private:
  AudioDecoderFFmpeg decoder_;
  AudioPlaybackInfo info_;
  TimeSec time_ = 0;
  bool playing_ = false;
  bool looping_ = false;
  float volume_ = 1.f;
  bool loaded_ = false;
  AudioEffectChain effects_;
};

/**
 * Mix timeline audio clips (FFmpeg decode windows) for project playback
 */
class TimelineAudioPlayback {
public:
  explicit TimelineAudioPlayback(ProjectSession* session = nullptr,
                                 FFmpegPaths paths = {});

  void set_session(ProjectSession* s) { session_ = s; }
  Error prepare();
  Error play();
  void pause();
  void stop();
  Error seek(TimeSec t);
  void advance(double dt);

  Error read_pcm(float* interleaved, int frames, int channels = 2);

  /**
   * Peak envelope for one clip, `bins` values in 0..1, taken from the decoded
   * PCM over the clip's source range. Used to draw the timeline waveform.
   * Decodes through the same cache as playback, so asking for a waveform also
   * warms the audio for preview.
   */
  Error clip_peaks(const ClipInfo& clip, int bins, std::vector<float>& out);

  AudioPlaybackInfo info() const;
  AudioEffectChain& effects() { return effects_; }
  const AudioEffectChain& effects() const { return effects_; }

private:
  /**
   * Mirrors each track's document insert list into its running chain. Only a
   * genuine parameter change is pushed down: AudioEffectChain::set() drops the
   * derived DSP state, so re-applying an unchanged list every block would clear
   * the delay lines and reverb tails on every audio callback.
   */
  void sync_track_chains();

  ProjectSession* session_ = nullptr;
  FFmpegPaths paths_;
  TimeSec time_ = 0;
  bool playing_ = false;
  float fps_ = 24.f;
  // cache path -> decoder
  std::unordered_map<std::string, std::unique_ptr<AudioDecoderFFmpeg>> decoders_;
  // One stateful chain per track that carries inserts, keyed by track id.
  std::unordered_map<TrackId, AudioEffectChain> track_chains_;
  // Scratch mix for a track with inserts, reused between blocks.
  std::vector<float> track_scratch_;
  AudioEffectChain effects_;
};

} // namespace ms
