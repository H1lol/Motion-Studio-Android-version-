// Viewport picking regression test.
//
// Tapping the preview selects the layer drawn under the finger, so the hit test
// has to agree with the compositor: the layer's box follows its scale and
// offset, the topmost track wins where they overlap, and control layers that
// draw nothing (nulls, adjustment layers) are never picked.
#include "ms_engine.hpp"

#include <cstdio>

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
    if (!e.init(320, 180) || !e.new_project("Hit test", 320, 180, 30.f)) {
        std::printf("ms_hit_test: engine setup failed\n");
        return 1;
    }
    const auto& tracks = e.project().tracks();
    if (tracks.empty()) {
        std::printf("ms_hit_test: default project has no track\n");
        return 1;
    }
    const TrackId base_track = tracks[0].id;
    const ClipId base = e.add_clip(base_track, 0, 0.0, 4.0);
    check(base != 0, "base layer added");

    TrackId hit_track = 0;
    ClipId hit_clip = 0;
    check(e.preview_hit_test(0.5f, 0.5f, &hit_track, &hit_clip),
          "a full-frame layer is picked at the centre");
    check(hit_track == base_track && hit_clip == base, "the pick names the base layer");
    check(e.preview_hit_test(0.02f, 0.02f, &hit_track, &hit_clip),
          "a full-frame layer reaches the corner");

    // ---- The box follows the layer, not the frame --------------------------
    check(e.add_keyframe(base_track, base, 1 /* scale */, 0.0, 0.5), "scale keyed");
    check(e.preview_hit_test(0.5f, 0.5f, &hit_track, &hit_clip),
          "a half-size layer is still picked at the centre");
    check(!e.preview_hit_test(0.02f, 0.02f, &hit_track, &hit_clip),
          "a point outside the half-size box misses");

    // 0.5 scale is 160 px wide; +80 px moves the box to x 160..320 (0.5..1.0).
    check(e.add_keyframe(base_track, base, 2 /* pos_x */, 0.0, 80.0), "position keyed");
    check(e.preview_hit_test(0.7f, 0.5f, &hit_track, &hit_clip),
          "the moved box is picked where it now sits");
    check(!e.preview_hit_test(0.2f, 0.5f, &hit_track, &hit_clip),
          "the old position no longer hits");

    // ---- Topmost layer wins ------------------------------------------------
    const TrackId top_track = e.add_track("Overlay", 1 /* Overlay */);
    check(top_track != 0, "overlay track added");
    const ClipId top = e.add_clip(top_track, 0, 0.0, 4.0);
    check(top != 0, "overlay layer added");
    check(e.add_keyframe(top_track, top, 1 /* scale */, 0.0, 0.25), "overlay scale keyed");
    // 0.25 of 320x180 is 80x45, centred: x 0.375..0.625.
    check(e.preview_hit_test(0.5f, 0.5f, &hit_track, &hit_clip),
          "the centre is covered by the overlay");
    check(hit_track == top_track && hit_clip == top, "the topmost track wins the pick");
    check(e.preview_hit_test(0.7f, 0.5f, &hit_track, &hit_clip),
          "outside the overlay the layer below is picked");
    check(hit_track == base_track && hit_clip == base, "the pick falls through to the base layer");

    // A null on top of the overlay has no pixels, so it can never be the pick.
    const ClipId null_id = e.add_null_clip(top_track, 0.0, 4.0);
    check(null_id != 0, "null object added");
    check(e.preview_hit_test(0.5f, 0.5f, &hit_track, &hit_clip) && hit_clip == top,
          "a null object is never picked");
    e.remove_clip(top_track, null_id);

    // ---- Guards ------------------------------------------------------------
    check(!e.preview_hit_test(-0.1f, 0.5f, &hit_track, &hit_clip),
          "a point left of the frame misses");
    check(!e.preview_hit_test(1.5f, 0.5f, &hit_track, &hit_clip),
          "a point right of the frame misses");
    e.seek(6.0);
    check(!e.preview_hit_test(0.5f, 0.5f, &hit_track, &hit_clip),
          "a playhead past every layer picks nothing");
    e.seek(1.0);

    if (failures == 0) {
        std::printf("ms_hit_test: all checks passed\n");
        return 0;
    }
    std::printf("ms_hit_test: %d failure(s)\n", failures);
    return 1;
}
