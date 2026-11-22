package com.motionstudio.app.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.motionstudio.app.editor.EditorEngine
import com.motionstudio.app.ui.theme.MsColors
import com.motionstudio.app.ui.theme.MsFonts

/** Small uppercase heading used by every panel in the lower half. */
@Composable
fun PanelHeader(title: String, trailing: String? = null) {
    Row(
        Modifier.fillMaxWidth().height(32.dp).background(MsColors.surfaceHigh)
            .padding(horizontal = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            title,
            color = MsColors.textTertiary,
            fontSize = 10.sp,
            fontFamily = MsFonts.mono,
            letterSpacing = 1.sp,
        )
        Spacer(Modifier.weight(1f))
        if (trailing != null) {
            Text(trailing, color = MsColors.textTertiary, fontSize = 9.sp, fontFamily = MsFonts.mono)
        }
    }
}

@Composable
private fun ParamRow(
    label: String,
    value: Float,
    range: ClosedFloatingPointRange<Float>,
    onChange: (Float) -> Unit,
) {
    Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.padding(vertical = 1.dp)) {
        Text(
            label.uppercase(),
            color = MsColors.textTertiary,
            fontSize = 10.sp,
            fontFamily = MsFonts.mono,
            modifier = Modifier.width(90.dp),
        )
        Slider(
            value = value.coerceIn(range.start, range.endInclusive),
            onValueChange = onChange,
            valueRange = range,
            colors = SliderDefaults.colors(
                thumbColor = MsColors.accent,
                activeTrackColor = MsColors.accent,
                inactiveTrackColor = MsColors.border,
            ),
            modifier = Modifier.weight(1f),
        )
        Text(
            "%.2f".format(value),
            color = MsColors.textSecondary,
            fontSize = 11.sp,
            fontFamily = MsFonts.mono,
            modifier = Modifier.width(54.dp),
        )
    }
}

/**
 * Master grade with the engine's own histogram drawn underneath. The bars are
 * the R/G/B/luma distribution of the last rendered frame, pulled from the
 * native scopes — not a decorative placeholder.
 */
@Composable
fun GradePane(engine: EditorEngine, modifier: Modifier = Modifier) {
    val bins = 48
    val histogram = engine.histogram(bins)

    Column(modifier.background(MsColors.background)) {
        PanelHeader("GRADE", if (engine.gradeActive) "ACTIVE" else "BYPASSED")
        Column(
            Modifier.fillMaxWidth().height(58.dp).background(MsColors.viewerBg)
                .padding(horizontal = 8.dp, vertical = 6.dp)
        ) {
            HistogramView(histogram, bins)
        }
        Column(
            Modifier
                .fillMaxWidth()
                .weight(1f)
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 12.dp, vertical = 4.dp)
        ) {
            ParamRow("Exposure", engine.gradeExposure, -2f..2f) {
                engine.applyGrade(exposure = it)
            }
            ParamRow("Contrast", engine.gradeContrast, 0.2f..2.5f) {
                engine.applyGrade(contrast = it)
            }
            ParamRow("Saturation", engine.gradeSaturation, 0f..2.5f) {
                engine.applyGrade(saturation = it)
            }
            ParamRow("Temperature", engine.gradeTemperature, -1f..1f) {
                engine.applyGrade(temperature = it)
            }
            ParamRow("Lift", engine.gradeLift, -1f..1f) {
                engine.applyGrade(lift = it)
            }
            ParamRow("Gamma", engine.gradeGamma, 0.2f..3f) {
                engine.applyGrade(gamma = it)
            }
            ParamRow("Gain", engine.gradeGain, 0f..2f) {
                engine.applyGrade(gain = it)
            }
            Spacer(Modifier.height(4.dp))
            Row {
                TextButton(onClick = { engine.resetGrade() }) {
                    Text("Reset grade", color = MsColors.error, fontSize = 11.sp)
                }
                TextButton(onClick = { engine.note(if (engine.gradeActive) "Grade active" else "Grade bypassed") }) {
                    Text(
                        "Applied after clip effects",
                        color = MsColors.textMuted,
                        fontSize = 10.sp,
                    )
                }
            }
        }
    }
}

@Composable
private fun HistogramView(data: IntArray, bins: Int) {
    Canvas(Modifier.fillMaxWidth().height(44.dp)) {
        if (data.size < bins * 4) return@Canvas
        var peak = 1
        for (i in 0 until bins * 4) if (data[i] > peak) peak = data[i]
        val channelWidth = size.width / bins
        val channels = listOf(
            MsColors.error.copy(alpha = 0.85f),
            MsColors.success.copy(alpha = 0.85f),
            MsColors.info.copy(alpha = 0.85f),
            Color(0xFFB9B9C2).copy(alpha = 0.45f),
        )
        for (c in 0..3) {
            for (i in 0 until bins) {
                val value = data[c * bins + i].toFloat() / peak
                if (value <= 0f) continue
                val h = value * size.height
                drawRect(
                    color = channels[c],
                    topLeft = Offset(i * channelWidth, size.height - h),
                    size = androidx.compose.ui.geometry.Size(
                        channelWidth * 0.92f,
                        h,
                    ),
                )
            }
        }
    }
}

