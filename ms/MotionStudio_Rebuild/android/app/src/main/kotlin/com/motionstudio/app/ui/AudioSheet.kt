package com.motionstudio.app.ui

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
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
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
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.motionstudio.app.editor.AUDIO_INSERT_NAMES
import com.motionstudio.app.editor.AudioInsert
import com.motionstudio.app.editor.EditorEngine
import com.motionstudio.app.model.NativeClip
import com.motionstudio.app.model.NativeTrack
import com.motionstudio.app.ui.theme.MsColors
import com.motionstudio.app.ui.theme.MsFonts
import kotlin.math.log10

/** One knob an insert exposes, with the range the mixer offers. */
data class AudioKnob(
    val name: String,
    val label: String,
    val min: Float,
    val max: Float,
    val default: Float,
)

private fun k(name: String, label: String, min: Float, max: Float, default: Float) =
    AudioKnob(name, label, min, max, default)

/**
 * Mirrors ms::AudioFxParams, in the same order as ms::AudioFxKind. Every name
 * here is accepted by ms_audio_fx_set_param; anything else is rejected by the
 * bridge, so this table cannot drift into silently dead sliders.
 */
fun audioKnobs(kind: Int): List<AudioKnob> = when (kind) {
    0 -> listOf(k("gain_db", "Gain", -60f, 12f, 0f))
    1 -> listOf(
        k("low_gain_db", "Low", -18f, 18f, 0f),
        k("low_freq_hz", "Low Hz", 30f, 320f, 100f),
        k("mid_gain_db", "Mid", -18f, 18f, 0f),
        k("mid_freq_hz", "Mid Hz", 300f, 6000f, 1000f),
        k("mid_q", "Mid Q", 0.2f, 8f, 1f),
        k("high_gain_db", "High", -18f, 18f, 0f),
        k("high_freq_hz", "High Hz", 2000f, 16000f, 8000f),
        k("wet", "Mix", 0f, 1f, 1f),
    )
    2 -> listOf(
        k("threshold_db", "Threshold", -48f, 0f, -18f),
        k("ratio", "Ratio", 1f, 20f, 4f),
        k("attack_ms", "Attack", 0.1f, 100f, 10f),
        k("release_ms", "Release", 10f, 1000f, 100f),
        k("makeup_db", "Makeup", 0f, 24f, 0f),
        k("wet", "Mix", 0f, 1f, 1f),
    )
    3 -> listOf(
        k("delay_ms", "Delay", 10f, 1500f, 280f),
        k("feedback", "Feedback", 0f, 0.9f, 0.25f),
        k("wet", "Mix", 0f, 1f, 0.4f),
    )
    4 -> listOf(
        k("room_size", "Room", 0f, 1f, 0.72f),
        k("damping", "Damping", 0f, 0.95f, 0.35f),
        k("wet", "Mix", 0f, 1f, 0.4f),
    )
    5 -> listOf(
        k("ceiling_db", "Ceiling", -12f, 0f, -1f),
        k("wet", "Mix", 0f, 1f, 1f),
    )
    else -> emptyList()
}

