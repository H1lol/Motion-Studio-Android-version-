# What is genuinely missing (2026-11-12, after the completion pass)

Scope: the shipping app is Kotlin/Compose + the C++ engine over the C ABI in
`native/cpp/bridge/ms_bridge.h`. "Missing" means **missing from the app**, not
from the engine source — that distinction was the original finding.

Legend: ✅ done in this pass · ❌ still missing · ⚠️ partial / untested on device

## Closed in this pass

| Item | What landed |
|---|---|
| ✅ Clip editing | `remove_clip`, `move_clip`, `trim_clip`, `ripple_delete` bridged; timeline drag moves, edge-drag trims, long-press deletes, drag snapping to playhead and clip edges. |
| ✅ Track management | `add_track`, `remove_track`, `set_track_flags` bridged; `+` adds a track, tap toggles mute, long-press removes, mute affects colour and label. |
| ✅ Effect stack UI | `add_effect` / `remove_effect` / `set_effect_param` / `set_effect_enabled` bridged; `EffectsSheet` lists the CPU catalogue (now 32 effects, grouped Basic / Colour / Stylise / Warp / Screen / Framing) with live parameter sliders, per-effect enable switch and remove. Clip chips show `FX n`. |
| ✅ Effect persistence | Clip effects are now serialized in `.msproj` (`"effects": [{id, enabled, params:[{name,value}]}]`) and restored on load; previously they were memory-only and silently lost on save. |
| ✅ Frame-cache invalidation | Every edit and effect change clears the frame cache; before this, retimed or re-effected clips could show stale pixels. |
| ✅ Video export | `VideoExporter` renders frames from the engine and encodes them with `MediaCodec` + `MediaMuxer` into an MP4 in `filesDir/exports`, muxing the engine's mixed audio as AAC. No ffmpeg. ⚠️ Compiled only — never executed, because there is no device or emulator here. |
| ✅ Export options | `ExportSettings` + `ExportSheet`: delivery presets, custom frame size, custom frame rate (1–240 fps), H.264/HEVC, bitrate and full/playhead-start range. Scaling preserves the delivery aspect; HEVC falls back to H.264 when the device has no encoder. |
| ✅ Canvas and preview shape | Canvas ratio presets resize the real composited canvas; preview ratio guides shade the delivery crop without cutting the composite; project frame rate accepts custom values. |
| ✅ Import placement | Imports land at the playhead or are appended to the end of the timeline (`EditorEngine.appendImportsAtEnd`). |
| ✅ Import durability | Imports now copy to `filesDir/imports` instead of `cacheDir`, which the OS can purge out from under a project. |
| ✅ Typography | Inter + JetBrains Mono (OFL) bundled and wired, mono for all numeric readouts. |
| ✅ LUT looks | `.cube` engine module, six baked looks, strength blend, applied after the cache. |

### Closed in the second pass

| Item | What landed |
|---|---|
| ✅ Master grade | Seven-parameter grade applied in the render path, with a live RGB/luma histogram from the engine's scopes. |
| ✅ Keyframes | Opacity, scale, pos_x, pos_y curves with add/clear, persisted in the project JSON. |
| ✅ Transform compositing | Scale and position curves now actually move the image (they were stored and ignored). |
| ✅ Transitions | The transitions module was never compiled; it is now built, blended centred on the cut, persisted, and exposed with five types in the UI. |
| ✅ Audio in export | AAC track muxed from the engine's PCM mix alongside H.264. |
| ✅ Text and titles | stb_truetype rasteriser, six bundled OFL faces, per-clip style, bridged and editable in `TextSheet`. |
| ✅ Waveform | `TimelineAudioPlayback::clip_peaks` reads the already-cached decoded PCM; audio clips draw their real peak envelope in the timeline. |
| ✅ Per-clip grade | `ClipInfo` carries its own seven-parameter grade, applied per clip before compositing; `ColorSheet` switches between Sequence and Selected clip. |
| ✅ Masks | `comp/mask.cpp` is bridged: rectangle/ellipse/full with feather and invert, applied before the clip's transform; `MaskSheet` edits it. |
| ✅ Markers | Session-level markers with names, serialized as `"markers"`, added/removed from the session sheet. |
| ✅ Project settings | Size and frame rate can be changed without discarding edits (`ms_project_settings`). |
| ✅ Android decode | MediaCodec/MediaExtractor decoder replaces the ffmpeg shell-out, plus PCM audio decode for playback, export and waveforms. ⚠️ Compiled and linked, never run on a device. |

## Still missing

