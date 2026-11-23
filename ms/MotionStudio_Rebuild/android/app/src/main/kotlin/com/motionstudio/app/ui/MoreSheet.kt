package com.motionstudio.app.ui

import android.content.Context
import android.graphics.Bitmap
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
import androidx.compose.foundation.layout.heightIn
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
import androidx.compose.runtime.mutableIntStateOf
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
import com.motionstudio.app.data.PreviewQuality
import com.motionstudio.app.data.PreviewQualityStore
import com.motionstudio.app.editor.EditorEngine
import com.motionstudio.app.export.ExportSettings
import com.motionstudio.app.media.ProxyGenerator
import com.motionstudio.app.media.ProxyStore
import com.motionstudio.app.model.CanvasRatio
import com.motionstudio.app.model.PreviewRatio
import com.motionstudio.app.native.NativeEngine
import com.motionstudio.app.ui.theme.MsColors
import java.io.File
import java.io.FileOutputStream
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun MoreSheet(
    engine: EditorEngine,
    onDismiss: () -> Unit,
    onExportFrame: () -> Unit,
    onOpenExport: () -> Unit,
) {
    val sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)
    var gainDb by remember { mutableFloatStateOf(0f) }
    var fpsText by remember { mutableStateOf(ExportSettings.formatFps(engine.fps)) }
    var proxyBusy by remember { mutableStateOf(false) }
    var proxyProgress by remember { mutableFloatStateOf(0f) }
    var proxyStatus by remember { mutableStateOf<String?>(null) }
    var proxyRevision by remember { mutableIntStateOf(0) }
    val scope = rememberCoroutineScope()
    val context = LocalContext.current

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
            SheetHeader(
                "Session",
                "${engine.doc.width} × ${engine.doc.height} · " +
                    "${ExportSettings.formatFps(engine.fps)} fps · " +
                    "${engine.doc.media.size} media · ${engine.doc.tracks.sumOf { it.clips.size }} clips",
                onDismiss = onDismiss,
            )
            Spacer(Modifier.height(6.dp))

            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                TextButton(onClick = { engine.splitAtPlayhead() }) { Text("Split clip") }
                TextButton(onClick = { engine.undo() }) { Text("Undo") }
                TextButton(onClick = { engine.redo() }) { Text("Redo") }
            }
            TextButton(onClick = { engine.addAdjustmentLayer() }) {
                Text("Add adjustment layer", color = MsColors.accentSoft, fontSize = 12.sp)
            }
            Text(
                "An adjustment layer has no pixels: its effects and grade run over " +
                    "every track below it, over the range it covers.",
                color = MsColors.textMuted,
                fontSize = 10.sp,
            )
            TextButton(onClick = { engine.addNullObject() }) {
                Text("Add null object", color = MsColors.accentSoft, fontSize = 12.sp)
            }
            Text(
                "A null is an invisible control layer. Parent clips to it in Motion, " +
                    "animate the null once, and the whole rig moves with it.",
                color = MsColors.textMuted,
                fontSize = 10.sp,
            )

            Spacer(Modifier.height(6.dp))
            SheetSection("CANVAS RATIO")
            Row(
                Modifier.horizontalScroll(rememberScrollState()),
                horizontalArrangement = Arrangement.spacedBy(6.dp),
            ) {
                CanvasRatio.entries.forEach { ratio ->
                    val active = ratio.matches(engine.doc.width, engine.doc.height)
                    SessionChip(ratio.label, active) {
                        if (!active) {
                            engine.setProjectSettings(ratio.width, ratio.height, engine.fps)
                        }
                    }
                }
            }
            Text(
                "Changing the canvas resizes the composited frame; clip times are untouched.",
                color = MsColors.textMuted,
                fontSize = 10.sp,
            )

            Spacer(Modifier.height(10.dp))
            SheetSection("PROJECT FRAME RATE")
            Row(
                Modifier.horizontalScroll(rememberScrollState()),
                horizontalArrangement = Arrangement.spacedBy(6.dp),
            ) {
                listOf(23.976, 24.0, 25.0, 29.97, 30.0, 50.0, 59.94, 60.0).forEach { f ->
                    val active = kotlin.math.abs(engine.fps - f) < 0.005
                    SessionChip(ExportSettings.formatFps(f), active) {
                        if (!active) {
                            engine.setProjectSettings(engine.doc.width, engine.doc.height, f)
                        }
                    }
                }
            }
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                OutlinedTextField(
                    value = fpsText,
                    onValueChange = { text ->
                        fpsText = text.filter { it.isDigit() || it == '.' }.take(6)
                    },
                    singleLine = true,
                    label = { Text("Custom fps", fontSize = 10.sp) },
                    textStyle = androidx.compose.ui.text.TextStyle(fontSize = 12.sp),
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal),
                    modifier = Modifier.width(112.dp),
                )
                TextButton(
                    enabled = (fpsText.toDoubleOrNull() ?: 0.0) in 1.0..240.0,
                    onClick = {
                        val f = fpsText.toDoubleOrNull() ?: return@TextButton
                        engine.setProjectSettings(engine.doc.width, engine.doc.height, f)
                    },
                ) {
                    Text("Set", color = MsColors.accentSoft, fontSize = 11.sp)
                }
            }

            Spacer(Modifier.height(10.dp))
            SheetSection("PREVIEW RATIO")
            Row(
                Modifier.horizontalScroll(rememberScrollState()),
                horizontalArrangement = Arrangement.spacedBy(6.dp),
            ) {
                PreviewRatio.entries.forEach { ratio ->
                    SessionChip(ratio.label, engine.previewRatio == ratio) {
                        engine.previewRatio = ratio
                    }
                }
            }
            Text(
                "Guide only: the delivery crop is shaded, the composite is never cut.",
                color = MsColors.textMuted,
                fontSize = 10.sp,
            )

            Spacer(Modifier.height(10.dp))
            SheetSection("PREVIEW QUALITY")
            Row(
                Modifier.horizontalScroll(rememberScrollState()),
                horizontalArrangement = Arrangement.spacedBy(6.dp),
            ) {
                PreviewQuality.entries.forEach { quality ->
                    SessionChip(
                        "${quality.label} ${(quality.scale * 100).toInt()}%",
                        PreviewQualityStore.quality == quality,
                    ) {
                        PreviewQualityStore.set(quality)
                    }
                }
            }
            Text(
                PreviewQualityStore.quality.detail +
                    ". A preview setting, not a delivery one: export always renders the " +
                    "project canvas in full.",
                color = MsColors.textMuted,
                fontSize = 10.sp,
            )

            Spacer(Modifier.height(10.dp))
            SheetSection("IMPORT PLACEMENT")
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                SessionChip("At playhead", !engine.appendImportsAtEnd) {
                    engine.appendImportsAtEnd = false
                }
                SessionChip("Append to end", engine.appendImportsAtEnd) {
                    engine.appendImportsAtEnd = true
                }
            }

            Spacer(Modifier.height(10.dp))
            SheetSection("MARKERS")
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                TextButton(onClick = { engine.addMarker() }) {
                    Text("Add at playhead (${engine.time.toInt()}s)", fontSize = 12.sp)
                }
            }
            engine.doc.markers.forEach { m ->
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        "%.2f s".format(m.time) + if (m.name.isBlank()) "" else "  ${m.name}",
                        color = MsColors.textSecondary,
                        fontSize = 11.sp,
                        modifier = Modifier.weight(1f),
                    )
                    TextButton(onClick = { engine.removeMarker(m.id) }) {
                        Text("Remove", color = MsColors.textTertiary, fontSize = 11.sp)
                    }
                }
            }

            Spacer(Modifier.height(10.dp))
            SheetSection("PROXY MEDIA")
            val proxyFiles = remember(proxyRevision) { ProxyStore.count() }
            val proxyBytes = remember(proxyRevision) { ProxyStore.totalBytes() }
            Text(
                "$proxyFiles file(s) · ${"%.1f".format(proxyBytes / 1_048_576.0)} MB · " +
                    "preview decodes 720p stand-ins, export still reads the originals",
                color = MsColors.textTertiary,
                fontSize = 11.sp,
            )
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                TextButton(
                    enabled = !proxyBusy,
                    onClick = {
                        val media = engine.doc.media
                        scope.launch {
                            proxyBusy = true
                            proxyProgress = 0f
                            proxyStatus = null
                            val missing = withContext(Dispatchers.IO) { ProxyStore.missing(media) }
                            if (missing.isEmpty()) {
                                proxyStatus =
                                    "Nothing to build: every source is already small enough or proxied."
                            } else {
                                var done = 0
                                var failed = 0
                                for ((index, m) in missing.withIndex()) {
                                    val result = withContext(Dispatchers.IO) {
                                        ProxyGenerator.generate(m.path) { p ->
                                            proxyProgress = (index + p) / missing.size
                                        }
                                    }
                                    when (result) {
                                        is ProxyGenerator.Result.Done -> {
                                            NativeEngine.nativeProxyRegister(
                                                m.path, result.file.absolutePath,
                                            )
                                            done++
                                        }
                                        is ProxyGenerator.Result.Skipped -> done++
                                        is ProxyGenerator.Result.Failed -> failed++
                                    }
                                }
                                proxyStatus = buildString {
                                    append("Built $done of ${missing.size} proxy file(s)")
                                    if (failed > 0) append(", $failed failed")
                                    append("\nPlayback uses them now; export re-reads the source.")
                                }
                            }
                            proxyBusy = false
                            proxyRevision++
                        }
                    },
                ) {
                    Text(
                        if (proxyBusy) {
                            "Building… ${(proxyProgress * 100).toInt()}%"
                        } else {
                            "Generate for this project"
                        },
                        color = MsColors.accentSoft,
                        fontSize = 13.sp,
                    )
                }
                TextButton(
                    enabled = !proxyBusy && proxyFiles > 0,
                    onClick = {
                        ProxyStore.clear()
                        proxyStatus = "Deleted every proxy file. Preview decodes the sources again."
                        proxyRevision++
                    },
                ) {
                    Text("Delete all", color = MsColors.textSecondary, fontSize = 13.sp)
                }
            }
            proxyStatus?.let {
                Text(it, color = MsColors.textSecondary, fontSize = 11.sp)
            }

            Spacer(Modifier.height(8.dp))
            Text(
                "Master gain  ${gainDb.toInt()} dB",
                color = MsColors.textSecondary,
                fontSize = 13.sp,
            )
            Slider(
                value = gainDb,
                onValueChange = {
                    gainDb = it
                    NativeEngine.nativeSetMasterGain(it)
                },
                valueRange = -24f..12f,
            )

            Spacer(Modifier.height(4.dp))
            val backend = NativeEngine.nativeDecoderBackend()
            Text(
                "Decoder: $backend" + if (backend == "slate") "  (placeholder — no real decode)" else "",
                color = if (backend == "slate") MsColors.error else MsColors.success,
                fontSize = 11.sp,
            )
            Text(
                "Renderer: ${NativeEngine.nativePerfFrameMs().toInt()} ms/frame · " +
                    "${NativeEngine.nativePerfFps().toInt()} fps · native C++ CPU compositor",
                color = MsColors.textMuted,
                fontSize = 11.sp,
            )

            Spacer(Modifier.height(12.dp))
            TextButton(onClick = onOpenExport) {
                Text(
                    "Export…",
                    color = MsColors.accentSoft,
                    fontWeight = FontWeight.Medium,
                )
            }
            TextButton(onClick = onExportFrame) {
                Text(
                    "Render current frame to PNG",
                    color = MsColors.textSecondary,
                    fontWeight = FontWeight.Medium,
                )
            }
            Spacer(Modifier.height(28.dp))
        }
    }
}