private fun formatKnob(knob: AudioKnob, value: Float): String = when {
    knob.name.endsWith("_db") -> "%.1f dB".format(value)
    knob.name.endsWith("_hz") -> "%.0f Hz".format(value)
    knob.name.endsWith("_ms") -> "%.0f ms".format(value)
    else -> "%.2f".format(value)
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun AudioSheet(engine: EditorEngine, onDismiss: () -> Unit) {
    val sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)
    val revision = engine.audioRevision
    val inserts = remember(revision) { engine.audioInserts() }
    // Slider positions live here while dragging so the engine round-trip does
    // not pull the handle back under the finger.
    val drafts = remember(revision) { mutableStateMapOf<String, Float>() }
    val tracks = engine.doc.tracks

    ModalBottomSheet(
        onDismissRequest = onDismiss,
        sheetState = sheetState,
        containerColor = MsColors.surface,
    ) {
        Column(Modifier.fillMaxWidth().padding(horizontal = 20.dp)) {
            Text("Audio", color = MsColors.textPrimary, fontSize = 16.sp, fontWeight = FontWeight.SemiBold)
            Spacer(Modifier.height(2.dp))
            Text(
                "Master insert chain and track mixer · mixed in C++",
                color = MsColors.textTertiary,
                fontSize = 12.sp,
            )
            Spacer(Modifier.height(10.dp))

            LazyColumn(Modifier.fillMaxWidth().height(430.dp)) {
                item { SectionLabel("MASTER CHAIN") }
                if (inserts.isEmpty()) {
                    item {
                        Text(
                            "No inserts — the mix passes through untouched.",
                            color = MsColors.textMuted,
                            fontSize = 11.sp,
                            modifier = Modifier.padding(vertical = 6.dp),
                        )
                    }
                }
                items(inserts) { insert ->
                    InsertCard(
                        title = AUDIO_INSERT_NAMES.getOrElse(insert.kind) { "Insert" },
                        subtitle = "INSERT ${insert.index + 1}",
                        kind = insert.kind,
                        enabled = insert.enabled,
                        draftPrefix = "m",
                        drafts = drafts,
                        onEnabledChange = { engine.setAudioInsertEnabled(insert.index, it) },
                        onRemove = { engine.removeAudioInsert(insert.index) },
                        param = { name, fallback ->
                            engine.audioInsertParam(insert.index, name, fallback)
                        },
                        onParam = { name, value ->
                            engine.setAudioInsertParam(insert.index, name, value)
                        },
                    )
                }
                if (inserts.isNotEmpty()) {
                    item {
                        TextButton(onClick = { engine.clearAudioInserts() }) {
                            Text("Clear chain", color = MsColors.error, fontSize = 11.sp)
                        }
                    }
                }

                item { SectionLabel("ADD INSERT") }
                items(AUDIO_INSERT_NAMES.chunked(3)) { row ->
                    Row(
                        Modifier.fillMaxWidth().padding(vertical = 2.dp),
                        horizontalArrangement = Arrangement.spacedBy(6.dp),
                    ) {
                        row.forEach { name ->
                            val kind = AUDIO_INSERT_NAMES.indexOf(name)
                            Box(Modifier.weight(1f)) {
                                TextButton(
                                    onClick = { engine.addAudioInsert(kind) },
                                    modifier = Modifier.fillMaxWidth(),
                                ) {
                                    Text(name, color = MsColors.accentSoft, fontSize = 11.sp, maxLines = 1)
                                }
                            }
                        }
                        // Keep the last row aligned with the grid above it.
                        repeat(3 - row.size) { Spacer(Modifier.weight(1f)) }
                    }
                }

                item { SectionLabel("TRACK MIXER") }
                if (tracks.isEmpty()) {
                    item {
                        Text(
                            "No tracks yet.",
                            color = MsColors.textMuted,
                            fontSize = 11.sp,
                            modifier = Modifier.padding(vertical = 6.dp),
                        )
                    }
                }
                items(tracks) { track -> TrackStrip(engine, track, revision, drafts) }

                item { SectionLabel("CLIP AUDIO") }
                val clips = tracks.flatMap { track ->
                    track.clips.filter { !it.isText }.map { track to it }
                }
                if (clips.isEmpty()) {
                    item {
                        Text(
                            "No clips to mix yet.",
                            color = MsColors.textMuted,
                            fontSize = 11.sp,
                            modifier = Modifier.padding(vertical = 6.dp),
                        )
                    }
                }
                items(clips) { (track, clip) ->
                    ClipAudioRow(engine, track, clip, revision, drafts)
                }
            }
            Spacer(Modifier.height(20.dp))
        }
    }
}

@Composable
private fun SectionLabel(text: String) {
    Text(
        text,
        color = MsColors.textMuted,
        fontSize = 9.sp,
        fontFamily = MsFonts.mono,
        modifier = Modifier.padding(top = 12.dp, bottom = 4.dp),
    )
}

@Composable
private fun InsertCard(
    title: String,
    subtitle: String,
    kind: Int,
    enabled: Boolean,
    draftPrefix: String,
    drafts: MutableMap<String, Float>,
    onEnabledChange: (Boolean) -> Unit,
    onRemove: () -> Unit,
    param: (String, Float) -> Float,
    onParam: (String, Float) -> Unit,
) {
    Column(Modifier.fillMaxWidth().padding(vertical = 4.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text(title, color = MsColors.textPrimary, fontSize = 14.sp, fontWeight = FontWeight.Medium)
                Text(
                    subtitle + if (enabled) "" else " · BYPASSED",
                    color = if (enabled) MsColors.textMuted else MsColors.warning,
                    fontSize = 9.sp,
                    fontFamily = MsFonts.mono,
                )
            }
            Switch(checked = enabled, onCheckedChange = onEnabledChange)
            TextButton(onClick = onRemove) {
                Text("Remove", color = MsColors.error, fontSize = 11.sp)
            }
        }
        audioKnobs(kind).forEach { knob ->
            val key = "$draftPrefix:${knob.name}"
            val current = drafts[key] ?: param(knob.name, knob.default)
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(
                    knob.label,
                    color = MsColors.textSecondary,
                    fontSize = 11.sp,
                    fontFamily = MsFonts.mono,
                    modifier = Modifier.fillMaxWidth(0.30f),
                )
                Slider(
                    value = current.coerceIn(knob.min, knob.max),
                    onValueChange = {
                        drafts[key] = it
                        onParam(knob.name, it)
                    },
                    valueRange = knob.min..knob.max,
                    enabled = enabled,
                    modifier = Modifier.weight(1f),
                )
                Text(
                    formatKnob(knob, current),
                    color = MsColors.textTertiary,
                    fontSize = 9.sp,
                    fontFamily = MsFonts.mono,
                    modifier = Modifier.padding(start = 6.dp),
                )
            }
        }
    }
}

