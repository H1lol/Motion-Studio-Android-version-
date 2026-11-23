package com.motionstudio.app.native

import android.graphics.Bitmap
import android.os.Build
import com.motionstudio.app.diag.Diagnostics

/**
 * Direct binding to the Motion Studio C++ engine (`libmotionstudio_jni.so`).
 *
 * Every call forwards to the C ABI in `native/cpp/bridge/ms_bridge.h`; the
 * project document, timeline, compositing, audio mix and export all live in
 * C++. This object is the only place the UI touches native code.
 */
object NativeEngine {

    /**
     * False when `libmotionstudio_jni.so` could not be loaded: a 32-bit device,
     * a missing dependent library or a stripped build. Every `external fun`
     * throws once this is false, so the UI checks it before calling anything
     * rather than letting an `UnsatisfiedLinkError` escape from a gesture.
     */
    val available: Boolean

    /** Why loading failed, when it did. */
    val loadError: String?

    init {
        var ok = false
        var error: String? = null
        try {
            System.loadLibrary("motionstudio_jni")
            ok = true
        } catch (t: Throwable) {
            error = buildString {
                append(t.javaClass.simpleName)
                append(": ")
                append(t.message ?: "no message")
                append(" · supported ABIs: ")
                append(Build.SUPPORTED_ABIS.joinToString(", "))
            }
            android.util.Log.e("MotionStudio", "native library failed to load", t)
        }
        available = ok
        loadError = error
        Diagnostics.nativeLoaded = ok
        Diagnostics.nativeError = error
    }

    /**
     * Installs the native signal handler. A segfault in the engine kills the
     * process before the Java handler can run, so it writes its own report into
     * [dir] (`native-crash.txt`).
     */
    external fun nativeInstallCrashHandler(dir: String): Boolean

    // Lifecycle
    external fun nativeInit(width: Int, height: Int): Boolean
    external fun nativeNewProject(name: String, width: Int, height: Int, fps: Double): Boolean
    external fun nativeShutdown()

    // Document
    external fun nativeProjectLoad(json: String): Boolean
    external fun nativeProjectToJson(): String
    external fun nativeSaveProject(path: String): Boolean
    external fun nativeAutosaveProject(path: String): Boolean

    // Transport
    external fun nativeSeek(seconds: Double)
    external fun nativePlay()
    external fun nativePause()
    external fun nativePreviewState(): DoubleArray

    // Rendering
    external fun nativeRenderFrame(): Boolean
    /** Changes whenever the next render would differ from the last one. */
    external fun nativeRenderSignature(): Long
    external fun nativeFrameBytes(): Long
    external fun nativeCopyFrame(bitmap: Bitmap): Boolean

    // Preview quality: a composite scale (0.25..1) applied to the preview only.
    // Export forces it back to 1 so the delivered master stays full-size.
    external fun nativePreviewScale(): Float
    external fun nativePreviewScaleSet(scale: Float): Boolean
    /** Size of the frame the preview currently produces, into [w, h]. */
    external fun nativePreviewFrameSize(out: IntArray): Boolean
    /**
     * Picks the layer under a normalized point (0..1) of the composite at the
     * playhead, into `[trackId, clipId]`. False when the tap missed every layer.
     */
    external fun nativePreviewHitTest(nx: Float, ny: Float, out: IntArray): Boolean

    // Proxy media: preview decodes the registered stand-in, export turns proxies
    // off first so the delivered master always comes from the source file.
    external fun nativeProxyRegister(media: String, proxy: String): Boolean
    external fun nativeProxyUnregister(media: String)
    external fun nativeProxyClear()
    external fun nativeProxyCount(): Int
    external fun nativeProxyFor(media: String): String
    external fun nativeProxySetEnabled(on: Boolean): Boolean
    external fun nativeProxyEnabled(): Boolean

    // Editing
    external fun nativeImportMedia(path: String): Int
    external fun nativeAddClip(trackId: Int, mediaId: Int, start: Double, duration: Double): Int

