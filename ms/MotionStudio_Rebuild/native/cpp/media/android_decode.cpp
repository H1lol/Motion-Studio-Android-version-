#include "ms_android_decode.hpp"

#if defined(__ANDROID__)

#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>
#include <android/log.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <cstdlib>
#include <cstring>
#include <vector>

#define MS_LOG(...) __android_log_print(ANDROID_LOG_INFO, "MotionStudio", __VA_ARGS__)

namespace ms {
namespace {

// MediaCodec colour formats we can convert. Anything else is treated as NV12,
// which is what practically every Android decoder emits.
constexpr int32_t kColorFormatYUV420Planar = 19;   // I420
constexpr int32_t kColorFormatYUV420SemiPlanar = 21; // NV12
constexpr int32_t kColorFormatYV12 = 0x32315659;   // YV12
constexpr int32_t kColorFormatFlexible = 0x7F420888;

int clamp_u8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

/** BT.601 limited-range YUV to RGBA, matching how Android delivers camera/decoder frames. */
inline void yuv_to_rgba(int y, int u, int v, uint8_t* dst) {
  const int c = y - 16;
  const int d = u - 128;
  const int e = v - 128;
  const int r = (298 * c + 409 * e + 128) >> 8;
  const int g = (298 * c - 100 * d - 208 * e + 128) >> 8;
  const int b = (298 * c + 516 * d + 128) >> 8;
  dst[0] = static_cast<uint8_t>(clamp_u8(r));
  dst[1] = static_cast<uint8_t>(clamp_u8(g));
  dst[2] = static_cast<uint8_t>(clamp_u8(b));
  dst[3] = 255;
}

struct TrackInfoLite {
  int index = -1;
  std::string mime;
};

} // namespace

bool android_decoder_available() { return true; }

class MediaCodecDecoder : public IMediaDecoder {
public:
  ~MediaCodecDecoder() override { close(); }

