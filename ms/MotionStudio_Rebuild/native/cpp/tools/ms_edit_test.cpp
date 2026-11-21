// Editing + effect-stack regression test: move, trim, ripple, tracks, effect
// persistence through the project JSON, enable/disable and removal.
#include "ms_engine.hpp"
#include "ms_project.hpp"
#include "ms_text.hpp"

#include <cmath>
#include <cstdio>
#include <string>
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
    if (!e.init(640, 360)) {
        std::printf("ms_edit_test: engine init failed\n");
        return 1;
    }
    if (!e.new_project("Edit test", 640, 360, 30.f)) {
        std::printf("ms_edit_test: new_project failed\n");
        return 1;
    }

    const auto& tracks = e.project().tracks();
    check(!tracks.empty(), "default project has a track");
    if (tracks.empty()) return 1;
    const TrackId track = tracks[0].id;

    ClipInfo c;
    c.name = "Clip A";
    c.start = 1.0;
    c.duration = 2.0;
    c.source_in = 0.0;
    c.source_out = 2.0;
    c.motion.ensure_defaults();
    const ClipId clip = e.project().add_clip(track, c);
    check(clip != 0, "clip added");

    check(e.move_clip(track, clip, 3.0), "move clip");
    check(std::fabs(e.project().find_clip(track, clip)->start - 3.0) < 1e-6, "move applied");

    check(e.trim_clip(track, clip, 3.0, 1.5), "trim clip");
    check(std::fabs(e.project().find_clip(track, clip)->duration - 1.5) < 1e-6, "trim applied");

    // Effects
    check(e.add_effect(track, clip, "gaussian_blur") == 0, "effect added at index 0");
    check(e.set_effect_param(track, clip, 0, "radius", 8.f), "param set");
    const ClipInfo* fx = e.project().find_clip(track, clip);
    check(fx && fx->motion.effects.size() == 1, "one effect on clip");
    {
        auto it = fx ? fx->motion.effects[0].params.find("radius")
                     : std::unordered_map<std::string, double>::const_iterator{};
        check(fx && it != fx->motion.effects[0].params.end() && std::fabs(it->second - 8.0) < 1e-4,
              "param stored");
    }
    check(e.set_effect_enabled(track, clip, 0, false), "effect disabled");
    check(e.add_effect(track, clip, "vignette") == 1, "second effect at index 1");

    // Stack reordering: the effect at `from` lands at `to`.
    check(e.add_effect(track, clip, "sepia") == 2, "third effect at index 2");
    check(e.move_effect(track, clip, 2, 0), "move sepia to the top");
    check(e.project().find_clip(track, clip)->motion.effects[0].id == "sepia", "sepia first");
    check(e.project().find_clip(track, clip)->motion.effects[1].id == "gaussian_blur",
          "blur pushed down one");
    check(e.move_effect(track, clip, 0, 2), "move sepia back to the bottom");
    check(e.project().find_clip(track, clip)->motion.effects[2].id == "sepia", "sepia last");
    check(!e.move_effect(track, clip, 3, 0), "out of range move rejected");
    check(!e.move_effect(track, clip, 0, -1), "negative target rejected");
    check(!e.move_effect(track, clip + 999, 0, 1), "unknown clip rejected");
    check(e.project().find_clip(track, clip)->motion.effects.size() == 3, "no effect lost");
    e.project().undo();
    check(e.project().find_clip(track, clip)->motion.effects[0].id == "sepia",
          "undo puts the old order back");
    check(e.move_effect(track, clip, 0, 0), "no-op move succeeds");
    // Drop the sepia that was only there for the reorder checks; the round-trip
    // assertions below expect the original blur-then-vignette order.
    e.remove_effect(track, clip, 0);
    check(e.project().find_clip(track, clip)->motion.effects.size() == 2, "back to two effects");
    check(e.project().find_clip(track, clip)->motion.effects[0].id == "gaussian_blur",
          "blur back in front");
    check(e.project().find_clip(track, clip)->motion.effects[1].id == "vignette",
          "vignette back at the end");

    // Round-trip: effects must survive save/load.
    const std::string json = e.project_to_json();
    check(json.find("\"effects\"") != std::string::npos, "effects serialized");
    check(e.load_project_json(json), "project reloads");
    const TrackInfo& rt = e.project().tracks()[0];
    check(rt.clips.size() == 1, "one clip after reload");
    check(rt.clips[0].motion.effects.size() == 2, "both effects reload");
    check(rt.clips[0].motion.effects[0].id == "gaussian_blur", "effect id preserved");
    check(!rt.clips[0].motion.effects[0].enabled, "disabled flag preserved");
    {
        const auto& params = rt.clips[0].motion.effects[0].params;
        auto it = params.find("radius");
        check(it != params.end() && std::fabs(it->second - 8.0) < 1e-4,
              "param preserved through json");
    }

    // Undo must be able to walk the edits back.
    const double before = e.project().find_clip(track, clip)->start;
    e.move_clip(track, clip, 7.0);
    check(std::fabs(e.project().find_clip(track, clip)->start - 7.0) < 1e-6, "move to 7");
    e.project().undo();
    check(std::fabs(e.project().find_clip(track, clip)->start - before) < 1e-6, "undo restores start");

    // Effect parameter automation: a curve per (effect, parameter), in
    // clip-local seconds, overriding the static value while it has keys.
    {
        check(e.add_effect_keyframe(track, clip, 0, "radius", 0.0, 0.0),
              "first radius key");
        check(e.add_effect_keyframe(track, clip, 0, "radius", 2.0, 20.0),
              "second radius key");
        check(e.effect_keyframe_count(track, clip, 0, "radius") == 2, "two radius keys");
        const ClipInfo* ac = e.project().find_clip(track, clip);
        check(ac && ac->motion.effects[0].animated("radius"), "radius reports animated");
        check(ac && std::fabs(ac->motion.effects[0].param_at("radius", 1.0) - 10.0) < 1e-4,
              "curve drives the value at the midpoint");
        double resting_radius = -1.0;
        if (ac) {
            const auto& resting = ac->motion.effects[0].params;
            const auto resting_it = resting.find("radius");
            if (resting_it != resting.end()) resting_radius = resting_it->second;
        }
        check(std::fabs(resting_radius - 8.0) < 1e-4, "static value stays the resting value");

        float curve[5] = {0.f, 0.f, 0.f, 0.f, 0.f};
        check(e.sample_effect_param(track, clip, 0, "radius", 0.0, 2.0, curve, 5),
              "curve samples");
        check(std::fabs(curve[0] - 0.0f) < 1e-3f && std::fabs(curve[4] - 20.0f) < 1e-3f,
              "samples hit both ends");

        // A second tap at the same instant retimes/revalues instead of stacking.
        check(e.add_effect_keyframe(track, clip, 0, "radius", 2.0, 4.0),
              "same-instant key accepted");
        check(e.effect_keyframe_count(track, clip, 0, "radius") == 2,
              "same instant does not duplicate");
        double kt = 0.0, kv = 0.0;
        int ki = 0;
        check(e.effect_keyframe_at(track, clip, 0, "radius", 1, &kt, &kv, &ki), "key readable");
        check(std::fabs(kv - 4.0) < 1e-6, "same-instant add replaced the value");
        check(e.set_effect_keyframe(track, clip, 0, "radius", 1, 2.0, 20.0, 1),
              "key retimed and eased");
        check(e.effect_keyframe_at(track, clip, 0, "radius", 1, &kt, &kv, &ki) && ki == 1,
              "interpolation stored");

        // Curves travel with the document.
        const std::string automation_json = e.project_to_json();
        check(automation_json.find("\"curves\"") != std::string::npos, "curves serialized");
        check(e.load_project_json(automation_json), "reload with curves");
        check(e.effect_keyframe_count(track, clip, 0, "radius") == 2, "keys reload");
        check(e.effect_keyframe_at(track, clip, 0, "radius", 0, &kt, &kv, &ki) &&
                  std::fabs(kv) < 1e-6,
              "reloaded key keeps its value");

        check(e.clear_effect_keyframes(track, clip, 0, "radius"), "keys cleared");
        check(!e.project().find_clip(track, clip)->motion.effects[0].animated("radius"),
              "curve off after clear");
        e.project().undo();
        check(e.effect_keyframe_count(track, clip, 0, "radius") == 2, "undo brings keys back");
        check(!e.clear_effect_keyframes(track, clip, 0, "no_such_param"),
              "clearing an unkeyed parameter is a no-op");
        check(!e.add_effect_keyframe(track, clip, 9, "radius", 0.0, 1.0),
              "unknown effect rejected");
        check(e.add_effect_keyframe(track, clip, 0, "radius", -4.0, 1.0),
              "negative time clamps to zero");
    }

    // The stack resolves curves per frame, so one op renders differently at two
    // clip-local times while its static parameter stays put.
    {
        ImageBuffer white;
        white.allocate(4, 4, PixelFormat::RGBA8);
        white.clear(1.f, 1.f, 1.f, 1.f);
        std::vector<EffectOp> stack(1);
        stack[0].id = "opacity";
        stack[0].params["opacity"] = 1.0;
        stack[0].curves["opacity"].add(0.0, 0.0);
        stack[0].curves["opacity"].add(2.0, 1.0);
        ImageBuffer early, late;
        check(EffectStack::apply(stack, white, early, 0.0).ok(), "stack applies at t=0");
        check(EffectStack::apply(stack, white, late, 2.0).ok(), "stack applies at t=2");
        const int a_early = early.valid() ? early.data()[3] : -1;
        const int a_late = late.valid() ? late.data()[3] : -1;
        check(a_early < 8 && a_late > 240, "curve changes the rendered alpha over time");
    }

    // Copy a look from one shot to another: ids, resting values, enable flags
    // and every parameter curve travel together.
    {
        check(e.copy_effects(track, clip) == 2, "copied the two-effect stack");
        check(e.effect_clipboard_count() == 2, "clipboard holds the stack");

        ClipInfo c3;
        c3.name = "Clip C";
        c3.start = 20.0;
        c3.duration = 1.0;
        c3.source_in = 0.0;
        c3.source_out = 1.0;
        c3.motion.ensure_defaults();
        const ClipId clip_c = e.project().add_clip(track, c3);
        check(clip_c != 0, "paste target clip added");

        check(e.paste_effects(track, clip_c) == 2, "pasted two effects");
        const ClipInfo* pc = e.project().find_clip(track, clip_c);
        check(pc && pc->motion.effects.size() == 2, "target has the pasted stack");
        check(pc && pc->motion.effects[0].id == "gaussian_blur", "pasted order preserved");
        check(pc && pc->motion.effects[0].animated("radius") &&
                  pc->motion.effects[0].curves.at("radius").keys().size() == 2,
              "pasted stack carries the curves");
        check(e.paste_effects(track, clip_c, true) == 4, "append stacks another copy");
        e.project().undo();
        check(e.project().find_clip(track, clip_c)->motion.effects.size() == 2,
              "undo restores the pasted stack");
        check(e.paste_effects(track, clip + 999) < 0, "pasting onto a missing clip fails");
    }

    // Transitions between two adjacent clips
    {
        ClipInfo c2;
        c2.name = "Clip B";
        c2.start = 12.0;
        c2.duration = 2.0;
        c2.source_in = 0.0;
        c2.source_out = 2.0;
        c2.motion.ensure_defaults();
        const ClipId clip_b = e.project().add_clip(track, c2);
        check(clip_b != 0, "second clip added");

        const uint64_t tid = e.add_transition(track, clip, clip_b, 1, 0.8);
        check(tid != 0, "transition added");
        check(e.project().tracks()[0].transitions.size() == 1, "transition stored");

        const std::string json2 = e.project_to_json();
        check(json2.find("transitions") != std::string::npos, "transitions serialized");
        check(e.load_project_json(json2), "reload with transitions");
        check(e.project().tracks()[0].transitions.size() == 1, "transition restored");
        check(e.project().tracks()[0].transitions[0].type == 1, "transition type restored");
        check(std::fabs(e.project().tracks()[0].transitions[0].duration - 0.8) < 1e-4,
              "transition duration restored");
        const uint64_t restored = e.project().tracks()[0].transitions[0].id;
        check(e.remove_transition(track, restored), "transition removed");
        check(e.project().tracks()[0].transitions.empty(), "no transitions left");
    }

    // Text / titles — real glyph rasterisation through stb_truetype
    {
        ms::TextStyle ts;
        ts.font = "anton";
        ts.size = 96.f;
        ts.align = ms::TextAlign::Center;
        ts.anchor = ms::TextAnchor::Middle;

        ms::TextMetrics metrics;
        const bool measured = ms::measure_text(ts, "MOTION STUDIO", metrics);
        check(measured, "title font resolves from the asset tree");
        if (measured) {
            check(metrics.width > 10.f, "measured title has width");
            check(metrics.height > 10.f, "measured title has height");
            check(metrics.lines == 1, "single line title");
        }

        ms::ImageBuffer tile;
        const ms::Error te = ms::render_text(tile, 640, 360, ts, "MOTION STUDIO");
        check(te.ok(), "title rasterised");
        int opaque = 0;
        if (tile.valid()) {
            for (int i = 0; i < tile.width() * tile.height(); ++i)
                if (tile.data()[i * 4 + 3] > 8) ++opaque;
        }
        check(opaque > 200, "title drew glyph coverage into the tile");

        // Wrapping respects the box width.
        ms::TextStyle wrapped = ts;
        wrapped.box_width = 200.f;
        ms::TextMetrics wm;
        if (ms::measure_text(wrapped, "MOTION STUDIO TITLE", wm))
            check(wm.lines > 1, "text wraps inside the box width");

        // A text clip persists as a first-class clip with its style.
        const int text_track = e.add_track("Titles", 4);
        const ms::ClipId title = e.add_text_clip(text_track, 0.0, 3.0, "HELLO", ts);
        check(title != 0, "text clip added");
        const std::string tjson = e.project_to_json();
        check(tjson.find("\"is_text\": true") != std::string::npos, "text clip serialized");
        check(tjson.find("\"tx_text\": \"HELLO\"") != std::string::npos, "title text serialized");
        check(e.load_project_json(tjson), "reload with a title");
        const ms::ClipInfo* restored = e.project().find_clip(text_track, title);
        check(restored && restored->is_text && restored->text == "HELLO", "title survives reload");
        if (restored) check(std::fabs(restored->text_style.size - 96.f) < 1e-3f, "title size survives");
    }

    // Undo/redo must actually restore edits — the preview cache is keyed on the
    // edit revision, so a missed invalidation here would freeze the preview.
    {
        const ms::ClipInfo* start_clip = e.project().find_clip(track, clip);
        check(start_clip != nullptr, "clip exists for undo test");
        const ms::TimeSec before = start_clip ? start_clip->start : 0.0;
        check(e.move_clip(track, clip, before + 4.0), "move for undo");
        check(std::fabs(e.project().find_clip(track, clip)->start - (before + 4.0)) < 1e-6,
              "move applied");
        check(e.undo(), "undo returns true");
        check(std::fabs(e.project().find_clip(track, clip)->start - before) < 1e-6,
              "undo restored the move");
        check(e.redo(), "redo returns true");
        check(std::fabs(e.project().find_clip(track, clip)->start - (before + 4.0)) < 1e-6,
              "redo re-applied the move");
        check(e.undo(), "undo again");

        const int before_count = e.project().clip_count();
        const ms::ClipId added = e.add_clip(track, 0, 30.0, 2.0);
        check(added != 0, "engine add_clip returns an id");
        check(e.project().clip_count() == before_count + 1, "add_clip grew the timeline");
        check(e.undo(), "undo the added clip");
        check(e.project().clip_count() == before_count, "undo removed the added clip");
    }

    // Timeline markers
    {
        const uint64_t m1 = e.add_marker(2.5, "intro");
        check(m1 != 0, "marker added");
        const uint64_t m2 = e.add_marker(7.0, "outro");
        check(m2 != 0 && m2 != m1, "second marker added");
        check(e.project().markers().size() == 2, "markers tracked");

        const std::string mjson = e.project_to_json();
        check(mjson.find("\"markers\"") != std::string::npos, "markers serialized");
        check(mjson.find("\"name\": \"intro\"") != std::string::npos, "marker name serialized");
        check(e.load_project_json(mjson), "reload with markers");
        check(e.project().markers().size() == 2, "markers restored");
        check(e.remove_marker(m1), "marker removed");
        check(e.project().markers().size() == 1, "marker list shrank");
    }

    // Shape masks
    {
        ms::Mask em;
        em.shape = ms::MaskShape::Ellipse;
        em.x = 0.25f;
        em.y = 0.25f;
        em.w = 0.5f;
        em.h = 0.5f;
        em.feather = 0.f;
        ms::ImageBuffer mask_img;
        check(ms::rasterize_mask(em, 64, 64, mask_img).ok(), "mask rasterized");
        auto strength = [&](int px, int py) -> int {
            const uint8_t* p = mask_img.data() + py * mask_img.stride() + px * 4;
            return p[0];
        };
        check(strength(32, 32) > 200, "mask centre is opaque");
        check(strength(2, 2) < 20, "mask corner is clear");

        check(e.set_clip_mask(track, clip, 1, 0.2f, 0.3f, 0.5f, 0.6f, 12.f, true),
              "clip mask set");
        float mk[6] = {};
        bool inv = false;
        check(e.clip_mask(track, clip, mk, &inv), "clip mask readable");
        check(std::fabs(mk[3] - 0.5f) < 1e-3f && inv, "clip mask values");

        const std::string mj = e.project_to_json();
        check(mj.find("\"mask_enabled\": true") != std::string::npos, "mask serialized");
        check(e.load_project_json(mj), "reload with a mask");
        const ms::ClipInfo* mc = e.project().find_clip(track, clip);
        check(mc && mc->mask_enabled && !mc->masks.empty() &&
                  mc->masks[0].shape == ms::MaskShape::Ellipse,
              "mask restored");

        // A second, subtractive mask has to survive save + reload too, with its
        // op intact: that is what makes a hole in the first mask.
        check(e.add_clip_mask(track, clip, 0, 0.4f, 0.4f, 0.2f, 0.2f, 0.f, false, 1) == 2,
              "second mask added");
        const std::string mj2 = e.project_to_json();
        check(mj2.find("\"masks\": [") != std::string::npos, "mask list serialized");
        check(e.load_project_json(mj2), "reload with two masks");
        check(e.clip_mask_count(track, clip) == 2, "both masks restored");
        float smk[6] = {};
        int sop = 0;
        check(e.clip_mask_at(track, clip, 1, smk, &inv, &sop) && sop == 1,
              "subtract mask keeps its op");

        check(e.clear_clip_mask(track, clip), "mask cleared");
        float mk2[6] = {};
        check(!e.clip_mask(track, clip, mk2, &inv), "cleared mask not reported");
        check(e.clip_mask_count(track, clip) == 0, "cleared mask list is empty");
    }

    // Per-clip grade + waveform guards
    {
        check(e.set_clip_grade(track, clip, 0.5f, 1.2f, 1.1f, 0.1f, 0.f, 1.f, 1.f),
              "clip grade set");
        float g[7] = {};
        check(e.clip_grade(track, clip, g), "clip grade readable");
        check(std::fabs(g[0] - 0.5f) < 1e-3f && std::fabs(g[1] - 1.2f) < 1e-3f,
              "clip grade values");

        const std::string gj = e.project_to_json();
        check(gj.find("\"grade_enabled\": true") != std::string::npos, "clip grade serialized");
        check(e.load_project_json(gj), "reload with a clip grade");
        const ms::ClipInfo* gc = e.project().find_clip(track, clip);
        check(gc && gc->grade_enabled && std::fabs(gc->grade[3] - 0.1f) < 1e-3f,
              "clip grade restored");

        check(e.clear_clip_grade(track, clip), "clip grade cleared");
        float g2[7] = {};
        check(!e.clip_grade(track, clip, g2), "cleared clip grade is not reported");

        // A clip with no decodable media must fail cleanly, not invent a waveform.
        float peaks[8] = {};
        check(!e.clip_peaks(track, clip, 8, peaks), "peaks refused for a clip with no media");
        check(!e.clip_peaks(track, 9999, 8, peaks), "peaks refused for an unknown clip");
        check(!e.clip_peaks(track, clip, 0, peaks), "peaks refused for zero bins");
    }

    // Master finish state: the grade and the master insert chain belong to the
    // document, so they survive save/load and the chain is undoable.
    {
        e.set_grade(0.4f, 1.2f, 0.9f, 0.1f, 0.f, 1.1f, 1.f);
        AudioFxParams eq;
        eq.kind = AudioFxKind::ParametricEq;
        eq.low_gain_db = 3.5f;
        check(e.add_audio_insert(eq) == 0, "master insert added");

        const std::string mj = e.project_to_json();
        check(mj.find("\"master\"") != std::string::npos, "master state serialized");
        check(mj.find("\"low_gain_db\": 3.5") != std::string::npos, "insert params serialized");
        check(e.load_project_json(mj), "reload with master state");

        float g7[7] = {};
        check(e.get_grade(g7), "master grade restored as active");
        check(std::fabs(g7[0] - 0.4f) < 1e-4f && std::fabs(g7[1] - 1.2f) < 1e-4f,
              "master grade values restored");
        auto inserts = e.audio_effects().parameters();
        check(inserts.size() == 1 && inserts[0].kind == AudioFxKind::ParametricEq &&
                  std::fabs(inserts[0].low_gain_db - 3.5f) < 1e-4f,
              "master insert restored");

        check(e.remove_audio_insert(0), "master insert removed");
        check(e.audio_effects().parameters().empty(), "chain empty after remove");
        check(e.undo(), "undo after a chain edit");
        check(e.audio_effects().parameters().size() == 1, "undo restored the insert");
        e.clear_audio_inserts();
        check(e.audio_effects().parameters().empty(), "chain cleared");
        check(e.has_grade(), "clearing the chain leaves the grade alone");
        e.clear_grade();
        check(!e.has_grade(), "grade cleared");
        const ms::ClipInfo* still = e.project().find_clip(track, clip);
        check(still != nullptr, "undo of a chain edit kept the timeline");
    }

    // Keyframe curves: read-back, interpolation modes and a save/load round trip.
    {
        check(e.add_keyframe(track, clip, 0, 0.0, 0.0), "curve: key 0 added");
        check(e.add_keyframe(track, clip, 0, 1.0, 1.0), "curve: key 1 added");
        check(e.add_keyframe(track, clip, 0, 2.0, 1.0), "curve: key 2 added");
        check(e.keyframe_count(track, clip, 0) == 3, "curve: three keys reported");

        double kt = -1.0, kv = -1.0;
        int ki = -1;
        check(e.keyframe_at(track, clip, 0, 0, &kt, &kv, &ki), "curve: key reads back");
        check(std::fabs(kt) < 1e-9 && std::fabs(kv) < 1e-9 && ki == 0,
              "curve: key values are the ones stored");
        check(!e.keyframe_at(track, clip, 0, 99, &kt, &kv, &ki),
              "curve: out-of-range key is refused");

        float lin[5] = {0, 0, 0, 0, 0};
        check(e.sample_keyframe(track, clip, 0, 0.0, 2.0, lin, 5), "curve: samples");
        check(std::fabs(lin[1] - 0.5f) < 0.02f, "curve: linear midpoint is halfway");

        // Hold keeps the outgoing key's value for the whole segment.
        check(e.set_keyframe_interp(track, clip, 0, 0, 1), "curve: hold set");
        check(e.sample_keyframe(track, clip, 0, 0.0, 2.0, lin, 5), "curve: hold samples");
        check(std::fabs(lin[1]) < 0.02f, "curve: hold stays on the key value");

        // Smooth (cubic Hermite through the neighbouring keys) leaves the
        // straight line when the segment before it is not a straight run.
        check(e.set_keyframe_interp(track, clip, 0, 0, 5), "curve: smooth set");
        float smooth[5] = {0, 0, 0, 0, 0};
        check(e.sample_keyframe(track, clip, 0, 0.0, 2.0, smooth, 5), "curve: smooth samples");
        check(std::fabs(smooth[1] - 0.5f) > 0.01f, "curve: smooth is not the straight line");
        check(smooth[0] < 1e-6f && std::fabs(smooth[4] - 1.0f) < 1e-3f,
              "curve: smooth still passes through its end keys");

        // Retiming re-sorts, so indices follow the new order.
        check(e.set_keyframe(track, clip, 0, 2, 1.5, 1.0, 0), "curve: key retimed");
        check(e.keyframe_at(track, clip, 0, 2, &kt, nullptr, nullptr) &&
                  std::fabs(kt - 1.5) < 1e-9,
              "curve: retimed key keeps its slot");

        const std::string json = e.project_to_json();
        check(json.find("[0, 0, 5]") != std::string::npos, "curve: interpolation is saved");
        check(e.load_project_json(json), "curve: project reloads");
        // Ids are assigned by the loader, so find the clip by name again.
        TrackId reloaded_track = 0;
        ClipId reloaded_clip = 0;
        for (const auto& rt : e.project().tracks()) {
            for (const auto& rc : rt.clips) {
                if (rc.name == "Clip A") {
                    reloaded_track = rt.id;
                    reloaded_clip = rc.id;
                }
            }
        }
        check(reloaded_clip != 0, "curve: animated clip came back after reload");
        check(e.keyframe_count(reloaded_track, reloaded_clip, 0) == 3,
              "curve: keys survive a reload with no phantom key");
        check(e.keyframe_at(reloaded_track, reloaded_clip, 0, 0, nullptr, nullptr, &ki) && ki == 5,
              "curve: interpolation survives a reload");
        check(e.clear_keyframes(reloaded_track, reloaded_clip, 0), "curve: channel cleared");
        check(e.keyframe_count(reloaded_track, reloaded_clip, 0) == 0,
              "curve: clear removed every key");
    }

    // Tracks
    const TrackId audio = e.add_track("A1", 1);
    check(audio != 0, "audio track added");
    check(e.set_track_flags(audio, true, false, true, false), "track flags set");
    check(e.project().find_track(audio)->muted, "mute stored");
    check(e.remove_track(audio), "track removed");

    // Deletion
    const int before_count = e.project().clip_count();
    check(e.remove_clip(track, clip), "clip removed");
    check(e.project().clip_count() == before_count - 1, "clip count dropped");

    e.shutdown();
    if (failures == 0) {
        std::printf("ms_edit_test: all checks passed\n");
        return 0;
    }
    std::printf("ms_edit_test: %d failure(s)\n", failures);
    return 1;
}