    // Retime + adjustment layers. Speed stretches the clip's timeline length so
    // the same slice of source plays faster; adjustment layers own no pixels and
    // run their effect stack over everything the tracks below composited.
    external fun nativeClipSpeedSet(trackId: Int, clipId: Int, speed: Float): Boolean
    external fun nativeAdjustmentAdd(trackId: Int, start: Double, duration: Double): Int

    // Null objects and layer parenting. A null is an invisible control layer;
    // a child inherits its parent's translation and scale at the playhead.
    external fun nativeNullAdd(trackId: Int, start: Double, duration: Double): Int
    external fun nativeClipParentSet(trackId: Int, clipId: Int, parentId: Int): Boolean
    /** Parent clip id (0 = none), or -1 when the clip is unknown. */
    external fun nativeClipParentGet(trackId: Int, clipId: Int): Int
    external fun nativeSplitClip(trackId: Int, clipId: Int, at: Double): Boolean
    external fun nativeUndo(): Boolean
    external fun nativeRedo(): Boolean

    // Audio
    external fun nativeAudioState(): DoubleArray
    external fun nativeReadAudioPcm(buffer: FloatArray, frames: Int, channels: Int): Boolean
    external fun nativeSetMasterGain(db: Float): Boolean
    /** Peak L, peak R, momentary, short-term, integrated (LUFS) from the mix. */
    external fun nativeAudioMeter(out: FloatArray): Boolean

    // Master insert chain (0 Gain, 1 EQ, 2 Compressor, 3 Delay, 4 Reverb, 5 Limiter)
    external fun nativeAudioFxCount(): Int
    external fun nativeAudioFxAdd(kind: Int): Int
    external fun nativeAudioFxRemove(index: Int): Boolean
    external fun nativeAudioFxClear(): Boolean
    external fun nativeAudioFxKind(index: Int): Int
    external fun nativeAudioFxSetEnabled(index: Int, enabled: Boolean): Boolean
    external fun nativeAudioFxEnabled(index: Int): Boolean
    external fun nativeAudioFxSetParam(index: Int, name: String, value: Float): Boolean
    external fun nativeAudioFxGetParam(index: Int, name: String, fallback: Float): Float
    external fun nativeTrackGain(trackId: Int, gain: Float): Boolean

    // Per-track insert chain (same kinds/parameter names as the master chain)
    external fun nativeTrackFxCount(trackId: Int): Int
    external fun nativeTrackFxAdd(trackId: Int, kind: Int): Int
    external fun nativeTrackFxRemove(trackId: Int, index: Int): Boolean
    external fun nativeTrackFxClear(trackId: Int): Boolean
    external fun nativeTrackFxKind(trackId: Int, index: Int): Int
    external fun nativeTrackFxSetEnabled(trackId: Int, index: Int, enabled: Boolean): Boolean
    external fun nativeTrackFxEnabled(trackId: Int, index: Int): Boolean
    external fun nativeTrackFxSetParam(trackId: Int, index: Int, name: String, value: Float): Boolean
    external fun nativeTrackFxGetParam(trackId: Int, index: Int, name: String, fallback: Float): Float

    // Title reveal: 0 none, 1 type-on, 2 word-by-word, 3 line-by-line
    external fun nativeTextRevealSet(
        trackId: Int,
        clipId: Int,
        mode: Int,
        seconds: Float,
    ): Boolean
    external fun nativeTextRevealGet(trackId: Int, clipId: Int, out: FloatArray): Boolean

    // Clip audio and its automation (prop 0 gain, 1 pan; clip-local seconds)
    external fun nativeClipAudioSet(trackId: Int, clipId: Int, gain: Float, pan: Float): Boolean
    external fun nativeClipAudioGet(trackId: Int, clipId: Int, out: FloatArray): Boolean
    external fun nativeAudioKeyframeAdd(
        trackId: Int,
        clipId: Int,
        prop: Int,
        time: Double,
        value: Double,
        interp: Int,
    ): Boolean
    external fun nativeAudioKeyframeClear(trackId: Int, clipId: Int, prop: Int): Boolean
    external fun nativeAudioKeyframeCount(trackId: Int, clipId: Int, prop: Int): Int
    external fun nativeAudioKeyframeGet(
        trackId: Int,
        clipId: Int,
        prop: Int,
        key: Int,
        out: DoubleArray,
    ): Boolean
    external fun nativeAudioKeyframeSet(
        trackId: Int,
        clipId: Int,
        prop: Int,
        key: Int,
        time: Double,
        value: Double,
        interp: Int,
    ): Boolean
    external fun nativeAudioKeyframeSample(
        trackId: Int,
        clipId: Int,
        prop: Int,
        t0: Double,
        t1: Double,
        out: FloatArray,
        bins: Int,
    ): Boolean

