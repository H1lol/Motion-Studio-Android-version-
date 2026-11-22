#pragma once
/**
 * Loudness metering for the timeline mix — BS.1770 / EBU R128, without libebur128.
 *
 * The meter K-weights each channel with the two standard biquads (a high shelf
 * at ~1.68 kHz and a high-pass at ~38 Hz), accumulates 100 ms block energies
 * and reports:
 *   - sample peak per channel (linear, since the last reset)
 *   - momentary loudness (400 ms window)
 *   - short-term loudness (3 s window)
 *   - integrated loudness (all blocks, with the -70 LUFS absolute gate and the
 *     -10 LU relative gate)
 *
 * Everything the UI reads is atomic: the audio thread fills the meter, the
 * preview thread polls it, and neither blocks the other.
 */
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace ms {

/** Transposed direct-form II biquad used by the K-weighting stages. */
struct LoudnessBiquad {
    float b0 = 1.f, b1 = 0.f, b2 = 0.f, a1 = 0.f, a2 = 0.f;
    float z1 = 0.f, z2 = 0.f;
    float run(float x) {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

class LoudnessMeter {
public:
    struct Reading {
        float peak_l = 0.f;       // linear sample peak, 0..1+
        float peak_r = 0.f;
        float momentary = -70.f;  // LUFS
        float short_term = -70.f; // LUFS
        float integrated = -70.f; // LUFS
    };

    /** Recomputes the K-weighting for `rate` and clears every buffer. */
    void set_sample_rate(int rate);
    void reset();
    /** Accepts interleaved float PCM; mono input feeds both meter channels. */
    void process(const float* interleaved, std::size_t frames, int channels);
    Reading read() const;

    /** Loudness of one block energy, the BS.1770 offset included. */
    static double loudness_of(double energy);

private:
    void finish_block();

    int rate_ = 48000;
    std::size_t block_frames_ = 4800; // 100 ms
    std::size_t block_pos_ = 0;
    double block_sum_ = 0.0;
    LoudnessBiquad shelf_l_, shelf_r_, hp_l_, hp_r_;

    // 24 minutes of 100 ms blocks: enough for any timeline this app builds, and
    // the cap keeps a forgotten session from growing without bound.
    std::deque<double> blocks_;
    static constexpr std::size_t kMaxBlocks = 14400;

    std::atomic<float> peak_l_{0.f};
    std::atomic<float> peak_r_{0.f};
    std::atomic<float> momentary_{-70.f};
    std::atomic<float> short_term_{-70.f};
    std::atomic<float> integrated_{-70.f};
};

} // namespace ms
