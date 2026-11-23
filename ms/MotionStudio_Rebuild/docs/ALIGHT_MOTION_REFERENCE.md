# Alight Motion — how it works, import to edit

A behavioural reference for Motion Studio, written from Alight Creative's public Help
Center, product page and Play listing. This file records *behaviour* only: no code,
shader, string, curve table or asset from Alight Motion is copied into this repository.
Its own documentation is silent on several points; those are marked **unverified**
rather than filled in from guesswork.

Sources are listed at the end.

## The short version

Alight Motion is not a video cutter. It is a **motion-graphics compositor that accepts
video**: the document is a project of layers, every layer property is animatable at any
time, and export renders that. Trimming a clip is a secondary operation; keyframing a
property is the primary one.

That single decision explains the interface: no trim handles on every edge, no separate
"cut" tab, a keyframe marker next to every numeric field, and a curve editor as a
first-class screen instead of a hidden menu.

## 1. Import

- **Add Layer → (+)** is the entry point. The official Quick Start describes the workflow
  as "Add, Adjust, Animate". Media, text, shapes, graphics, audio, camera and null
  objects all enter through that one menu and each becomes a **layer** with its own
  timeline.
- **Project vs Element.** A project is the working document. An **Element** is a reusable
  component with its own editable timeline and settings, kept in a separate library;
  inserting one keeps it linked to the original until it is converted to a group.
- **File access.** On Android, media permissions must be granted first; on iOS, media must
  live in Photos or in the app's folder in Files, and audio is added from the app folder.
- **No published codec matrix.** The "Failed Starting Asset Reader" article names three
  causes — unsupported format, media too heavy for RAM, unsupported encoding — without
  listing which formats work.
- **Storage.** Alight states iOS media shared with Photos/music may be stored only once,
  which argues against "import always copies". Whether Android copies, references or
  caches on import is **unverified**.
- **No proxy pipeline.** Nothing describes automatic proxies. Instead the Performance
  Guide tells the user to convert oversized sources down, and the Android build has a
  **low-quality preview** toggle that lowers preview resolution without changing export
  resolution. That is a preview-quality switch, not proxy media.

**For Motion Studio.** The add menu should offer *types* (media, text, shape, null,
camera, adjustment) in one place. Preview quality should be a setting separate from
export settings — Motion Studio already generates real proxies, which is ahead of Alight
here, but they are hidden behind a sheet section rather than exposed as a quality control.

## 2. The editing model

| Concept | How Alight Motion treats it |
|---|---|
| Layer | The unit: video, image, text, shape, audio, camera, null. Stacked in a timeline. |
| Keyframe | Every setting is animatable. N keyframes on a property give N−1 **intervals**, each with its own easing. |
| Easing | Linear, ease-in, ease-out, ease-in-out presets, plus Bounce, Elastic, Elastic Steps, Cyclic, Random and Steps. The curve dialog plots project time horizontally against remapped animation time vertically. |
| Hold | Reachable through **Steps** easing; no separate hold mode is documented. |
| Curve editor | Property-specific. The selected property's keys are solid white diamonds with a dark outline; other properties' keys are faint. The playhead must sit inside an interval for its curve to appear. |
| Parenting | A layer has at most one parent; a parent can have many children; chains are allowed. The parent drives child translation, rotation and scale, never the reverse. Timeline order is unrelated to parenting. |
| Null object | An invisible control layer used as a parent, to split axes, or to share one motion across layers. Drawn as a wireframe while editing, absent from export. |
| Groups | Combine layers and act as the container for masking and effects. Converting to a group also detaches an Element instance. |
| Effects | 100+ effects named in official docs, 160+ building blocks in the Play listing, applied per layer with parameters. Order matters, and for shape-changing effects it is not a naive serial chain (Motion Blur is the documented example). |
| Adjustment layer | **Not confirmed** by any official source, despite being common elsewhere. |
| Blend modes / alpha | Named on the product page; video transparency export is Apple-only. |

**For Motion Studio.** The two features with the most editorial value per line of engine
code are **parenting + null objects** and **per-interval easing with a curve editor**.
Motion Studio already has per-key interpolation and a curve editor. Parenting is the gap
that decides whether a scene can be built like a rig.

## 3. Timeline and gesture conventions

- **Playhead + layer rows.** Time along the top, layers stacked below, keyframes on the
  layer row for the selected property.
