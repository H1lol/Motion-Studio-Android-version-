package com.motionstudio.app.ui

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Rect
import android.graphics.RectF
import android.view.MotionEvent
import android.view.ScaleGestureDetector
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

    private val scaleDetector = ScaleGestureDetector(
        context,
        object : ScaleGestureDetector.SimpleOnScaleGestureListener() {
            override fun onScale(detector: ScaleGestureDetector): Boolean {
                scale = (scale * detector.scaleFactor).coerceIn(0.5f, 6f)
                invalidate()
                return true
            }
        }
    )

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
        val w = state[1].toInt().coerceAtLeast(1)
        val h = state[2].toInt().coerceAtLeast(1)

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
        }
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        scaleDetector.onTouchEvent(event)
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                lastX = event.x
                lastY = event.y
                parent?.requestDisallowInterceptTouchEvent(true)
            }
            MotionEvent.ACTION_MOVE -> {
                if (!scaleDetector.isInProgress && scale > 1.01f) {
                    offsetX += event.x - lastX
                    offsetY += event.y - lastY
                    lastX = event.x
                    lastY = event.y
                    invalidate()
                }
            }
            MotionEvent.ACTION_UP -> {
                parent?.requestDisallowInterceptTouchEvent(false)
                // The view handles its own touches, so it must answer the
                // accessibility click action too; a screen reader's "activate"
                // otherwise does nothing at all here.
                performClick()
            }
            MotionEvent.ACTION_CANCEL -> {
                parent?.requestDisallowInterceptTouchEvent(false)
            }
        }
        return true
    }

    override fun performClick(): Boolean {
        super.performClick()
        return true
    }

    fun resetView() {
        scale = 1f
        offsetX = 0f
        offsetY = 0f
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
