# Changelog

## Unreleased — viewport tap-to-select and double-tap recentre

### Added
- **Tap a layer in the preview to select it.** A finger that touches down and lifts without
  moving is a tap, not a pan: the point is mapped through the composite exactly as last
  drawn (canvas zoom, letterbox and preview quality all resolved), and the engine picks the
  layer composited there. The pick uses the renderer's own transform, so a scaled, moved or
  parented layer is picked where its pixels are, not where the frame is. The topmost track
  wins where layers overlap, and a tap on empty picture clears the selection
- **Two-finger double-tap recentres the canvas**, in PAN / ZOOM mode only. Two fingers that
  lift within 280 ms without moving count as a tap; a second one within 350 ms resets the
  pan and zoom. With the toggle off the same fingers mean a layer transform, so they never
  recentre by accident
- **Engine surface.** `TimelinePlayer::hit_test` returns the track and clip under a
  normalized point at the playhead; `Engine::preview_hit_test` exposes it, bridged as
  `ms_preview_hit_test` and mirrored in JNI as `nativePreviewHitTest`, with
  `EditorEngine.pickAt` owning the selection change. The compositor and the hit test share
  one `clip_transform`, so the two cannot drift apart. Null objects and adjustment layers
  draw no pixels of their own and are never picked

### Verified
- `ms_hit_test` (new, in `ctest`): a full-frame layer is picked at the centre and the
  corner; a half-size layer loses the corner; a moved box is picked where it now sits and
  not where it was; the overlay above wins at the covered point and the base layer is
  picked outside it; a null object above is never picked; out-of-frame points and a
  playhead past every layer pick nothing. Native suite: **29/30**, with
  `ms_audio_playback_test` still the only failure because this container has no `ffmpeg`
  CLI for its fixtures
- `:app:assembleDebug` succeeds, and `libmotionstudio_jni.so` carries
  `nativePreviewHitTest` for arm64-v8a and x86_64
- Not verified: no device or emulator here, so tap picking against a real finger, the tap
  slop, and the double-tap timing were not exercised by hand

### Provenance
Behaviour described here comes from Alight Creative's public documentation of preview pan
and zoom and from this project's own implementation. No code, string, curve table or asset
is taken from another editor.

## Unreleased — preview pan/zoom mode and preview quality

### Added
- **Explicit pan/zoom mode.** The viewport now carries a visible **PAN / ZOOM** toggle,
  the mode the Alight Motion reference describes. With it on, two fingers always pan and
  pinch the canvas, and one finger pans at any zoom; the pinch keeps the point under the
  fingers fixed instead of zooming around the view centre
- **Two-finger layer transform.** With the toggle off and a layer selected, a two-finger
  gesture moves and scales that layer instead of the canvas: the centroid drag becomes a
  position offset, the pinch becomes a scale factor, and the gesture commits position and
  scale keyframes at the playhead on release, so it is one undo step and Motion sees
  exactly the values the gesture produced. A readout over the viewport reports the
  accumulated offset and percentage while the fingers are down. With nothing selected,
  the toggle off still pans the canvas, because there is no ambiguity to resolve
- **Preview quality as its own setting.** Settings → Preview and Session → **Preview
  quality** offer Full, Half and Quarter. The engine composites the frame at 100%, 50% or
  25% of the canvas (`Engine::set_preview_scale`, `ms_preview_scale_set`, bridged to
  `NativeEngine.nativePreviewScaleSet`), so a heavy scene previews at a quarter of the
  pixels without touching the document. The setting is persisted on the device and
  restored at start-up
- **Delivery stays full size.** `render_frame` reports its composite size separately from
  the project canvas, so   the preview bitmap follows the quality setting without changing
  what export means. The PNG still export and the MP4 encoder force scale 1 and restore
  the preview scale afterwards, so neither is ever delivered at the reduced size

### Verified
- `ms_preview_quality_test` (new, in `ctest`): full quality reports and renders the 640×360
  canvas; half quality reports and copies a 320×180 frame; the preview state still reports
  the 640×360 project canvas; changing quality changes the render signature, so the frame
  cache cannot serve the old size; the scale clamps at 25%. Native suite: **28/29**, with
  `ms_audio_playback_test` still the only failure because this container has no `ffmpeg`
  CLI for its fixtures
- `:app:assembleDebug` succeeds with the new JNI entry points and the Compose changes
- Not verified: no device or emulator here, so the gesture modes, the pinch feel, the
  layer-transform keyframes and the visible quality change were not exercised by hand

### Provenance
Behaviour described here comes from Alight Creative's public documentation and from this
project's own implementation. No code, string, curve table or asset is taken from another
editor.

## Unreleased — layer parenting and null objects

### Added
- **Parenting.** A clip can now name another clip as its parent. The child inherits the
  parent's animated translation and scale at the playhead, and chains compose from the
  nearest parent outwards. Parenting deliberately does not carry opacity: a control layer
  moves a rig, it does not fade it. Each ancestor is sampled on its own clip-local clock
  and clamped to its own range, so a parent that ends holds its last pose instead of
  snapping back
- **Null objects.** `add_null_clip` creates an invisible control layer with a transform
  but no pixels. It exists on the timeline, can be keyframed and parented like any other
  layer, and is skipped by the compositor, the frame cache and export, so it can never
  appear in a delivered frame. This is the same rig model the released Alight Motion and
  Nova editors describe: animate one control, parent the layers to it
- **Engine surface.** `add_null_clip`, `set_clip_parent` and `clip_parent`, bridged as
  `ms_null_add` / `ms_clip_parent_set` / `ms_clip_parent_get` and mirrored in JNI and
  `EditorEngine.addNullObject` / `setClipParent` / `clipParent`. Self-parenting, an unknown
  parent and any cycle are refused; linking and clearing are one undo step each
- **Persistence.** Clips carry `"parent"` and `"null"` in the project JSON. The keys are
  written for every clip, so an older document loads with no parent and documents from
  this build stay readable by the same parser
- **UI.** Session gains **Add null object**, Motion gains a **Parent** row listing every
  legal parent (descendants are excluded before the engine checks again), and the clip
  inspector reports `null · invisible control layer` and the parent id

### Verified
- `ms_parent_test` (new, in `ctest`): a null contributes zero pixels; linking a child to a
  keyframed null moves it; the parent's scale multiplies the child's (0.5 × 2 fills more
  of the frame); self-parenting, an unknown parent and a cycle are refused; clearing a
  parent undoes back to the link; `parent` and `null` survive save/load. Full native suite:
  **27/28**, with `ms_audio_playback_test` failing only because this container has no
  `ffmpeg` CLI to generate its tone and media fixtures — not a parenting regression
- `:app:assembleDebug` succeeds (Kotlin/Compose, JNI for both ABIs, signed APK) and
  `:app:testDebugUnitTest` passes
- Not verified: nothing was run on a device, so the new Parent row, the ghost null clip on
  the timeline and the feel of an animated rig are unexercised

### Provenance
- Original work on the project's own transform compositor and `AnimCurve`. The released
  editors' descriptions were used to choose the behaviour; no code, asset, string or
  parameter text came from either app

## Unreleased — build verification and second interface pass

### Added
- **A build you can run.** `android/tools/verify.sh` compiles the app, runs the JVM unit
  tests, runs Android lint and assembles the debug APK with the native engine for every
  ABI. It reads `JAVA_HOME` and `ANDROID_HOME` and takes `GRADLE_MEMORY` for machines
  smaller than the 3 GB default heap
- **Alight Motion reference note.** `docs/alight-motion-reference.md` records how that
  editor works from import to export as behaviour only — parenting and null objects,
  per-interval easing, modal canvas gestures, project packages — with the gaps its own
  documentation does not settle marked as such. No code, shader, string or asset came
  from it

### Fixed
- **The preview allocated a `RectF` on every frame.** `PreviewView.onDraw` built the
  aspect guide rectangle inside the draw pass, so a playing comp allocated at 60 Hz.
  The rectangle is reused now
- **The curve editor boxed a `Double` on every drag event.** Its drag time and value
  used `mutableStateOf(0.0)`; they use `mutableDoubleStateOf`, so dragging a key no longer
  allocates per pointer move
- **The preview ignored accessibility clicks.** The view consumes its own touches but
  never answered `performClick`, so an assistive "activate" did nothing. It reports the
  click on touch-up and overrides `performClick`
- **Three media permissions were requested and never used.** Import goes through
  `ActivityResultContracts.OpenDocument`, which grants the chosen file, so
  `READ_MEDIA_IMAGES`, `READ_MEDIA_VIDEO` and `READ_MEDIA_AUDIO` bought only a wider
  permission prompt — and on Android 14 implied partial-photo handling that did not
  exist. Removed, along with the activity's duplicate label

### Changed
- **More sheet, settings, curve editor and node graph now match the sheets.** They were
  the last surfaces still on the old scale: 9 sp mono labels, 19 dp chips, 14 dp node
  ports. All four use the shared `SheetSection` rule, a 10–11 sp floor for labels, 32–40 dp
  targets for every chip and stepper, 18 dp graph ports and a 40 dp graph toolbar. The
  node inspector is a weighted pane instead of a fixed 150 dp strip
- **A real close button on the session sheet.** `MoreSheet` opened on a bare "Session"
  heading with no way out but the drag handle; it uses `SheetHeader` like the rest

### Verified
- `assembleDebug` succeeds from a clean checkout with JDK 17, platform 35, build-tools
  35.0.0, NDK 27.0.12077973 and CMake 3.22.1: Kotlin/Compose compile, resources link,
  the C++ engine builds for arm64-v8a and x86_64, and a 64 MB debug APK is signed and
  packaged
- `testDebugUnitTest` passes (`ProjectDocTest`, `TimelineRulerTest`)
- `lintDebug` passes with no errors; the remaining warnings are icon-shape and
  dependency-version notices
- Not verified: nothing was run on a device or emulator, so the visual pass, gestures,
  playback, audio and export are unexercised. Motion Studio still has no instrumented
  tests

## Unreleased — interface pass

### Fixed
- **Launcher screens drew under the system bars.** The activity is edge-to-edge and only
  the editor had a Scaffold supplying insets, so the projects list started beneath the
  status bar clock and the last row sat under the gesture bar. Home, settings and the
  engine-unavailable screen are now wrapped in one safe-drawing frame
- **Every sheet stopped at a fixed height.** Effects and audio pinned their lists to
  430 dp and looks to 320 dp: on a landscape phone the bottom third was cut off and on a
  tablet the sheet ended in empty panel. Each sheet now takes 92% of the screen and the
  list fills whatever the header and meter leave behind
- **Faders stuttered.** A track or clip gain slider pushed a value per pointer event and
  `setTrackGain`/`setClipAudio` answered by serialising and reparsing the whole project
  through `reload()`. Both take `refresh = false` now, the slider follows a local value
  and the document is re-read once on `onValueChangeFinished`