- **Keyframe markers** are diamonds; selection is a filled white diamond with a dark edge.
- **Canvas gestures are modal.** Pan/zoom requires the magnifying-glass (Pan and Zoom
  Mode). Two-finger pinch zooms; two-finger double-tap recentres. Critically, **with a
  layer selected, two-finger gestures transform that layer instead of panning the
  canvas.** The explicit mode toggle exists to resolve that ambiguity.
- The exact tap-and-hold-to-add-keyframe and keyframe-drag gestures are **not documented**
  by Alight; treat them as version behaviour to test, not as a specification.

**For Motion Studio.** Motion Studio's viewport has no pan/zoom mode, so a two-finger
gesture can only mean one thing at a time. A visible pan/zoom toggle, plus two-finger
transform when a clip is selected, would remove the ambiguity that makes canvas work feel
unreliable on a phone.

## 4. Export and sharing

- **Formats:** MP4 (H.264 and HEVC), GIF, PNG sequence, stills.
- **Settings:** resolution, frame rate, quality/bitrate, and a codec choice on iOS.
- **Project Package:** the documented transfer path — Export → Project Package produces an
  **Alight link and QR code**; opening it in the app imports the project, editable.
  Projects therefore travel as a package plus a share link, not as a bare file.
- **Storage:** projects live on the device, not in the account. Uninstalling deletes them;
  Project Package is the backup story.
- **Internal format:** third-party reverse engineering reports XML scene files. No project
  format is officially published, so a stable `.am` interchange spec is **unverified**.

**For Motion Studio.** A share package (archive + link/QR) is cheap and high-value.
Motion Studio's project document is already JSON, so this is a packaging problem, not a
format problem.

## 5. Free vs paid

The free tier renders with a watermark; membership removes it and unlocks premium effects
and features. The editing model itself is not paywalled — the paywall sits on output and
on the effect library. Minimum device requirement is 1.5 GB RAM.

## 6. What this means for Motion Studio

1. **Keep the project-as-composition model** — tracks, clips, keyframes, comp network.
2. **Parenting and null objects — landed.** Clips carry a `parent_id`, a null object is a
   control layer with no pixels, and the player composes the parent chain's translation and
   scale per frame (`ms_parent_test`). Opacity is intentionally not inherited. The UI row
   lives in Motion; Session adds the null.
3. **Pan/zoom is explicit — landed.** The viewport has a visible PAN / ZOOM toggle. On, two
   fingers pan and pinch the canvas. Off with a layer selected, a two-finger gesture moves
   and scales that layer and commits position and scale keys at the playhead; off with
   nothing selected it falls back to canvas panning. The only documented gesture left out
   is two-finger double-tap recentre, which the app does not need yet.
4. **Preview quality — landed.** Full / Half / Quarter in Settings and Session set the
   composite scale (100/50/25%) in the engine; proxies remain the separate, automatic
   decode path. Export and the still export force full resolution, so the setting cannot
   leak into delivery.
5. **Ship a share package** for a project: one archive, one link.
6. **Copy behaviour, never content.** The provenance rule in `CHANGELOG.md` applies to
   effect names, parameter text, curves and shaders too. Everything above is described in
   our own terms.

## Sources

- [Alight Motion Quick Start Guide](https://support.alightmotion.com/hc/en-us/articles/10536777320337-Alight-Motion-Quick-Start-Guide)
- [Elements: The Complete Guide](https://support.alightmotion.com/hc/en-us/articles/10536791122449-Elements-The-Complete-Guide)
- [Layer Parenting and Null Objects](https://support.alightmotion.com/hc/en-us/articles/10536997444369-Layer-Parenting-and-Null-Objects)
- [Animation Easing Curves](https://support.alightmotion.com/hc/en-us/articles/10536934703889-Animation-Easing-Curves)
- [Preview Pan and Zoom](https://support.alightmotion.com/hc/en-us/articles/10536990235409-Preview-Pan-and-Zoom)
- [Performance Guide](https://support.alightmotion.com/hc/en-us/articles/10537513451793-Performance-Guide)
- [Back up my projects (Project Package)](https://support.alightmotion.com/hc/en-us/articles/13724965664785-How-do-I-back-up-my-projects)
- [Effects Guide](https://support.alightmotion.com/hc/en-us/articles/10536991943569-Effects-Guide)
- [Failed Starting Asset Reader](https://support.alightmotion.com/hc/en-us/articles/16144444402065-Failed-Starting-Asset-Reader)
- [Google Play listing](https://play.google.com/store/apps/details?id=com.alightcreative.motion)
- [alightmotion.com](https://alightmotion.com/)
