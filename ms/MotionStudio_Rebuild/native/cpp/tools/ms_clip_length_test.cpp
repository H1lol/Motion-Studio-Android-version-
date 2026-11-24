// Regression test for clip length / project length.
//
// The editor used to advertise every project as 15 s no matter what it held,
// so a clip dropped past that point still left the saved document at 15 s and
// reopened short. Two rules are checked here:
//   * a clip over the 15 s floor grows the project (timeline and saved doc),
//   * a short clip does not shrink the floor back.
#include "ms_engine.hpp"
#include "ms_project.hpp"

#include <cmath>
#include <cstdio>
#include <string>

using namespace ms;

static int failures = 0;

static void check(bool cond, const char* what) {
    if (!cond) {
        std::printf("FAIL %s\n", what);
        ++failures;
    } else {
        std::printf("ok: %s\n", what);
    }
}

static bool near(TimeSec a, TimeSec b) { return std::fabs(a - b) < 1e-6; }

int main() {
    Engine& e = Engine::instance();
    if (!e.init(640, 360)) {
        std::printf("ms_clip_length_test: engine init failed\n");
        return 1;
    }
    if (!e.new_project("Clip length", 640, 360, 30.f)) {
        std::printf("ms_clip_length_test: new_project failed\n");
        return 1;
    }

    const auto& tracks = e.project().tracks();
    check(tracks.size() == 1, "a new project starts with one picture track");
    if (tracks.empty()) return 1;
    const TrackId track = tracks[0].id;

    check(near(e.project().sequence_end(), 15.0), "a new project is 15 s long");

    const ClipId short_clip = e.add_clip(track, 0, 0.0, 3.0);
    check(short_clip != 0, "short clip added");
    check(near(e.project().sequence_end(), 15.0), "a clip inside 15 s keeps the floor");

    const ClipId long_clip = e.add_clip(track, 0, 20.0, 4.0);
    check(long_clip != 0, "long clip added");
    check(e.project().sequence_end() >= 24.0 - 1e-6, "a clip past 15 s grows the sequence");
    check(e.project().meta().duration >= 24.0 - 1e-6, "the project metadata grows with it");

    // The growth has to survive the save/reopen round trip, which is where the
    // old code lost it: the document still declared 15 s.
    const std::string json = e.project_to_json();
    ProjectSession reopened;
    check(reopened.load_from_json(json).ok(), "project reloads from JSON");
    check(reopened.sequence_end() >= 24.0 - 1e-6, "the long clip survives a reload");
    check(reopened.meta().duration >= 24.0 - 1e-6, "the saved duration is the longer one");

    if (failures) {
        std::printf("ms_clip_length_test: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("ms_clip_length_test PASSED\n");
    return 0;
}