- **The eighth mask was unreachable.** The mask strip and both chip rows overflowed the
  sheet with no scroll; they scroll horizontally now
- **Two inserts of the same kind shared one slider.** Insert cards derived their slider
  draft key from the chain ("master", "track 4") instead of the insert, so a second
  compressor on the same chain was dragged by the first card. The key now carries the
  insert index
- **Nothing to close a sheet with.** Sheets opened on a bare title and the only exit was
  the drag handle. `SheetHeader` gives every one a 40 dp close button next to the title

### Changed
- **Contrast.** Tertiary and muted text were 3:1 on the workspace black and read as
  disabled. Both were lifted (muted #5D5573 → #857C9C, tertiary #8B82A4 → #A79EBD) and the
  panel edges a step lighter
- **Touch targets.** Timeline chips were 19 dp pills with a 2 dp radius; they are 30 dp
  minimum with 6 dp corners and 10 dp sides, and every sheet chip is at least 38 dp.
  Workspace tabs are 42 dp, the selection strip 46 dp and panel headers 32 dp
- **Type scale.** Micro labels moved up a step (8–9 sp headings to 10 sp, 10 sp values to
  11–12 sp) so the chrome reads as labels instead of debug output
- **Viewport framing.** The inset rectangle drawn over the picture is gone; four corner
  rules mark the canvas edge without boxing in the image being judged. The master mix
  meter sits on its own panel instead of floating in the sheet background
- **Dark only.** The theme no longer branches on the system light/dark setting: it always
  returned the same scheme, and the warning comment now says why


## Unreleased — mix loudness meter

### Added
- **BS.1770 metering.** The timeline mix now has a real loudness meter: K-weighting with
  the two standard biquads (a ~1.68 kHz high shelf and a ~38 Hz high-pass), 100 ms block
  energies, and momentary (400 ms), short-term (3 s) and integrated readings with the
  -70 LUFS absolute gate and the -10 LU relative gate. Per-channel sample peaks are kept
  since the last reset. Coefficients are recomputed for the rate the host feeds, so a
  44.1 kHz session is not measured with 48 kHz filters
- **Measured where it is heard.** The meter runs on the final post-fader, post-insert
  buffer at the end of `TimelineAudioPlayback::read_pcm`, not on an intermediate mix, and
  resets on seek and stop so an integrated reading cannot span two unrelated sections
- **Engine surface.** `audio_meter` fills five floats (peak L, peak R, momentary,
  short-term, integrated); bridged as `ms_audio_meter`. `MIX` polls it every 120 ms while
  open and shows a peak bar (red at full scale) and the three LUFS windows

### Verified
- `ms_loudness_test` (new, in `ctest`): a -20 dBFS stereo 1 kHz sine reads -20.69 LUFS
  within 1.5 LU in all three windows, doubling the amplitude adds 6.02 LU, mono input
  feeds both meter channels, silence reads at the -70 floor rather than -inf, reset clears
  peaks and loudness, and a 44.1 kHz meter reads the same tone as the 48 kHz one
- `ms_edit_test` still passes with the meter in the playback path; `bridge/ms_bridge.cpp`
  compiles clean under `-fsyntax-only`. Kotlin/Compose were not compiled here

### Provenance
- Original implementation from the published BS.1770 coefficient formulas; libebur128 was
  not copied or linked

## Unreleased — mask geometry automation

### Added
- **Keyed masks.** Each mask can animate any of its five geometry values — centre X,
  centre Y, width, height and feather — on its own clip-local curve, so a matte can grow
  over a face, drift across the frame or soften as a shot ends. Un-keyed fields keep their
  static values and sliders, so keying width alone does not freeze the mask's position
- **Resolved per frame.** The clip renderer checks whether any mask on the clip is
  animated, resolves the keyed fields at the frame's clip-local time and rasterises the
  matte from that copy. An un-animated mask rasterises its stored struct exactly as
  before, and the resolved values are clamped to the frame so an interpolating curve
  cannot push a mask out of view
- **Engine surface.** `add_mask_keyframe`, `clear_mask_keyframes`,
  `mask_keyframe_count/at/set` and `sample_mask_param`, with the same
  replace-on-the-same-instant rule as the other curves. Bridged as `ms_mask_keyframe_*`
  and surfaced in `MASK` as ◆ chips per value that show their key count and clear when
  tapped
- **Persistence.** `keys_x`, `keys_y`, `keys_w`, `keys_h` and `keys_feather` are written
  only when a mask is keyed, so an un-animated mask keeps the record older builds wrote

### Verified
- `ms_edit_test`: a mask key round-trips, a same-instant key replaces, the width curve
  interpolates 0.2 → 0.4 → 0.6 across half a second while an un-keyed field stays put,
  the curve samples agree, the curves survive a save/load round trip, clearing empties
  only that curve, and a bad mask index or property is rejected
- `bridge/ms_bridge.cpp` compiles clean under `-fsyntax-only`. Kotlin/JNI and Compose were
  not compiled here (no Android toolchain in this environment)

### Provenance
- Original work: the project's own `AnimCurve` and matte rasteriser, no third-party roto
  or tracking code

## Unreleased — title reveal presets

### Added
- **Type-on, word-by-word and line-by-line titles.** `TextStyle` carries a reveal mode
  and a duration; the rasteriser cuts the string on codepoint, word or line boundaries
  before laying it out, so a multibyte character is never split in half. The reveal runs
  on the timeline clock, not the retimed source clock, so a slowed title still types on
  in the time the ruler shows
- **Nothing at rest.** A finished reveal is truncated to the whole string, so the last
  frame is byte-identical to an un-revealed title: there is no permanent fade or offset
  left behind by turning a preset on
- **Engine surface.** `set_clip_text_reveal` / `clip_text_reveal`, `ms_text_reveal_set` /
  `ms_text_reveal_get`, saved as `tx_reveal` and `tx_reveal_sec`. A style edit from the
  title sheet keeps the current reveal instead of silently dropping it, and the mode and
  duration are undoable
- **UI.** `TEXT` gains a **Reveal** row (None / Type on / Word / Line) and a reveal-time
  slider for the selected title

### Verified
- `ms_edit_test`: a reveal starts with zero coverage, a half reveal covers part of the
  title, the finished frame matches the plain render exactly, word and line modes cut on
  their own boundaries, the mode and duration survive a save/load round trip, a style
  edit keeps the reveal, and a non-title or an out-of-range mode is rejected
- `bridge/ms_bridge.cpp` compiles clean under `-fsyntax-only`. Kotlin/JNI and Compose were
  not compiled here (no Android toolchain in this environment)

### Provenance
- Original work on the project's own stb_truetype rasteriser; no third-party title
  animation code or preset was copied

## Unreleased — clip audio and automation

### Added
- **Per-clip fader and pan.** A clip now carries its own gain (linear, 0–4) and pan
  (−1 left … +1 right); the timeline mix multiplies the clip fader by the track fader
  and applies a linear pan law per sample. Before this the mixer honoured both values but
  nothing in the app could set them
- **Gain and pan curves.** Either value can be automated in clip-local seconds: the gain
  curve multiplies the clip's fader (so a fade is 0 → 1 and the fader still trims the
  whole move) and the pan curve is the absolute position. The mixer resolves the curves
  per sample, and evaluates nothing while a curve holds no keys, so an un-automated clip
  costs exactly what it did before
- **Engine surface.** `set_clip_audio`, `clip_audio`, `add_audio_keyframe`,
  `clear_audio_keyframes`, `audio_keyframe_count/at/set` and `sample_audio_param`. Adding
  a key at an instant that already holds one replaces it, matching effect automation;
  structure and value edits are undoable
- **Persistence.** `keys_gain` and `keys_pan` sit beside the transform curves in the clip
  record, so automation survives save, load and undo. Older builds ignore the two keys
- **UI.** `MIX` gains a **CLIP AUDIO** section listing every non-text clip with GAIN and
  PAN sliders, **◆ GAIN** / **◆ PAN** keys at the playhead, and a key count that clears
  the curve when tapped

### Verified
- `ms_edit_test`: fader and pan round-trip, an unknown clip is rejected, a same-instant
  key replaces instead of stacking, the pan curve interpolates to ±1 at its ends, both
  curves and the fader survive a save/load round trip, clearing one curve leaves the
  other alone, and an out-of-range property is rejected
- `bridge/ms_bridge.cpp` compiles clean under `-fsyntax-only`. Kotlin/JNI and Compose were
  not compiled here (no Android toolchain in this environment)

### Provenance
- Original work: the mixer's own gain/pan stage and `AnimCurve` evaluation, no external
  automation code

## Unreleased — per-track insert chains

### Added
- **Track processing.** Every track now carries its own insert chain, applied to that
  track's clips before the master bus. Previously only the master bus had inserts, so
  cleaning up one noisy source meant processing the whole mix. The chain uses the same
  six insert kinds and the same parameter names as the master chain (Gain, 3-band EQ,
  compressor, delay, reverb, limiter)
- **Stateful per track.** The mixer keeps one running `AudioEffectChain` per track, so a
  reverb tail or delay repeat continues across audio callbacks. A parameter edit updates
  the chain in place; only a structural change (add, remove, kind swap) rebuilds it
- **Saved with the project.** Track inserts are written into the track record as
  `"audio_inserts"` and parsed back, so a mixed project reopens with its processing
  intact. Documents written before this load with dry tracks, as before
- **Engine surface.** `add_track_insert`, `remove_track_insert`, `clear_track_inserts`,
  `set_track_insert`, `set_track_insert_enabled`, `track_insert_count` and
  `track_insert_params`; structure edits are undoable, knob values are not, matching the
  master chain. Bridged as `ms_track_fx_*` and surfaced in `MIX` via a per-track
  **CHAIN** chip that expands to the track's inserts

### Verified
- `ms_edit_test`: a track insert is added, counted, saved and reloaded with its kind and
  threshold, an out-of-range index is rejected, bypass is stored, the master chain stays
  untouched, a structure edit undoes, and clearing empties the chain
- `bridge/ms_bridge.cpp` compiles clean under `-fsyntax-only`. Kotlin/JNI and Compose were
  not compiled here (no Android toolchain in this environment)

### Provenance
- Original work. The insert DSP is the project's own `AudioEffectChain`; no third-party
  effect code was copied

## Unreleased — three finishing looks

### Added
- **Zoom blur.** `zoom_blur` averages up to 64 bilinear taps along the ray from a
  movable centre through each pixel, centred on the pixel so a static frame does not
  drift. It works on premultiplied coverage, so an opaque white frame stays white and
  no pixel with zero coverage carries colour; amount 0 is a byte-for-byte passthrough