  Error open(const std::string& path) override {
    close();

    extractor_ = AMediaExtractor_new();
    if (!extractor_) return Error::make(ErrorCode::OutOfMemory, "extractor");
    media_status_t st =
        AMediaExtractor_setDataSource(extractor_, path.c_str());
    if (st != AMEDIA_OK) {
      MS_LOG("setDataSource failed (%d) for %s", static_cast<int>(st), path.c_str());
      close();
      return Error::make(ErrorCode::MediaOpenFailed, "AMediaExtractor rejected the file", path);
    }

    // Pick the first video track.
    const size_t track_count = AMediaExtractor_getTrackCount(extractor_);
    int video_track = -1;
    AMediaFormat* chosen = nullptr;
    for (size_t i = 0; i < track_count; ++i) {
      AMediaFormat* fmt = AMediaExtractor_getTrackFormat(extractor_, i);
      if (!fmt) continue;
      const char* mime = nullptr;
      if (AMediaFormat_getString(fmt, AMEDIAFORMAT_KEY_MIME, &mime) && mime &&
          std::strncmp(mime, "video/", 6) == 0) {
        video_track = static_cast<int>(i);
        chosen = fmt;
        break;
      }
      AMediaFormat_delete(fmt);
    }
    if (video_track < 0 || !chosen) {
      close();
      return Error::make(ErrorCode::MediaOpenFailed, "no video track", path);
    }

    const char* mime = nullptr;
    AMediaFormat_getString(chosen, AMEDIAFORMAT_KEY_MIME, &mime);
    const std::string mime_str = mime ? mime : "";
    int32_t w = 0, h = 0;
    AMediaFormat_getInt32(chosen, AMEDIAFORMAT_KEY_WIDTH, &w);
    AMediaFormat_getInt32(chosen, AMEDIAFORMAT_KEY_HEIGHT, &h);
    int64_t duration_us = 0;
    AMediaFormat_getInt64(chosen, AMEDIAFORMAT_KEY_DURATION, &duration_us);
    // A decoder's track format usually omits DURATION (it is a property of the
    // extractor's file format, not of the coded track). Without this fallback
    // info().duration_ms stayed 0, every imported clip was cut to a guessed
    // five seconds, and the sequence appeared to end at the project's default
    // length. Ask the container, then the last sample, before giving up.
    if (duration_us <= 0) {
      if (AMediaFormat* file_fmt = AMediaExtractor_getFileFormat(extractor_)) {
        int64_t file_us = 0;
        if (AMediaFormat_getInt64(file_fmt, AMEDIAFORMAT_KEY_DURATION, &file_us) && file_us > 0) {
          duration_us = file_us;
        }
        AMediaFormat_delete(file_fmt);
      }
    }
    if (duration_us <= 0) {
      AMediaExtractor_seekTo(extractor_, std::numeric_limits<int64_t>::max(),
                             AMEDIAEXTRACTOR_SEEK_CLOSEST_SYNC);
      const int64_t last_us = AMediaExtractor_getSampleTime(extractor_);
      if (last_us > 0) duration_us = last_us + 40000;
      AMediaExtractor_seekTo(extractor_, 0, AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC);
    }
    float frame_rate = 0.f;
    AMediaFormat_getFloat(chosen, AMEDIAFORMAT_KEY_FRAME_RATE, &frame_rate);

    codec_ = AMediaCodec_createDecoderByType(mime_str.c_str());
    if (!codec_) {
      AMediaFormat_delete(chosen);
      close();
      return Error::make(ErrorCode::NotSupported, "no decoder for mime", mime_str);
    }
    if (AMediaCodec_configure(codec_, chosen, nullptr, nullptr, 0) != AMEDIA_OK ||
        AMediaCodec_start(codec_) != AMEDIA_OK) {
      AMediaFormat_delete(chosen);
      close();
      return Error::make(ErrorCode::MediaOpenFailed, "codec configure/start failed", mime_str);
    }
    AMediaFormat_delete(chosen);

    AMediaExtractor_selectTrack(extractor_, static_cast<size_t>(video_track));
    track_ = video_track;
    input_done_ = false;
    eos_ = false;
    source_w_ = w > 0 ? w : 640;
    source_h_ = h > 0 ? h : 360;

    info_.path = path;
    info_.container = mime_str;
    info_.duration_ms = duration_us > 0 ? duration_us / 1000 : 0;
    MediaStreamInfo vs;
    vs.type = "video";
    vs.codec = mime_str;
    vs.width = source_w_;
    vs.height = source_h_;
    vs.fps = frame_rate;
    vs.duration_ms = info_.duration_ms;
    info_.streams.push_back(vs);

    // Audio presence is reported from the extractor so the UI can tell.
    for (size_t i = 0; i < track_count; ++i) {
      AMediaFormat* fmt = AMediaExtractor_getTrackFormat(extractor_, i);
      if (!fmt) continue;
      const char* m = nullptr;
      if (AMediaFormat_getString(fmt, AMEDIAFORMAT_KEY_MIME, &m) && m &&
          std::strncmp(m, "audio/", 6) == 0) {
        MediaStreamInfo as;
        as.type = "audio";
        as.codec = m;
        int32_t channels = 2, sample_rate = 48000;
        AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &channels);
        AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_SAMPLE_RATE, &sample_rate);
        as.channels = channels;
        as.sample_rate = sample_rate;
        as.duration_ms = info_.duration_ms;
        info_.streams.push_back(as);
      }
      AMediaFormat_delete(fmt);
    }

