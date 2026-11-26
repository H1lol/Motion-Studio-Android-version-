# Industry-standard review, and what this app will actually do about it

The specification in `Motion_Studio_Industry_Standard_Spec.txt` describes a desktop
suite: Nuke-class compositing, Houdini-class procedural work, After Effects motion
graphics and Blender-class 3D in one application. This document records what the
Android app already has, what it deliberately will not build, and the order the
remaining work should happen in.

The app is a 7.9 MB APK targeting phones with 2–4 GB of RAM. That is the constraint
every decision below is measured against.

## Covered today

| Spec area | Where it lives |
|---|---|
| Atomic, versioned project file | `engine/project_session.cpp` writes `path.tmp`, then renames; the document carries `format`/`version` |
| Autosave and recovery | `autosave.cpp` + `nativeAutosaveProject`; the Android app writes a recovery copy on pause/stop and offers it back on launch |
| Cache and proxies | `cache_proxy.cpp`, `proxy_registry.cpp`, `ProxyStore`, `ProxyGenerator` (720p H.264 stand-ins, generated on device) |
| Colour pipeline | `color/color_pipeline.cpp`, `color/lut.cpp`, `color/scopes.cpp`, `color/finishing.cpp`; ACES/OCIO references |
| Text | `text/text_render.cpp` with six bundled families, tracking, outline, shadow, reveal |
| Effects | ~60 CPU effects across `effects/cpu_effects*.cpp`, plus the MSP catalogue and OpenFX host stub |
| Timeline | `engine/edit_ops.cpp`, `engine/project_session.cpp`, insert/overwrite, masks, parenting, nulls |
| Keyframes | `anim/animation.cpp`, six interpolation modes, curve editor, expression baking |
| Node graph | `graph/node_graph.cpp`, comp network with typed sockets |
| Export | `media/export_engine.cpp` plus `VideoExporter.kt`: MediaCodec H.264/HEVC + AAC, no ffmpeg binary |
| Diagnostics | `diag/Diagnostics.kt`: crash handler, native signal handler, breadcrumb trail, report file |

## Gaps the spec exposes, in the order they matter

### 1. Verification (was the largest gap)

- **Golden-image tests.** `ms_render_golden_test` renders a fixed scene through
  eleven effects and compares against `native/cpp/tests/golden/*.pam` with a
  tolerance (max channel 8, mean 0.75). It also asserts each effect changes pixels
  and that a repeated render is byte-identical. Regenerate deliberately with
  `ms_render_golden_test --write` after looking at the diff.
- **CI.** `.github/workflows/android.yml` now runs the native suite (`ctest`,
  ffmpeg installed for the audio fixture) and builds the release APK, not only the
  debug one. Before this, 32 native tests and the R8/minified build were never
  exercised by CI.
- **Device validation** is still the missing half: nothing has been installed on a
  phone. The diagnostics report in Settings is the route to closing it.

### 2. Never lose work

Autosaves were written but never offered back, so an OS kill at the wrong moment
lost the session anyway. `ProjectStore.recoverable()` now lists autosaves newer
than their project file, the starter screen shows them as **Unsaved work**, and
`EditorEngine.openRecovered()` reopens one as a dirty document that saves to a
normal file. `ON_STOP` forces an autosave rather than waiting out the ten-minute
interval.

### 3. Cancel anything long

Import, proxy generation and export were all unstoppable. Export and proxy
generation now poll a cancellation callback once per frame/iteration, delete the
partial file and still run the cleanup that restores proxies, preview scale and
the playhead. Both screens expose a Cancel button while busy.

### 4. Not planned

Sculpting, retopology, rigging, character animation, solvers (pyro/FLIP/cloth),
hair/crowds/terrain, USD/Hydra/OpenVDB, render farms, multi-user collaboration,
VST3 hosting, DCP/IMF delivery, an embedded Python interpreter, and multi-monitor
desktop UI. The expression baker in the SCRIPT pane is this app's answer to
"scripting" — a real interpreter is not affordable on a phone and not needed for
the workflows the app targets.

## Phased plan

**Phase A — trustworthy (current)**
Golden images, native CI, release-APK CI, autosave recovery, cancelable
import/proxy/export, device validation via the diagnostics report.

**Phase B — finish the editor that exists**
Auto-proxy on import instead of a menu action; keyframe dragging in the curve
editor (still the largest "unfinished" tell); text depth (per-character animators,
safe margins); audio waveform and sync verification; export queue with resume;
SRT/WebVTT captions.

**Phase C — one discipline deeper, not four**
Mobile compositing: spline roto, paint/clone, chroma keyer, 2D point tracking.
These are the only genuinely missing P0 items in the spec's §17, and they are what
makes the app useful beyond social video.

## Acceptance tests that apply

Adapted from §25; the rest describe a desktop product.

- [ ] Produce an animated title package and export delivery versions.
- [ ] Conform an edit from OpenTimelineIO or EDL with correct timecode.
- [ ] Render the same project headless on another machine with matching output.
- [x] Survive a forced crash and recover the work *(implemented; needs a device run)*
- [ ] Complete a short film made by artists who are not the developers.

## Licensing notes

The project is GPL v3. Autodesk FBX SDK is proprietary (use `ufbx` if FBX import
ever matters). H.264/H.265 carry patent licensing independent of the GPL. No code,
shader, icon, parameter text or asset from Alight Motion or Nova Vision may enter
this tree; those tools are behavioural references only.
