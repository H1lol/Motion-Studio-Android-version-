package com.motionstudio.app.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.motionstudio.app.editor.EditorEngine
import com.motionstudio.app.ui.theme.MsColors
import com.motionstudio.app.ui.theme.MsFonts

data class EffectParamDef(
    val name: String,
    val label: String,
    val min: Float,
    val max: Float,
    val default: Float,
)

data class EffectDef(
    val id: String,
    val name: String,
    val note: String,
    val params: List<EffectParamDef>,
    val group: String = "Basic",
)

private fun p(name: String, label: String, min: Float, max: Float, default: Float) =
    EffectParamDef(name, label, min, max, default)

/**
 * Mirrors the CPU registry: cpu_effects.cpp, cpu_effects_extra.cpp and
 * cpu_effects_catalogue.cpp. Every id here is reachable through
 * cpu_effect_apply and covered by ms_effects_catalogue_test.
 */
val MsEffects = listOf(
    // ---- Basic ----
    EffectDef("gaussian_blur", "Gaussian Blur", "Separable CPU blur",
        listOf(p("radius", "Radius", 0f, 64f, 4f)), "Basic"),
    EffectDef("box_blur", "Box Blur", "Running-sum blur",
        listOf(p("radius", "Radius", 0f, 64f, 4f)), "Basic"),
    EffectDef("sharpen", "Sharpen", "Unsharp 3x3",
        listOf(p("amount", "Amount", 0f, 3f, 0.6f)), "Basic"),
    EffectDef("brightness_contrast", "Brightness / Contrast", "Levels",
        listOf(p("brightness", "Brightness", -1f, 1f, 0.06f), p("contrast", "Contrast", 0f, 2f, 1.1f)), "Basic"),
    EffectDef("levels", "Levels", "Black / white / gamma",
        listOf(
            p("black", "Black", 0f, 0.9f, 0f),
            p("white", "White", 0.1f, 1f, 1f),
            p("gamma", "Gamma", 0.2f, 3f, 1.2f),
        ), "Basic"),
    EffectDef("opacity", "Opacity", "Blend to transparent",
        listOf(p("opacity", "Opacity", 0f, 1f, 0.8f)), "Basic"),

    // ---- Colour ----
    EffectDef("grayscale", "Grayscale", "Luma desaturate", emptyList(), "Colour"),
    EffectDef("sepia", "Sepia", "Warm monochrome",
        listOf(p("amount", "Amount", 0f, 1f, 1f)), "Colour"),
    EffectDef("invert", "Invert", "Negative", emptyList(), "Colour"),
    EffectDef("saturation", "Saturation", "1 = unchanged, 0 = grey",
        listOf(p("saturation", "Amount", 0f, 3f, 1.4f)), "Colour"),
    EffectDef("hue_shift", "Hue Shift", "Rotate the colour wheel",
        listOf(p("degrees", "Degrees", -180f, 180f, 30f)), "Colour"),
    EffectDef("white_balance", "White Balance", "Temperature / tint",
        listOf(
            p("temperature", "Temp", -1f, 1f, 0.2f),
            p("tint", "Tint", -1f, 1f, 0f),
        ), "Colour"),
    EffectDef("posterize", "Posterize", "Quantise to N levels",
        listOf(p("levels", "Levels", 2f, 32f, 8f)), "Colour"),
    EffectDef("threshold", "Threshold", "Binarize on luma",
        listOf(p("level", "Level", 0f, 1f, 0.5f)), "Colour"),
    EffectDef("chromakey", "Chroma Key", "Knock out green",
        listOf(p("tolerance", "Tolerance", 0f, 1f, 0.2f)), "Colour"),

    // ---- Stylise ----
    EffectDef("glow", "Glow", "Blur screened back over the image",
        listOf(p("amount", "Amount", 0f, 1f, 0.5f), p("radius", "Radius", 0f, 32f, 8f)), "Stylise"),
    EffectDef("noise", "Film Grain", "Deterministic per-pixel noise",
        listOf(p("amount", "Amount", 0f, 0.6f, 0.15f)), "Stylise"),
    EffectDef("emboss", "Emboss", "Directional relief",
        listOf(p("amount", "Amount", 0f, 2f, 0.5f)), "Stylise"),
    EffectDef("edge_detect", "Edges", "Sobel gradient",
        listOf(p("amount", "Amount", 0f, 3f, 1f)), "Stylise"),
    EffectDef("pixelate", "Pixelate", "Block mosaic",
        listOf(p("size", "Size", 1f, 64f, 8f)), "Stylise"),
    EffectDef("vignette", "Vignette", "Edge falloff",
        listOf(p("strength", "Strength", 0f, 1f, 0.6f), p("radius", "Radius", 0.1f, 1.5f, 1f)), "Stylise"),
    EffectDef("motion_blur", "Motion Blur", "Directional shutter smear",
        listOf(
            p("distance", "Distance", 0f, 64f, 12f),
            p("angle", "Angle", -180f, 180f, 0f),
            p("samples", "Samples", 1f, 64f, 16f),
        ), "Stylise"),
    EffectDef("particles", "Particles", "Additive sparks rising through frame",
        listOf(
            p("count", "Count", 0f, 256f, 64f),
            p("size", "Size", 1f, 32f, 6f),
            p("speed", "Speed", 0f, 2f, 0.6f),
            p("time", "Time", 0f, 60f, 0f),
            p("seed", "Seed", 1f, 16f, 1f),
        ), "Stylise"),

    // ---- Warp ----
    EffectDef("chromatic_aberration", "Chromatic Aberration", "Radial RGB split, red out / blue in",
        listOf(
            p("amount", "Amount", 0f, 1f, 0.6f),
            p("center_x", "Centre X", 0f, 1f, 0.5f),
            p("center_y", "Centre Y", 0f, 1f, 0.5f),
        ), "Warp"),
    EffectDef("rgb_split", "RGB Split", "Directional channel offset",
        listOf(p("distance", "Distance", 0f, 40f, 6f), p("angle", "Angle", -180f, 180f, 0f)), "Warp"),
    EffectDef("kaleidoscope", "Kaleidoscope", "Mirrored wedges around the centre",
        listOf(
            p("segments", "Segments", 2f, 24f, 6f),
            p("rotation", "Rotation", -180f, 180f, 0f),
            p("zoom", "Zoom", 0.25f, 3f, 1f),
        ), "Warp"),
    EffectDef("twirl", "Twirl", "Swirl inside a radius",
        listOf(
            p("angle", "Angle", -720f, 720f, 180f),
            p("radius", "Radius", 0.05f, 2f, 0.8f),
            p("center_x", "Centre X", 0f, 1f, 0.5f),
            p("center_y", "Centre Y", 0f, 1f, 0.5f),
        ), "Warp"),
    EffectDef("wave_warp", "Wave Warp", "Sinusoidal displacement",
        listOf(
            p("amplitude", "Amplitude", 0f, 0.15f, 0.02f),
            p("frequency", "Frequency", 0.1f, 32f, 6f),
            p("phase", "Phase", -360f, 360f, 0f),
        ), "Warp"),
    EffectDef("fisheye", "Fisheye", "Barrel lens warp",
        listOf(
            p("amount", "Amount", -0.9f, 1f, 0.5f),
            p("zoom", "Zoom", 0.5f, 2f, 1f),
            p("center_x", "Centre X", 0f, 1f, 0.5f),
            p("center_y", "Centre Y", 0f, 1f, 0.5f),
        ), "Warp"),

    // ---- Screen ----
    EffectDef("halftone", "Halftone", "Rotated dot screen from luma",
        listOf(
            p("cell", "Cell", 2f, 40f, 6f),
            p("angle", "Angle", -90f, 90f, 45f),
            p("mix", "Mix", 0f, 1f, 1f),
        ), "Screen"),
    EffectDef("scanlines", "Scanlines", "CRT line darkening",
        listOf(
            p("spacing", "Spacing", 2f, 24f, 4f),
            p("strength", "Strength", 0f, 1f, 0.45f),
            p("phase", "Phase", -360f, 360f, 0f),
        ), "Screen"),
    EffectDef("glitch", "Glitch", "Band slips, torn rows, channel fringe",
        listOf(
            p("amount", "Amount", 0f, 1f, 0.5f),
            p("bands", "Bands", 4f, 64f, 24f),
            p("seed", "Seed", 1f, 16f, 1f),
        ), "Screen"),
    EffectDef("vhs", "VHS Tape", "Wobble, chroma bleed, tracking band",
        listOf(
            p("amount", "Amount", 0f, 1f, 0.6f),
            p("tracking", "Tracking", 0f, 1f, 0.35f),
            p("seed", "Seed", 1f, 16f, 3f),
        ), "Screen"),

    // ---- Framing ----
    EffectDef("mirror", "Mirror", "Flip horizontally or vertically",
        listOf(p("horizontal", "Horizontal", 0f, 1f, 1f)), "Framing"),
    EffectDef("crop", "Crop", "Clear the frame edges",
        listOf(
            p("left", "Left", 0f, 0.9f, 0.08f),
            p("right", "Right", 0f, 0.9f, 0.08f),
            p("top", "Top", 0f, 0.9f, 0.08f),
            p("bottom", "Bottom", 0f, 0.9f, 0.08f),
        ), "Framing"),
)