@Composable
private fun TrackStrip(
    engine: EditorEngine,
    track: NativeTrack,
    revision: Int,
    drafts: MutableMap<String, Float>,
) {
    val gain = track.gain.toFloat().coerceIn(0f, 2f)
    var chainOpen by remember(track.id) { mutableStateOf(false) }
    val chain = remember(revision, track.id) { engine.trackAudioInserts(track.id) }
    Column(Modifier.fillMaxWidth().padding(vertical = 4.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            Column(Modifier.weight(1f)) {
                Text(track.name, color = MsColors.textPrimary, fontSize = 13.sp)
                Text(
                    (if (track.type == 1) "AUDIO" else "VIDEO") + " · ${track.clips.size} CLIPS",
                    color = MsColors.textMuted,
                    fontSize = 9.sp,
                    fontFamily = MsFonts.mono,
                )
            }
            MiniChip("MUTE", track.muted) { engine.toggleTrackMuted(track.id) }
            MiniChip("SOLO", track.solo) { engine.toggleTrackSolo(track.id) }
            MiniChip(if (chain.isEmpty()) "CHAIN" else "CHAIN ${chain.size}", chainOpen) {
                chainOpen = !chainOpen
            }
        }
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(
                "FADER",
                color = MsColors.textSecondary,
                fontSize = 10.sp,
                fontFamily = MsFonts.mono,
                modifier = Modifier.fillMaxWidth(0.30f),
            )
            Slider(
                value = gain,
                onValueChange = { engine.setTrackGain(track.id, it) },
                valueRange = 0f..2f,
                modifier = Modifier.weight(1f),
            )
            Text(
                if (gain <= 0.0001f) "-inf" else "%.1f dB".format(20.0 * log10(gain.toDouble())),
                color = MsColors.textTertiary,
                fontSize = 9.sp,
                fontFamily = MsFonts.mono,
                modifier = Modifier.padding(start = 6.dp),
            )
        }
        if (chainOpen) {
            // Inserts here run over this track's clips before the master bus.
            chain.forEach { insert ->
                InsertCard(
                    title = AUDIO_INSERT_NAMES.getOrElse(insert.kind) { "Insert" },
                    subtitle = "${track.name.uppercase()} · INSERT ${insert.index + 1}",
                    kind = insert.kind,
                    enabled = insert.enabled,
                    draftPrefix = "t${track.id}",
                    drafts = drafts,
                    onEnabledChange = {
                        engine.setTrackAudioInsertEnabled(track.id, insert.index, it)
                    },
                    onRemove = { engine.removeTrackAudioInsert(track.id, insert.index) },
                    param = { name, fallback ->
                        engine.trackAudioInsertParam(track.id, insert.index, name, fallback)
                    },
                    onParam = { name, value ->
                        engine.setTrackAudioInsertParam(track.id, insert.index, name, value)
                    },
                )
            }
            AUDIO_INSERT_NAMES.chunked(3).forEach { row ->
                Row(
                    Modifier.fillMaxWidth().padding(vertical = 2.dp),
                    horizontalArrangement = Arrangement.spacedBy(6.dp),
                ) {
                    row.forEach { name ->
                        Box(Modifier.weight(1f)) {
                            TextButton(
                                onClick = {
                                    engine.addTrackAudioInsert(
                                        track.id,
                                        AUDIO_INSERT_NAMES.indexOf(name),
                                    )
                                },
                                modifier = Modifier.fillMaxWidth(),
                            ) {
                                Text(
                                    "+ $name",
                                    color = MsColors.accentSoft,
                                    fontSize = 10.sp,
                                    maxLines = 1,
                                )
                            }
                        }
                    }
                    repeat(3 - row.size) { Spacer(Modifier.weight(1f)) }
                }
            }
            if (chain.isNotEmpty()) {
                TextButton(onClick = { engine.clearTrackAudioInserts(track.id) }) {
                    Text("Clear ${track.name} chain", color = MsColors.error, fontSize = 11.sp)
                }
            }
        }
    }
}