/**
 * Selected-clip inspector: identity, timing and the effect stack summary, with
 * doors into the detailed editors.
 */
@Composable
fun ClipPane(
    engine: EditorEngine,
    onOpenEffects: () -> Unit,
    onOpenText: () -> Unit,
    onOpenMask: () -> Unit,
    onOpenMotion: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val track = engine.doc.tracks.firstOrNull { it.id == engine.selectedTrackId }
    val clip = engine.doc.clipById(engine.selectedTrackId, engine.selectedClipId)

    Column(modifier.background(MsColors.background)) {
        PanelHeader("CLIP", track?.name?.uppercase() ?: "NO TRACK")
        Column(
            Modifier.fillMaxWidth().weight(1f).verticalScroll(rememberScrollState())
                .padding(horizontal = 12.dp, vertical = 6.dp)
        ) {
            if (clip == null) {
                Text(
                    "Nothing selected. Tap a clip on the timeline.",
                    color = MsColors.textTertiary,
                    fontSize = 12.sp,
                )
                return@Column
            }
            Text(clip.name, color = MsColors.textPrimary, fontSize = 14.sp, fontWeight = FontWeight.Medium)
            Spacer(Modifier.height(4.dp))
            InfoRow("START", "%.3fs".format(clip.start))
            InfoRow("LENGTH", "%.3fs".format(clip.duration))
            InfoRow("END", "%.3fs".format(clip.end))
            InfoRow("OPACITY", "%.2f".format(clip.opacity))
            InfoRow(
                "KIND",
                when {
                    clip.isAdjustment -> "adjustment · tracks below"
                    clip.isText -> "title · ${clip.textFont}"
                    else -> "media #${clip.mediaId}"
                },
            )
            if (!clip.isAdjustment && !clip.isText) {
                InfoRow("SPEED", "%.2f×".format(clip.speed))
            }
            InfoRow("EFFECTS", clip.effects.size.toString())
            Spacer(Modifier.height(8.dp))

            if (clip.effects.isNotEmpty()) {
                Text(
                    "STACK",
                    color = MsColors.textMuted,
                    fontSize = 9.sp,
                    fontFamily = MsFonts.mono,
                    letterSpacing = 1.sp,
                )
                clip.effects.forEachIndexed { index, effect ->
                    Row(
                        Modifier.fillMaxWidth().padding(vertical = 2.dp),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Box(
                            Modifier.size(5.dp).background(
                                if (effect.enabled) MsColors.accent else MsColors.textMuted,
                                CircleShape,
                            )
                        )
                        Spacer(Modifier.width(8.dp))
                        Text(
                            MsEffects.firstOrNull { it.id == effect.id }?.name
                                ?: effect.id.replace('_', ' '),
                            color = MsColors.textSecondary,
                            fontSize = 12.sp,
                            modifier = Modifier.weight(1f),
                        )
                        Text(
                            "${effect.params.size}p",
                            color = MsColors.textMuted,
                            fontSize = 9.sp,
                            fontFamily = MsFonts.mono,
                        )
                        TextButton(onClick = { engine.setEffectEnabled(index, !effect.enabled) }) {
                            Text(
                                if (effect.enabled) "ON" else "OFF",
                                color = if (effect.enabled) MsColors.success else MsColors.textMuted,
                                fontSize = 9.sp,
                                fontFamily = MsFonts.mono,
                            )
                        }
                    }
                }
            }

            Spacer(Modifier.height(10.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                SmallAction("Effects", onOpenEffects)
                SmallAction("Title", onOpenText)
            }
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                SmallAction("Mask", onOpenMask)
                SmallAction("Motion", onOpenMotion)
            }
        }
    }
}

@Composable
private fun InfoRow(label: String, value: String) {
    Row(Modifier.fillMaxWidth().padding(vertical = 1.dp)) {
        Text(
            label,
            color = MsColors.textMuted,
            fontSize = 10.sp,
            fontFamily = MsFonts.mono,
            modifier = Modifier.width(78.dp),
        )
        Text(value, color = MsColors.textSecondary, fontSize = 12.sp, fontFamily = MsFonts.mono)
    }
}

@Composable
private fun SmallAction(label: String, onClick: () -> Unit) {
    Box(
        Modifier
            .heightIn(min = 34.dp)
            .clip(RoundedCornerShape(8.dp))
            .background(MsColors.surfaceHigh)
            .border(1.dp, MsColors.border, RoundedCornerShape(8.dp))
            .clickable(onClick = onClick)
            .padding(horizontal = 14.dp, vertical = 7.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(label, color = MsColors.accentSoft, fontSize = 12.sp)
    }
}
