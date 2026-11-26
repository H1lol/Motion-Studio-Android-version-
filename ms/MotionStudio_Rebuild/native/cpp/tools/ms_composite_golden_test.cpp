// Golden-image test for the whole compositor, not one effect.
//
// ms_render_golden_test covers the CPU effect functions in isolation. This one
// goes through Engine::render_frame(), so it captures everything between the
// timeline and the delivered RGBA frame: slate generation, mask geometry and
// feather, per-clip grade, the master grade, the clip effect stack and preview
// scaling. Those are the paths a user sees on every frame, and none of them had
// a pixel assertion before.
//
//   ms_composite_golden_test            compare against tests/golden
//   ms_composite_golden_test --write    rewrite the goldens from this build
#include "ms_engine.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#ifndef MS_GOLDEN_DIR
#define MS_GOLDEN_DIR "tests/golden"
#endif

using namespace ms;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL %s\n", what.c_str());
        ++failures;
    }
}

bool write_pam(const std::string& path, const uint8_t* data, int w, int h) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f, "P7\nWIDTH %d\nHEIGHT %d\nDEPTH 4\nMAXVAL 255\n"
                    "TUPLTYPE RGB_ALPHA\nENDHDR\n", w, h);
    const size_t bytes = static_cast<size_t>(w) * h * 4;
    const size_t wrote = std::fwrite(data, 1, bytes, f);
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
        else if (std::strncmp(line, "ENDHDR", 6) == 0) { header_ok = true; break; }
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

struct Frame {
    std::vector<uint8_t> pixels;
    int w = 0, h = 0;
    bool ok = false;
};

struct Mismatch {
    int max_diff = 0;
    double mean_diff = 0.0;
    int at_x = -1, at_y = -1, at_channel = -1;
};

Mismatch compare(const Frame& got, const std::vector<uint8_t>& want, int w, int h) {
    Mismatch m;
    long long sum = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            for (int c = 0; c < 4; ++c) {
                const size_t i = (static_cast<size_t>(y) * w + x) * 4 + c;
                const int d = std::abs(static_cast<int>(got.pixels[i]) - static_cast<int>(want[i]));
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

constexpr int kMaxChannelDiff = 8;
constexpr double kMaxMeanDiff = 0.75;

}  // namespace

int main(int argc, char** argv) {
    const bool write_mode = argc > 1 && std::strcmp(argv[1], "--write") == 0;
    const std::string dir = MS_GOLDEN_DIR;

    Engine& e = Engine::instance();
    if (!e.init(640, 360)) {
        std::printf("ms_composite_golden_test: engine init failed\n");
        return 1;
    }

    // Each case starts from a fresh small project so state cannot leak between
    // them; 96x54 keeps the goldens tiny and the render instant.
    auto build = [&](const std::function<void(TrackId, ClipId)>& setup) -> Frame {
        // Master grade and preview scale are engine state, not document state:
        // reset them so one case cannot tint the next.
        e.clear_grade();
        e.set_preview_scale(1.f);
        if (!e.new_project("golden", 96, 54, 30.f)) return {};
        const auto& tracks = e.project().tracks();
        if (tracks.empty()) return {};
        const TrackId track = tracks[0].id;
        const ClipId clip = e.add_clip(track, 0, 0.0, 10.0);
        if (clip == 0) return {};
        setup(track, clip);
        e.seek(1.0);
        if (!e.render_frame()) return {};
        int w = 0, h = 0;
        e.preview_frame_size(&w, &h);
        Frame f;
        f.w = w;
        f.h = h;
        f.pixels.assign(e.last_frame_bytes(), 0);
        f.ok = w > 0 && h > 0 && e.copy_last_frame(f.pixels.data(), f.pixels.size());
        return f;
    };

    struct Case {
        const char* name;
        std::function<void(TrackId, ClipId)> setup;
    };

    const std::vector<Case> cases = {
        {"composite_slate", [](TrackId, ClipId) {}},
        {"composite_mask", [&e](TrackId t, ClipId c) {
             e.set_clip_mask(t, c, 1, 0.25f, 0.25f, 0.5f, 0.5f, 0.04f, false);
         }},
        {"composite_clip_grade", [&e](TrackId t, ClipId c) {
             e.set_clip_grade(t, c, 0.5f, 1.4f, 1.3f, 0.2f, 0.05f, 1.1f, 1.05f);
         }},
        {"composite_master_grade", [&e](TrackId, ClipId) {
             e.set_grade(0.4f, 1.2f, 1.1f, 0.15f, 0.02f, 1.05f, 1.0f);
         }},
        {"composite_half_scale", [&e](TrackId, ClipId) {
             e.set_preview_scale(0.5f);
         }},
    };

    // The plain slate is the control: every other case has to move pixels away
    // from it, otherwise its golden would happily lock in a no-op.
    const Frame baseline = build(cases[0].setup);
    if (!baseline.ok) {
        std::printf("ms_composite_golden_test: engine could not render a baseline frame\n");
        return 1;
    }

    for (size_t index = 0; index < cases.size(); ++index) {
        const Case& c = cases[index];
        const Frame f = build(c.setup);
        if (!f.ok) {
            check(false, std::string(c.name) + ": engine produced no frame");
            continue;
        }
        if (index > 0 && f.pixels == baseline.pixels) {
            check(false, std::string(c.name) + ": the edit changes no pixels");
            continue;
        }
        const std::string path = dir + "/" + c.name + ".pam";
        if (write_mode) {
            check(write_pam(path, f.pixels.data(), f.w, f.h),
                  std::string(c.name) + ": wrote " + path);
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
        if (gw != f.w || gh != f.h) {
            std::printf("FAIL %s: frame is %dx%d, golden %dx%d\n", c.name, f.w, f.h, gw, gh);
            ++failures;
            continue;
        }
        const Mismatch m = compare(f, want, gw, gh);
        if (m.max_diff > kMaxChannelDiff || m.mean_diff > kMaxMeanDiff) {
            std::printf("FAIL %s: max channel diff %d at (%d,%d) channel %d, mean %.3f "
                        "(limits %d / %.2f)\n",
                        c.name, m.max_diff, m.at_x, m.at_y, m.at_channel, m.mean_diff,
                        kMaxChannelDiff, kMaxMeanDiff);
            ++failures;
        } else {
            std::printf("ok: %-22s %dx%d  max %d  mean %.3f\n",
                        c.name, f.w, f.h, m.max_diff, m.mean_diff);
        }
    }

    e.set_preview_scale(1.f);
    if (write_mode) {
        if (failures) {
            std::printf("ms_composite_golden_test: %d failure(s) while writing\n", failures);
            return 1;
        }
        std::printf("ms_composite_golden_test: wrote %zu goldens to %s\n",
                    cases.size(), dir.c_str());
        return 0;
    }
    if (failures) {
        std::printf("ms_composite_golden_test: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("ms_composite_golden_test PASSED (%zu frames)\n", cases.size());
    return 0;
}
