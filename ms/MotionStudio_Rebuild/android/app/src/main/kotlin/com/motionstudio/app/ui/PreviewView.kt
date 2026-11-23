package com.motionstudio.app.ui

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Rect
import android.graphics.RectF
import android.view.MotionEvent
import android.view.View
import com.motionstudio.app.editor.EditorEngine
import com.motionstudio.app.model.PreviewRatio
import com.motionstudio.app.native.NativeEngine

/**
 * Streams frames straight out of the C++ renderer into a Bitmap and blits them.
 *
 * This is the real preview path (`ms_render_frame` + `ms_copy_frame_rgba`), not
 * a placeholder: what the engine composites is what the viewer shows.
 *
 * Rendering runs on its own thread. It used to be a `post`ed Runnable on the UI
 * thread, which meant a slow composite — a 4K clip, a blur, a few effects —
 * blocked input and could trip the ANR watchdog.
 *
 * Two frames of buffering keep that worker off the UI thread entirely: it
 * writes the back bitmap and swaps references, so [onDraw] never waits for a
 * full-frame copy. The worker also asks the engine for a state signature first
 * and does nothing when it has not moved, which is what keeps a paused editor
 * from compositing and repainting twenty times a second for no reason.
 *
 * [EditorEngine.previewRatio] adds a delivery-aspect guide: the composite is
 * kept whole inside the guide and everything outside it is shaded, so you can
 * see the 9:16 crop of a 16:9 canvas without changing the project.
 */
class PreviewView(context: Context) : View(context) {

    private var engine: EditorEngine? = null

    private val lock = Any()

    /** Bitmap [onDraw] reads; only ever replaced by the worker, under [lock]. */
    private var front: Bitmap? = null

    /** Bitmap the worker copies into; swapped with [front] when it is complete. */
    private var back: Bitmap? = null

    private var frameWidth = 0
    private var frameHeight = 0
    private var lastRatio: PreviewRatio = PreviewRatio.Project

    /** Engine fingerprint of the last frame copied out, and its transport state. */
    private var lastSignature = Long.MIN_VALUE
    private var lastPlaying = false

    @Volatile
    private var looping = false

    @Volatile
    private var visible = true

    private var worker: Thread? = null

