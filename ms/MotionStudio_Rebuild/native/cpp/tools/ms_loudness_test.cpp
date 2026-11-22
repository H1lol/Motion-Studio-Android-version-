/**
 * Loudness meter checks on synthetic signals — no media, no device.
 *
 * A -20 dBFS stereo 1 kHz sine is the reference: BS.1770 sums both channel
 * energies, so steady state reads -20.69 LUFS. The meter also has to report
 * silence at the floor (not -inf), keep mono in both channels, and clear on
 * reset so a seek cannot carry an old reading into a new section.
 */
#include "ms_loudness.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++failures;
}

constexpr double kPi = 3.14159265358979323846;

} // namespace

int main() {
    const int rate = 48000;
    const double inc = 2.0 * kPi * 1000.0 / rate;

    // A -20 dBFS stereo 1 kHz tone, 3 s, fed in 4096-frame blocks.
    ms::LoudnessMeter meter;
    meter.set_sample_rate(rate);
    {
        std::vector<float> buf(4096 * 2);
        double phase = 0.0;
        const int blocks = (rate * 3) / 4096 + 1;
        for (int b = 0; b < blocks; ++b) {
            for (size_t i = 0; i < 4096; ++i) {
                const float s = static_cast<float>(0.1 * std::sin(phase));
                phase += inc;
                buf[i * 2] = s;
                buf[i * 2 + 1] = s;
            }
            meter.process(buf.data(), 4096, 2);
        }
    }
    const ms::LoudnessMeter::Reading r = meter.read();
    check(std::fabs(r.peak_l - 0.1f) < 2e-3f, "peak tracks the tone amplitude");
    check(std::fabs(r.peak_r - 0.1f) < 2e-3f, "both peaks track the tone");
    check(std::fabs(r.momentary + 20.69f) < 1.5f, "momentary reads about -20.7 LUFS");
    check(std::fabs(r.short_term + 20.69f) < 1.5f, "short-term reads about -20.7 LUFS");
    check(std::fabs(r.integrated + 20.69f) < 1.5f, "integrated reads about -20.7 LUFS");
    check(r.integrated <= r.momentary + 0.01f || std::fabs(r.integrated - r.momentary) < 0.5f,
          "integrated and momentary agree on a steady tone");

    // Twice the amplitude is six dB louder.
    ms::LoudnessMeter louder;
    louder.set_sample_rate(rate);
    {
        std::vector<float> buf(4096 * 2);
        double phase = 0.0;
        for (int b = 0; b < 10; ++b) {
            for (size_t i = 0; i < 4096; ++i) {
                const float s = static_cast<float>(0.2 * std::sin(phase));
                phase += inc;
                buf[i * 2] = s;
                buf[i * 2 + 1] = s;
            }
            louder.process(buf.data(), 4096, 2);
        }
    }
    const ms::LoudnessMeter::Reading lr = louder.read();
    check(std::fabs((lr.integrated - r.integrated) - 6.02f) < 0.6f,
          "doubling amplitude adds about 6 LU");

    // Mono input feeds both meter channels rather than only the left.
    ms::LoudnessMeter mono;
    mono.set_sample_rate(rate);
    {
        std::vector<float> buf(1000);
        for (int b = 0; b < 20; ++b) {
            for (size_t i = 0; i < buf.size(); ++i)
                buf[i] = static_cast<float>(0.2 * std::sin(static_cast<double>(i) * inc));
            mono.process(buf.data(), buf.size(), 1);
        }
    }
    const ms::LoudnessMeter::Reading mr = mono.read();
    check(std::fabs(mr.peak_l - mr.peak_r) < 1e-6f, "mono feeds both channels");
    check(mr.momentary > -30.f, "mono produces a real loudness reading");

    // Silence sits at the floor, not negative infinity.
    ms::LoudnessMeter quiet;
    quiet.set_sample_rate(rate);
    {
        std::vector<float> zeros(4800 * 2, 0.f);
        for (int b = 0; b < 5; ++b) quiet.process(zeros.data(), 4800, 2);
    }
    const ms::LoudnessMeter::Reading q = quiet.read();
    check(q.momentary <= -69.f && q.momentary >= -70.f, "silence reads at the floor");
    check(q.integrated <= -69.f, "silent integrated reads at the floor");
    check(q.peak_l == 0.f && q.peak_r == 0.f, "silence has no peak");

    // Reset must clear the peaks and the integrated reading.
    meter.reset();
    const ms::LoudnessMeter::Reading after = meter.read();
    check(after.peak_l == 0.f && after.peak_r == 0.f, "reset clears peaks");
    check(after.integrated <= -69.f && after.momentary <= -69.f, "reset clears loudness");

    // A different rate rebuilds the filters instead of reusing 48k coefficients:
    // a 44.1 kHz meter should still read a 1 kHz tone near -20.7 LUFS.
    ms::LoudnessMeter other;
    other.set_sample_rate(44100);
    {
        const double inc44 = 2.0 * kPi * 1000.0 / 44100.0;
        std::vector<float> buf(4410 * 2);
        double phase = 0.0;
        for (int b = 0; b < 20; ++b) {
            for (size_t i = 0; i < 4410; ++i) {
                const float s = static_cast<float>(0.1 * std::sin(phase));
                phase += inc44;
                buf[i * 2] = s;
                buf[i * 2 + 1] = s;
            }
            other.process(buf.data(), 4410, 2);
        }
    }
    const ms::LoudnessMeter::Reading orr = other.read();
    check(std::fabs(orr.integrated + 20.69f) < 1.5f, "44.1 kHz meter reads the same tone");

    if (failures == 0) {
        std::printf("ms_loudness_test: all checks passed\n");
        return 0;
    }
    std::printf("ms_loudness_test: %d failure(s)\n", failures);
    return 1;
}
