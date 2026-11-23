package com.motionstudio.app.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.motionstudio.app.editor.EditorEngine
import com.motionstudio.app.ui.theme.MsColors
import com.motionstudio.app.ui.theme.MsFonts

private data class AnimProp(
    val id: Int,
    val label: String,
    val min: Float,
    val max: Float,
    val default: Float,
    val note: String,
)

/** Mirrors ms::TransitionType indices in native/cpp/include/ms_transitions.hpp. */
/**
 * Every implemented transition (comp/transitions.cpp). Indices match
 * ms::TransitionType exactly; None (0) is not offered because it does nothing.
 */
private val TransitionTypes = listOf(
    "Dissolve" to 1,
    "Fade Black" to 2,
    "Fade White" to 3,
    "Fade Colour" to 4,
    "Wipe L" to 5,
    "Wipe R" to 6,
    "Wipe Up" to 7,
    "Wipe Down" to 8,
    "Wipe Diag" to 9,
    "Wipe Diag R" to 10,
    "Circle" to 11,
    "Barn Door H" to 12,
    "Barn Door V" to 13,
    "Slide L" to 14,
    "Slide R" to 15,
    "Slide Up" to 16,
    "Slide Down" to 17,
    "Iris" to 18,
    "Zoom" to 19,
    "Dip to Colour" to 20,
    "Blinds H" to 21,
    "Blinds V" to 22,
    "Checker" to 23,
)

private val Props = listOf(
    AnimProp(0, "Opacity", 0f, 1f, 1f, "fade in / out"),
    AnimProp(1, "Scale", 0.1f, 4f, 1f, "push in / out"),
    AnimProp(2, "Position X", -2000f, 2000f, 0f, "pixels"),
    AnimProp(3, "Position Y", -2000f, 2000f, 0f, "pixels"),
)