### 1. Text editing depth (titles themselves now render)
Titles are real now: `native/cpp/text/text_render.cpp` rasterises glyphs with stb_truetype, six OFL display faces ship in `assets/text/`, the C ABI has `ms_text_*`, and `TextSheet` edits text, family, size, tracking, line spacing, outline, shadow, wrap width, fill/outline colour, align, anchor and uppercase. A title is an ordinary clip, so keyframed transform, effects, grade and transitions all apply.
Closed since this list was written: reveal presets — type-on, word-by-word and
line-by-line, with a duration per title.
What is still *not* there: per-character or per-word styling (one style per clip), text on
a path, curved/warped text, right-to-left shaping and complex-script shaping (stb_truetype
has no HarfBuzz, so Arabic/Devanagari will not join correctly), and importing a user font —
a SAF picker plus the licence rule that user fonts must never be redistributed.

### 1b. Nobody has looked at a frame
`tools/ms_shots.cpp` renders real frames to PPM so the compositor output can be inspected without a
phone (it found the straight-alpha blur bug). It is a harness, not a test, and it is not in `ctest`.
What it shows today: everything is 8-bit, the transform samples nearest-neighbour so a scaled clip
aliases, and there is no thread pool — one core renders a 1080p frame. None of that has been timed
on a device.

### 2. User-supplied effect packages
The `.msp` format is specified (`docs/plugins/MSP_FORMAT.md`: zip + `manifest.json` + shaders + `icon.png`), and `MspRegistry` can scan/parse it — but:
- `install_msp` shells out to the system `unzip` binary, which does not exist on Android;
- `EffectStore::install_package` is still a stub;
- there is no import UI and no package validator;
- the format is **JSON, not XML**, and only the `composite` entry type is data-only safe — `shader`/`wasm`/`native` packages execute code.
Needed: a Kotlin zip extractor, manifest validation, checksum, a browser with thumbnails, and a decision on whether third-party code execution is in scope.

### 3. Keyframe curve editor
Closed since this list was written: the KEYS tab draws the real curve from engine samples,
every key carries its own interpolation (linear, hold, ease in/out/in-out, smooth), and theselected key can be retimed and retargeted. Six interpolation modes and smooth
(Catmull-Rom Hermite) evaluation are covered by `ms_edit_test`.

Keys can also be dragged on the curve now: horizontally to retime, vertically to change the
value, clamped between neighbouring keys so the index under the finger stays put.

Closed since this list was written: automation curves on effect parameters — a curve per
(effect, knob) in clip-local seconds, with add/read/retime/clear, curve sampling, undo and
persistence, evaluated per frame by the effect stack. Effect stacks can also be copied to
and pasted onto other clips, curves included.

Closed since this list was written: automation curves on audio — clip gain and pan are
keyframed in clip-local seconds and resolved per sample by the mixer.
Still missing: editing in/out tangents by hand, easing presets shared between keys, curve
zoom/pan, and automation curves on the grade and the master insert chain.

### 4. Mask depth
Shapes are there (rectangle, ellipse, full frame, feather, invert), and a clip now carries up to
eight masks, each Add (union) or Subtract (cut out of that union); a list of only Subtract masks
starts from a full frame. Closed since this list was written: multi-mask, and the bug where mask
strength was written into RGB while `MaskMode::Alpha` read alpha, which made an ordinary mask a
no-op and an inverted one hide the whole clip. Also closed: a full-frame mask now honours invert (the
shared fast path returned white for both polarities), and each mask's mode (Alpha, Luma and their
inverted forms) is folded into the Add/Subtract combine instead of being ignored. Missing:
bezier/roto shapes and mask tracking to follow motion.
Closed since this list was written: mask keyframes — centre X/Y, width, height and
feather each carry a clip-local curve, resolved per frame before the matte is rasterised
and saved with the mask.

### 5. Grade controls
Both the master grade and per-clip grades work, each with ten parameters and a live histogram.
Closed since this list was written: the highlight/shadow split (both pivot on mid grey, so midtones
stay put) and vibrance (weighted by how muted a pixel already is), and per-clip `.cube` looks: a clip
carries its own LUT plus strength, applied after its grade and before its transform, and the LOOKS
sheet switches between Sequence and Selected clip. Missing: curves, colour wheels, and stacking
several LUTs on one clip.

### 6. Audio depth
Export carries AAC audio, and audio clips now draw a real waveform from decoded PCM. The master insert chain is now editable from the app (Gain, 3-band EQ, compressor, delay, reverb, limiter) with a MIX sheet carrying per-track faders, mute and solo; clip gain and pan are honoured by the mix. The master grade and the master insert chain are saved with the project document (a `master` block holding the seven grade values and every insert parameter), and insert structure edits — add, remove, clear, bypass — are on the undo stack; knob values and the grade are persisted without one undo entry per slider frame. Closed since this list was written: insert chains per track — every track carries its own
chain, run over that track's clips before the master bus, kept stateful in the mixer,
saved with the track and editable from a **CHAIN** chip in `MIX`.
Closed since this list was written: clip fader and pan are editable from `MIX`, and both
carry clip-local automation curves that the mixer resolves per sample; the curves and the
fader are saved with the clip.
Closed since this list was written: loudness metering — the final mix is metered to
BS.1770 (momentary, short-term, gated integrated plus per-channel peaks) and `MIX` shows
it while open.
Still missing: master-bus insert automation and sample-rate choice.

