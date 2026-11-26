// Golden-image regression test for the CPU render path.
//
// Every other native test checks logic: a clip grew, a curve interpolated, a
// node linked. None of them would notice if the compositor started producing
// wrong pixels, which is the one failure a user sees immediately. This test
// renders a fixed scene through a representative set of effects and compares
// the result against a stored reference image.
//
// Goldens are PAM (P7, RGBA8) so they carry alpha, stay uncompressed and can be
// opened by any image tool. Comparison is tolerant: the same code built by a
// different compiler may move a channel by a unit or two, and failing on that
// would make the suite useless in CI. A real regression (colour shift, missing
// alpha, wrong warp) is orders of magnitude larger than the tolerance.
//
//   ms_render_golden_test            compare against tests/golden
//   ms_render_golden_test --write    rewrite the goldens from this build
//
// Rewriting is deliberate: goldens are only meaningful when a human has looked
// at the diff and decided the new pixels are the correct ones.
#include "ms_cpu_effects.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#ifndef MS_GOLDEN_DIR
#define MS_GOLDEN_DIR "tests/golden"
#endif

using namespace ms;
using Params = std::unordered_map<std::string, double>;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL %s\n", what.c_str());
        ++failures;
    }
}

// The reference scene: a gradient disc with soft edges over a square, plus
// bright garbage under alpha 0, so a filter that ignores alpha is visible.
ImageBuffer reference_scene(int w, int h) {
    ImageBuffer img;
    img.allocate(w, h, PixelFormat::RGBA8);
    uint8_t* p = img.data();
    const float cx = w * 0.5f, cy = h * 0.5f, r = std::min(w, h) * 0.34f;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint8_t* o = p + (static_cast<size_t>(y) * w + x) * 4;
            const float d = std::hypot(x - cx, y - cy);
            const float a = d < r ? 1.f : (d < r + 2.f ? (r + 2.f - d) / 2.f : 0.f);
            o[0] = static_cast<uint8_t>(x * 255 / (w - 1));
            o[1] = static_cast<uint8_t>(y * 255 / (h - 1));
            o[2] = static_cast<uint8_t>(40 + (x * y) % 120);
            o[3] = static_cast<uint8_t>(a * 255.f + 0.5f);
            if (a == 0.f) {  // bright garbage, hidden by alpha
                o[0] = 255;
                o[1] = 255;
                o[2] = 255;
            }
        }
    }
    return img;
}

bool write_pam(const std::string& path, const ImageBuffer& img) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f, "P7\nWIDTH %d\nHEIGHT %d\nDEPTH 4\nMAXVAL 255\n"
                    "TUPLTYPE RGB_ALPHA\nENDHDR\n",
                 img.width(), img.height());
    const size_t bytes = static_cast<size_t>(img.width()) * img.height() * 4;
    const size_t wrote = std::fwrite(img.data(), 1, bytes, f);
    std::fclose(f);
    return wrote == bytes;
}

bool read_pam(const std::string& path, int& w, int& h, std::vector<uint8_t>& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    char line[256];
    w = h = 0;
    int depth = 0;
    bool header_ok = false;
    while (std::fgets(line, sizeof(line), f)) {
        if (std::strncmp(line, "WIDTH ", 6) == 0) w = std::atoi(line + 6);
        else if (std::strncmp(line, "HEIGHT ", 7) == 0) h = std::atoi(line + 7);
        else if (std::strncmp(line, "DEPTH ", 6) == 0) depth = std::atoi(line + 6);
        else if (std::strncmp(line, "ENDHDR", 6) == 0) {
            header_ok = true;
            break;
        }
    }
    if (!header_ok || w <= 0 || h <= 0 || depth != 4) {
        std::fclose(f);
        return false;
    }
    out.assign(static_cast<size_t>(w) * h * 4, 0);
    const size_t got = std::fread(out.data(), 1, out.size(), f);
    std::fclose(f);
    return got == out.size();
}

struct Mismatch {
    int max_diff = 0;
    double mean_diff = 0.0;
    int at_x = -1, at_y = -1, at_channel = -1;
};

Mismatch compare(const ImageBuffer& got, const std::vector<uint8_t>& want, int w, int h) {
    Mismatch m;
    long long sum = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            for (int c = 0; c < 4; ++c) {
                const size_t i = (static_cast<size_t>(y) * w + x) * 4 + c;
                const int d = std::abs(static_cast<int>(got.data()[i]) - static_cast<int>(want[i]));
                sum += d;
                if (d > m.max_diff) {
                    m.max_diff = d;
                    m.at_x = x;
                    m.at_y = y;
                    m.at_channel = c;
                }
            }
        }
    }
    const double count = static_cast<double>(w) * h * 4;
    m.mean_diff = count > 0 ? static_cast<double>(sum) / count : 0.0;
    return m;
}

