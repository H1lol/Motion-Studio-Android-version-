package com.motionstudio.app.data

import android.app.ActivityManager
import android.content.Context

/**
 * What this phone can afford, so the editor opens at a size it can composite.
 *
 * A budget phone is not a slower flagship: it usually reports
 * `isLowRamDevice` (Android Go, or 2 GB of RAM or less), a 128–192 MB heap
 * class, and two to four cores. The project canvas is never touched — only the
 * preview starts smaller, and only as a first-run default that an explicit
 * choice in Settings overrides.
 */
object DeviceProfile {

    @Volatile
    private var lowRam = false

    @Volatile
    private var heapMb = 256

    @Volatile
    private var cores = Runtime.getRuntime().availableProcessors().coerceAtLeast(1)

    /**
     * The preview a device class should start with.
     *
     * Pure, so it can be unit-tested off-device. A low-RAM build, a heap class
     * at or below 192 MB, or two cores or fewer gets the quarter preview — one
     * sixteenth of the frame pixels, about 2 MB of buffers for a 1080p project.
     * Everything else starts at half size.
     */
    fun defaultPreviewQuality(
        lowRamDevice: Boolean,
        memoryClassMb: Int,
        cpuCores: Int,
    ): PreviewQuality =
        if (lowRamDevice || memoryClassMb <= 192 || cpuCores <= 2) {
            PreviewQuality.Quarter
        } else {
            PreviewQuality.Half
        }

    /** Reads the device class once, before anything composites a frame. */
    fun init(context: Context) {
        val manager = context.getSystemService(Context.ACTIVITY_SERVICE) as? ActivityManager
        lowRam = manager?.isLowRamDevice ?: false
        heapMb = manager?.memoryClass ?: 256
        cores = Runtime.getRuntime().availableProcessors().coerceAtLeast(1)
    }

    val isLowRam: Boolean get() = lowRam

    val heapClassMb: Int get() = heapMb

    val cpuCores: Int get() = cores

    /** The first-run preview quality for the device this process is on. */
    val suggestedQuality: PreviewQuality
        get() = defaultPreviewQuality(lowRam, heapMb, cores)

    /** One line for Settings, so a softer preview reads as a decision, not a fault. */
    fun describe(): String = buildString {
        append("$heapMb MB heap, $cores core")
        if (cores != 1) append('s')
        if (lowRam) append(", low-RAM device")
    }
}