### 6b. Effect catalogue size
39 CPU effects against Kdenlive's roughly 84 hand-written effect definitions plus 145 MLT avfilter and 118 frei0r entries. Most of that gap is provided by third-party GPL plugins we cannot link; adding them would relicense the app. Audio effects are still the thinnest area: the master bus has six inserts, every track
hosts its own six-kind chain and gain stage, and clips carry keyframed fader and pan, but
there is no effect-parameter automation on audio inserts and no loudness metering.

### 7. Playback performance
Closed since this list was written: decode is no longer seek-per-frame (`IMediaDecoder::read_video_at`
streams forward from the codec position and caches the frame for a repeated instant), the preview
renders on its own thread, and proxy media exists — `ProxyGenerator` transcodes a source to
1280×720 H.264 with the platform codecs, `ProxyStore` manages and re-registers the files, and
`create_media_decoder` decodes the proxy for preview while export disables it so delivery still
comes from the source. Generation is a button in MORE → PROXY MEDIA.

Still missing: proxies are not created automatically on import, there is no per-clip override or
"which clips are proxied" indicator, no frame-cache size/eviction UI, compositing is still CPU
into RGBA8 (`gpu_mock`) with no GPU path, and nothing here has run on a device, so thermal and
battery behaviour at 4K is unmeasured.

### 7b. Comp network depth
The node graph evaluates (Constant, Blur, Grade, Saturate, Invert, Merge, Over, Viewer) and is saved
with the project, and `Read` decodes media or samples the edited timeline through the engine's
provider. `Transform` now does translate/scale/rotate/opacity, and knobs can be linked to other
nodes' numeric knobs (cycle-checked, persisted). It is not yet a full compositor: there are no
expressions (links only), no with wired parameters, no per-node persistent cache across frames,
viewer AOI/tiling or proxy playback, one decoder per media path with no prefetch, and node colour
for Constant is not editable from the UI.

### 8. Multicam and nested sequences
Markers are done. Multicam and nested sequences still have zero references in the engine — a nested sequence would need a clip type that renders another timeline.

### 9. Project settings depth
Size presets and 24/30/60 fps are editable in the session sheet. Missing: arbitrary width/height entry, colour space and bit-depth choice, and audio sample-rate selection.

### 10. Colour pipeline depth
8-bit RGBA8 preview path; no 10-bit, no HDR, no practical linear working space.

### 11. Verification on hardware
**No APK has ever been launched in this environment** (no device, `/dev/kvm` not accessible to this user). Every runtime claim — JNI, `MediaCodec` export, `AudioTrack` playback, gestures — is compile/link/test evidence only.

## Test coverage now

- `ctest` **25/25** (the new `ms_loudness_test` checks BS.1770 metering on synthetic tones:
  a -20 dBFS stereo 1 kHz sine reads -20.69 LUFS within 1.5 LU in all three windows,
  doubling amplitude adds 6 LU, mono feeds both channels, silence sits at the -70 floor,
  reset clears the peaks and an integrated reading, and a 44.1 kHz meter reads the same
  tone): existing suites plus `ms_effects_screen_test` (a chroma split leaves alpha untouched, a warp keeps fully transparent areas colour-free, zero settings are exact identities, kaleidoscope and VHS are deterministic) and `ms_effects_alpha_test` (a blur conserves coverage, spreads, keeps colour under partial alpha, and is a passthrough at radius 0) and `ms_lut_test` (cube parsing, blend, rejection), `ms_edit_test` (move, trim, ripple, tracks, effect add/param/enable, JSON round-trip, undo, delete, transition add/persist/remove, font resolution, title rasterisation, word wrap, title persistence, keyframe curves) and `ms_proxy_test` (proxy resolution, enable/disable, no-op registration, generation counter).
- `:app:testDebugUnitTest` **6/6**: native JSON contract, timeline end, malformed JSON, aspect ratio, timecode, clip lookup.
- Android build: `:app:assembleDebug` green, both ABIs, all assets packaged (6 title fonts, 6 looks, 7 UI fonts).
- `ms_title_demo <dir>` renders four title cards to PPM for visual inspection without a device — this is how the rasteriser was confirmed, not just unit-tested.