    // Master grade + scopes
    external fun nativeGradeSet(
        exposure: Float,
        contrast: Float,
        saturation: Float,
        temperature: Float,
        lift: Float,
        gamma: Float,
        gain: Float,
    )
    external fun nativeGradeClear()
    /** Shadow / highlight split and vibrance, on top of the seven base moves. */
    external fun nativeGradeTonalSet(shadows: Float, highlights: Float, vibrance: Float)
    /** Fills 10 floats with the persisted master grade; true when one is active. */
    external fun nativeGradeGet(out: FloatArray): Boolean
    external fun nativeScopesHistogram(out: IntArray, bins: Int): Boolean

    /** "mediacodec", "ffmpeg" or "slate" — the last one means no real decoding. */
    // ---- Comp network (node graph) ----
    external fun nativeCompNodeCount(): Int
    external fun nativeCompNodeAt(index: Int, out: IntArray): Boolean
    external fun nativeCompEdgeCount(): Int
    external fun nativeCompEdgeAt(index: Int, out: IntArray): Boolean
    external fun nativeCompAddNode(op: Int, x: Float, y: Float): Int
    external fun nativeCompRemoveNode(id: Int): Boolean
    external fun nativeCompConnect(from: Int, to: Int, slot: Int): Boolean
    external fun nativeCompDisconnect(from: Int, to: Int, slot: Int): Boolean
    external fun nativeCompSetParam(id: Int, name: String, value: Double): Boolean
    external fun nativeCompGetParam(id: Int, name: String, fallback: Double): Double
    external fun nativeCompSetMedia(id: Int, path: String): Boolean
    external fun nativeCompGetMedia(id: Int): String
    external fun nativeCompLinkParam(node: Int, param: String, src: Int, srcParam: String): Boolean
    external fun nativeCompUnlinkParam(node: Int, param: String): Boolean
    external fun nativeCompLinkCount(): Int
    external fun nativeCompLinkAt(index: Int): String
    external fun nativeCompSetEnabled(id: Int, enabled: Boolean): Boolean
    external fun nativeCompSetPosition(id: Int, x: Float, y: Float): Boolean
    external fun nativeCompSetSink(id: Int): Boolean
    external fun nativeCompSink(): Int
    external fun nativeCompSetActive(active: Boolean): Boolean
    external fun nativeCompActive(): Boolean

    external fun nativeDecoderBackend(): String

    // Timeline markers + project settings
    external fun nativeMarkerAdd(time: Double, name: String): Int
    external fun nativeMarkerRemove(id: Int): Boolean
    external fun nativeProjectSettings(width: Int, height: Int, fps: Double): Boolean

    // Shape masks (shape: 0 rect, 1 ellipse, 2 full frame; x/y/w/h normalized).
    // A clip carries a list of them: op 0 unions, 1 subtracts.
    external fun nativeClipMaskAdd(
        track: Int, clip: Int, shape: Int, x: Float, y: Float, w: Float, h: Float,
        feather: Float, inverted: Boolean, op: Int,
    ): Int
    external fun nativeClipMaskUpdate(
        track: Int, clip: Int, index: Int, shape: Int, x: Float, y: Float, w: Float, h: Float,
        feather: Float, inverted: Boolean, op: Int,
    ): Boolean
    external fun nativeClipMaskRemove(track: Int, clip: Int, index: Int): Boolean
    external fun nativeClipMaskCount(track: Int, clip: Int): Int
    external fun nativeClipMaskRead(
        track: Int, clip: Int, index: Int, out: FloatArray, invertedOut: IntArray, opOut: IntArray,
    ): Boolean
    external fun nativeClipMaskClear(track: Int, clip: Int): Boolean

