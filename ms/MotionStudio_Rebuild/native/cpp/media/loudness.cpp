#include "ms_loudness.hpp"

#include <algorithm>
#include <cmath>

namespace ms {
namespace {

constexpr double kPi = 3.14159265358979323846;
// BS.1770 constants: the offset that maps mean-square to LUFS.
constexpr double kLoudnessOffset = -0.691;
constexpr double kSilence = -70.0;

void high_shelf(LoudnessBiquad& out, double rate) {
    // Stage 1 of the K-weighting filter, from the BS.1770 coefficient formulas.
    const double f0 = 1681.974450955533;
    const double G = 3.999843853973347;
    const double Q = 0.7071752369554196;
    const double K = std::tan(kPi * f0 / rate);
    const double Vh = std::pow(10.0, G / 20.0);
    const double Vb = std::pow(Vh, 0.4996667741545416);
    const double a0 = 1.0 + K / Q + K * K;
    out.b0 = static_cast<float>((Vh + Vb * K / Q + K * K) / a0);
    out.b1 = static_cast<float>(2.0 * (K * K - Vh) / a0);
    out.b2 = static_cast<float>((Vh - Vb * K / Q + K * K) / a0);
    out.a1 = static_cast<float>(2.0 * (K * K - 1.0) / a0);
    out.a2 = static_cast<float>((1.0 - K / Q + K * K) / a0);
}

void high_pass(LoudnessBiquad& out, double rate) {
    // Stage 2: the ~38 Hz high-pass in the K-weighting chain.
    const double f0 = 38.13547087602444;
    const double Q = 0.5003270373238773;
    const double K = std::tan(kPi * f0 / rate);
    const double a0 = 1.0 + K / Q + K * K;
    out.b0 = 1.f;
    out.b1 = -2.f;
    out.b2 = 1.f;
    out.a1 = static_cast<float>(2.0 * (K * K - 1.0) / a0);
    out.a2 = static_cast<float>((1.0 - K / Q + K * K) / a0);
}

} // namespace

double LoudnessMeter::loudness_of(double energy) {
    if (energy <= 1e-12) return kSilence;
    return std::max(kSilence, kLoudnessOffset + 10.0 * std::log10(energy));
}

void LoudnessMeter::set_sample_rate(int rate) {
    rate_ = rate > 0 ? rate : 48000;
    block_frames_ = static_cast<std::size_t>(std::max(1, rate_ / 10));
    reset();
}

void LoudnessMeter::reset() {
    block_pos_ = 0;
    block_sum_ = 0.0;
    blocks_.clear();
    shelf_l_ = LoudnessBiquad{};
    shelf_r_ = LoudnessBiquad{};
    hp_l_ = LoudnessBiquad{};
    hp_r_ = LoudnessBiquad{};
    high_shelf(shelf_l_, rate_);
    high_shelf(shelf_r_, rate_);
    high_pass(hp_l_, rate_);
    high_pass(hp_r_, rate_);
    peak_l_ = 0.f;
    peak_r_ = 0.f;
    momentary_ = -70.f;
    short_term_ = -70.f;
    integrated_ = -70.f;
}

void LoudnessMeter::finish_block() {
    const double energy = block_sum_ / static_cast<double>(block_frames_);
    block_sum_ = 0.0;
    block_pos_ = 0;
    blocks_.push_back(energy);
    while (blocks_.size() > kMaxBlocks) blocks_.pop_front();

    auto window = [&](std::size_t n) {
        double sum = 0.0;
        std::size_t used = 0;
        for (auto it = blocks_.rbegin(); it != blocks_.rend() && used < n; ++it, ++used)
            sum += *it;
        return used == 0 ? kSilence
                         : static_cast<float>(loudness_of(sum / static_cast<double>(used)));
    };
    momentary_ = window(4);   // 400 ms
    short_term_ = window(30); // 3 s

    // Integrated: absolute gate first, then the relative gate 10 dB below the
    // mean of what passed. Both gates are required by EBU R128 for a reading
    // that does not drift with silence.
    std::vector<double> gated;
    gated.reserve(blocks_.size());
    for (double e : blocks_)
        if (loudness_of(e) > -70.0) gated.push_back(e);
    if (gated.empty()) {
        integrated_ = -70.f;
        return;
    }
    double mean = 0.0;
    for (double e : gated) mean += e;
    mean /= static_cast<double>(gated.size());
    const double relative = loudness_of(mean) - 10.0;
    double sum = 0.0;
    std::size_t used = 0;
    for (double e : gated) {
        if (loudness_of(e) <= relative) continue;
        sum += e;
        ++used;
    }
    integrated_ = used == 0 ? -70.f
                            : static_cast<float>(loudness_of(sum / static_cast<double>(used)));
}

void LoudnessMeter::process(const float* interleaved, std::size_t frames, int channels) {
    if (!interleaved || frames == 0) return;
    const int ch = channels > 0 ? channels : 2;
    for (std::size_t i = 0; i < frames; ++i) {
        const float l = interleaved[i * static_cast<std::size_t>(ch)];
        const float r = ch > 1 ? interleaved[i * static_cast<std::size_t>(ch) + 1] : l;
        peak_l_ = std::max(peak_l_.load(), std::fabs(l));
        peak_r_ = std::max(peak_r_.load(), std::fabs(r));
        // BS.1770 sums the channel energies (both channels weigh 1.0).
        const float kl = hp_l_.run(shelf_l_.run(l));
        const float kr = hp_r_.run(shelf_r_.run(r));
        block_sum_ += static_cast<double>(kl) * kl + static_cast<double>(kr) * kr;
        if (++block_pos_ >= block_frames_) finish_block();
    }
}

LoudnessMeter::Reading LoudnessMeter::read() const {
    Reading r;
    r.peak_l = peak_l_.load();
    r.peak_r = peak_r_.load();
    r.momentary = momentary_.load();
    r.short_term = short_term_.load();
    r.integrated = integrated_.load();
    return r;
}

} // namespace ms