/** Keyframe authoring for the selected clip: value + "Key" at the playhead. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun MotionSheet(engine: EditorEngine, onDismiss: () -> Unit) {
    val sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)
    val clip = engine.doc.clipById(engine.selectedTrackId, engine.selectedClipId)

    ModalBottomSheet(
        onDismissRequest = onDismiss,
        sheetState = sheetState,
        containerColor = MsColors.surface,
    ) {
        Column(
            Modifier
                .fillMaxWidth()
                .padding(horizontal = 20.dp)
                .verticalScroll(rememberScrollState()),
        ) {
            Text("Motion", color = MsColors.textPrimary, fontSize = 16.sp, fontWeight = FontWeight.SemiBold)
            Text(
                if (clip != null) "“${clip.name.ifBlank { "Clip" }}” · keyframes are clip-local"
                else "Select a clip to animate it",
                color = MsColors.textTertiary,
                fontSize = 12.sp,
            )
            Spacer(Modifier.height(10.dp))

            Props.forEach { prop ->
                var value by remember(prop.id) { mutableFloatStateOf(prop.default) }
                val count = engine.keyframeCount(prop.id)
                Column(Modifier.fillMaxWidth().padding(vertical = 6.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Column(Modifier.weight(1f)) {
                            Text(
                                prop.label,
                                color = MsColors.textPrimary,
                                fontSize = 14.sp,
                                fontWeight = FontWeight.Medium,
                            )
                            Text(
                                "$count keyframe${if (count == 1) "" else "s"} · ${prop.note}",
                                color = MsColors.textMuted,
                                fontSize = 11.sp,
                            )
                        }
                        TextButton(
                            enabled = clip != null,
                            onClick = { engine.addKeyframe(prop.id, value) },
                        ) {
                            Text(
                                "Key",
                                color = if (clip != null) MsColors.accentSoft else MsColors.textMuted,
                                fontSize = 12.sp,
                            )
                        }
                        TextButton(
                            enabled = count > 0,
                            onClick = { engine.clearKeyframes(prop.id) },
                        ) {
                            Text(
                                "Clear",
                                color = if (count > 0) MsColors.error else MsColors.textMuted,
                                fontSize = 12.sp,
                            )
                        }
                    }
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text(
                            "%.2f".format(value),
                            color = MsColors.textSecondary,
                            fontSize = 11.sp,
                            fontFamily = MsFonts.mono,
                            modifier = Modifier.fillMaxWidth(0.22f),
                        )
                        Slider(
                            value = value.coerceIn(prop.min, prop.max),
                            onValueChange = { value = it },
                            valueRange = prop.min..prop.max,
                            modifier = Modifier.fillMaxWidth(),
                        )
                    }
                }
            }
            Spacer(Modifier.height(8.dp))
            Text(
                "Move the playhead, set a value and tap Key. Two keys on the same " +
                    "property animate it; the compositor reads the curve per frame.",
                color = MsColors.textMuted,
                fontSize = 11.sp,
            )

            Spacer(Modifier.height(14.dp))
            val parentId = engine.clipParent
            Text("Parent", color = MsColors.textPrimary, fontSize = 14.sp, fontWeight = FontWeight.SemiBold)
            Text(
                when {
                    clip == null -> "Select a clip to parent it"
                    parentId == 0 -> "None · this clip's transform is its own"
                    else -> "Inherits translation and scale from clip $parentId"
                },
                color = MsColors.textMuted,
                fontSize = 11.sp,
            )
            Row(
                Modifier.horizontalScroll(rememberScrollState()),
                horizontalArrangement = Arrangement.spacedBy(2.dp),
            ) {
                TextButton(
                    enabled = clip != null,
                    onClick = { engine.setClipParent(0) },
                ) {
                    Text(
                        "None",
                        color = if (clip != null && parentId == 0) MsColors.accentSoft
                        else MsColors.textMuted,
                        fontSize = 11.sp,
                    )
                }
                engine.parentCandidates().forEach { candidate ->
                    val active = parentId == candidate.id
                    TextButton(onClick = { engine.setClipParent(candidate.id) }) {
                        Text(
                            candidate.name.ifBlank { "Clip ${candidate.id}" } +
                                if (candidate.isNull) " · null" else "",
                            color = if (active) MsColors.accentSoft else MsColors.textSecondary,
                            fontSize = 11.sp,
                        )
                    }
                }
            }
            Text(
                "Animate the parent once and every child follows. A null object is invisible: " +
                    "add one from Session, then parent the clips you want to move together.",
                color = MsColors.textMuted,
                fontSize = 11.sp,
            )

            Spacer(Modifier.height(14.dp))
            val retimeTarget = clip?.takeIf { !it.isAdjustment && !it.isText && !it.isNull }
            var speed by remember(retimeTarget?.id, retimeTarget?.speed) {
                mutableFloatStateOf(retimeTarget?.speed?.toFloat() ?: 1f)
            }
            Text("Retime", color = MsColors.textPrimary, fontSize = 14.sp, fontWeight = FontWeight.SemiBold)
            Text(
                if (retimeTarget == null) {
                    "Select a media clip to change its speed"
                } else {
                    "%.2f× · clip length %s s".format(
                        speed,
                        "%.2f".format(
                            if (speed > 0.01f) retimeTarget.duration * retimeTarget.speed / speed
                            else retimeTarget.duration,
                        ),
                    )
                },
                color = MsColors.textMuted,
                fontSize = 11.sp,
            )
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(
                    "%.2f×".format(speed),
                    color = MsColors.textSecondary,
                    fontSize = 11.sp,
                    fontFamily = MsFonts.mono,
                    modifier = Modifier.fillMaxWidth(0.22f),
                )
                Slider(
                    enabled = retimeTarget != null,
                    value = speed.coerceIn(0.1f, 4f),
                    onValueChange = { speed = it },
                    onValueChangeFinished = {
                        if (retimeTarget != null) engine.setClipSpeed(speed)
                    },
                    valueRange = 0.1f..4f,
                    modifier = Modifier.fillMaxWidth(),
                )
            }
            Row(horizontalArrangement = Arrangement.spacedBy(2.dp)) {
                listOf(0.25f, 0.5f, 1f, 2f, 4f).forEach { preset ->
                    TextButton(
                        enabled = retimeTarget != null,
                        onClick = {
                            speed = preset
                            engine.setClipSpeed(preset)
                        },
                    ) {
                        Text(
                            if (preset == 1f) "1× reset" else "%.2f×".format(preset),
                            color = if (retimeTarget != null) MsColors.accentSoft else MsColors.textMuted,
                            fontSize = 11.sp,
                        )
                    }
                }
            }
            Text(
                "The same slice of source plays faster or slower and the clip's length " +
                    "follows. Audio pitches with it — the mixer resamples by speed, it does " +
                    "not time-stretch.",
                color = MsColors.textMuted,
                fontSize = 11.sp,
            )

            Spacer(Modifier.height(14.dp))
            Text("Transition to next clip", color = MsColors.textPrimary, fontSize = 14.sp, fontWeight = FontWeight.SemiBold)
            Spacer(Modifier.height(2.dp))
            Text(
                if (engine.nextClip() != null) "Centred on the cut, blended by the C++ compositor"
                else "Select a clip that has another clip after it",
                color = MsColors.textMuted,
                fontSize = 11.sp,
            )
            Spacer(Modifier.height(6.dp))
            TransitionTypes.chunked(3).forEach { rowTypes ->
                Row {
                    rowTypes.forEach { (label, type) ->
                        TextButton(
                            enabled = engine.nextClip() != null,
                            onClick = { engine.addTransitionToNext(type) },
                        ) {
                            Text(
                                label,
                                color = if (engine.nextClip() != null) MsColors.accentSoft
                                else MsColors.textMuted,
                                fontSize = 11.sp,
                            )
                        }
                    }
                }
            }
            engine.transitionsOnSelectedTrack().forEach { tr ->
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        "#${tr.id} · type ${tr.type} · ${"%.2f".format(tr.duration)}s",
                        color = MsColors.textSecondary,
                        fontSize = 11.sp,
                        fontFamily = MsFonts.mono,
                        modifier = Modifier.weight(1f),
                    )
                    TextButton(onClick = { engine.removeTransition(tr.id) }) {
                        Text("Remove", color = MsColors.error, fontSize = 11.sp)
                    }
                }
            }
            Spacer(Modifier.height(20.dp))
        }
    }
}