    // Mask geometry automation (prop 0 x, 1 y, 2 w, 3 h, 4 feather)
    external fun nativeMaskKeyframeAdd(
        track: Int,
        clip: Int,
        mask: Int,
        prop: Int,
        time: Double,
        value: Double,
        interp: Int,
    ): Boolean
    external fun nativeMaskKeyframeClear(track: Int, clip: Int, mask: Int, prop: Int): Boolean
    external fun nativeMaskKeyframeCount(track: Int, clip: Int, mask: Int, prop: Int): Int
    external fun nativeMaskKeyframeGet(
        track: Int,
        clip: Int,
        mask: Int,
        prop: Int,
        key: Int,
        out: DoubleArray,
    ): Boolean
    external fun nativeMaskKeyframeSet(
        track: Int,
        clip: Int,
        mask: Int,
        prop: Int,
        key: Int,
        time: Double,
        value: Double,
        interp: Int,
    ): Boolean
    external fun nativeMaskKeyframeSample(
        track: Int,
        clip: Int,
        mask: Int,
        prop: Int,
        t0: Double,
        t1: Double,
        out: FloatArray,
        bins: Int,
    ): Boolean

    // Per-clip LUT look: a `.cube` graded over one clip, after its grade.
    external fun nativeClipLookSet(track: Int, clip: Int, path: String): Boolean
    external fun nativeClipLookSetStrength(track: Int, clip: Int, strength: Float): Boolean
    external fun nativeClipLookClear(track: Int, clip: Int): Boolean
    /** Path of the clip's look, or "" when it has none; strength lands in out[0]. */
    external fun nativeClipLookGet(track: Int, clip: Int, out: FloatArray): String

    // Per-clip grade
    external fun nativeClipGradeSet(
        track: Int, clip: Int, exposure: Float, contrast: Float, saturation: Float,
        temperature: Float, lift: Float, gamma: Float, gain: Float,
    ): Boolean
    external fun nativeClipGradeClear(track: Int, clip: Int): Boolean
    external fun nativeClipGradeGet(track: Int, clip: Int, out: FloatArray): Boolean
    external fun nativeClipGradeTonalSet(
        track: Int, clip: Int, shadows: Float, highlights: Float, vibrance: Float,
    ): Boolean

    // Timeline waveform: fills `out` with `bins` peaks in 0..1.
    external fun nativeClipPeaks(track: Int, clip: Int, out: FloatArray, bins: Int): Boolean

    // Text / titles (stb_truetype rasteriser in the engine)
    external fun nativeTextSetFontDir(dir: String): Boolean
    external fun nativeTextAddClip(
        track: Int, start: Double, duration: Double, text: String, font: String,
        size: Double, color: String, align: Int, anchor: Int, outline: Double,
        outlineColor: String, shadow: Double, lineSpacing: Double, tracking: Double,
        boxWidth: Double, uppercase: Boolean,
    ): Int
    external fun nativeTextUpdateClip(
        track: Int, clipId: Int, text: String, font: String, size: Double, color: String,
        align: Int, anchor: Int, outline: Double, outlineColor: String, shadow: Double,
        lineSpacing: Double, tracking: Double, boxWidth: Double, uppercase: Boolean,
    ): Boolean

    // Transitions
    external fun nativeTransitionAdd(trackId: Int, outgoing: Int, incoming: Int, type: Int, duration: Double): Int
    external fun nativeTransitionRemove(trackId: Int, id: Int): Boolean

    // Keyframes (prop: 0 opacity, 1 scale, 2 pos_x, 3 pos_y; time clip-local)
    external fun nativeKeyframeAdd(trackId: Int, clipId: Int, prop: Int, time: Double, value: Double): Boolean
    external fun nativeKeyframeClear(trackId: Int, clipId: Int, prop: Int): Boolean