@Composable
private fun SessionChip(label: String, selected: Boolean, onClick: () -> Unit) {
    Text(
        label,
        color = if (selected) MsColors.accentSoft else MsColors.textSecondary,
        fontSize = 11.sp,
        modifier = Modifier
            .heightIn(min = 32.dp)
            .background(MsColors.surfaceHigh, RoundedCornerShape(5.dp))
            .border(
                1.dp,
                if (selected) MsColors.accentSoft else MsColors.border,
                RoundedCornerShape(5.dp),
            )
            .clickable(onClick = onClick)
            .padding(horizontal = 11.dp, vertical = 7.dp),
    )
}

/** Renders the current engine frame and writes it as a PNG. */
fun exportCurrentFrame(context: Context, engine: EditorEngine): File? {
    val state = NativeEngine.nativePreviewState()
    if (state.size < 4) return null
    // A still is delivery, not preview: render at the project canvas even when
    // the preview-quality setting is reduced.
    val previewScale = NativeEngine.nativePreviewScale()
    val frameSize = IntArray(2)
    return try {
        if (previewScale < 0.999f) NativeEngine.nativePreviewScaleSet(1f)
        if (!NativeEngine.nativePreviewFrameSize(frameSize)) return null
        val width = frameSize[0]
        val height = frameSize[1]
        if (width <= 0 || height <= 0) return null
        if (!NativeEngine.nativeRenderFrame()) return null

        val bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
        if (!NativeEngine.nativeCopyFrame(bitmap)) return null

        val dir = File(context.filesDir, "exports").apply { mkdirs() }
        val target = File(dir, "frame_${System.currentTimeMillis()}.png")
        FileOutputStream(target).use { out ->
            bitmap.compress(Bitmap.CompressFormat.PNG, 100, out)
        }
        bitmap.recycle()
        target
    } finally {
        if (previewScale < 0.999f) NativeEngine.nativePreviewScaleSet(previewScale)
    }
}
