// Preview-quality regression test.
//
// The preview composites at a reduced scale while you edit; the project canvas
// and delivery stay at full size. This checks the engine end of that setting:
// the reported frame size, the produced frame bytes, the project metadata, and
// that changing quality actually invalidates the cached frame.
#include "ms_engine.hpp"

#include <cstdio>
#include <vector>

using namespace ms;

static int failures = 0;

static void check(bool cond, const char* what) {
    if (!cond) {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}

int main() {
    Engine& e = Engine::instance();
    if (!e.init(640, 360) || !e.new_project("Preview quality", 640, 360, 30.f)) {
        std::printf("ms_preview_quality_test: engine setup failed\n");
        return 1;
    }

    int w = 0;
    int h = 0;
    check(e.preview_scale() == 1.f, "default preview scale is full");
    e.preview_frame_size(&w, &h);
    check(w == 640 && h == 360, "full quality is the project canvas");
    check(e.render_frame(), "full-quality frame renders");
    check(e.last_frame_bytes() >= static_cast<size_t>(w) * h * 4, "full-quality frame is complete");

    e.set_preview_scale(0.5f);
    e.preview_frame_size(&w, &h);
    check(w == 320 && h == 180, "half quality halves the frame");
    check(e.render_frame(), "half-quality frame renders");
    {
        std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
        check(e.copy_last_frame(px.data(), px.size()), "half-quality frame copies at its own size");
        check(e.last_frame_bytes() >= px.size(), "half-quality frame is complete");
    }
    // Reduced preview must never change the document's canvas: export and the
    // rest of the UI read the project size from here.
    check(e.state().width == 640 && e.state().height == 360,
          "preview state still reports the project canvas");
    const unsigned long long half_signature = e.render_signature();

    e.set_preview_scale(1.f);
    e.preview_frame_size(&w, &h);
    check(w == 640 && h == 360, "full quality restores the canvas");
    check(e.render_signature() != half_signature, "quality change invalidates the preview");
    check(e.render_frame(), "full-quality frame renders again");
    check(e.last_frame_bytes() == static_cast<size_t>(w) * h * 4,
          "restored frame is the canvas size");

    e.set_preview_scale(0.01f);
    check(e.preview_scale() == 0.25f, "preview scale clamps to the floor");
    e.set_preview_scale(1.f);

    if (failures == 0) {
        std::printf("ms_preview_quality_test: ok\n");
        return 0;
    }
    std::printf("ms_preview_quality_test: %d failure(s)\n", failures);
    return 1;
}