    open_ = true;
    pos_ms_ = 0;
    return Error::success();
  }

  void close() override {
    if (codec_) {
      AMediaCodec_stop(codec_);
      AMediaCodec_delete(codec_);
      codec_ = nullptr;
    }
    if (extractor_) {
      AMediaExtractor_delete(extractor_);
      extractor_ = nullptr;
    }
    open_ = false;
    input_done_ = false;
    eos_ = false;
  }

  Error seek_ms(int64_t ms) override {
    if (!open_) return Error::make(ErrorCode::NotInitialized, "decoder closed");
    if (extractor_) {
      AMediaExtractor_seekTo(extractor_, std::max<int64_t>(0, ms) * 1000,
                             AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC);
    }
    if (codec_) AMediaCodec_flush(codec_);
    pos_ms_ = std::max<int64_t>(0, ms);
    input_done_ = false;
    eos_ = false;
    have_frame_ = false;
    return Error::success();
  }

  Error read_video(ImageBuffer& out, int64_t& pts_ms) override {
    if (!open_) return Error::make(ErrorCode::NotInitialized, "decoder closed");

    // Bounded work: feed input and drain output until a frame comes out.
    for (int guard = 0; guard < 512; ++guard) {
      if (!input_done_) {
        ssize_t in_index = AMediaCodec_dequeueInputBuffer(codec_, 0);
        if (in_index >= 0) {
          size_t buf_size = 0;
          uint8_t* buf = AMediaCodec_getInputBuffer(codec_, static_cast<size_t>(in_index),
                                                    &buf_size);
          ssize_t sample = buf ? AMediaExtractor_readSampleData(extractor_, buf, buf_size) : -1;
          if (sample < 0) {
            AMediaCodec_queueInputBuffer(codec_, static_cast<size_t>(in_index), 0, 0, 0,
                                         AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
            input_done_ = true;
          } else {
            const int64_t pts_us = AMediaExtractor_getSampleTime(extractor_);
            AMediaCodec_queueInputBuffer(codec_, static_cast<size_t>(in_index), 0,
                                         static_cast<size_t>(sample),
                                         static_cast<uint64_t>(std::max<int64_t>(0, pts_us)), 0);
            AMediaExtractor_advance(extractor_);
          }
        }
      }

      AMediaCodecBufferInfo info{};
      const ssize_t out_index = AMediaCodec_dequeueOutputBuffer(codec_, &info, 0);
      if (out_index == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED ||
          out_index == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
        AMediaFormat* ofmt = AMediaCodec_getOutputFormat(codec_);
        if (ofmt) {
          int32_t w = 0, h = 0, stride = 0, slice = 0, color = kColorFormatFlexible;
          AMediaFormat_getInt32(ofmt, AMEDIAFORMAT_KEY_WIDTH, &w);
          AMediaFormat_getInt32(ofmt, AMEDIAFORMAT_KEY_HEIGHT, &h);
          AMediaFormat_getInt32(ofmt, AMEDIAFORMAT_KEY_STRIDE, &stride);
          AMediaFormat_getInt32(ofmt, "slice-height", &slice);
          AMediaFormat_getInt32(ofmt, AMEDIAFORMAT_KEY_COLOR_FORMAT, &color);
          if (w > 0) out_w_ = w;
          if (h > 0) out_h_ = h;
          stride_ = stride > 0 ? stride : out_w_;
          slice_h_ = slice > 0 ? slice : out_h_;
          color_format_ = color;
          MS_LOG("output %dx%d stride=%d slice=%d color=0x%x", out_w_, out_h_, stride_, slice_h_,
                 color_format_);
          AMediaFormat_delete(ofmt);
        }
        continue;
      }
      if (out_index < 0) {
        if (eos_ || input_done_) {
          // Nothing more to produce for this position; hold the last frame.
          if (have_frame_) {
            pts_ms = pos_ms_;
            return Error::success();
          }
          return Error::make(ErrorCode::MediaDecodeFailed, "end of stream");
        }
        continue;
      }

      if (info.size > 0 && out_buf_valid_) {
        size_t cap = 0;
        uint8_t* yuv = AMediaCodec_getOutputBuffer(codec_, static_cast<size_t>(out_index), &cap);
        if (yuv) {
          const Error e = convert(yuv, static_cast<size_t>(info.size), out);
          AMediaCodec_releaseOutputBuffer(codec_, static_cast<size_t>(out_index), false);
          if (!e.ok()) return e;
          // Report the frame's own timestamp. This used to report pos_ms_ — the
          // position that was *asked for* — so a frame decoded after a seek was
          // labelled with the seek target rather than with when it really is.
          const int64_t frame_pts =
              info.presentationTimeUs > 0 ? info.presentationTimeUs / 1000 : pos_ms_;
          pts_ms = frame_pts;
          last_pts_ = frame_pts;
          pos_ms_ = frame_pts;
          have_frame_ = true;
          return Error::success();
        }
      }
      AMediaCodec_releaseOutputBuffer(codec_, static_cast<size_t>(out_index), false);
      if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
        eos_ = true;
        if (have_frame_) {
          pts_ms = pos_ms_;
          return Error::success();
        }
        return Error::make(ErrorCode::MediaDecodeFailed, "end of stream");
      }
    }
    if (have_frame_) {
      pts_ms = pos_ms_;
      return Error::success();
    }
    return Error::make(ErrorCode::MediaDecodeFailed, "no frame decoded");
  }

  /**
   * Frame-at-time with a streaming fast path.
   *
   * Playback and export both step forward one frame at a time, but the plain
   * seek+read path restarts decoding at the previous keyframe for every frame,
   * so a 2-second GOP costs 60 frame decodes per displayed frame. When the
   * request is a small step ahead of the frame already decoded, this decodes
   * forward instead and only skips the frames in between.
   *
   * Requests for the same instant still hit the codec's released buffer, so the
   * frame is cached once and copied; rendering a paused playhead asks for that
   * instant repeatedly.
   */
  Error read_video_at(int64_t ms, ImageBuffer& out, int64_t& pts_ms) override {
    if (!open_) return Error::make(ErrorCode::NotInitialized, "decoder closed");
    const int64_t target = std::max<int64_t>(0, ms);

    if (cached_valid_ && std::llabs(target - cached_request_ms_) <= kSameFrameMs) {
      if (!copy_cached(out)) return Error::make(ErrorCode::OutOfMemory, "frame copy");
      pts_ms = cached_pts_;
      return Error::success();
    }

    const int64_t ahead = target - last_pts_;
    const bool stream_forward =
        have_frame_ && !eos_ && !input_done_ && ahead > 0 && ahead <= kStreamWindowMs;
    if (!stream_forward) {
      Error seek = seek_ms(target);
      if (!seek.ok()) return seek;
    }

    int64_t got = 0;
    Error e = read_video(out, got);
    if (!e.ok()) {
      cached_valid_ = false;
      return e;
    }

    // Decoding forward from a keyframe returns frames before the request; walk
    // to the one that covers it.
    for (int guard = 0; guard < 64 && got + 1 < target; ++guard) {
      ImageBuffer next;
      int64_t next_pts = got;
      if (!read_video(next, next_pts).ok()) break;
      got = next_pts;
      out = std::move(next);
    }

    pts_ms = got;
    store_cache(out, target, got);
    return Error::success();
  }

  Error read_audio(AudioBuffer& out, int64_t& pts_ms) override {
    // Video path only: audio goes through android_decode_audio() so the mixer
    // and the waveform share one decode.
    out.samples.clear();
    out.sample_rate = 48000;
    out.channels = 2;
    pts_ms = pos_ms_;
    return Error::success();
  }

  const MediaProbeResult& info() const override { return info_; }