    // Curve editing. Keys are addressed by index in time order; interp is
    // 0 linear, 1 hold, 2 ease-in, 3 ease-out, 4 ease-in-out, 5 smooth (bezier).
    external fun nativeKeyframeCount(trackId: Int, clipId: Int, prop: Int): Int
    /** Fills [time, value, interp]; false when that key does not exist. */
    external fun nativeKeyframeGet(trackId: Int, clipId: Int, prop: Int, index: Int, out: DoubleArray): Boolean
    external fun nativeKeyframeSet(
        trackId: Int, clipId: Int, prop: Int, index: Int, time: Double, value: Double, interp: Int,
    ): Boolean
    external fun nativeKeyframeSetInterp(trackId: Int, clipId: Int, prop: Int, index: Int, interp: Int): Boolean
    /** Samples the real engine curve so the drawn shape cannot drift from the render. */
    external fun nativeKeyframeSample(
        trackId: Int, clipId: Int, prop: Int, t0: Double, t1: Double, out: FloatArray, bins: Int,
    ): Boolean

    // Timeline editing
    external fun nativeClipRemove(trackId: Int, clipId: Int): Boolean
    external fun nativeClipMove(trackId: Int, clipId: Int, start: Double): Boolean
    external fun nativeClipTrim(trackId: Int, clipId: Int, start: Double, duration: Double): Boolean
    external fun nativeClipRippleDelete(trackId: Int, clipId: Int): Boolean
    external fun nativeTrackAdd(name: String, type: Int): Int
    external fun nativeTrackRemove(trackId: Int): Boolean
    external fun nativeTrackFlags(trackId: Int, muted: Boolean, locked: Boolean, visible: Boolean, solo: Boolean): Boolean

    // Clip effect stack
    external fun nativeEffectAdd(trackId: Int, clipId: Int, baseId: String): Int
    external fun nativeEffectRemove(trackId: Int, clipId: Int, index: Int): Boolean
    external fun nativeEffectMove(trackId: Int, clipId: Int, from: Int, to: Int): Boolean
    external fun nativeEffectSetParam(trackId: Int, clipId: Int, index: Int, name: String, value: Float): Boolean
    external fun nativeEffectSetEnabled(trackId: Int, clipId: Int, index: Int, enabled: Boolean): Boolean

    // Effect parameter automation (clip-local seconds; interp = CurveInterp)
    external fun nativeEffectKeyframeAdd(
        trackId: Int, clipId: Int, index: Int, param: String,
        time: Double, value: Double, interp: Int,
    ): Boolean
    external fun nativeEffectKeyframeClear(trackId: Int, clipId: Int, index: Int, param: String): Boolean
    external fun nativeEffectKeyframeCount(trackId: Int, clipId: Int, index: Int, param: String): Int
    /** Reads one key into [time, value, interp]; same shape as nativeKeyframeGet. */
    external fun nativeEffectKeyframeGet(
        trackId: Int, clipId: Int, index: Int, param: String, key: Int, out: DoubleArray,
    ): Boolean
    external fun nativeEffectKeyframeSet(
        trackId: Int, clipId: Int, index: Int, param: String,
        key: Int, time: Double, value: Double, interp: Int,
    ): Boolean
    external fun nativeEffectKeyframeSample(
        trackId: Int, clipId: Int, index: Int, param: String,
        t0: Double, t1: Double, out: FloatArray, bins: Int,
    ): Boolean

    // Effect stack clipboard: copy returns the stack size, paste the new size.
    external fun nativeEffectCopy(trackId: Int, clipId: Int): Int
    external fun nativeEffectPaste(trackId: Int, clipId: Int, append: Boolean): Int
    external fun nativeEffectClipboardCount(): Int

    // Display look (3D .cube LUT applied by the engine to the preview frame)
    external fun nativeLookLoad(path: String): Boolean
    external fun nativeLookClear()
    external fun nativeLookSetStrength(strength: Float)
    external fun nativeLookStrength(): Float

    // Diagnostics
    external fun nativePerfFrameMs(): Double
    external fun nativePerfFps(): Double
}
