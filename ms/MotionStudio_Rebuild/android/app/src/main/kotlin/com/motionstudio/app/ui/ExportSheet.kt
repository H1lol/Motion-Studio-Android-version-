package com.motionstudio.app.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.motionstudio.app.editor.EditorEngine
import com.motionstudio.app.export.ExportPresets
import com.motionstudio.app.export.ExportSettings
import com.motionstudio.app.export.VideoExporter
import com.motionstudio.app.ui.theme.MsColors
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File

/**
 * The delivery sheet: size, frame rate (including custom values), bitrate,
 * codec and range, then an on-device encode of exactly those settings.
 *
 * Nothing here mutates the project. The C++ engine renders the composited frame
 * at the project canvas, this sheet decides how it is delivered.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ExportSheet(engine: EditorEngine, onDismiss: () -> Unit) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)

    var settings by remember {
        mutableStateOf(ExportSettings.fromProject(engine.doc.width, engine.doc.height, engine.fps))
    }
    var widthText by remember { mutableStateOf(settings.width.toString()) }
    var heightText by remember { mutableStateOf(settings.height.toString()) }
    var fpsText by remember { mutableStateOf(ExportSettings.formatFps(settings.saneFps)) }
    var fromPlayhead by remember { mutableStateOf(false) }
    var busy by remember { mutableStateOf(false) }
    var progress by remember { mutableFloatStateOf(0f) }
    var message by remember { mutableStateOf<String?>(null) }
    // A long encode on a budget phone must be stoppable; the exporter polls this
    // once per frame and cleans up after itself.
    val cancelRequested = remember { java.util.concurrent.atomic.AtomicBoolean(false) }

    fun sync(s: ExportSettings) {
        settings = s
        widthText = s.width.toString()
        heightText = s.height.toString()
        fpsText = ExportSettings.formatFps(s.saneFps)
    }

    val rangeStart = if (fromPlayhead) engine.time.coerceAtMost(engine.doc.timelineEnd) else 0.0
    val active = settings.copy(startSec = rangeStart, endSec = engine.doc.timelineEnd)
    val frames = active.frameCount(engine.doc.timelineEnd)
    val seconds = active.durationSec(engine.doc.timelineEnd)

    ModalBottomSheet(
        onDismissRequest = onDismiss,
        sheetState = sheetState,
        containerColor = MsColors.surface,
    ) {
        Column(
            Modifier
                .fillMaxWidth()
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 20.dp, vertical = 4.dp)
        ) {
            Text("Export", color = MsColors.textPrimary, fontSize = 16.sp, fontWeight = FontWeight.SemiBold)
            Spacer(Modifier.height(2.dp))
            Text(
                "Project canvas ${engine.doc.width}×${engine.doc.height} · " +
                    "${ExportSettings.formatFps(engine.fps)} fps · ${engine.doc.media.size} media",
                color = MsColors.textTertiary,
                fontSize = 11.sp,
            )
            Spacer(Modifier.height(10.dp))

            Text("PRESET", color = MsColors.textTertiary, fontSize = 10.sp, letterSpacing = 1.sp)
            Row(
                Modifier.horizontalScroll(rememberScrollState()),
                horizontalArrangement = Arrangement.spacedBy(6.dp),
            ) {
                ExportPresets.delivery.forEach { preset ->
                    SheetChip(
                        label = preset.label,
                        selected = settings.presetId == preset.id,
                        onClick = { sync(preset.applyTo(settings)) },
                    )
                }
                SheetChip(
                    label = "Custom",
                    selected = settings.presetId == "custom",
                    onClick = { settings = settings.copy(presetId = "custom") },
                )
            }
            Spacer(Modifier.height(2.dp))
            Text(
                ExportPresets.delivery.firstOrNull { it.id == settings.presetId }?.detail
                    ?: "Custom delivery settings",
                color = MsColors.textMuted,
                fontSize = 10.sp,
            )

            Spacer(Modifier.height(10.dp))
            Text("FRAME SIZE", color = MsColors.textTertiary, fontSize = 10.sp, letterSpacing = 1.sp)
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                OutlinedTextField(
                    value = widthText,
                    onValueChange = { text ->
                        widthText = text.filter { it.isDigit() }.take(5)
                        widthText.toIntOrNull()?.let { v ->
                            if (v in 16..7680) settings = settings.copy(width = v, presetId = "custom")
                        }
                    },
                    singleLine = true,
                    label = { Text("Width", fontSize = 10.sp) },
                    textStyle = androidx.compose.ui.text.TextStyle(fontSize = 12.sp),
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                    modifier = Modifier.width(112.dp),
                )
                OutlinedTextField(
                    value = heightText,
                    onValueChange = { text ->
                        heightText = text.filter { it.isDigit() }.take(5)
                        heightText.toIntOrNull()?.let { v ->
                            if (v in 16..4320) settings = settings.copy(height = v, presetId = "custom")
                        }
                    },
                    singleLine = true,
                    label = { Text("Height", fontSize = 10.sp) },
                    textStyle = androidx.compose.ui.text.TextStyle(fontSize = 12.sp),
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                    modifier = Modifier.width(112.dp),
                )
                TextButton(onClick = {
                    sync(ExportSettings.fromProject(engine.doc.width, engine.doc.height, engine.fps))
                }) {
                    Text("Project", color = MsColors.textSecondary, fontSize = 11.sp)
                }
            }

            Spacer(Modifier.height(8.dp))
            Text("FRAME RATE", color = MsColors.textTertiary, fontSize = 10.sp, letterSpacing = 1.sp)
            Row(
                Modifier.horizontalScroll(rememberScrollState()),
                horizontalArrangement = Arrangement.spacedBy(6.dp),
            ) {
                ExportPresets.frameRates.forEach { rate ->
                    SheetChip(
                        label = ExportSettings.formatFps(rate),
                        selected = kotlin.math.abs(settings.saneFps - rate) < 0.005,
                        onClick = {
                            sync(settings.copy(fps = rate, presetId = "custom"))
                        },
                    )
                }
            }
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                OutlinedTextField(
                    value = fpsText,
                    onValueChange = { text ->
                        fpsText = text.filter { it.isDigit() || it == '.' }.take(6)
                        fpsText.toDoubleOrNull()?.let { v ->
                            if (v >= 1.0 && v <= 240.0) {
                                settings = settings.copy(fps = v, presetId = "custom")
                            }
                        }
                    },
                    singleLine = true,
                    label = { Text("Custom fps", fontSize = 10.sp) },
                    textStyle = androidx.compose.ui.text.TextStyle(fontSize = 12.sp),
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal),
                    modifier = Modifier.width(112.dp),
                )
                Text(
                    "Any rate from 1 to 240",
                    color = MsColors.textMuted,
                    fontSize = 10.sp,
                )
            }

            Spacer(Modifier.height(8.dp))
            Text("CODEC", color = MsColors.textTertiary, fontSize = 10.sp, letterSpacing = 1.sp)
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                ExportSettings.Codec.entries.forEach { codec ->
                    SheetChip(
                        label = codec.label,
                        selected = settings.codec == codec,
                        onClick = { settings = settings.copy(codec = codec, presetId = "custom") },
                    )
                }
            }

            Spacer(Modifier.height(8.dp))
            Text(
                "BITRATE  ${"%.1f".format(settings.bitrateKbps / 1000.0)} Mbps",
                color = MsColors.textSecondary,
                fontSize = 12.sp,
            )
            Slider(
                value = settings.bitrateKbps / 1000f,
                onValueChange = {
                    settings = settings.copy(bitrateKbps = (it * 1000).toInt(), presetId = "custom")
                },
                valueRange = 1f..60f,
            )

            Spacer(Modifier.height(8.dp))
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                SheetChip(
                    label = "Whole timeline",
                    selected = !fromPlayhead,
                    onClick = { fromPlayhead = false },
                )
                SheetChip(
                    label = "From playhead (${"%.2f".format(engine.time)}s)",
                    selected = fromPlayhead,
                    onClick = { fromPlayhead = true },
                )
            }

            Spacer(Modifier.height(10.dp))
            Text(
                "${ExportSettings.describe(active)} · $frames frames · " +
                    "${"%.1f".format(seconds)}s · ~${"%.1f".format(active.estimatedMegabytes(engine.doc.timelineEnd))} MB",
                color = MsColors.textSecondary,
                fontSize = 11.sp,
            )
            Spacer(Modifier.height(8.dp))

            TextButton(
                enabled = !busy,
                onClick = {
                    scope.launch {
                        busy = true
                        progress = 0f
                        message = null
                        cancelRequested.set(false)
                        val dir = File(context.filesDir, "exports").apply { mkdirs() }
                        val safeName = engine.doc.name
                            .replace(Regex("[^A-Za-z0-9 _-]"), "")
                            .trim()
                            .ifBlank { "motion" }
                            .replace(' ', '_')
                        val out = File(dir, "${safeName}_${System.currentTimeMillis()}.mp4")
                        val job = active
                        val result = withContext(Dispatchers.Default) {
                            VideoExporter(engine).export(
                                out,
                                job,
                                onProgress = { progress = it },
                                isCancelled = { cancelRequested.get() },
                            )
                        }
                        busy = false
                        message = if (result.cancelled) {
                            "Export cancelled — the partial file was deleted."
                        } else if (result.error == null) {
                            buildString {
                                append("Exported ${out.name} · ${result.frames} frames · ")
                                append("${job.codec.label}")
                                append(if (result.hasAudio) " + AAC" else " (no audio track)")
                                result.note?.let { append("\n$it") }
                                append("\n${out.absolutePath}")
                            }
                        } else {
                            "Export failed: ${result.error}"
                        }
                    }
                },
            ) {
                Text(
                    if (busy) "Encoding… ${(progress * 100).toInt()}%" else "Encode MP4",
                    color = MsColors.accentSoft,
                    fontWeight = FontWeight.Medium,
                    fontSize = 14.sp,
                )
            }
            if (busy) {
                TextButton(onClick = { cancelRequested.set(true) }) {
                    Text("Cancel export", color = MsColors.error, fontSize = 13.sp)
                }
            }
            Text(
                "Encoded on-device with MediaCodec from the C++ engine's frames and mixed audio. " +
                    "Scaling keeps the delivery aspect; frames are rendered at the project canvas.",
                color = MsColors.textMuted,
                fontSize = 10.sp,
            )
            message?.let { msg ->
                Spacer(Modifier.height(6.dp))
                Text(msg, color = MsColors.textSecondary, fontSize = 11.sp)
            }
            Spacer(Modifier.height(28.dp))
        }
    }
}

@Composable
private fun SheetChip(label: String, selected: Boolean, onClick: () -> Unit) {
    Text(
        label,
        color = if (selected) MsColors.accentSoft else MsColors.textSecondary,
        fontSize = 11.sp,
        modifier = Modifier
            .background(MsColors.surfaceHigh, RoundedCornerShape(3.dp))
            .border(
                1.dp,
                if (selected) MsColors.accentSoft else MsColors.border,
                RoundedCornerShape(3.dp),
            )
            .clickable(onClick = onClick)
            .padding(horizontal = 9.dp, vertical = 5.dp),
    )
}