private:
  Error convert(const uint8_t* yuv, size_t size, ImageBuffer& out) {
    const int w = out_w_ > 0 ? out_w_ : source_w_;
    const int h = out_h_ > 0 ? out_h_ : source_h_;
    if (w <= 0 || h <= 0) return Error::make(ErrorCode::InvalidArgument, "bad frame size");
    if (!out.allocate(w, h, PixelFormat::RGBA8))
      return Error::make(ErrorCode::OutOfMemory, "frame buffer");

    const bool planar = color_format_ == kColorFormatYUV420Planar ||
                        color_format_ == kColorFormatYV12;
    const bool swapped_uv = color_format_ == kColorFormatYV12;

    // The plane layout has to fit inside the buffer the codec actually handed
    // over. Some devices report the padded stride/slice but deliver a tight
    // buffer, and reading the padding ran past the mapping: that was a native
    // crash the moment a video frame was composited. Fall back to the tight
    // layout when the padded one does not fit, and refuse the frame when even
    // that is too short, instead of reading outside it.
    int stride = stride_ > 0 ? stride_ : w;
    int slice = slice_h_ > 0 ? slice_h_ : h;
    auto layout_bytes = [&](int s, int sh) -> size_t {
      const size_t y = static_cast<size_t>(s) * static_cast<size_t>(sh);
      const size_t chroma = planar
          ? static_cast<size_t>(s / 2) * static_cast<size_t>(sh / 2)
          : static_cast<size_t>(s) * static_cast<size_t>(sh / 2);
      return y + (planar ? 2 * chroma : chroma);
    };
    if (size < layout_bytes(stride, slice)) {
      stride = w;
      slice = h;
      if (size < layout_bytes(stride, slice)) {
        return Error::make(ErrorCode::MediaDecodeFailed, "short YUV buffer");
      }
    }

    const size_t y_size = static_cast<size_t>(stride) * static_cast<size_t>(slice);
    const size_t chroma_stride = planar ? static_cast<size_t>(stride / 2) : static_cast<size_t>(stride);
    const size_t chroma_size = chroma_stride * static_cast<size_t>(slice / 2);

    const uint8_t* y_plane = yuv;
    const uint8_t* u_plane = yuv + y_size;
    // Only planar formats have a second chroma plane; semi-planar interleaves
    // U and V in the single plane after luma.
    const uint8_t* v_plane = planar ? u_plane + chroma_size : nullptr;

    for (int y = 0; y < h; ++y) {
      const uint8_t* yrow = y_plane + static_cast<size_t>(y) * static_cast<size_t>(stride);
      uint8_t* drow = out.data() + static_cast<size_t>(y) * out.stride();
      const int cy = y / 2;
      for (int x = 0; x < w; ++x) {
        const int cx = x / 2;
        const uint8_t* u_src = nullptr;
        const uint8_t* v_src = nullptr;
        if (planar) {
          u_src = u_plane + static_cast<size_t>(cy) * chroma_stride + static_cast<size_t>(cx);
          v_src = v_plane + static_cast<size_t>(cy) * chroma_stride + static_cast<size_t>(cx);
          if (swapped_uv) std::swap(u_src, v_src);
        } else {
          u_src = u_plane + static_cast<size_t>(cy) * chroma_stride + static_cast<size_t>(cx) * 2;
          v_src = u_src + 1;
        }
        yuv_to_rgba(yrow[x], *u_src, *v_src, drow + static_cast<size_t>(x) * 4);
      }
    }
    return Error::success();
  }

  AMediaExtractor* extractor_ = nullptr;
  AMediaCodec* codec_ = nullptr;
  MediaProbeResult info_;
  int track_ = -1;
  bool open_ = false;
  bool input_done_ = false;
  bool eos_ = false;
  bool have_frame_ = false;
  bool out_buf_valid_ = true;
  int source_w_ = 0;
  int source_h_ = 0;
  int out_w_ = 0;
  int out_h_ = 0;
  int stride_ = 0;
  int slice_h_ = 0;
  int32_t color_format_ = kColorFormatFlexible;
  int64_t pos_ms_ = 0;
  int64_t last_pts_ = -1;

  // One-frame cache so a repeated request for the same instant does not have to
  // reopen the codec at a keyframe.
  ImageBuffer cached_;
  int64_t cached_pts_ = 0;
  int64_t cached_request_ms_ = -1;
  bool cached_valid_ = false;
  static constexpr int64_t kSameFrameMs = 2;
  static constexpr int64_t kStreamWindowMs = 500;

  bool copy_cached(ImageBuffer& out) const {
    if (!cached_valid_ || !cached_.valid()) return false;
    if (!out.allocate(cached_.width(), cached_.height(), cached_.format(), cached_.color_space()))
      return false;
    std::memcpy(out.data(), cached_.data(), cached_.stride() * static_cast<size_t>(cached_.height()));
    return true;
  }

  void store_cache(const ImageBuffer& src, int64_t request_ms, int64_t pts) {
    if (!src.valid()) {
      cached_valid_ = false;
      return;
    }
    // Bounded: one 4K RGBA frame is 33 MB and is not worth holding.
    const size_t bytes = src.stride() * static_cast<size_t>(src.height());
    if (bytes == 0 || bytes > (64u << 20)) {
      cached_valid_ = false;
      return;
    }
    if (!cached_.allocate(src.width(), src.height(), src.format(), src.color_space())) {
      cached_valid_ = false;
      return;
    }
    std::memcpy(cached_.data(), src.data(), bytes);
    cached_pts_ = pts;
    cached_request_ms_ = request_ms;
    cached_valid_ = true;
  }
};