@Composable
private fun ClipAudioRow(
    engine: EditorEngine,
    track: NativeTrack,
    clip: NativeClip,
    revision: Int,
    drafts: MutableMap<String, Float>,
) {
    val audio = remember(revision, clip.id) { engine.clipAudio(track.id, clip.id) } ?: return
    val gainKey = "c${clip.id}:gain"
    val panKey = "c${clip.id}:pan"
    val gain = drafts[gainKey] ?: audio.first
    val pan = drafts[panKey] ?: audio.second
    val gainKeys = remember(revision, clip.id) {
        engine.clipAudioKeyframeCount(track.id, clip.id, 0)
    }
    val panKeys = remember(revision, clip.id) {
        engine.clipAudioKeyframeCount(track.id, clip.id, 1)
    }
    Column(Modifier.fillMaxWidth().padding(vertical = 4.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text(clip.name, color = MsColors.textPrimary, fontSize = 13.sp)
                Text(
                    "${track.name.uppercase()} · ${clip.start.seconds()}–${clip.end.seconds()}",
                    color = MsColors.textMuted,
                    fontSize = 9.sp,
                    fontFamily = MsFonts.mono,
                )
            }
            MiniChip("◆ GAIN", gainKeys > 0) {
                engine.addClipAudioKeyframe(track.id, clip.id, 0, gain.toDouble())
            }
            MiniChip("◆ PAN", panKeys > 0) {
                engine.addClipAudioKeyframe(track.id, clip.id, 1, pan.toDouble())
            }
            if (gainKeys > 0) {
                MiniChip("G $gainKeys ✕", false) {
                    engine.clearClipAudioKeyframes(track.id, clip.id, 0)
                }
            }
            if (panKeys > 0) {
                MiniChip("P $panKeys ✕", false) {
                    engine.clearClipAudioKeyframes(track.id, clip.id, 1)
                }
            }
        }
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(
                "GAIN",
                color = MsColors.textSecondary,
                fontSize = 10.sp,
                fontFamily = MsFonts.mono,
                modifier = Modifier.fillMaxWidth(0.30f),
            )
            Slider(
                value = gain.coerceIn(0f, 2f),
                onValueChange = {
                    drafts[gainKey] = it
                    engine.setClipAudio(track.id, clip.id, it, pan)
                },
                valueRange = 0f..2f,
                modifier = Modifier.weight(1f),
            )
            Text(
                if (gain <= 0.0001f) "-inf" else "%.1f dB".format(20.0 * log10(gain.toDouble())),
                color = MsColors.textTertiary,
                fontSize = 9.sp,
                fontFamily = MsFonts.mono,
                modifier = Modifier.padding(start = 6.dp),
            )
        }
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(
                "PAN",
                color = MsColors.textSecondary,
                fontSize = 10.sp,
                fontFamily = MsFonts.mono,
                modifier = Modifier.fillMaxWidth(0.30f),
            )
            Slider(
                value = pan.coerceIn(-1f, 1f),
                onValueChange = {
                    drafts[panKey] = it
                    engine.setClipAudio(track.id, clip.id, gain, it)
                },
                valueRange = -1f..1f,
                modifier = Modifier.weight(1f),
            )
            Text(
                when {
                    pan < -0.01f -> "L %.0f".format(-pan * 100)
                    pan > 0.01f -> "R %.0f".format(pan * 100)
                    else -> "C"
                },
                color = MsColors.textTertiary,
                fontSize = 9.sp,
                fontFamily = MsFonts.mono,
                modifier = Modifier.padding(start = 6.dp),
            )
        }
    }
}

/** MM:SS.mmm, so two clips on the same second are still distinguishable. */
private fun Double.seconds(): String {
    val total = if (this < 0) 0.0 else this
    val minutes = (total / 60).toInt()
    val secs = total - minutes * 60
    return "%d:%06.3f".format(minutes, secs)
}

@Composable
private fun MiniChip(label: String, active: Boolean, onClick: () -> Unit) {
    Box(
        Modifier
            .border(1.dp, if (active) MsColors.accent else MsColors.border, RoundedCornerShape(2.dp))
            .background(if (active) MsColors.accentDim else Color.Transparent)
            .clickable(onClick = onClick)
            .padding(horizontal = 8.dp, vertical = 4.dp)
    ) {
        Text(
            label,
            color = if (active) MsColors.accentSoft else MsColors.textSecondary,
            fontSize = 9.sp,
            fontFamily = MsFonts.mono,
        )
    }
}