- **Duotone.** `duotone` maps luma onto a dark-to-light colour ramp with seven knobs and
  a mix back to the original. Luma is measured on straight colour, so a half-covered
  edge is not mapped towards the dark end as it would be from premultiplied values; the
  clip's alpha is untouched
- **Pencil sketch.** `sketch` runs a Sobel gradient over luma and places ink on paper,
  with ink strength, paper level and mix. Flat areas stay clean white, edges go dark,
  and transparent areas stay empty
- `cpu_effects_looks.cpp` holds the three behind the same premultiplied-plane helpers as
  the motion and screen families and is hooked into `cpu_effect_try`. `EffectsSheet`
  lists them under **Colour**, **Stylise** and **Warp** with sliders matching the
  dispatcher defaults; the catalogue is 39 effects

### Provenance
- Original implementations of standard image operations. No shader, source file,
  parameter text or asset from Nova Editor or any other editor was copied

### Verified
- `ms_effects_looks_test` (new): all three zero settings are exact identities; a zoom
  smear leaves an opaque white frame white and never puts colour under zero coverage; a
  duotone maps black and white onto the configured dark and light colours within two
  levels and keeps alpha; a sketch keeps flat paper clean, inks an edge, preserves
  coverage and is deterministic; every id resolves through the catalogue dispatcher

## Unreleased — effect parameter automation

### Added
- **Animated effect knobs.** Every effect parameter can carry its own curve.
  `EffectOp::curves` maps a parameter name to an `AnimCurve` in clip-local seconds,
  the same clock the transform keys use. While a curve holds keys it drives the
  parameter; the value in `params` stays the resting value the slider edits, so a
  knob you have not keyed keeps working exactly as before
- **Per-frame evaluation.** `EffectStack::apply` takes an optional clip-local time and
  resolves each animated parameter through `EffectOp::params_at` before dispatching the
  effect, so a blur radius, fisheye amount or particle speed can change across one
  clip. `TimelinePlayer::apply_clip_look` passes the frame's clip-local time for clips
  and for adjustment layers alike
- **Editing API.** `Engine::add_effect_keyframe`, `clear_effect_keyframes`,
  `effect_keyframe_count`, `effect_keyframe_at`, `set_effect_keyframe` and
  `sample_effect_param` cover authoring, reading, retiming, easing and curve sampling.
  Adding a key at an instant that already has one replaces that key instead of stacking
  a duplicate, because the UI adds a key on every tap. Structure edits are undoable and
  drop the frame cache; negative times clamp to zero
- **Persistence.** Effect objects in `.msproj` gain a `curves` array of
  `{name, keys:[[time, value, interp]]}`; empty curves are not written, so documents
  from earlier builds load unchanged and older builds ignore the new field
- **App surface.** `ms_effect_keyframe_*` on the C bridge, `nativeEffectKeyframe*` in
  the JNI bridge, and `EditorEngine.addEffectKeyframe` / `effectKeyframes` /
  `setEffectKeyframe` / `clearEffectKeyframes`. Each parameter row in `EffectsSheet`
  gains a `◆+` button that keys the current slider value at the playhead and a `◇n`
  counter that clears the curve

### Verified
- `ms_edit_test` (extended): two keys on a blur radius, the curve driving the midpoint
  while the static value stays 8; samples hit both ends; a same-instant add replaces
  instead of duplicating; retiming with an ease stores its interpolation; keys survive
  the project JSON round trip; clear turns the curve off and undo brings it back;
  clearing an unkeyed parameter and keying an unknown effect are rejected; a negative
  time clamps. A 4×4 opacity stack renders transparent at t=0 and opaque at t=2 from the
  same op, proving the stack resolves curves per frame. All checks pass

## Unreleased — effect stack clipboard

### Added
- **Reusable looks.** `Engine::copy_effects` snapshots a clip's whole stack and
  `paste_effects` writes it onto another clip, replacing its stack or appending a copy.
  Ids, resting values, enable flags and every parameter curve travel together, so a
  pasted look animates the same way the original does. Both are undoable and drop the
  frame cache. The clipboard is engine-session state, not part of the document
- `ms_effect_copy` / `ms_effect_paste` / `ms_effect_clipboard_count` on the C bridge,
  `nativeEffectCopy` / `nativeEffectPaste` / `nativeEffectClipboardCount` in JNI, and
  `EditorEngine.copyEffects` / `pasteEffects` / `effectClipboardCount`. `EffectsSheet`
  gains a STACK row with COPY FX and, once something is copied, PASTE FX

### Verified
- `ms_edit_test` (extended): a two-effect stack is copied, pasted onto a fresh clip in
  order, and the pasted blur still carries its two-key radius curve; appending doubles
  the stack to four, undo returns it to two, and pasting onto a missing clip fails

## Unreleased — effect stack order