    private val bitmapPaint = Paint(Paint.FILTER_BITMAP_FLAG or Paint.ANTI_ALIAS_FLAG)
    private val backgroundPaint = Paint().apply { color = Color.parseColor("#0B0B0E") }
    private val shadePaint = Paint().apply { color = Color.parseColor("#A6000000") }
    private val borderPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.parseColor("#33FFFFFF")
        style = Paint.Style.STROKE
        strokeWidth = 1f
    }
    private val labelPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.parseColor("#8AFFFFFF")
        textSize = 24f
        letterSpacing = 0.08f
    }
    private val src = Rect()
    private val dst = RectF()

    /** Reused across frames: onDraw must not allocate, or it allocates at 60 Hz. */
    private val guideRect = RectF()

    private var scale = 1f
    private var offsetX = 0f
    private var offsetY = 0f
    private var lastX = 0f
    private var lastY = 0f

    // ---- Gesture mode ------------------------------------------------------
    //
    // The viewport has two jobs and a two-finger gesture cannot mean both at
    // once. [navigateMode] (the visible PAN / ZOOM toggle) always pans and
    // pinches the canvas. With the toggle off, a selected layer takes the
    // two-finger gesture and the canvas is only reachable when nothing is
    // selected — the ambiguity the explicit mode exists to remove.
    private var navigateMode = false
    private var layerTransformEnabled = false

    /** Called once per two-finger layer gesture: dx/dy as fractions of the frame, relative scale. */
    var onLayerTransform: ((dxFraction: Float, dyFraction: Float, scaleFactor: Float) -> Unit)? = null

    private var twoFinger = false
    private var oneFingerPan = false
    private var lastCentroidX = 0f
    private var lastCentroidY = 0f
    private var lastSpan = 0f
    private var gestureDx = 0f
    private var gestureDy = 0f
    private var gestureScale = 1f
    private var gestureActive = false

    /** Size of the composite as last drawn, so gestures can speak in canvas fractions. */
    private var drawnFrameW = 0f
    private var drawnFrameH = 0f

    private val hudPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.parseColor("#E6FFFFFF")
        textSize = 26f
        letterSpacing = 0.04f
        textAlign = Paint.Align.CENTER
    }
    private val hudBgPaint = Paint().apply { color = Color.parseColor("#B3000000") }

    private val frameSize = IntArray(2)

    fun attach(editor: EditorEngine) {
        engine = editor
        if (isAttachedToWindow) startWorker()
    }

    override fun onAttachedToWindow() {
        super.onAttachedToWindow()
        if (engine != null) startWorker()
    }

    override fun onDetachedFromWindow() {
        stopWorker()
        super.onDetachedFromWindow()
    }

    override fun onVisibilityChanged(changedView: View, visibility: Int) {
        super.onVisibilityChanged(changedView, visibility)
        // A hidden preview (another screen on top) has no reason to composite.
        visible = visibility == VISIBLE
    }

    private fun startWorker() {
        if (worker != null) return
        looping = true
        worker = Thread({
            while (looping) {
                val produced = try {
                    renderOnce()
                } catch (t: Throwable) {
                    // A preview failure must not kill the editor; report once.
                    com.motionstudio.app.diag.Diagnostics.note("preview render failed: $t")
                    false
                }
                if (produced) postInvalidateOnAnimation()
                // Playback wants every frame the renderer can manage; a paused
                // playhead only needs to notice edits, and that poll is a single
                // native call that usually returns "nothing changed".
                val delay = if (lastPlaying) 16L else 60L
                try {
                    Thread.sleep(delay)
                } catch (_: InterruptedException) {
                    break
                }
            }
        }, "ms-preview").apply {
            priority = Thread.NORM_PRIORITY + 1
            start()
        }
    }

    private fun stopWorker() {
        looping = false
        worker?.interrupt()
        worker = null
    }

    /** Renders one frame. Runs on the preview thread. Returns true when new pixels landed. */
    private fun renderOnce(): Boolean {
        val editor = engine ?: return false
        if (!visible) return false

        // Nothing that affects the picture has moved: do not composite, do not
        // copy pixels and do not invalidate the view. This is the whole reason a
        // paused editor stays cool; the previous version re-rendered on a timer
        // whether or not anything had changed.
        val signature = NativeEngine.nativeRenderSignature()
        if (signature == lastSignature && !lastPlaying) return false

        val state = NativeEngine.nativePreviewState()
        if (state.size < 4) return false
        // The frame size follows the preview-quality setting, not the project
        // canvas; the engine reports what it will actually composite.
        if (!NativeEngine.nativePreviewFrameSize(frameSize)) return false
        val w = frameSize[0].coerceAtLeast(1)
        val h = frameSize[1].coerceAtLeast(1)

        if (!NativeEngine.nativeRenderFrame()) return false

        val ready = synchronized(lock) {
            if (w != frameWidth || h != frameHeight) {
                // Both buffers are stale; the next draw shows the background
                // until the new size has been copied.
                front?.recycle()
                front = null
                back?.recycle()
                back = null
                frameWidth = w
                frameHeight = h
            }
            val expected = NativeEngine.nativeFrameBytes()
            if (expected > 0 && expected != w.toLong() * h * 4) return@synchronized false
            val target = back ?: runCatching {
                Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888)
            }.getOrNull() ?: return@synchronized false
            // The UI thread only reads `front`, so this copy no longer blocks a
            // draw frame; the swap below is the only contested work.
            if (!NativeEngine.nativeCopyFrame(target)) return@synchronized false
            back = front
            front = target
            lastSignature = signature
            lastPlaying = state[3] == 1.0
            true
        }

        if (ready) {
            // Playhead state belongs to the UI thread.
            post { editor.onFrameRendered(state[0], state[3] == 1.0) }
        }
        val ratio = editor.previewRatio
        if (ratio != lastRatio) {
            lastRatio = ratio
            post { resetView() }
        }
        return ready
    }

    override fun onDraw(canvas: Canvas) {
        canvas.drawRect(0f, 0f, width.toFloat(), height.toFloat(), backgroundPaint)
        val ratio = engine?.previewRatio ?: PreviewRatio.Project
        val pad = 12f
        val availableW = width - pad * 2
        val availableH = height - pad * 2
        if (availableW <= 0f || availableH <= 0f) return

        // The worker may be writing into the bitmap on another thread; hold the
        // same lock for the whole draw so a frame is never half-copied.
        synchronized(lock) {
            val bitmap = front ?: return

            // 1. Guide = the chosen delivery aspect, contained in the view.
            val guideAspect = ratio.aspect ?: (bitmap.width.toFloat() / bitmap.height.toFloat())
            var guideW = availableW
            var guideH = guideW / guideAspect
            if (guideH > availableH) {
                guideH = availableH
                guideW = guideH * guideAspect
            }
            guideW *= scale
            guideH *= scale
            val guideLeft = (width - guideW) / 2f + offsetX
            val guideTop = (height - guideH) / 2f + offsetY
            val guide = guideRect.also { it.set(guideLeft, guideTop, guideLeft + guideW, guideTop + guideH) }

            // 2. The composite, contained inside the guide (never cropped by it).
            val frameAspect = bitmap.width.toFloat() / bitmap.height.toFloat()
            var drawW = guide.width()
            var drawH = drawW / frameAspect
            if (drawH > guide.height()) {
                drawH = guide.height()
                drawW = drawH * frameAspect
            }
            val left = guide.centerX() - drawW / 2f
            val top = guide.centerY() - drawH / 2f
            dst.set(left, top, left + drawW, top + drawH)
            src.set(0, 0, bitmap.width, bitmap.height)

            // 3. Everything outside the guide is shaded so the crop reads at a glance.
            if (ratio != PreviewRatio.Project) {
                canvas.drawRect(0f, 0f, width.toFloat(), guide.top, shadePaint)
                canvas.drawRect(0f, guide.bottom, width.toFloat(), height.toFloat(), shadePaint)
                canvas.drawRect(0f, guide.top, guide.left, guide.bottom, shadePaint)
                canvas.drawRect(guide.right, guide.top, width.toFloat(), guide.bottom, shadePaint)
                canvas.drawRect(guide, borderPaint)
                canvas.drawText(ratio.label, guide.left + 10f, guide.top + 30f, labelPaint)
            }

            canvas.drawBitmap(bitmap, src, dst, bitmapPaint)
            canvas.drawRect(dst, borderPaint)
            drawnFrameW = dst.width()
            drawnFrameH = dst.height()
        }

        // While a layer gesture is in flight the picture only updates on release,
        // so the readout is the feedback: without it the two fingers feel dead.
        if (gestureActive && !navigating()) {
            val label = buildString {
                append("LAYER  ")
                append("%+.0f, %+.0f px".format(gestureDx * drawnFrameW, gestureDy * drawnFrameH))
                append("  ·  ")
                append("%.0f%%".format(gestureScale * 100f))
            }
            val cx = width / 2f
            val cy = height - 34f
            val half = hudPaint.measureText(label) / 2f + 14f
            canvas.drawRoundRect(cx - half, cy - 22f, cx + half, cy + 10f, 6f, 6f, hudBgPaint)
            canvas.drawText(label, cx, cy, hudPaint)
        }
    }

    /** The visible PAN / ZOOM toggle: two-finger gestures always move the canvas. */
    fun setNavigateMode(on: Boolean) {
        if (navigateMode == on) return
        navigateMode = on
        cancelGesture()
        invalidate()
    }

    /** True when a layer is selected and the toggle is off, so gestures edit it. */
    fun setLayerTransformEnabled(on: Boolean) {
        if (layerTransformEnabled == on) return
        layerTransformEnabled = on
        if (on) cancelGesture()
        invalidate()
    }

    private fun navigating(): Boolean = navigateMode || !layerTransformEnabled

    override fun onTouchEvent(event: MotionEvent): Boolean {
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                lastX = event.x
                lastY = event.y
                twoFinger = false
                oneFingerPan = navigating()
                resetAccumulators()
                parent?.requestDisallowInterceptTouchEvent(true)
            }
            MotionEvent.ACTION_POINTER_DOWN -> {
                if (event.pointerCount >= 2) beginTwoFinger(event)
            }
            MotionEvent.ACTION_MOVE -> {
                if (event.pointerCount >= 2) {
                    if (!twoFinger) beginTwoFinger(event)
                    updateTwoFinger(event)
                } else if (oneFingerPan) {
                    // In PAN / ZOOM mode one finger pans at any zoom, so the
                    // mode is usable without pinching first.
                    offsetX += event.x - lastX
                    offsetY += event.y - lastY
                    lastX = event.x
                    lastY = event.y
                    invalidate()
                }
            }
            MotionEvent.ACTION_POINTER_UP -> {
                if (twoFinger) endTwoFinger()
                // A finger left the screen: re-anchor the one-finger pan instead
                // of letting the remaining pointer jump the canvas.
                val remaining = (0 until event.pointerCount).firstOrNull {
                    it != event.actionIndex
                }
                if (remaining != null) {
                    lastX = event.getX(remaining)
                    lastY = event.getY(remaining)
                    oneFingerPan = navigating()
                }
            }
            MotionEvent.ACTION_UP -> {
                if (twoFinger) endTwoFinger()
                parent?.requestDisallowInterceptTouchEvent(false)
                // The view handles its own touches, so it must answer the
                // accessibility click action too; a screen reader's "activate"
                // otherwise does nothing at all here.
                performClick()
            }
            MotionEvent.ACTION_CANCEL -> {
                parent?.requestDisallowInterceptTouchEvent(false)
                cancelGesture()
            }
        }
        return true
    }

    private fun beginTwoFinger(event: MotionEvent) {
        twoFinger = true
        oneFingerPan = false
        lastCentroidX = (event.getX(0) + event.getX(1)) / 2f
        lastCentroidY = (event.getY(0) + event.getY(1)) / 2f
        lastSpan = spread(event)
    }

    private fun updateTwoFinger(event: MotionEvent) {
        val cx = (event.getX(0) + event.getX(1)) / 2f
        val cy = (event.getY(0) + event.getY(1)) / 2f
        val span = spread(event)
        val dx = cx - lastCentroidX
        val dy = cy - lastCentroidY
        val factor = if (lastSpan > 1f && span > 1f) (span / lastSpan).coerceIn(0.5f, 2f) else 1f
        lastCentroidX = cx
        lastCentroidY = cy
        lastSpan = span

        if (navigating()) {
            offsetX += dx
            offsetY += dy
            applyZoom(factor, cx, cy)
            return
        }

        // A layer transform: accumulate and commit on release. Live-compositing
        // every move would re-render the frame faster than the fingers move.
        val frameW = if (drawnFrameW > 1f) drawnFrameW else width.toFloat().coerceAtLeast(1f)
        val frameH = if (drawnFrameH > 1f) drawnFrameH else height.toFloat().coerceAtLeast(1f)
        gestureDx += dx / frameW
        gestureDy += dy / frameH
        gestureScale = (gestureScale * factor).coerceIn(0.05f, 20f)
        gestureActive = true
        invalidate()
    }

    private fun endTwoFinger() {
        if (gestureActive && !navigating()) {
            onLayerTransform?.invoke(gestureDx, gestureDy, gestureScale)
        }
        twoFinger = false
        cancelGesture()
    }

    private fun cancelGesture() {
        twoFinger = false
        oneFingerPan = false
        gestureActive = false
        resetAccumulators()
        invalidate()
    }

    private fun resetAccumulators() {
        gestureDx = 0f
        gestureDy = 0f
        gestureScale = 1f
    }

    private fun spread(event: MotionEvent): Float {
        val dx = event.getX(0) - event.getX(1)
        val dy = event.getY(0) - event.getY(1)
        return kotlin.math.hypot(dx, dy)
    }

    /** Pinch around the point under the fingers instead of the view centre. */
    private fun applyZoom(factor: Float, focusX: Float, focusY: Float) {
        if (!factor.isFinite() || factor <= 0f) return
        val next = (scale * factor).coerceIn(0.5f, 6f)
        if (kotlin.math.abs(next - scale) < 1e-4f) return
        val ratio = next / scale
        val cx = width / 2f
        val cy = height / 2f
        offsetX = (focusX - cx) - (focusX - cx - offsetX) * ratio
        offsetY = (focusY - cy) - (focusY - cy - offsetY) * ratio
        scale = next
        invalidate()
    }

    override fun performClick(): Boolean {
        super.performClick()
        return true
    }

    fun resetView() {
        scale = 1f
        offsetX = 0f
        offsetY = 0f
        cancelGesture()
        invalidate()
    }

    /** Frees the frame bitmap. Called when the editor leaves the screen. */
    fun releaseFrame() {
        stopWorker()
        synchronized(lock) {
            front?.recycle()
            front = null
            back?.recycle()
            back = null
            frameWidth = 0
            frameHeight = 0
            lastSignature = Long.MIN_VALUE
        }
    }
}