std::unique_ptr<IMediaDecoder> create_android_decoder(const std::string& path) {
  auto dec = std::make_unique<MediaCodecDecoder>();
  if (!dec->open(path).ok()) return nullptr;
  return dec;
}

Error android_decode_audio(const std::string& path, AudioBuffer& out, int target_rate,
                           int target_ch, double max_seconds) {
  out.samples.clear();
  out.sample_rate = target_rate;
  out.channels = target_ch;

  AMediaExtractor* ex = AMediaExtractor_new();
  if (!ex) return Error::make(ErrorCode::OutOfMemory, "extractor");
  if (AMediaExtractor_setDataSource(ex, path.c_str()) != AMEDIA_OK) {
    AMediaExtractor_delete(ex);
    return Error::make(ErrorCode::MediaOpenFailed, "AMediaExtractor rejected the file", path);
  }

  int audio_track = -1;
  AMediaFormat* fmt = nullptr;
  const size_t count = AMediaExtractor_getTrackCount(ex);
  for (size_t i = 0; i < count; ++i) {
    AMediaFormat* f = AMediaExtractor_getTrackFormat(ex, i);
    if (!f) continue;
    const char* mime = nullptr;
    if (AMediaFormat_getString(f, AMEDIAFORMAT_KEY_MIME, &mime) && mime &&
        std::strncmp(mime, "audio/", 6) == 0) {
      audio_track = static_cast<int>(i);
      fmt = f;
      break;
    }
    AMediaFormat_delete(f);
  }
  if (audio_track < 0 || !fmt) {
    AMediaExtractor_delete(ex);
    return Error::make(ErrorCode::NotSupported, "no audio track", path);
  }

  const char* mime = nullptr;
  AMediaFormat_getString(fmt, AMEDIAFORMAT_KEY_MIME, &mime);
  AMediaCodec* codec = AMediaCodec_createDecoderByType(mime ? mime : "audio/mp4a-latm");
  if (!codec || AMediaCodec_configure(codec, fmt, nullptr, nullptr, 0) != AMEDIA_OK ||
      AMediaCodec_start(codec) != AMEDIA_OK) {
    if (codec) {
      AMediaCodec_delete(codec);
    }
    AMediaFormat_delete(fmt);
    AMediaExtractor_delete(ex);
    return Error::make(ErrorCode::MediaOpenFailed, "audio codec", path);
  }
  AMediaFormat_delete(fmt);
  AMediaExtractor_selectTrack(ex, static_cast<size_t>(audio_track));

  const int64_t max_samples = static_cast<int64_t>(max_seconds * target_rate);
  int src_channels = 2;
  int src_rate = target_rate;
  bool input_done = false;
  bool output_done = false;

  for (int guard = 0; guard < 100000 && !output_done; ++guard) {
    if (!input_done) {
      ssize_t in_index = AMediaCodec_dequeueInputBuffer(codec, 0);
      if (in_index >= 0) {
        size_t buf_size = 0;
        uint8_t* buf = AMediaCodec_getInputBuffer(codec, static_cast<size_t>(in_index), &buf_size);
        ssize_t sample = buf ? AMediaExtractor_readSampleData(ex, buf, buf_size) : -1;
        if (sample < 0) {
          AMediaCodec_queueInputBuffer(codec, static_cast<size_t>(in_index), 0, 0, 0,
                                       AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
          input_done = true;
        } else {
          const int64_t pts_us = AMediaExtractor_getSampleTime(ex);
          AMediaCodec_queueInputBuffer(codec, static_cast<size_t>(in_index), 0,
                                       static_cast<size_t>(sample),
                                       static_cast<uint64_t>(std::max<int64_t>(0, pts_us)), 0);
          AMediaExtractor_advance(ex);
        }
      }
    }

    AMediaCodecBufferInfo info{};
    const ssize_t out_index = AMediaCodec_dequeueOutputBuffer(codec, &info, 0);
    if (out_index == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
      AMediaFormat* ofmt = AMediaCodec_getOutputFormat(codec);
      if (ofmt) {
        AMediaFormat_getInt32(ofmt, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &src_channels);
        AMediaFormat_getInt32(ofmt, AMEDIAFORMAT_KEY_SAMPLE_RATE, &src_rate);
        AMediaFormat_delete(ofmt);
      }
      continue;
    }
    if (out_index < 0) {
      if (input_done && out_index == AMEDIACODEC_INFO_TRY_AGAIN_LATER) continue;
      continue;
    }
    if (info.size > 0) {
      size_t cap = 0;
      uint8_t* pcm = AMediaCodec_getOutputBuffer(codec, static_cast<size_t>(out_index), &cap);
      if (pcm && info.size >= 2) {
        // Decoders emit 16-bit PCM in the track's channel layout.
        const int frames = info.size / (2 * std::max(1, src_channels));
        const int16_t* src = reinterpret_cast<const int16_t*>(pcm + info.offset);
        for (int f = 0; f < frames; ++f) {
          for (int c = 0; c < target_ch; ++c) {
            const int sc = std::min(c, src_channels - 1);
            const float v = static_cast<float>(src[f * src_channels + sc]) / 32768.f;
            out.samples.push_back(std::max(-1.f, std::min(1.f, v)));
          }
          if (static_cast<int64_t>(out.samples.size() / target_ch) >= max_samples) {
            output_done = true;
            break;
          }
        }
      }
    }
    const bool eos = (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0;
    AMediaCodec_releaseOutputBuffer(codec, static_cast<size_t>(out_index), false);
    if (eos) output_done = true;
  }

  AMediaCodec_stop(codec);
  AMediaCodec_delete(codec);
  AMediaExtractor_delete(ex);
  return Error::success();
}

} // namespace ms

#else

namespace ms {
bool android_decoder_available() { return false; }
std::unique_ptr<IMediaDecoder> create_android_decoder(const std::string&) { return nullptr; }
Error android_decode_audio(const std::string&, AudioBuffer&, int, int, double) {
  return Error::make(ErrorCode::NotSupported, "not an Android build");
}
} // namespace ms

#endif