### Added
- **Reordering.** A clip's effect stack can be rearranged: `Engine::move_effect(track,
  clip, from, to)` removes the entry at `from` and inserts it at `to`, so the moved
  effect ends up exactly at the requested slot. Order runs top to bottom, matching the
  order `cpu_effect_apply` walks it, so a blur can sit under a glitch instead of only
  over it. The move is one undo step and drops the frame cache; the parameters, enabled
  flag and id travel with the effect because the whole `EffectOp` moves
- `ms_effect_move` on the C bridge and `nativeEffectMove` in the JNI bridge expose it to
  the app; `EditorEngine.moveEffect` marks the project dirty and reports the new slot
- `EffectsSheet` gains ▲/▼ buttons per applied effect, disabled at the ends of the stack,
  plus a one-line hint that the stack runs top to bottom. Reordering or removing an
  effect clears the local slider drafts, which were keyed by stack index and would
  otherwise show the previous effect's positions

### Verified
- `ms_edit_test` (extended): moving an effect to the top, to the bottom, to its own
  index, out of range, to a negative index and onto an unknown clip; a move is one undo
  step that restores the old order; and the effects-serialised round trip still sees
  blur-then-vignette after the reorder checks. All checks pass

## Unreleased — motion, lens and light

### Added
- **Motion blur.** `motion_blur` accumulates up to 64 bilinear taps across a shutter
  distance at any angle, centred on each pixel so a static frame does not drift. The
  accumulation runs on premultiplied coverage, so it inherits the alpha fix that made
  Gaussian and box blur stop darkening soft edges; a zero distance is a byte-for-byte
  passthrough
- **Lens warp.** `fisheye` inverse-maps each destination pixel through a radial
  `1 + amount·r²` scale around a movable centre, with zoom. It samples the outside as
  empty rather than clamping, so a strong warp clears the corners instead of smearing
  the border pixel across the frame; amount 0 with zoom 1 is the identity
- **Glitch.** `glitch` slips horizontal bands sideways, tears a minority of them about
  twice as far, and separates the channels by up to 8 px so hard edges fringe. The row
  offset, tear selection and fringe are all seeded, so a frame renders identically on
  every pass; coverage travels with the tear, and colour never appears under zero alpha
- **Particles.** `particles` stamps up to 512 seeded emitters with a quadratic falloff
  into a separate light buffer and adds it once, so crossing sparks sum like light
  instead of overwriting like paint. Warm and cool emitters mix; time and speed move the
  field; added light raises coverage so the compositor cannot multiply it back down
- `cpu_effects_motion.cpp` holds the four behind the same premultiplied plane helpers as
  the warp/screen family. `EffectsSheet` lists them under **Stylise**, **Warp** and
  **Screen** with sliders matching the dispatcher defaults; the catalogue is 36 effects
- `docs/MISSING_FEATURES_2026-11-12.md` still governs what is absent from the app; Nova
  Editor's published feature list was used only to pick which operations to add

### Provenance
- No decompiled class file, resource, shader, string or parameter text from Nova Editor
  was read into this tree. The effects are original implementations of standard image
  operations, written against public descriptions of what the operations do

### Verified
- `ms_effects_motion_test` (new): the zero settings are exact identities, a directional
  blur leaves an opaque white frame white and keeps transparent regions empty, a strong
  fisheye clears its corners, a glitch keeps colour out of transparent pixels, and the
  particle field is deterministic, adds coverage and never removes it. `ms_effects_screen_test`,
  `ms_effects_catalogue_test` and `ms_effects_alpha_test` still pass against the same objects

## Unreleased — eight warp and screen effects

### Added
- **Lens dispersion.** `chromatic_aberration` pushes red out and blue in around a movable
  centre, and `rgb_split` offsets the channels along an angle by a distance in pixels
- **Distortion.** `kaleidoscope` (mirrored wedges, rotation, zoom), `twirl` (swirl inside a
  radius, centre and reach adjustable) and `wave_warp` (sinusoidal displacement in both axes)
- **Screens.** `halftone` (rotated dot screen driven by luma, with mix), `scanlines` (CRT line
  darkening with spacing, strength and phase) and `vhs` (row wobble, chroma bleed, a seeded
  tracking band and tape noise)
- `cpu_effects_screen.cpp` holds them behind one premultiplied bilinear sampler, so a warp
  averages coverage instead of straight colour and cannot drag a transparent neighbour's colour
  into a glyph edge. Every zero setting (amount, distance, angle, amplitude, mix, strength) is a
  byte-for-byte passthrough rather than a lossy premultiply/unpremultiply round trip
- `EffectsSheet` lists them under **Warp** and **Screen** with sliders and defaults that match
  the dispatcher; the catalogue is 32 effects

### Provenance
- Nova Editor's published decompile was used only as a checklist of which looks matter on a
  phone. No shader, source file, parameter text or asset from it was copied; these are original
  implementations of standard operations.

### Verified
- `ms_effects_screen_test` (new): chroma split leaves alpha untouched, a warp keeps fully
  transparent areas colour-free, the zero settings are exact identities, and kaleidoscope and
  VHS render deterministically. Suite is **24/24**
- `ms_shots` gained six shots (`12_chromatic` … `17_twirl`) and they were rendered and looked at:
  fringing, folded wedges, halftone dots (the first pass left solid white text solid, so the dot
  radius was cut from 0.72 to 0.5 of a cell), VHS wobble, wave displacement, swirl

## Unreleased — effects that respect alpha, and a harness that renders pictures

### Fixed
- **A blur darkened what it touched.** The CPU effects average pixels that hold straight (not
  premultiplied) colour, and a transparent pixel still carries a colour — black. Averaging straight
  values dragged that black into every soft edge, and because the compositor then multiplies by
  alpha again the result was dim as well as soft: a title blurred at radius 14 lost roughly two
  thirds of its brightness, and a soft matte edge came out muddy. `cpu_gaussian_blur` and
  `cpu_box_blur` now premultiply, average, and unpremultiply through two new helpers
  (`premultiply_rgba`, `unpremultiply_rgba` in `ms_cpu_effects.hpp`), so a blur moves coverage
  without destroying colour. Glow and every `frei0r`/MLT path that maps onto a blur inherit it

### Added
- `tools/ms_shots.cpp` — a harness, not a test: it builds a small timeline (titles, a matte, blur,
  tonal grade, an overlay track, an adjustment layer) and renders real frames from `TimelinePlayer`
  to PPM with a centre-mean printout per shot, so output can be looked at instead of only asserted
  on. Finding the bug above took one render
- `ms_effects_alpha_test`: coverage is conserved by a blur, alpha spreads outside the source shape,
  a partial-alpha pixel keeps the source colour instead of fading towards black, a zero radius is a
  byte-for-byte passthrough, and glow leaves opaque white white. Suite is **23/23**

## per-clip looks and mask correctness

### Added
- **A look on one clip.** `ClipInfo` gained `look_path` and `look_strength`, so a `.cube` LUT can
  grade a single clip instead of only the whole sequence. The clip look runs after that clip's grade
  and before its transform, so it survives into export the same way the grade does
- `TimelinePlayer::apply_clip_look` (now a member, so it can reach the LUT cache) and a
  `lut_cache_` keyed by path that also remembers parse failures — a frame never re-reads a cube
  from disk, and an unreadable file is not retried every frame
- `Engine::set_clip_look`, `set_clip_look_strength`, `clear_clip_look` and `clip_look`, bridged as
  `ms_clip_look_set/set_strength/clear/get` and as four `NativeEngine` externals. `look_path` and
  `look_strength` are written into each clip in the project JSON and restored on load; documents
  without them load with no clip look
- LOOKS sheet: a **Sequence / Selected clip** target toggle. In clip mode the strength slider, the
  clear button and the preset list act on the selected clip's look, and the active preset is matched
  by file name because the clip stores a resolved path

### Fixed
- **An inverted full-frame mask did nothing.** `rasterize_mask` took a constant-white fast path for
  `MaskShape::Full` regardless of polarity; it now goes through the same strength function, so an
  inverted Full mask is black
- **Mask mode was ignored when masks combined.** Alpha/Luma and their inverted variants were only
  honoured by the single-mask apply step, so an InvertedAlpha mask combined as a plain Add kept its
  shape and cleared everything else. `mask_mode_strength()` now folds the mode into both the Add
  union and the Subtract cut
- **Duplicated `"grade_enabled"` key** in the clip JSON (written twice on every save)
- **Mask ids could collide** after removing a mask and adding another: `make_mask` used
  `masks.size() + 1`, now the next id above the highest in use

### Tests
- `ms_clip_look_test`: a look reaches the clip's pixels and not its neighbour's, strength blends
  toward the slate, the look survives a save/load, a missing cube is refused, and clearing restores
  the original frame
- `ms_mask_test`: a full-frame mask covers the frame and its inverted form is black; an
  InvertedAlpha Add mask keeps only the area outside its shape; an InvertedAlpha Subtract mask cuts
  everything except its own shape. Suite is **22/22**

## Unreleased — tonal grade split

### Added
- **Shadows, highlights and vibrance** on the master grade and on every clip grade. Shadows and
  highlights move the dark and bright ends of the curve independently, weighted so mid grey is the
  pivot and is left alone: that is what makes them different from contrast, which rotates the whole
  curve. Vibrance boosts a pixel's distance from its own luma in proportion to how *muted* it is, so
  a grey stays grey and an already-saturated colour barely moves, unlike the saturation slider
- `GradeSettings` gained the three fields, the grade arrays grew from 7 to 10 values, and the extra
  three are persisted for clips and for the master block. Documents written before this load with
  their seven values and the new three defaulted to neutral, so old projects grade identically
- `Engine::set_grade_tonal` / `Engine::set_clip_grade_tonal`, bridged as `ms_grade_tonal_set` and
  `ms_clip_grade_tonal_set`, so the existing seven-argument setters did not have to change. The
  grade *getters* now fill 10 floats
- GRADE sheet: three new sliders in both the Sequence and Selected clip modes

### Tests
- `ms_grade_test`: a neutral grade is a no-op, shadows lift a dark pixel while leaving a bright one
  alone, highlights do the reverse, mid grey does not move, vibrance moves a muted colour more than
  a saturated one and leaves grey untouched, and both the clip and master tonal values survive a
  save/load

## Unreleased — multi-mask

### Added
- **Several masks per clip (up to 8).** `ClipInfo` now carries a `std::vector<Mask>` instead of one
  mask. Each mask is *Add* (unions with the clip's other masks) or *Subtract* (cut out of that
  union), so a matte can be "this box, minus this hole". A clip whose masks are all Subtract starts
  from a full frame, which makes a single subtract mask a plain cut-out instead of an empty frame.
  The Mask sheet lists the masks as +/− chips and adds, selects, edits and removes them
  individually
- **Multi-mask API.** `Engine::add_clip_mask`, `update_clip_mask_at`, `remove_clip_mask_at`,
  `clip_mask_count` and `clip_mask_at`, bridged as `ms_clip_mask_add/update/remove/count/read` and
  as five `NativeEngine` externals. The single-mask functions still work and address mask 0
- Masks are saved as a `"masks": [...]` array inside each clip. The old `mk_*` keys are still
  written (mirroring the first mask) and still read, so documents written before this load with
  their mask intact and older builds still see one mask

### Fixed
- **Masks did nothing, and inverted masks hid the whole clip.** `rasterize_mask` wrote the mask
  strength into RGB and left alpha at 255, while `MaskMode::Alpha` reads alpha: a normal mask was
  a no-op and an inverted one made the clip fully transparent. Strength now goes into alpha as
  well, and the single- and multi-mask paths share one `mask_strength_at()`
- Matting is applied in place (`apply_mask_inplace`) instead of copying the whole frame into a
  second buffer for every clip on every frame

### Tests
- `ms_mask_test`: strength reaches alpha, two Add masks union with the gap between them clear,
  Add + Subtract punches a hole, a lone Subtract mask keeps the rest of the frame, disabled masks
  are skipped, feather ramps down to clear, and the mask list round-trips through the project JSON
  with its op, geometry, invert flag and eight-mask cap. `ms_edit_test` now saves, reloads and
  checks two masks including the subtract op

## Unreleased — retime + adjustment layers

### Added
- **Retime.** `ClipInfo::speed` was already honoured by the player and the audio mixer and
  already saved with the project, but nothing in the app could change it. `Engine::set_clip_speed`
  now retimes a clip: the same slice of source plays at 0.1–8× and the clip's timeline length
  follows it, so 2× halves the clip and 0.5× doubles it. The Motion sheet has a speed slider and
  0.25/0.5/1/2/4× presets, and the clip inspector shows the current speed. Titles and adjustment
  layers are rejected because they have no source range. Audio pitches with the change: the
  mixer resamples by `speed`, it does not time-stretch
- **Adjustment layers.** A clip can now carry `is_adjustment`: it owns no pixels, and its effect
  stack + per-clip grade run over the frame the tracks below have already composited, for the
  range it covers. Added from Session → "Add adjustment layer" on the overlay (or first video)
  track; it then behaves like any other clip for effects, grade, keyframes and ripples, and the
  flag round-trips through the project JSON as `"adj"`

### Changed
- The clip's effect stack and grade moved into `apply_clip_look()` in the timeline player, so an
  adjustment layer and an ordinary clip run exactly the same look pipeline instead of two
  copies of it

### Tests
- `ms_adjustment_test`: retime keeps the source slice and moves the length, a title/adjustment
  clip cannot be retimed, a neutral adjustment layer renders pixel-identical, a graded one lifts
  the composite below it, it does not reach outside its range, and `"adj"` survives a save/load

## Unreleased — smoothness

### Fixed
- The preview loop composited and copied a whole frame on a timer whether or not anything
  had changed, so a paused editor kept a core busy and the view invalidated 20 times a
  second. The engine now exposes `render_signature()` (playhead, edit revision, clip and
  track counts, grade, look, comp sink) and the preview thread does nothing until it moves
- `engine.time` was read inside the header, the transport, the viewport badges and the
  whole timeline body, so every delivered frame recomposed all of them. Playhead readouts
  are leaf composables now and the lane playhead moves through a placement lambda, so a
  playing frame invalidates one Text and one offset instead of the whole editor
- The preview worker held the bitmap lock across the full-frame `ms_copy_frame_rgba`
  memcpy, so the UI thread waited behind a multi-megabyte copy whenever it drew. Two
  bitmaps are swapped under a short lock instead
- The compositor built four to six full-size `ImageBuffer`s per clip per frame, each
  allocating and zeroing its own pixel store, and an empty effect stack copied the whole
  frame for nothing. Clip images now come from a per-player scratch pool and are rotated
  with `ImageBuffer::swap()`. Measured on a 720p, four-clip masked project with effects:
  59.7 → 48.6 ms/frame (16.7 → 20.5 fps)
- The viewport called `nativeDecoderBackend()` — a JNI call returning a String — on every
  recomposition; the backend name is remembered once

### Removed
- `third_party/curated_sources/`: 292 MB and 19,611 files of complete upstream archives
  (Blender, OpenShot, LMMS, Natron, Gaffer, appleseed, Kdenlive, Enzo, LuxCore, Styk,
  Sprocket, EffectCraft). No CMake target, Gradle source set, engine file or app file
  reads them, and `THIRD_PARTY.md` already documented them as not included; the selected
  excerpts in `third_party/upstream_reference_sources/` are untouched

## Unreleased — editor chrome

### Changed
- The editor is laid out the way a phone editor is laid out: a header with the project
  name, `time / duration` and one filled **Export** button; a transport row of previous
  keyframe / play / next keyframe / undo / redo; and a bottom tool dock of icon+label
  buttons (Media, Text, Effects, Look, Grade, Motion, Mask, Audio, More) instead of a
  strip of mono-uppercase chips that read as debug output
- Undo and redo moved out of the header into the transport, where they sit next to the
  playhead they act on, and the save icon gave way to the settings sheet (autosave saves)
- Text in the dock creates a title at the playhead and opens the text sheet, instead of
  only editing an existing title
- Timeline: the playhead carries its own timecode tab, the selected clip gets a solid
  accent cap, and track headers show a visibility (eye) toggle with mute/remove only on
  the selected track, so an unselected row stays quiet

### Added
- `Engine.keyframeTimes()` and `jumpToKeyframe(dir)`: the transport jumps to the nearest
  key on the selected clip and falls back to a one-frame step when nothing is animated

## Unreleased — proxy media

### Added
- Proxy media. A large source is transcoded once into a 1280×720 H.264 stand-in and
  the engine decodes that while you scrub and play. `ProxyGenerator` decodes to an
  `ImageReader` surface, nearest-neighbour downscales the Y and UV planes into NV12
  and encodes/muxes with `MediaCodec` + `MediaMuxer` — there is no ffmpeg on Android
  and shipping one would drag the whole GPL toolchain in
- `ProxyStore`: proxies are named from a SHA-1 of the absolute source path under
  `filesDir/proxies`, so regenerating replaces the old file and two projects pointing
  at the same footage share it. Existing proxies are re-registered on every project
  load, which is why they never appear in the document
- MORE → PROXY MEDIA: generate for the open project with live progress, see file count
  and disk use, delete all
- C ABI `ms_proxy_register/unregister/clear/count/for/enabled/is_enabled` plus the JNI
  mirrors. The swap lives in `create_media_decoder`, the single place every decoder is
  built, so preview, scrubbing and the comp viewer all pick it up
- `ms_proxy_test`: resolution, the enabled/disabled switch, no-op re-registration and
  the generation counter (18/18 under `ctest`)

### Changed
- Export disables proxies before it renders and restores them in `finally`, so a
  delivered master is still encoded from the source rather than from a 720p stand-in
- `TimelinePlayer` tracks the registry generation and drops its cached decoders when
  proxies are registered, enabled or disabled — otherwise a decoder opened before a
  proxy existed would keep reading the original

### Notes
- Proxies carry no audio on purpose: preview audio decodes from the source through
  `android_decode_audio`, so a video-only proxy changes nothing about sound
- Compiled and unit-tested only. Generation and playback have not run on a device

## Unreleased — keyframe curves

### Added
- Keys can be dragged on the curve: horizontal retimes, vertical changes the value, time
  clamped between the neighbouring keys so the key under the finger cannot swap places
  with a neighbour mid-drag. The drag is drawn locally and committed on release, because
  writing the engine on every move re-keys the gesture and cancels it
- The KEYS tab is a curve editor. It used to count keys; it now draws the channel's
  curve from samples the engine returns (`ms_keyframe_sample`), so the shape on screen
  is the shape that renders, with keys as dots and the playhead as a line
- Per-key interpolation: linear, hold, ease in, ease out, ease in-out and smooth. The
  selected key can be retimed and retargeted with steppers
- `ms_keyframe_count/get/set/set_interp/sample` and their JNI mirrors. Keys serialise as
  `[time, value, interp]`; a two-element key from an older file still loads as linear
- Smooth interpolation actually smooths: cubic Hermite through the neighbouring keys
  (Catmull-Rom tangents). The enum value existed, but evaluation fell through to linear

### Fixed
- **A second keyframe deleted the first.** "Replace the seeded default" was tested with
  `keys().size() == 1`, which is also true of a curve holding the single key you just
  authored, so every add after the first cleared the curve
- **Opening a project added a phantom key to every clip** and left the curves flagged as
  seeded, so the next keyframe added to a reopened project wiped the animation that had
  just been loaded. The loader seeds after parsing, and a seeded placeholder is no longer
  written to the file at all

## Unreleased — streaming decode and an off-thread preview

### Changed
- Playback and export no longer seek to the previous keyframe for every frame.
  `IMediaDecoder::read_video_at` has a streaming fast path in the MediaCodec
  decoder: a small step forward decodes ahead from the current codec position
  instead of flushing and re-decoding the whole GOP, and a request for the same
  instant returns a cached frame. On a 2-second GOP the old path decoded up to
  60 frames to show one; the forward path decodes one
- The preview renders on its own thread. It used to be a Runnable on the UI
  thread, so a heavy composite blocked input and could trip the ANR watchdog
- Playhead time now comes from the UI thread (`post` from the render thread), so
  Compose state is never written from the worker

### Fixed
- `MediaCodecDecoder` reported the *seek target* as every frame's timestamp
  (`pts_ms = pos_ms_`) instead of the decoded frame's own presentation time. The
  timestamp handed back for a frame is now the frame's real PTS

## Unreleased — crash-proofing and diagnostics

### Added
- `Diagnostics` records an uncaught-exception report (`last-crash.txt`) and a bounded
  breadcrumb trail (`session.log`) in `filesDir/diagnostics`, installed from a new
  `MsApplication` so it exists before the native library loads or the first activity inflates
- Native signal handler (`android/app/src/main/cpp/ms_crash.cpp`): SIGSEGV/SIGBUS/SIGILL/
  SIGFPE/SIGABRT write signal, fault address and a 24-frame return-address backtrace to
  `native-crash.txt` on an alternate stack, then re-raise so the system still reports it
  normally. A segfault used to leave nothing behind at all
- Settings → Diagnostics: engine state, decoder backend, last crash, COPY REPORT /
  REFRESH / CLEAR CRASHES

### Fixed
- A missing `libmotionstudio_jni.so` no longer crashes the app on launch. `NativeEngine`
  captures the load failure instead of letting `ExceptionInInitializerError` escape from
  `MainActivity.onCreate`, and the UI shows an ENGINE NOT LOADED screen with the ABI and
  the loader error
- Font unpacking and the crash-handler install are wrapped: a full disk or a rejected
  font asset can no longer stop the editor from opening
- `EditorEngine` construction is guarded, so an engine that refuses to start produces a
  message instead of a crash

## Unreleased — purple interface pass

### Changed
- Palette is now black and lavender, the colour of the app mark: violet-tinted
  neutrals, lavender accent on every control, playhead and selection
- Timeline ruler rewritten: ticks are drawn in one canvas (major plus minor), labels
  step by integer index instead of accumulating floats, and each zoom level prints one
  unambiguous unit — `15s` below a minute, `1:19` above it, `0.5s` / `2:03.5` when
  ticks are sub-second. The old `mm:ss:ff` label read like minutes for a 15 s tick
- Timeline lanes alternate tone, the selected clip gets a 2 px lavender border and a
  brighter fill, and the timeline takes more of a portrait screen

### Added
- Selection bar under the ruler: clip name, in/out timecode, and SPLIT · FX · MOTION ·
  MASK · INSPECT · DEL for whatever is selected — tapping a clip now leads somewhere
- Double-tap a clip to open the CLIP inspector
- Empty-timeline state with an IMPORT MEDIA button instead of a one-line hint
- Launcher icon, adaptive icon and round icon generated from the logo
  (`android/tools/make_icons.py`), and the starter header now draws the same ring mark

## Unreleased — delivery pass

### Added
- Export sheet: delivery presets, custom frame size, custom frame rate (1–240 fps,
  including 23.976 / 29.97 / 59.94), H.264 or HEVC, bitrate slider and
  whole-timeline / from-playhead range
- `ExportSettings` model: output scaling keeps the delivery aspect, frame and size
  estimates are shown before encoding, and a device without HEVC falls back to H.264
- `VideoExporter` now honours size, frame rate, bitrate, codec and range, and encodes
  the engine's mixed audio as AAC
- Canvas ratio presets (9:16, 16:9, 1:1, 4:5, 21:9, 4:3) and a custom project frame rate
- Preview ratio guides (16:9, 9:16, 1:1, 4:5, 21:9): the delivery crop is shaded, the
  composite itself is never cut
- Import placement: at the playhead or appended to the end of the timeline

## 2026.0.0-alpha — 2026-10-30

### Added
- Native C++17 core: project, tracks, clips, undo foundations
- FFmpeg CLI + in-process libav decode (MS_HAVE_LIBAV)
- Frame buffer C ABI (`ms_frame_buffer_publish_rgba`)
- MLT timeline spike: playlist/tractor, XML save/load, edit rules
- Effects host: builtins, keyframes, frei0r catalog, OpenFX scan API
- Export presets, lossless cut/join, export queue
- Scene/silence auto-edit, SRT/ASS captions, JSON templates
- Flutter mobile editor shell (EffectCraft-inspired palette)
- Online effects catalog hook (H1lol/ms-effects)

### Known limits (alpha)
- Android NDK / MediaCodec not verified in CI
- Flutter not fully bound to MLT/libav graph
- GPU path is not production
- OpenFX host is discovery-only

### Gates
M0–M5 desktop spikes: PASSED

## 2026-10-30 — Milestones 17–22
- Added native 3D materials/camera/light scene data and DCC job export.
- Added Blender background render submission path.
- Added named color transforms and grading/display transform.
- Added OpenFX host registry boundary with built-in validation plugin.
- Added asynchronous background render queue.
- Added `ms_17_22_test` verification suite.

## 23–30
- Added settings/theme controller and professional starter panel.
- Added touch/performance policy helpers and atomic media/project storage helpers.
- Added Android packaging scaffold.
- Added release-check target and 23–30 readiness/license documentation.

## 2026-11-04 — Desktop reference alignment pass

- Replaced the Flutter runtime branding asset with the exact user-supplied MS logo (same bytes as the top-level branding asset).
- Matched the desktop workspace order to the reference: Edit, Animation, Compositing, Nodes, 3D, Color, Audio, Export.
- Tuned wide-screen dock widths, desktop header, composition-tab strip, transport bar, timeline region, and Render Queue footer toward the 1648×923 reference.
- Made Inspector / Effects / Details tabs selectable, with separate content views.
- Kept the mobile editor layout separate and adaptive.
- Validation limitation: Flutter/Dart SDK is not installed in this environment; no Flutter compile or runtime screenshot is claimed.
- Updated the native integration test's stale version expectation (`0.6`) to the current `2026.0.0-alpha` project version.
- Native CMake build and all six native test executables pass in the working environment; Flutter build remains unverified because the SDK is unavailable.

## 2026-11-04 — Upstream-informed native integration

- Added stateful native audio insert chains: gain, 3-band EQ, compressor, delay, reverb, and sample-peak limiter; exposed by AudioMixer, AudioPlayback, and TimelineAudioPlayback and applied to native PCM output.
- Rebuilt node-graph validation with cycle/port/endpoint checks, disconnect and parameter update APIs, ordered input evaluation, and recursion protection.
- Added saturation, invert, and straight-alpha CompositeOver graph operations.
- Added an optional Appleseed offline-renderer handoff target with generated project XML and a launcher (sphere proxy geometry pending real mesh serialization).
- Expanded export profiles for UHD, vertical, square, WebM/VP9, and ProRes delivery.
- Added selected upstream reference source and license texts for Sprocket, Gaffer, Kdenlive, and appleseed; retained LMMS license/design notes only due GPL-2.0-only compatibility boundary.
- Added CTest registration and `scripts/test_native.sh` for repeatable native regression testing, including generated-WAV standalone and timeline playback insert tests.
- Native Release build and all ten registered test executables pass. Flutter/device build and end-to-end UI playback remain unverified in this environment.


## 2026-11-04 — Blender 5.2.2 DCC integration

- Expanded scene-job JSON to preserve transforms, hierarchy, visibility, camera/light properties, mesh paths, and material data.
- Hardened JSON string escaping.
- Upgraded generated Blender jobs with OBJ/FBX/glTF/STL/USD imports, Principled materials, image textures, camera settings, light settings, object parenting, and render output.
- Added a selected Blender 5.2.2 reference-header set and retained upstream GPL license texts.
- Added regression assertions for scene serialization and Blender script capabilities.
- Validation: C++ Release build passed; CTest 10/10 passed. Blender runtime rendering remains unverified because Blender is not installed in this environment.


## 2026-11-04 — Mobile editor workspace pass

- Reworked the phone editor into a touch-first vertical stack that preserves the desktop editor's order: composition viewer, transport, timeline controls, multi-layer timeline, and quick tools.
- Kept the same MS logo asset, dark charcoal surfaces, violet active states, workspace names, and export affordance.
- Added a dedicated mobile timeline action strip for Snap, Keyframe, and Add Layer.
- Replaced cramped equal-width phone actions with a horizontally scrollable touch dock for Media, Effects, Text, Shape, Audio, Mask, Keyframe, Inspector, and More.
- Made narrow phone headers adapt by switching Export to an icon action and reducing secondary toolbar controls.
- Kept phone landscape in the touch-first layout; the full docked desktop workspace activates at 900 logical pixels.
- Flutter/Dart SDK is unavailable in this environment, so no Flutter compile or Android runtime screenshot is claimed. Native C++ tests are unchanged from the passing 10/10 v4 baseline.

## 2026-11-04 — Mobile polish and autosave

- Added atomic Flutter-side `.msproj` save and 10-minute recovery snapshots.
- Added recovery restore when the autosave is newer than the manual save.
- Added lifecycle-triggered recovery saves and visible Saved / Autosaved / Unsaved status.
- Persisted Recent and Archived project lists so recovery remains reachable after an app restart.
- Added Android 10 (API 29) minimum target and documented device/storage guidance.
- Improved mobile header touch targets and added system requirements to Settings.


## Cache safety update
- Added automatic generated-cache enforcement: clear the dedicated cache directory when usage exceeds 75% of a 1 GiB budget.
- Checks at startup and every five minutes.
- Project files, recovery snapshots, autosaves, source media and exports remain outside the cleanup scope.

## 2026-11-16 — Transform node and linkable knobs (half of what Natron actually is)
Read Natron's own architecture notes before writing this. Two things define that class of tool: every
knob can be driven by something other than its own stored value, and the graph is evaluated by pulling
from the output. The first is now implemented; the second was already true here.

- **Transform node is real.** It used to copy its input and ignore every parameter. It now does
  translate, scale, rotate (about the frame centre) and opacity with a bilinear inverse map, and an
  identity transform takes a straight-copy path so default settings never resample and soften the
  image.
- **Knob links, in Natron's sense.** `ParamLink{node, param, src, src_param}` makes one knob follow
  another node's knob. Values resolve through the link on every fetch, so moving the source changes
  the result without retouching the link. A Constant node's `Number` knob is the numeric source this
  was built for.
- **Cycles are refused where the link is made**, not discovered when a frame is cooked: linking
  follows the source's own chain and rejects the link if it reaches the target, matching Natron's
  "connecting would create a cycle" rule. Resolution is also depth-guarded so a corrupt file cannot
  hang the render thread.
- Links are serialised in the same record format (`~L|node|param|src|srcparam`), dropped on load if
  either end is missing, and removed when a node is deleted.
- UI: the NODES inspector is now driven by a knob table rather than a `when` per op, with a LINK chip
  per knob that opens a picker of every numeric knob on every other node. A linked knob's slider is
  disabled and tinted, because dragging a driven value would be a lie.
- Because the `Constant`'s colour and its number are separate, a Constant can now act as a number
  node without affecting the picture.
- Tests: the comp suite grew to cover translation exposing transparency, pixel shifting, opacity on
  alpha, identity pass-through, link resolution, a linked scale of 1 matching the unlinked image
  byte-for-byte, self-link and cycle refusal, link persistence, and cleanup on node removal.
  `ctest` 16/16, `:app:assembleDebug` and `:app:testDebugUnitTest` green, 82 Kotlin externals against
  83 native symbols with none missing. Still no device, so none of this has been seen rendering.

## 2026-11-16 — Timeline follows the playhead, pinch zooms the time axis
- **Playhead follow**: while playing, the shared horizontal scroll pages so the playhead never
  leaves the viewport — it crosses the right margin, then resumes 12% in from the left, and it
  scrolls back when playback loops or the user seeks behind the view. It is page-style on purpose:
  sliding the offset on every frame would fight the user for the scroll state. A FOLLOW chip in the
  strip turns it off.
- **Two-finger pinch zoom** on both the ruler and the lanes, 0.4x–24x, anchored on the pinch focus:
  the time under your fingers stays under your fingers. Because the content width changes with the
  zoom, the corrected scroll offset is applied after the new width has been laid out rather than
  guessed from the old one.
- Deliberately **not** `detectTransformGestures`: that claims one-finger pans as well and would
  fight the scroll containers and the clip drags beneath them. The detector here only acts once a
  second pointer is down, and it consumes on the Initial pass so no scrollable sees the gesture as a
  pan.
- Checks: `:app:assembleDebug` and `:app:testDebugUnitTest` green. Not run on a device, so the
  feel of the follow paging and the pinch anchor is unverified; the maths is the part that was
  reasoned through and the gestures are the part that was not.

## 2026-11-16 — Read nodes decode real media, and the timeline is rebuilt
Two things were blocking actual compositing: the comp's Read node was a slate, and the
timeline pane could not be touched.

### Read nodes
- `NodeGraph` gained a read provider: `ReadProvider` is a callback the engine installs, and
  a graph without one still renders its explicit slate. That keeps the unit tests
  deterministic and stops a missing decoder from leaving uninitialised pixels on screen.
- `Engine::read_comp_source` backs the provider. An empty `media_path` reads the *edited
  sequence* at the cook time, so a comp can be built on top of the timeline. A path opens
  that media through the same decoder factory the timeline uses (MediaCodec on Android,
  FFmpeg on desktop), seeks to the node's `offset` parameter and bilinearly scales to the
  comp resolution.
- The timeline frame the comp cooks over is kept for the cook, so several Read nodes do not
  each decode the same frame; comp decoders live in their own cache so a comp read cannot
  disturb playback state.
- Bridged end to end: `ms_comp_get_param`/`set_media`/`get_media`, three JNI entry points,
  Kotlin externals, and a source chooser in the node inspector (Timeline, or any imported
  media, plus a time offset).

### Timeline
- **Fixed a real hit-testing bug**: the scrub layer used to sit on top of the lanes, so it
  swallowed every clip tap and drag. Scrubbing now lives *under* the clips — Compose only
  hit-tests nodes that declare pointer input, so empty lane space scrubs and clips receive
  their own gestures.
- Ruler with adaptive tick/frame labels, marker dots, a grabbable playhead, and tap-or-drag
  scrubbing.
- Clip body and trim grips are now separate pointer regions, so one gesture can no longer
  move and trim at once. Grips appear on every clip wide enough to hold them.
- Snapping to the playhead, markers and neighbouring clip edges, with a snap guide line and
  a SNAP/FREE toggle; drags commit through the engine so undo stays authoritative.
- Track headers got mute, visibility and remove controls, a selection highlight and clip
  counts; audio tracks draw their real peak envelope. Ruler and lanes share one horizontal
  scroll, headers and lanes share one vertical scroll.
- Zoom (0.4x–24x, FIT) with a timecode/frame readout, grid lines per lane, transition marks
  on the cut, and per-clip FX/mute badges.

### Checks
`ctest` 16/16 suites (the comp graph test now also proves the provider receives the path,
cook time and offset, that its output reaches the frame, and that a provider-less graph
falls back to the slate). `:app:assembleDebug` and `:app:testDebugUnitTest` green. All 78
Kotlin externals matched against the built `.so` symbol table — that audit caught three
JNI/bridge functions my text edits had silently missed, which would have thrown
`UnsatisfiedLinkError` the first time a parameter slider was touched. Nothing was run on a
device: the layouts, gestures and decode path are compile- and test-verified only.

## 2026-11-16 — Comp network: a real node graph, wired end to end
Built after reading how production compositors actually evaluate a graph rather than guessing:
Foundry's NDK guide (an Op's hash comes from its inputs' hashes plus its knob values at that frame)
and Houdini's Copernicus cooking notes (pull each node's inputs, cache intermediate results).

- **`NodeGraph` is now reachable from the app.** The engine already had the class and cycle-safe
  validation but nothing called it. `Engine` owns a graph, `set_comp_active()` switches the viewport
  from the timeline player to a cook of the graph's Viewer node, and master grade plus LUT still apply
  afterwards as display finishing.
- **Persistence in the project document.** `project_to_json()` appends `"comp":"<records>"` and
  `load_project_json()` restores it. The record format (`MSCOMP1~N|…~E|…`) is deliberate: fields are
  sanitised of the separators, so parsing is exact and a quote in a node name cannot corrupt it.
- **C++ bridge + JNI + Kotlin**: 16 new entry points (node count/at, edge count/at, add, remove,
  connect, disconnect, get/set param, enable, position, sink, active).
- **NODES tab is a real DAG editor.** Nodes render at their saved positions, wires are drawn between
  them, dragging moves a node, tapping selects, tapping a node's IN port connects the selected node
  into that slot, and cycles are rejected by the evaluator instead of being applied. Selecting a node
  exposes its parameters (blur radius, grade exposure/contrast, saturation amount, merge mix), Bypass,
  "View this" (promotes the node to the sink) and Delete.
- **Tests**: `ms_comp_graph_test` (27 checks) covers cook, parameter change affecting pixels,
  validation, self-link and cycle rejection, the save format, position/param survival and that a
  restored comp renders byte-identically. `ms_engine_load_test` now also proves a comp survives a
  full project save/load cycle. `ctest` **16/16** suites, `:app:testDebugUnitTest` 6/6, APK green,
  76 Kotlin externals vs 76 JNI symbols.
- Known limits, stated plainly: the `Read` node still renders a slate (media decode is not wired to
  the graph), Constant colour channels are not bridged to sliders, the node cache is per-cook rather
  than persisted across frames, and the comp-active flag itself is not saved. The UI has not been
  viewed on a device.

## 2026-11-15 — Workspace UI: compositor shell
- **The workspace strip was fake.** `EditorScreen` kept a `workspace` state that nothing read, so Animation / Compositing / Nodes / 3D were decoration. Replaced with five tabs that each swap the lower panel: TIMELINE, NODES, KEYS, GRADE, CLIP.
- **NODES** — new `NodeGraphPane` renders the clip's actual render chain as a left-to-right graph: SRC → each effect in stack order → XFM → GRD → LUT → OUT, with bypass state on every node. Selecting a node shows its real parameters, so effects can be bypassed, deleted and dialled from the graph.
- **GRADE** — seven grade parameters plus a live R/G/B/luma histogram drawn from `nativeScopesHistogram` on the last rendered frame.
- **KEYS** — per-channel keyframe counts read from the project, with key-at-playhead and clear, and a plain note that dragging keys on a curve is not implemented.
- **CLIP** — selection inspector: start/length/end, opacity, kind, effect stack with inline enable, doors into the detail sheets.
- **New palette and chrome**: near-black panels with warm amber accent (violet removed), 1 px edges instead of rounded cards, monospace for every number, viewport overlays for decoder backend, resolution, transport state and timecode, plus safe-area guides.
- Starter screen rebuilt as a technical project browser: identity strip with the live decoder backend, bordered project rows with accent spine, mono timestamps and file sizes, framed format/frame-rate pickers.
- Verified: `:app:assembleDebug` and `:app:testDebugUnitTest` green, `ctest` 15/15. ⚠️ The layout has not been viewed on a device or emulator — it is compile-verified only.

## 2026-11-15 — Fixed: effects that appeared to do nothing
- A new effect was stored as `{base_id, {}, true}` — **no parameters**. The engine then rendered its own internal defaults while the sheet displayed the picker's defaults, so the numbers on screen were not the numbers being rendered. Dragging a slider fixed it, which made effects feel half-broken.
- `EditorEngine.addEffect` now seeds the effect with the picker's values, so displayed parameters and rendered pixels always agree.
- Five defaults were neutral (opacity 1.0, levels gamma 1.0, white balance 0/0, crop 0, brightness 0 / contrast 1), so adding them changed nothing at all. Retuned to 0.8, 1.2, 0.2, 0.08 and 0.06/1.1.
- `ms_effects_catalogue_test` now also checks all 23 effects at **shipped** defaults and fails if any of them is invisible (mean channel delta < 0.5). It caught the neutral five.

## 2026-11-15 — Fixed: black preview when opening a saved project
- `Engine::load_project_json` never set `impl_->initialized`, and `render_frame()` returns false immediately while that flag is clear. Creating a new project went through `new_project()`, which does set it — so a fresh project rendered and an **opened** one did not. The starter screen opens projects directly, so every existing project showed a black preview and exported black frames.
- Load now brings the engine up the same way `new_project()` does: playhead tick reset, `initialized = true`, frame cache cleared, backend registered.
- Regression test `ms_engine_load_test`: create → serialise → shutdown (assert cold) → load → assert initialized, `render_frame()` true, and size restored from the file.

## 2026-11-15 — Effect catalogue and pattern wipes
- **14 new CPU effects**, all original implementations of standard operations — no GPL code or XML copied from Kdenlive/MLT/frei0r (Kdenlive is GPL-3.0 and is only a capability reference here): saturation, hue shift, levels, white balance, posterize, sharpen, edge detect (Sobel), emboss, pixelate, glow (blur screened back), film grain, box blur (running sum, O(n) per pass), crop, threshold.
- **Two effects that were dead code are now reachable**: `cpu_threshold` and `cpu_chromakey_simple` existed in `cpu_effects_extra.cpp` but no dispatch branch called them.
- **23 effects exposed in the UI**, grouped into Basic / Colour / Stylise / Framing, replacing the flat 8-item list.
- **3 pattern wipes**: blinds horizontal, blinds vertical and checkerboard, appended to the enum so existing project files keep their integer ids. Transitions now total 23 implemented and 23 listed.
- **New test `ms_effects_catalogue_test`** applies all 15 catalogue entries through `cpu_effect_apply` and fails on a passthrough (mean per-channel delta < 0.5), then checks an unknown id reports `EffectNotFound`. Note the first run failed five cases because saturation/hue/levels/white balance/crop are deliberately identity at their defaults — the test now drives them with off-default parameters.
- Verified: `ctest` **14/14**, `:app:testDebugUnitTest` **6/6**, `:app:assembleDebug` green.

## 2026-11-13 — Real decoding on Android
- **MediaCodec decoder**: `media/android_decode.cpp` implements `IMediaDecoder` over `AMediaCodec` + `AMediaExtractor`, and `create_media_decoder` now prefers it on Android. Before this the only decoder was a shell-out to the `ffmpeg` CLI, which does not exist on Android, so every imported clip silently fell through to `SlateDecoder` and rendered as a flat placeholder colour. Handles H.264/HEVC/VP9 platform codecs, keyframe seeking, and NV12/I420/YV12/flexible YUV output with stride and slice-height from the reported format. Links `libmediandk`; no bundled binaries, no new licence obligations.
- **Audio on Android**: `Android_decode_audio` decodes the extractor's audio track to float PCM, and `AudioDecoderFFmpeg::decode_window` uses it when ffmpeg is absent. That makes timeline playback, the export AAC track and the waveform real on device instead of silent.
- **Honest import metadata**: `ProjectSession::import_media` asks a real decoder for duration/size/audio presence before falling back to the container sniff, which used to hardcode 10 s, 1920×1080 and "has audio" for every MP4.
- **Decoder backend is visible**: `ms_decoder_backend()` reports `mediacodec` / `ffmpeg` / `slate`; the session sheet shows it in red when it is `slate`, so a placeholder can never masquerade as footage.
- **All effects exposed**: chroma key was implemented in `cpu_effects.cpp` but missing from `EffectsSheet` — now listed, bringing the UI to all nine.
- **All transitions exposed**: `MotionSheet` listed 5 of the 20 implemented transitions; it now lists all 20 (dissolve, 3 fades, 9 wipes, 4 slides, iris, zoom, dip).
- Verified: `ctest` **13/13**, `:app:testDebugUnitTest` **6/6**, `:app:assembleDebug` green, both ABIs linked against `libmediandk.so` with 28 `AMediaCodec`/`AMediaExtractor`/`AMediaFormat` call sites. ⚠️ Still never executed on a device.

## 2026-11-13 — Waveforms, per-clip grade, masks, markers, project settings
- **Timeline waveform**: `TimelineAudioPlayback::clip_peaks` computes a `bins`-wide peak envelope from the decoded PCM it already caches for playback, so asking for a waveform also warms the audio. `ms_clip_peaks` → JNI → `EditorEngine.peaks` (cached per clip/range) → audio clips draw real bars behind their label. Clips with no decodable audio return empty rather than a fake shape.
- **Per-clip grade**: `ClipInfo` gained its own seven-parameter grade plus `grade_enabled`, applied in `render_clip` **after** the clip's effects and **before** compositing, so shots can be matched without touching the master grade. Serialized as `grade_enabled` + `grade: [...]`. `ColorSheet` now switches between **Sequence** and **Selected clip** with the same seven sliders.
- **Masks**: the long-dormant `comp/mask.cpp` is finally used — rectangle, ellipse or full frame, normalised centre/size, feather in pixels and invert, multiplied into the clip's alpha before its transform. `MaskSheet` drives it; persisted as `mask_enabled` + `mk_*` keys.
- **Markers**: session markers with names, `add_marker`/`remove_marker`, serialized as `"markers"`, listed and added/removed in the session sheet.
- **Project settings**: `Engine::set_project_settings` changes size and frame rate without discarding edits; size presets (1080×1920, 1920×1080, 1080×1080, 2160×3840) and 24/30/60 fps in the session sheet.
- Fixed a duplicated `ms::TimelineMarker` definition: it now lives in `ms_project.hpp` (so it can be serialized) and `ms_edit_ops.hpp` only declares the helpers that use it.
- Verified: `ctest` **13/13** (the edit suite now also covers mask rasterisation, mask/grade/marker persistence and the clean-failure paths of `clip_peaks`), `:app:testDebugUnitTest` **6/6**, `:app:assembleDebug` green.

## 2026-11-13 — Titles that actually render
- **Text engine**: `native/cpp/text/text_render.cpp` rasterises real glyphs with stb_truetype 1.21 (public domain). Supports UTF-8, kerning, tracking, multiple lines, greedy word wrap against a box width, align (left/centre/right), anchor (top/middle/bottom), fill colour, outline with configurable colour, drop shadow, and uppercase. Fonts are cached per path; resolution falls back through the host font dir, the repo asset tree and `MS_FONT_DIR`.
- **Bundled faces**: Anton, Bebas Neue, Archivo Black, Oswald, Playfair Display and Lobster (all SIL OFL 1.1, licences in `third_party/fonts/titles/`). `TextFonts` unpacks them from assets to `filesDir/fonts` on first launch and hands the path to the engine.
- **Text clips are first-class**: `ClipInfo` gained `is_text`/`text`/`TextStyle`; the clip renderer calls the rasteriser instead of a decoder, so titles inherit keyframed transform, opacity, effects, grade and transitions. Serialised as `is_text` + `tx_*` keys in `.msproj`.
- **C ABI / JNI**: `ms_text_set_font_dir`, `ms_text_add_clip`, `ms_text_update_clip`, `ms_text_measure` and their JNI mirrors.
- **UI**: `TextSheet` — text field, six typefaces, size/tracking/line-spacing/outline/shadow/wrap sliders, fill and outline swatches, align/anchor, uppercase, add-at-playhead and live re-render of the selected title. Reached from a new **Title** button on the tool rail.
- **`ms_title_demo`**: renders sample title cards to PPM for inspection without a device; used to confirm glyphs, outlines and shadows visually.
- Verified: `ctest` **13/13** (the edit suite now checks font resolution, rasterised coverage, wrapping and title persistence), `:app:testDebugUnitTest` **6/6**, `:app:assembleDebug` green, 62 MB APK with all six fonts in `assets/text/`.

## 2026-11-12 — Grade, scopes, keyframes, transitions, audio export
- **Master grade**: `ms_grade_set` / `ms_grade_clear` apply exposure, contrast, saturation, temperature, lift, gamma and gain through `apply_finishing_grade` after the frame cache. `ColorSheet` has all seven sliders plus a live RGB/luma histogram drawn from `Scopes::histogram` over the last rendered frame.
- **Keyframes**: `ms_keyframe_add` / `ms_keyframe_clear` for opacity, scale, pos_x and pos_y; curves persist in `.msproj` (`keys_opacity` etc.) and are evaluated per frame.
- **Transform compositing**: `TimelinePlayer::blit_transformed` honours keyframed scale and position, not just opacity — previously scale/position curves were stored and ignored.
- **Transitions**: `comp/transitions.cpp` was **not in the build**; it is now compiled, and `TimelineTransition` (id, outgoing, incoming, type, duration, softness) lives on `TrackInfo`, serializes to `.msproj`, and is blended by `apply_transition` centred on the cut. `add_transition` / `remove_transition` are bridged; five transition types have a UI.
- **Renderer rewrite**: each clip now renders into a transparent premultiplied buffer which is composited with an explicit over-operator, so transitions and per-clip alpha are correct. With no transition the output is unchanged (all 13 native suites still pass).
- **Audio in export**: `VideoExporter` now feeds an AAC encoder from `ms_audio_read_pcm` alongside the H.264 video, buffering samples until both output formats are known, then muxing both tracks.
- Toolchain note: the cloud runner was restored mid-task; the JDK/SDK/NDK/Gradle/ffmpeg were reinstalled under `~/tools` and `~/android-sdk`.
- Verified: `ctest` **13/13**, `:app:assembleDebug` + `:app:testDebugUnitTest` green. Still not runtime-verified on a device.

## 2026-11-12 — Editing, effects, tracks, export
- **Clip editing bridged and wired**: `ms_clip_remove/move/trim/ripple_delete`; timeline drag moves a clip, right-edge drag trims it, long-press deletes, and drags snap to the playhead and neighbouring clip edges (0.12 s tolerance).
- **Track management**: `ms_track_add/remove/flags`; `+` adds a track, tap toggles mute, long-press removes.
- **Effect stack**: `ms_effect_add/remove/set_param/set_enabled` plus `EffectsSheet` (eight CPU effects, live sliders, enable switch, remove). Previously nine engine effects were unreachable from the UI.
- **Effect persistence**: clip effects are serialized in `.msproj` and restored on load — they used to be memory-only.
- **Frame cache correctness**: every edit/effect change calls `invalidate_frames()`; stale frames could previously outlive a trim or an effect change.
- **Real video export**: `VideoExporter` renders from the engine and encodes H.264 via `MediaCodec` + `MediaMuxer` (RGBA→NV12 conversion in Kotlin). Video only; audio muxing is not wired.
- Imports now land in `filesDir/imports` rather than `cacheDir`, which Android may purge.
- New `ms_edit_test` (move/trim/test coverage of effects, JSON round-trip, undo, delete): native suite is now **13/13**.
- Honest status: no APK has ever been launched here, so export and gestures are compile-verified only.

## 2026-11-12 — Open-source assets and 3D LUT looks
- Bundled **Inter 4.1** and **JetBrains Mono 2.304** (both SIL OFL 1.1) as real font resources; `MsFonts.sans` drives the Compose typography and `MsFonts.mono` drives every numeric readout (timecode, ruler, track labels). Licenses travel with them in `third_party/fonts/`.
- New `native/cpp/color/lut.cpp` + `ms_lut.hpp`: an Adobe `.cube` reader (TITLE / LUT_3D_SIZE / DOMAIN handling, red-fastest ordering) with trilinear sampling and a strength blend, applied to RGBA8 frames.
- Engine: `load_look` / `clear_look` / `set_look_strength`. The look is applied **after** the frame cache, so changing a LUT or its strength never needs a cache flush and never poisons cached frames.
- Bridge + JNI: `ms_look_load`, `ms_look_clear`, `ms_look_set_strength`, `ms_look_strength` → `NativeEngine.nativeLook*`.
- Six looks baked by `scripts/gen_looks.py` into `android/app/src/main/assets/looks/` (Teal & Orange, Bleach Bypass, Warm Film, Cool Noir, Vivid Punch, Fade Matte), 17³ each, ~130 KB.
- Kotlin: `LooksSheet` (apply/strength/None), a “Look” rail button, and a `LOOK · NAME` badge in the viewer header.
- Verified: `ms_lut_test` added to the native suite (identity cube, channel swap, half-strength blend, alpha preservation, garbage/short/1D/non-unit-domain rejection) → **12/12 CTest pass**. `gradle :app:assembleDebug` → 60 MB APK containing the six cubes, seven TTFs and the new JNI symbols; `:app:testDebugUnitTest` → 6/6.
- Not verified: visual result of each look on device (no emulator/device in this environment).

## 2026-11-12 — Flutter removed: Kotlin + C++ app
- Deleted the entire `flutter_ui/` tree (Dart UI, FFI bindings, Flutter web scaffolding, Dart tests). No Dart remains in the repository.
- Replaced the Android scaffold with a real Gradle project: AGP 8.7.2, Kotlin 2.0.21, Jetpack Compose, `compileSdk 35`, `minSdk 29`, NDK 27 + CMake building `native/cpp` through `MS_BUILD_TESTS=OFF`.
- Added `android/app/src/main/cpp/ms_jni_bridge.cpp`: JNI over the existing C ABI, including `AndroidBitmap` frame copy for the preview and `FloatArray` PCM pull for audio.
- New Kotlin layers: `NativeEngine` (sole JNI surface), `EditorEngine` (session + Compose state), `PreviewView` (real `ms_render_frame` output with pinch-zoom), `TimelinePane` (one shared horizontal scroll for ruler + lanes, playhead inside the scrolled content), `NativeAudioPlayer` (`AudioTrack` consuming the engine mix), `ProjectStore`/`SettingsStore`, starter/settings screens.
- The C++ engine gained `Engine::new_project(name, width, height, fps)` and the bridge export `ms_project_new`, so Kotlin can create named projects without touching the document schema.
- Project documents stay owned by C++: Kotlin parses the engine's JSON for drawing only and saves through the engine.
- Verified: `gradle :app:assembleDebug` produces a 58 MB debug APK containing `libmotionstudio_jni.so` and `libmotionstudio_native.so` for `arm64-v8a` and `x86_64`; `gradle :app:testDebugUnitTest` passes 6/6; the desktop native suite still passes 11/11 with `MS_BUILD_TESTS=ON`.
- Not verified: installation or interaction on a physical device/emulator (no KVM access in the build environment). Hardware video decode and video encoding remain unwired; PNG frame export works.

## 2026-11-12 — Engine timeline audio output
- `Engine` now owns a `TimelineAudioPlayback` bound to the active session: `init`, project load and `shutdown` rebind/stop it, `seek` moves the audio playhead with the video playhead, `play`/`pause` drive the audio transport, and every `render_frame` tick keeps both clocks on the same time.
- Added `Engine::read_audio`, `Engine::audio_info` and `Engine::audio_effects` (master insert chain).
- New C ABI for hosts: `ms_audio_state`, `ms_audio_read_pcm`, `ms_audio_set_master_gain`, with matching FFI bindings (`MSAudioState`, `readAudioPcm`, `setMasterGain`) in `flutter_ui/lib/bridge/native_bridge.dart`.
- Added `ms_engine_audio_test` (CTest 11/11): imports a generated tone, proves the engine mix has energy, the -20 dB master gain stage attenuates it, the audio playhead follows bridge seek/render ticks, and paused playback emits silence.
- Platform audio device output (AAudio/OpenSL/desktop callback) remains host-side; the engine now supplies the PCM and chain for it.

## 2026-11-17 — Master audio insert chain and a working mixer
- The `AudioEffectChain` that already lived in C++ (Gain, 3-band parametric EQ, compressor, delay, Schroeder reverb, limiter) is now a real part of the app instead of a test-only class: the master bus is editable from the UI and runs for playback and export.
- New C ABI in `ms_bridge.h`: `ms_audio_fx_count/add/remove/clear/kind/set_enabled/enabled/set_param/get_param`. Parameters are addressed by the names in `ms::AudioFxParams` (`wet`, `gain_db`, `low_freq_hz`, `threshold_db`, `ratio`, `delay_ms`, `room_size`, `ceiling_db`, …); an unknown name is rejected rather than silently ignored, so a typo cannot leave a dead slider.
- New `ms_track_gain(track_id, gain)` and `Engine::set_track_gain` (undoable) expose the per-track fader that `TrackInfo::gain` already carried.
- Fixed a real bug in the timeline audio path: `TimelineAudioPlayback::read_pcm` used `clip.opacity` as a gain proxy, so a clip's own gain, pan and track gain were never heard, clips flagged `mute` still played, and solo did nothing. It now honours clip gain × track gain, pan, clip mute and track solo (any soloed audible track mutes the rest).
- Android: ten JNI entry points (`nativeAudioFx*`, `nativeTrackGain`), `NativeEngine` externals, `EditorEngine.audioInserts/addAudioInsert/removeAudioInsert/clearAudioInserts/setAudioInsertEnabled/audioInsertParam/setAudioInsertParam/setTrackGain/toggleTrackSolo`, plus `NativeTrack.gain`/`.solo` parsed from the engine JSON. New `AudioSheet` (rail **MIX**): master inserts with per-kind knob tables matching `AudioFxParams`, bypass switches and removal, plus a track mixer strip per track with a fader (dB readout), MUTE and SOLO.
- Verified: `ctest` **17/17** (new `ms_audio_bridge_test`: 33 checks over add/remove/clear/kind/bypass/named parameters/rejection paths/master gain compatibility/track fader); `:app:assembleDebug` and `:app:testDebugUnitTest` pass; JNI audit — 92 Kotlin `external fun` against 93 symbols in `libmotionstudio_jni.so`, none missing.
- Not verified: actual audio output, slider response and sheet layout (no device or emulator in this environment).

## 2026-11-17 — Master grade and insert chain saved with the project
- The two pieces of finish state that used to die with the process are now part of the project document: `ProjectSession` carries a `MasterState` (`grade_enabled`, the seven grade values and the master insert list) and writes it as a `"master"` object, with every writable `AudioFxParams` field emitted from one field table (`wet`, `gain_db`, `low_freq_hz`, `threshold_db`, `attack_ms`, `room_size`, `ceiling_db`, …). A document without a `master` block loads as "no grade, no inserts", so older projects still open.
- `Engine::sync_master_state()` pushes the live grade and chain into the document, `Engine::apply_master_state()` pushes a loaded document back into the engine. Load, new project, undo and redo all call the latter, so a document is the single truth after any of them.
- Insert **structure** edits are undoable now (add/remove/clear/bypass go through `Engine::add_audio_insert`, `remove_audio_insert`, `clear_audio_inserts`, `set_audio_insert_enabled`, each taking a snapshot first). Knob values are persisted but do not push an undo entry per slider frame — one drag would otherwise bury every timeline edit under the 50-snapshot cap. The grade behaves the same way: saved, not on the undo stack.
- New `ms_grade_get` / `Engine::get_grade` and JNI `nativeGradeGet`, so the UI can read the grade back; `EditorEngine.reload()` now calls `syncGradeFromEngine()` and bumps `audioRevision`, which means the colour sliders and the MIX sheet follow an undo, a redo or a project load instead of showing stale values.
- Verified: `ctest` **17/17** — `ms_edit_test` gained checks that the master grade and insert parameters survive `project_to_json`/`load_project_json`, that removing an insert is undoable, and that an undo of a chain edit keeps the timeline; `:app:assembleDebug` and `:app:testDebugUnitTest` pass; JNI audit — 93 Kotlin `external fun` against 94 symbols in `libmotionstudio_jni.so`, none missing.
- Not verified: anything that needs a running app (no device or emulator here): slider round-trip, sheet refresh after undo and the heard result of a restored chain.