// A channel may move a little between compilers; it may not move like this.
constexpr int kMaxChannelDiff = 8;
constexpr double kMaxMeanDiff = 0.75;

struct Case {
    const char* name;
    const char* effect;   // nullptr = call the dedicated function below
    Params params;
};

}  // namespace

int main(int argc, char** argv) {
    const bool write_mode = argc > 1 && std::strcmp(argv[1], "--write") == 0;
    const std::string dir = MS_GOLDEN_DIR;

    const int w = 96, h = 64;
    const ImageBuffer src = reference_scene(w, h);

    const std::vector<Case> cases = {
        {"blur",         "blur",                  Params{{"radius", 3.0}}},
        {"saturation",   "saturation",            Params{{"amount", 1.8}}},
        {"glow",         "glow",                  Params{{"amount", 0.7}, {"radius", 6.0}}},
        {"chromatic",    "chromatic_aberration",  Params{{"amount", 0.8}}},
        {"wave",         "wave_warp",             Params{{"amplitude", 0.08}}},
        {"kaleidoscope", "kaleidoscope",          Params{{"segments", 5.0}, {"rotation", 12.0}}},
        {"halftone",     "halftone",              Params{{"cell", 6.0}, {"mix", 0.8}}},
        {"vhs",          "vhs",                   Params{{"amount", 0.7}, {"seed", 5.0}}},
        {"twirl",        "twirl",                 Params{{"angle", 40.0}}},
        {"pixelate",     "pixelate",              Params{{"size", 6.0}}},
        {"duotone",      "duotone",               Params{{"mix", 0.8}}},
    };

    for (const auto& c : cases) {
        ImageBuffer out;
        const Error err = cpu_effect_apply(c.effect, src, out, c.params);
        if (!err.ok() || out.width() != w || out.height() != h || !out.data()) {
            check(false, std::string(c.name) + ": effect did not produce a frame: " + err.message + " " + err.detail);
            continue;
        }

        // Determinism is checked on every run, not implied by the golden: two
        // renders of the same frame must be byte-identical.
        {
            ImageBuffer again;
            cpu_effect_apply(c.effect, src, again, c.params);
            const bool same = again.data() && std::memcmp(out.data(), again.data(),
                                 static_cast<size_t>(w) * h * 4) == 0;
            check(same, std::string(c.name) + ": repeated render is identical");
        }

        // A golden regenerated from a broken effect would happily agree with
        // itself, so the effect must also visibly do something.
        {
            const bool changed = std::memcmp(out.data(), src.data(),
                                             static_cast<size_t>(w) * h * 4) != 0;
            check(changed, std::string(c.name) + ": effect changes pixels");
        }

        const std::string path = dir + "/" + c.name + ".pam";
        if (write_mode) {
            check(write_pam(path, out), std::string(c.name) + ": wrote " + path);
            continue;
        }

        int gw = 0, gh = 0;
        std::vector<uint8_t> want;
        if (!read_pam(path, gw, gh, want)) {
            std::printf("FAIL %s: no readable golden at %s (run with --write once)\n",
                        c.name, path.c_str());
            ++failures;
            continue;
        }
        if (gw != w || gh != h) {
            check(false, std::string(c.name) + ": golden size does not match the scene");
            continue;
        }
        const Mismatch m = compare(out, want, w, h);
        const bool ok = m.max_diff <= kMaxChannelDiff && m.mean_diff <= kMaxMeanDiff;
        if (!ok) {
            std::printf("FAIL %s: max channel diff %d at (%d,%d) channel %d, mean %.3f "
                        "(limits %d / %.2f)\n",
                        c.name, m.max_diff, m.at_x, m.at_y, m.at_channel, m.mean_diff,
                        kMaxChannelDiff, kMaxMeanDiff);
            ++failures;
        } else {
            std::printf("ok: %-12s max %d  mean %.3f\n", c.name, m.max_diff, m.mean_diff);
        }
    }

    if (write_mode) {
        if (failures) {
            std::printf("ms_render_golden_test: %d failure(s) while writing\n", failures);
            return 1;
        }
        std::printf("ms_render_golden_test: wrote %zu goldens to %s\n", cases.size(), dir.c_str());
        return 0;
    }
    if (failures) {
        std::printf("ms_render_golden_test: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("ms_render_golden_test PASSED (%zu effects)\n", cases.size());
    return 0;
}
