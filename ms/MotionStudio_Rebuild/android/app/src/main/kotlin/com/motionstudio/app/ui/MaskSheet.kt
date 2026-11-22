package com.motionstudio.app.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
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
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.motionstudio.app.editor.EditorEngine
import com.motionstudio.app.model.ClipMask
import com.motionstudio.app.ui.theme.MsColors
import com.motionstudio.app.ui.theme.MsFonts

/**
 * Shape masks, up to eight per clip.
 *
 * Add masks union together and Subtract masks are cut out of that union, so a
 * matte can be "this box, minus this hole". A clip whose masks are all Subtract
 * starts from a full frame, which makes one subtract mask a simple cut-out.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun MaskSheet(engine: EditorEngine, onDismiss: () -> Unit) {
    val sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)

    val masks = engine.clipMasks
    var index by remember { mutableIntStateOf(0) }
    val current: ClipMask? = masks.getOrNull(index)

    LaunchedEffect(engine.selectedClipId) { engine.refreshClipMasks() }
    LaunchedEffect(masks.size) {
        if (index >= masks.size) index = (masks.size - 1).coerceAtLeast(0)
    }

    // Local edits while the sliders move; reseeded when a different mask is
    // selected (ClipMask is a data class, so an unchanged mask does not reset).
    var shape by remember(current) { mutableIntStateOf(current?.shape ?: 0) }
    var x by remember(current) { mutableFloatStateOf(current?.x ?: 0.2f) }
    var y by remember(current) { mutableFloatStateOf(current?.y ?: 0.2f) }
    var w by remember(current) { mutableFloatStateOf(current?.w ?: 0.6f) }
    var h by remember(current) { mutableFloatStateOf(current?.h ?: 0.6f) }
    var feather by remember(current) { mutableFloatStateOf(current?.feather ?: 0f) }
    var inverted by remember(current) { mutableStateOf(current?.inverted ?: false) }
    var op by remember(current) { mutableIntStateOf(current?.op ?: 0) }

    fun push() {
        if (current == null) return
        engine.updateClipMask(index, shape, x, y, w, h, feather, inverted, op)
    }

    ModalBottomSheet(
        onDismissRequest = onDismiss,
        sheetState = sheetState,
        containerColor = MsColors.surface,
    ) {
        Column(
            Modifier
                .fillMaxWidth()
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 20.dp)
                .padding(bottom = 24.dp),
        ) {
            SheetHeader(
                title = "Masks",
                subtitle = when {
                    engine.selectedClipId < 0 -> "Select a clip to mask it"
                    masks.isEmpty() -> "No masks on the selected clip"
                    masks.size == 1 -> "1 mask on the selected clip"
                    else -> "${masks.size} masks on the selected clip"
                },
                onDismiss = onDismiss,
            )

            Spacer(Modifier.height(10.dp))

            // Mask list. Each chip selects a mask; the glyph says whether it
            // adds to or subtracts from the matte.
            // Eight masks plus the add chip overflowed the sheet on a phone; the
            // strip scrolls so the last mask is still reachable.
            Row(
                Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()),
                horizontalArrangement = Arrangement.spacedBy(6.dp),
            ) {
                masks.forEachIndexed { i, m ->
                    val selected = i == index
                    MaskChip(
                        label = "${if (m.op == 1) "−" else "+"} ${i + 1}",
                        selected = selected,
                        onClick = { index = i },
                    )
                }
                if (masks.isNotEmpty()) {
                    MaskChip(
                        label = "＋",
                        selected = false,
                        enabled = masks.size < 8,
                        onClick = { engine.addClipMask(op = 0) },
                    )
                }
            }

            if (masks.isEmpty()) {
                Spacer(Modifier.height(8.dp))
                TextButton(onClick = { engine.addClipMask(op = 0) }) {
                    Text(
                        "Add a mask",
                        color = if (engine.selectedClipId >= 0) MsColors.accentSoft else MsColors.textMuted,
                        fontSize = 13.sp,
                        fontWeight = FontWeight.SemiBold,
                    )
                }
            }

            if (current != null) {
                Spacer(Modifier.height(6.dp))
                Row(
                    Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()),
                    horizontalArrangement = Arrangement.spacedBy(6.dp),
                ) {
                    MaskChip("Add", op == 0, onClick = { op = 0; push() })
                    MaskChip("Subtract", op == 1, onClick = { op = 1; push() })
                }

                Spacer(Modifier.height(8.dp))
                Row(
                    Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()),
                    horizontalArrangement = Arrangement.spacedBy(6.dp),
                ) {
                    listOf("Rect" to 0, "Ellipse" to 1, "Full" to 2).forEach { (label, value) ->
                        MaskChip(label, shape == value, onClick = { shape = value; push() })
                    }
                }

                Spacer(Modifier.height(8.dp))
                MaskSlider("Centre X", x, 0f, 1f) { x = it; push() }
                MaskSlider("Centre Y", y, 0f, 1f) { y = it; push() }
                MaskSlider("Width", w, 0.02f, 1f) { w = it; push() }
                MaskSlider("Height", h, 0.02f, 1f) { h = it; push() }
                MaskSlider("Feather (px)", feather, 0f, 200f) { feather = it; push() }

                // ◆ keys the current value at the playhead; a keyed chip shows
                // its key count and clears that curve when tapped.
                var keyRevision by remember { mutableIntStateOf(0) }
                Row(
                    Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()),
                    horizontalArrangement = Arrangement.spacedBy(4.dp),
                ) {
                    listOf(
                        Triple("X", 0, x),
                        Triple("Y", 1, y),
                        Triple("W", 2, w),
                        Triple("H", 3, h),
                        Triple("F", 4, feather),
                    ).forEach { (label, prop, value) ->
                        val count = remember(keyRevision, index, prop) {
                            engine.maskKeyframeCount(index, prop)
                        }
                        MaskChip(
                            label = if (count > 0) "$label $count ✕" else "◆ $label",
                            selected = count > 0,
                            onClick = {
                                if (count > 0) engine.clearMaskKeyframes(index, prop)
                                else engine.addMaskKeyframe(index, prop, value)
                                keyRevision++
                            },
                        )
                    }
                }

                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        "Invert this mask",
                        color = MsColors.textSecondary,
                        fontSize = 12.sp,
                        modifier = Modifier.weight(1f),
                    )
                    Switch(
                        checked = inverted,
                        onCheckedChange = {
                            inverted = it
                            push()
                        },
                    )
                }
            }

            Spacer(Modifier.height(10.dp))
            Text(
                "Add masks union together; subtract masks are cut out of that union. " +
                    "With only subtract masks the clip starts from a full frame. " +
                    "The matte multiplies the clip's alpha before its transform, so " +
                    "keyframed scale and position still apply to the matted image.",
                color = MsColors.textTertiary,
                fontSize = 11.sp,
            )

            Spacer(Modifier.height(8.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                TextButton(onClick = { push(); onDismiss() }) {
                    Text("Apply", color = MsColors.accentSoft, fontSize = 13.sp, fontWeight = FontWeight.SemiBold)
                }
                TextButton(
                    enabled = current != null,
                    onClick = {
                        if (current != null) engine.removeClipMask(index)
                    },
                ) {
                    Text(
                        "Remove this mask",
                        color = if (current != null) MsColors.textSecondary else MsColors.textMuted,
                        fontSize = 13.sp,
                    )
                }
                TextButton(
                    enabled = masks.isNotEmpty(),
                    onClick = {
                        engine.clearClipMask()
                        onDismiss()
                    },
                ) {
                    Text(
                        "Remove all",
                        color = if (masks.isNotEmpty()) MsColors.error else MsColors.textMuted,
                        fontSize = 13.sp,
                    )
                }
            }
        }
    }
}

@Composable
private fun MaskChip(
    label: String,
    selected: Boolean,
    enabled: Boolean = true,
    onClick: () -> Unit,
) {
    Box(
        Modifier
            .heightIn(min = 38.dp)
            .background(
                if (selected) MsColors.accentSoft.copy(alpha = 0.16f) else MsColors.surfaceHigh,
                RoundedCornerShape(8.dp),
            )
            .border(
                1.dp,
                if (selected) MsColors.accentSoft else Color.Transparent,
                RoundedCornerShape(8.dp),
            )
            .clickable(enabled = enabled, onClick = onClick)
            .padding(horizontal = 14.dp, vertical = 9.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            label,
            color = when {
                !enabled -> MsColors.textMuted
                selected -> MsColors.accentSoft
                else -> MsColors.textSecondary
            },
            fontSize = 12.sp,
        )
    }
}

@Composable
private fun MaskSlider(label: String, value: Float, min: Float, max: Float, onChange: (Float) -> Unit) {
    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
        Text(
            label,
            color = MsColors.textSecondary,
            fontSize = 11.sp,
            modifier = Modifier.width(120.dp),
        )
        Slider(
            value = value.coerceIn(min, max),
            valueRange = min..max,
            onValueChange = onChange,
            modifier = Modifier.weight(1f),
        )
        Text(
            "%.2f".format(value),
            color = MsColors.textTertiary,
            fontSize = 11.sp,
            fontFamily = MsFonts.mono,
            modifier = Modifier.width(44.dp),
        )
    }
}