private fun definition(id: String): EffectDef =
    MsEffects.firstOrNull { it.id == id } ?: EffectDef(id, id.replace('_', ' '), "Plugin effect", emptyList())

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun EffectsSheet(engine: EditorEngine, onDismiss: () -> Unit) {
    val sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)
    val applied = engine.selectedEffects
    val clipName = engine.doc.clipById(engine.selectedTrackId, engine.selectedClipId)?.name
    // Local slider positions so dragging does not fight the engine round-trip.
    val drafts = remember(applied.size, engine.selectedClipId) { mutableStateMapOf<String, Float>() }

    ModalBottomSheet(
        onDismissRequest = onDismiss,
        sheetState = sheetState,
        containerColor = MsColors.surface,
    ) {
        Column(Modifier.fillMaxWidth().padding(horizontal = 20.dp)) {
            Text("Effects", color = MsColors.textPrimary, fontSize = 16.sp, fontWeight = FontWeight.SemiBold)
            Spacer(Modifier.height(2.dp))
            Text(
                if (clipName != null) "On “$clipName” · rendered by the C++ effect stack"
                else "Select a clip to add effects",
                color = MsColors.textTertiary,
                fontSize = 12.sp,
            )
            Spacer(Modifier.height(10.dp))

            LazyColumn(Modifier.fillMaxWidth().height(430.dp)) {
                if (clipName != null && applied.isNotEmpty()) {
                    item {
                        Column(Modifier.padding(bottom = 2.dp)) {
                            Text("APPLIED", color = MsColors.textMuted, fontSize = 9.sp, fontFamily = MsFonts.mono)
                            Text(
                                "Runs top to bottom \u00b7 \u25b2\u25bc reorder",
                                color = MsColors.textMuted,
                                fontSize = 9.sp,
                                fontFamily = MsFonts.mono,
                            )
                        }
                    }
                    items(applied.size) { index ->
                        val effect = applied[index]
                        val def = definition(effect.id)
                        Column(Modifier.fillMaxWidth().padding(vertical = 6.dp)) {
                            Row(verticalAlignment = Alignment.CenterVertically) {
                                Column(Modifier.weight(1f)) {
                                    Text(
                                        def.name,
                                        color = MsColors.textPrimary,
                                        fontSize = 14.sp,
                                        fontWeight = FontWeight.Medium,
                                    )
                                    Text(
                                        "${effect.params.size} params · ${def.note}",
                                        color = MsColors.textMuted,
                                        fontSize = 11.sp,
                                    )
                                }
                                Column(
                                    horizontalAlignment = Alignment.CenterHorizontally,
                                    modifier = Modifier.padding(horizontal = 6.dp),
                                ) {
                                    Text(
                                        "\u25b2",
                                        fontSize = 10.sp,
                                        color = if (index > 0) MsColors.textSecondary else MsColors.textMuted,
                                        modifier = Modifier
                                            .clickable(enabled = index > 0) {
                                                if (engine.moveEffect(index, index - 1)) drafts.clear()
                                            }
                                            .padding(4.dp),
                                    )
                                    Text(
                                        "\u25bc",
                                        fontSize = 10.sp,
                                        color = if (index < applied.size - 1) MsColors.textSecondary
                                        else MsColors.textMuted,
                                        modifier = Modifier
                                            .clickable(enabled = index < applied.size - 1) {
                                                if (engine.moveEffect(index, index + 1)) drafts.clear()
                                            }
                                            .padding(4.dp),
                                    )
                                }
                                Switch(
                                    checked = effect.enabled,
                                    onCheckedChange = { engine.setEffectEnabled(index, it) },
                                )
                                TextButton(onClick = {
                                    engine.removeEffect(index)
                                    drafts.clear()
                                }) {
                                    Text("Remove", color = MsColors.error, fontSize = 11.sp)
                                }
                            }
                            def.params.forEach { param ->
                                val key = "$index:${param.name}"
                                val current = drafts[key]
                                    ?: (effect.params[param.name]?.toFloat() ?: param.default)
                                Row(verticalAlignment = Alignment.CenterVertically) {
                                    Text(
                                        "${param.label}  ${"%.2f".format(current)}",
                                        color = MsColors.textSecondary,
                                        fontSize = 11.sp,
                                        fontFamily = MsFonts.mono,
                                        modifier = Modifier.fillMaxWidth(0.34f),
                                    )
                                    Slider(
                                        value = current.coerceIn(param.min, param.max),
                                        onValueChange = {
                                            drafts[key] = it
                                            engine.setEffectParam(index, param.name, it)
                                        },
                                        valueRange = param.min..param.max,
                                        modifier = Modifier.weight(1f),
                                    )
                                }
                            }
                        }
                    }
                    item { Spacer(Modifier.height(10.dp)) }
                }

                item {
                    Text("ADD", color = MsColors.textMuted, fontSize = 9.sp, fontFamily = MsFonts.mono)
                }
                MsEffects.groupBy { it.group }.forEach { (group, defs) ->
                    item {
                        Text(
                            group.uppercase(),
                            color = MsColors.textMuted,
                            fontSize = 9.sp,
                            fontFamily = MsFonts.mono,
                            modifier = Modifier.padding(top = 8.dp),
                        )
                    }
                    items(defs) { def ->
                        Row(
                            Modifier.fillMaxWidth().padding(vertical = 3.dp),
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(8.dp),
                        ) {
                            Column(Modifier.weight(1f)) {
                                Text(def.name, color = MsColors.textPrimary, fontSize = 14.sp)
                                Text(def.note, color = MsColors.textMuted, fontSize = 11.sp)
                            }
                            TextButton(
                                onClick = {
                                    engine.addEffect(def.id, def.params.associate { it.name to it.default })
                                },
                                enabled = clipName != null,
                            ) {
                                Text(
                                    "Add",
                                    color = if (clipName != null) MsColors.accentSoft
                                    else MsColors.textMuted,
                                    fontSize = 12.sp,
                                )
                            }
                        }
                    }
                }
            }
            Spacer(Modifier.height(20.dp))
        }
    }
}
