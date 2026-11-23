// Layer parenting and null-object regression test.
//
// Both released motion editors this project follows headline the same model:
// a layer can have one parent, chains are allowed, cycles are not, and an
// invisible null object is the usual control handle. This checks that behaviour
// at the engine level rather than trusting the UI.
#include "ms_engine.hpp"
#include "ms_image.hpp"
#include "ms_project.hpp"
#include "ms_timeline_player.hpp"

#include <cmath>
#include <cstdio>
#include <string>

using namespace ms;

static int failures = 0;

static void check(bool cond, const char* what) {
    if (!cond) {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}

static int red_pixels(const ImageBuffer& img) {
    if (!img.valid()) return 0;
    int n = 0;
    for (int y = 0; y < img.height(); ++y) {
        const uint8_t* row = img.data() + static_cast<size_t>(y) * img.stride();
        for (int x = 0; x < img.width(); ++x) {
            const uint8_t* p = row + x * 4;
            if (p[0] > 60 && p[0] > p[1] + 20) ++n;
        }
    }
    return n;
}

static int differing_pixels(const ImageBuffer& a, const ImageBuffer& b) {
    if (!a.valid() || !b.valid() || a.width() != b.width() || a.height() != b.height())
        return -1;
    int n = 0;
    for (int y = 0; y < a.height(); ++y) {
        const uint8_t* ra = a.data() + static_cast<size_t>(y) * a.stride();
        const uint8_t* rb = b.data() + static_cast<size_t>(y) * b.stride();
        for (int x = 0; x < a.width(); ++x) {
            for (int c = 0; c < 4; ++c) {
                if (std::abs(static_cast<int>(ra[x * 4 + c]) - rb[x * 4 + c]) > 8) {
                    ++n;
                    break;
                }
            }
        }
    }
    return n;
}

static const ClipInfo* find_clip(const ProjectSession& project, ClipId id) {
    for (const auto& t : project.tracks())
        for (const auto& c : t.clips)
            if (c.id == id) return &c;
    return nullptr;
}

int main() {
    Engine& e = Engine::instance();
    if (!e.init(320, 180) || !e.new_project("Parent test", 320, 180, 30.f)) {
        std::printf("ms_parent_test: engine setup failed\n");
        return 1;
    }
    const auto& tracks = e.project().tracks();
    if (tracks.empty()) {
        std::printf("ms_parent_test: default project has no track\n");
        return 1;
    }
    const TrackId media_track = tracks[0].id;
    const TrackId control_track = e.add_track("Controls", 1 /* Overlay */);
    check(control_track != 0, "control track added");

    const ClipId child = e.add_clip(media_track, 0, 0.0, 2.0);
    check(child != 0, "child clip added");
    TimelinePlayer player(&e.project());

    // A half-size child keeps the inherited scale measurable: 0.5 gives a
    // quarter-frame of pixels, and 0.5 x 2 should fill the frame again.
    check(e.add_keyframe(media_track, child, 1 /* scale */, 0.0, 0.5),
          "child scale keyed");

    ImageBuffer small;
    check(player.render_frame(1.0, small).ok(), "baseline frame renders");
    const int small_pixels = red_pixels(small);
    check(small_pixels > 1000 && small_pixels < 320 * 180 / 2,
          "a half-size clip covers roughly a quarter frame");

    // ---- Null object is invisible -----------------------------------------
    const ClipId null_id = e.add_null_clip(control_track, 0.0, 2.0);
    check(null_id != 0, "null object added");
    const ClipInfo* nc = find_clip(e.project(), null_id);
    check(nc && nc->is_null, "null clip carries the flag");

    ImageBuffer with_null;
    check(player.render_frame(1.0, with_null).ok(), "frame with a null renders");
    check(differing_pixels(small, with_null) == 0,
          "a null object contributes no pixels");

    // ---- Parenting: translation and scale compose -------------------------
    check(e.set_clip_parent(media_track, child, null_id), "parent linked");
    {
        ClipId read = 99;
        check(e.clip_parent(media_track, child, &read) && read == null_id,
              "parent id reads back");
    }

    check(e.add_keyframe(control_track, null_id, 2 /* pos_x */, 0.0, 0.0) &&
              e.add_keyframe(control_track, null_id, 2, 2.0, 120.0),
          "parent position keyed");
    check(e.add_keyframe(control_track, null_id, 1 /* scale */, 0.0, 1.0) &&
              e.add_keyframe(control_track, null_id, 1, 2.0, 2.0),
          "parent scale keyed");

    ImageBuffer parented;
    check(player.render_frame(1.0, parented).ok(), "parented frame renders");
    const int parented_pixels = red_pixels(parented);
    check(differing_pixels(small, parented) > 1000,
          "parent motion moves the child");
    check(parented_pixels > small_pixels + 1000,
          "parent scale grows the child (0.5 x 2 fills more of the frame)");

    // ---- Refusals ---------------------------------------------------------
    check(!e.set_clip_parent(media_track, child, child), "self-parenting refused");
    check(!e.set_clip_parent(media_track, child, 999999), "unknown parent refused");
    check(!e.set_clip_parent(control_track, null_id, child),
          "a cycle is refused (null cannot parent to its own child)");
    check(e.set_clip_parent(media_track, child, 0), "parent can be cleared");
    check(e.undo(), "clearing the parent is undoable");
    {
        ClipId read = 0;
        check(e.clip_parent(media_track, child, &read) && read == null_id,
              "undo restores the parent link");
    }

    // ---- Persistence ------------------------------------------------------
    const std::string json = e.project_to_json();
    check(json.find("\"parent\": " + std::to_string(null_id)) != std::string::npos,
          "parent id serialized");
    check(json.find("\"null\": true") != std::string::npos, "null flag serialized");
    check(e.load_project_json(json), "project with a rig reloads");
    const ClipInfo* rc = find_clip(e.project(), child);
    const ClipInfo* rn = find_clip(e.project(), null_id);
    check(rc && rc->parent_id == null_id, "parent survives save/load");
    check(rn && rn->is_null, "null flag survives save/load");

    if (failures == 0) std::printf("ms_parent_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
