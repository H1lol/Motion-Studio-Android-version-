package com.motionstudio.app.data

import android.content.Context
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import com.motionstudio.app.native.NativeEngine

/**
 * How many pixels the preview composites while you edit.
 *
 * This is deliberately separate from export: the delivery sheet decides the
 * final size, frame rate and bitrate, and always renders the project canvas.
 * A phone can then preview a heavy 1080p scene at half size and still deliver
 * it at full size.
 */
enum class PreviewQuality(val label: String, val scale: Float, val detail: String) {
    Full("Full", 1f, "Composites the project canvas"),
    Half("Half", 0.5f, "Quarter of the pixels — about 4× cheaper"),
    Quarter("Quarter", 0.25f, "One sixteenth of the pixels — for heavy effects"),
}

/**
 * The preview-quality setting, persisted on the device and pushed into the
 * native engine. It lives beside [SettingsStore] instead of in it because the
 * engine, not the UI theme, is what has to be told about a change.
 */
object PreviewQualityStore {

    private var prefs: android.content.SharedPreferences? = null

    var quality by mutableStateOf(PreviewQuality.Half)
        private set

    /**
     * Reads the saved value and applies it. Safe before the engine is initialised.
     *
     * With nothing saved — a new install, or a device that has never run this
     * version — the first value comes from [DeviceProfile], so a budget phone
     * opens at a size it can composite instead of at half the canvas. The pick
     * is written down immediately, so a later default never overrides a choice
     * the user has made.
     */
    fun init(context: Context) {
        val store = context.getSharedPreferences("motion_studio_settings", Context.MODE_PRIVATE)
        prefs = store
        quality = if (store.contains(KEY)) {
            PreviewQuality.entries.getOrNull(store.getInt(KEY, -1)) ?: DeviceProfile.suggestedQuality
        } else {
            DeviceProfile.suggestedQuality.also { store.edit().putInt(KEY, it.ordinal).apply() }
        }
        apply()
    }

    fun set(value: PreviewQuality) {
        if (quality == value) return
        quality = value
        prefs?.edit()?.putInt(KEY, value.ordinal)?.apply()
        apply()
    }

    /** Pushes the current quality into the engine; the next render uses it. */
    fun apply() {
        if (!NativeEngine.available) return
        runCatching { NativeEngine.nativePreviewScaleSet(quality.scale) }
    }

    private const val KEY = "preview_quality"
}
