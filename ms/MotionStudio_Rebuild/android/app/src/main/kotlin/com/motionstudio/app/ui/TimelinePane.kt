package com.motionstudio.app.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.calculateCentroid
import androidx.compose.foundation.gestures.calculateZoom
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Visibility
import androidx.compose.material.icons.filled.VisibilityOff
import androidx.compose.material.icons.automirrored.filled.VolumeOff
import androidx.compose.material.icons.automirrored.filled.VolumeUp
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshotFlow
import androidx.compose.runtime.withFrameNanos
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.PointerEventPass
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.motionstudio.app.editor.EditorEngine
import com.motionstudio.app.model.NativeClip
import com.motionstudio.app.model.NativeTrack
import com.motionstudio.app.ui.theme.MsColors
import com.motionstudio.app.ui.theme.MsFonts
import java.util.Locale
import kotlin.math.abs
import kotlin.math.floor
import kotlin.math.max
import kotlin.math.roundToInt

private val HeaderWidth = 104.dp
private val RulerHeight = 34.dp
private val LaneHeight = 48.dp
private val ClipInset = 7.dp
private val GripWidth = 14.dp

/**
 * Timeline: ruler, track headers and lanes over one shared time axis.
 *
 * Two things here matter more than the decoration. First, scrubbing is layered
 * *under* the clips — Compose only hit-tests nodes that declare pointer input,
 * so an overlay drawn on top of the lanes swallows every clip gesture (the
 * previous version had exactly that bug and clips could not be touched at all).
 * Second, every drag is committed through the C++ engine on release, so undo and
 * the rendered frame stay authoritative and the preview cannot drift from the
 * picture the drag was drawn from.
 */
@Composable
fun TimelinePane(
    engine: EditorEngine,
    modifier: Modifier = Modifier,
    onImport: () -> Unit = {},
    onOpenEffects: () -> Unit = {},
    onOpenMotion: () -> Unit = {},
    onOpenMask: () -> Unit = {},
    onInspect: () -> Unit = {},
) {
    val doc = engine.doc
    // An empty project has no duration yet; keep a usable axis instead of dividing by zero.
    val span = max(doc.timelineEnd, 4.0)
    var zoom by remember { mutableFloatStateOf(1f) }
    var follow by remember { mutableStateOf(true) }
    var pendingScroll by remember { mutableStateOf<Int?>(null) }
    var snapping by remember { mutableStateOf(true) }
    var snapTime by remember { mutableStateOf<Double?>(null) }
    val hScroll = rememberScrollState()
    val vScroll = rememberScrollState()
    val density = LocalDensity.current

    BoxWithConstraints(modifier.background(MsColors.surface)) {
        val laneViewport: Dp = (maxWidth - HeaderWidth).coerceAtLeast(1.dp)
        val contentWidth: Dp = (laneViewport * zoom).coerceAtLeast(160.dp)
        val secondsPerDp = span / contentWidth.value.toDouble()

        fun timeAt(xPx: Float): Double =
            (xPx / with(density) { contentWidth.toPx() }).toDouble() * span

        fun xOf(seconds: Double): Dp = (seconds / span).toFloat().let { it * contentWidth.value }.dp

        val laneViewportPx = with(density) { laneViewport.toPx() }
        val contentWidthPx = contentWidth.value * density.density

        // ---- follow the playhead while playing -------------------------------
        // Page-style: once the playhead crosses the right margin, jump it back to
        // the left margin. Scrolling every frame would fight the user's own drags
        // for the scroll state.
        // One collector rather than a LaunchedEffect keyed on the playhead: a
        // time-keyed effect was cancelled and restarted once per rendered frame.
        LaunchedEffect(follow, zoom, span, laneViewportPx, contentWidthPx) {
            if (!follow) return@LaunchedEffect
            val margin = laneViewportPx * 0.12f
            snapshotFlow { engine.time to engine.playing }.collect { (t, isPlaying) ->
                if (!isPlaying || laneViewportPx <= 0f) return@collect
                val playX = (t / span).toFloat() * contentWidthPx
                when {
                    playX > hScroll.value + laneViewportPx - margin ->
                        hScroll.scrollTo((playX - laneViewportPx + margin * 2f).roundToInt())
                    playX < hScroll.value + margin * 0.4f ->
                        hScroll.scrollTo((playX - margin).roundToInt().coerceAtLeast(0))
                }
            }
        }

        // Pinch changes the content width, so the scroll offset can only be
        // corrected after the new width has been laid out.
        LaunchedEffect(pendingScroll, zoom) {
            val target = pendingScroll ?: return@LaunchedEffect
            withFrameNanos { }
            // scrollTo clamps to the real maximum, which may still be the pre-zoom value.
            hScroll.scrollTo(target.coerceAtLeast(0))
            pendingScroll = null
        }

        fun pinch(factor: Float, focusX: Float) {
            if (!factor.isFinite() || factor <= 0f) return
            val pxPerSecond = contentWidthPx / span
            if (pxPerSecond <= 0.0 || laneViewportPx <= 0f) return
            val timeAtFocus = (hScroll.value + focusX) / pxPerSecond
            val next = (zoom * factor).coerceIn(0.4f, 24f)
            if (abs(next - zoom) < 1e-4f) return
            zoom = next
            val newContentPx = laneViewportPx * next
            pendingScroll = ((timeAtFocus / span) * newContentPx - focusX).roundToInt().coerceAtLeast(0)
        }

        Column(Modifier.fillMaxSize()) {
            // ---- head-up strip -------------------------------------------------
            Row(
                Modifier.fillMaxWidth().height(38.dp).background(MsColors.surfaceHigh)
                    .padding(horizontal = 10.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(10.dp),
            ) {
                HeadUpClock(engine = engine, span = span)
                Spacer(Modifier.weight(1f))
                Chip(
                    label = if (snapping) "SNAP" else "FREE",
                    active = snapping,
                    onClick = { snapping = !snapping },
                )
                Chip(
                    label = "FOLLOW",
                    active = follow,
                    onClick = { follow = !follow },
                )
                Chip(label = "−", active = false) { zoom = (zoom / 1.5f).coerceAtLeast(0.4f) }
                Text(
                    "${(zoom * 100).roundToInt()}%",
                    color = MsColors.textTertiary,
                    fontSize = 9.sp,
                    fontFamily = MsFonts.mono,
                )
                Chip(label = "+", active = false) { zoom = (zoom * 1.5f).coerceAtMost(24f) }
                Chip(label = "FIT", active = false) { zoom = 1f }
            }

            // ---- ruler ---------------------------------------------------------
            Row(Modifier.fillMaxWidth()) {
                Box(Modifier.width(HeaderWidth).height(RulerHeight).background(MsColors.surface)) {
                    Text(
                        "TRACKS",
                        color = MsColors.textMuted,
                        fontSize = 8.sp,
                        fontFamily = MsFonts.mono,
                        modifier = Modifier.align(Alignment.Center),
                    )
                }
                Box(
                    Modifier.weight(1f).height(RulerHeight)
                        .pinchToZoom { factor, focusX -> pinch(factor, focusX) }
                        .horizontalScroll(hScroll)
                        .background(MsColors.surfaceHigh)
                ) {
                    Ruler(
                        engine = engine,
                        span = span,
                        contentWidth = contentWidth,
                        fps = engine.fps,
                        markers = doc.markers.map { it.time },
                        onSeek = { x -> engine.seek(timeAt(x)) },
                    )
                }
            }

            SelectionBar(
                engine = engine,
                onOpenEffects = onOpenEffects,
                onOpenMotion = onOpenMotion,
                onOpenMask = onOpenMask,
                onInspect = onInspect,
            )

            // ---- tracks --------------------------------------------------------
            Row(Modifier.fillMaxWidth().weight(1f)) {
                Column(
                    Modifier.width(HeaderWidth).fillMaxHeight().verticalScroll(vScroll)
                        .background(MsColors.background)
                ) {
                    doc.tracks.forEach { track ->
                        TrackHeader(
                            track = track,
                            selected = track.id == engine.selectedTrackId,
                            onMute = { engine.toggleTrackMuted(track.id) },
                            onVisible = { engine.toggleTrackVisible(track.id) },
                            onRemove = { engine.removeTrack(track.id) },
                        )
                    }
                    Row(
                        Modifier.fillMaxWidth().height(LaneHeight).padding(horizontal = 6.dp),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(6.dp),
                    ) {
                        Chip(label = "+V", active = false) { engine.addTrack(video = true) }
                        Chip(label = "+A", active = false) { engine.addTrack(video = false) }
                    }
                }

                Box(
                    Modifier.weight(1f).fillMaxHeight()
                        .pinchToZoom { factor, focusX -> pinch(factor, focusX) }
                        .horizontalScroll(hScroll)
                        .verticalScroll(vScroll)
                ) {
                    Box(
                        Modifier
                            .width(contentWidth)
                            .height(LaneHeight * doc.tracks.size + LaneHeight)
                    ) {
                        // Scrubbing sits *below* the clips: taps on a clip reach the
                        // clip, taps on empty lane space reach this layer.
                        Box(
                            Modifier.matchParentSize().pointerInput(span, contentWidth) {
                                detectTapGestures { offset -> engine.seek(timeAt(offset.x)) }
                            }
                        )
                        Box(
                            Modifier.matchParentSize().pointerInput(span, contentWidth) {
                                detectDragGestures { change, _ -> engine.seek(timeAt(change.position.x)) }
                            }
                        )

                        doc.tracks.forEachIndexed { index, track ->
                            Lane(
                                engine = engine,
                                track = track,
                                top = LaneHeight * index,
                                span = span,
                                contentWidth = contentWidth,
                                gridStep = gridStep(secondsPerDp),
                                secondsPerDp = secondsPerDp,
                                snapping = snapping,
                                even = index % 2 == 0,
                                onSnapChange = { snapTime = it },
                                onInspect = onInspect,
                            )
                        }

                        if (doc.tracks.isEmpty() || doc.tracks.all { it.clips.isEmpty() }) {
                            Column(Modifier.padding(start = 14.dp, top = 12.dp)) {
                                Text(
                                    "TIMELINE IS EMPTY",
                                    color = MsColors.textTertiary,
                                    fontSize = 9.sp,
                                    fontFamily = MsFonts.mono,
                                    letterSpacing = 1.sp,
                                )
                                Spacer(Modifier.height(4.dp))
                                Text(
                                    "Import a clip \u2014 it lands at the playhead, then drag it along the lane.",
                                    color = MsColors.textMuted,
                                    fontSize = 11.sp,
                                )
                                Spacer(Modifier.height(8.dp))
                                Chip("IMPORT MEDIA", true) { onImport() }
                            }
                        }

                        snapTime?.let { snapped ->
                            Box(
                                Modifier.offset(x = xOf(snapped))
                                    .width(1.dp)
                                    .fillMaxHeight()
                                    .background(MsColors.warning)
                            )
                        }

                        // Playhead last so it sits above clips and the snap guide.
                        PlayheadLine(engine = engine, span = span, contentWidth = contentWidth)
                    }
                }
            }
        }
    }
}

@Composable
private fun Ruler(
    engine: EditorEngine,
    span: Double,
    contentWidth: Dp,
    fps: Double,
    markers: List<Double>,
    onSeek: (Float) -> Unit,
) {
    val step = rulerStep(span, contentWidth)
    val minorStep = step / 5.0
    val density = LocalDensity.current.density
    val minorPx = (minorStep / span).toFloat() * contentWidth.value * density

    Box(
        Modifier.width(contentWidth).height(RulerHeight)
            .pointerInput(span, contentWidth) {
                detectTapGestures { offset -> onSeek(offset.x) }
            }
            .pointerInput(span, contentWidth) {
                detectDragGestures { change, _ -> onSeek(change.position.x) }
            },
    ) {
        // Ticks go into one canvas: a few hundred offset Boxes are slower to lay
        // out and land on half pixels, which reads as a fuzzy axis.
        Canvas(Modifier.fillMaxSize()) {
            val h = size.height
            if (minorPx >= 5f) {
                var j = 0
                while (j * minorStep <= span) {
                    if (j % 5 != 0) {
                        val x = ((j * minorStep) / span).toFloat() * size.width
                        drawLine(MsColors.border, Offset(x, h), Offset(x, h * 0.78f), 1f)
                    }
                    j++
                }
            }
            var i = 0
            while (i * step <= span) {
                val x = ((i * step) / span).toFloat() * size.width
                drawLine(MsColors.guideStrong, Offset(x, h), Offset(x, h * 0.45f), 1f)
                i++
            }
            drawLine(MsColors.border, Offset(0f, h - 0.5f), Offset(size.width, h - 0.5f), 1f)
        }

        // Labels are stepped by an integer index, so no float drift can repeat or
        // drop a label the way the old accumulating loop did.
        var i = 0
        while (i * step <= span) {
            val t = i * step
            Text(
                rulerLabel(t, step),
                color = MsColors.textTertiary,
                fontSize = 9.sp,
                fontFamily = MsFonts.mono,
                maxLines = 1,
                modifier = Modifier.offset(
                    x = ((t / span).toFloat() * contentWidth.value + 3f).dp,
                    y = 3.dp,
                ),
            )
            i++
        }

        markers.forEach { marker ->
            Box(
                Modifier.offset(x = xDp(marker, span, contentWidth) - 3.dp, y = 16.dp)
                    .size(6.dp)
                    .background(MsColors.info, CircleShape)
            )
        }

        // The moving parts live in their own composable so the ruler's ticks and
        // labels are not recomposed for every frame of playback.
        RulerPlayhead(engine = engine, span = span, contentWidth = contentWidth, fps = fps)
    }
}

/**
 * Playhead tab carrying its own timecode, so the current position is readable
 * where the eye already is instead of only in a corner readout.
 */
@Composable
private fun RulerPlayhead(engine: EditorEngine, span: Double, contentWidth: Dp, fps: Double) {
    val playX = xDp(engine.time, span, contentWidth)
    Box(
        Modifier.offset(x = (playX - 28.dp).coerceAtLeast(0.dp), y = 1.dp)
            .width(56.dp).height(13.dp)
            .background(MsColors.accent, RoundedCornerShape(3.dp)),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            EditorEngine.formatTimecode(engine.time, fps),
            color = MsColors.background,
            fontSize = 8.sp,
            fontFamily = MsFonts.mono,
            fontWeight = FontWeight.Bold,
            maxLines = 1,
        )
    }
    Box(
        Modifier.offset(x = playX - 0.5.dp).width(1.dp).fillMaxHeight()
            .background(MsColors.accent)
    )
}

/**
 * The lane playhead. Its position is read inside the placement lambda, so a
 * moving playhead re-places one node instead of recomposing the whole timeline.
 */
@Composable
private fun PlayheadLine(engine: EditorEngine, span: Double, contentWidth: Dp) {
    val widthPx = with(LocalDensity.current) { contentWidth.toPx() }
    Box(
        Modifier
            .offset { IntOffset(((engine.time / span).toFloat() * widthPx).roundToInt(), 0) }
            .width(1.dp)
            .fillMaxHeight()
            .background(MsColors.accent)
    )
}

/** Head-up readout of the playhead, split out so the timeline body stays still. */
@Composable
private fun HeadUpClock(engine: EditorEngine, span: Double) {
    Text(
        EditorEngine.formatTimecode(engine.time, engine.fps),
        color = MsColors.accent,
        fontSize = 12.sp,
        fontFamily = MsFonts.mono,
        fontWeight = FontWeight.Bold,
    )
    Text(
        "/ ${EditorEngine.formatTimecode(span, engine.fps)}",
        color = MsColors.textMuted,
        fontSize = 10.sp,
        fontFamily = MsFonts.mono,
    )
    Text(
        "F${(engine.time * engine.fps).roundToInt()}",
        color = MsColors.textMuted,
        fontSize = 9.sp,
        fontFamily = MsFonts.mono,
    )
}

/**
 * What is selected, and what can be done to it. Tapping a clip used to change
 * colour and nothing else, which on a phone reads as "nothing happened"; this
 * strip is the answer to "where do I edit this".
 */
@Composable
private fun SelectionBar(
    engine: EditorEngine,
    onOpenEffects: () -> Unit,
    onOpenMotion: () -> Unit,
    onOpenMask: () -> Unit,
    onInspect: () -> Unit,
) {
    val track = engine.doc.tracks.firstOrNull { it.id == engine.selectedTrackId }
    val clip = engine.doc.clipById(engine.selectedTrackId, engine.selectedClipId)
    Column {
        Box(Modifier.fillMaxWidth().height(1.dp).background(MsColors.border))
        Row(
            Modifier.fillMaxWidth().height(46.dp).background(MsColors.surfaceHigh)
                .horizontalScroll(rememberScrollState()).padding(horizontal = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            if (clip == null) {
                Text(
                    "TAP A CLIP TO SELECT IT",
                    color = MsColors.textMuted,
                    fontSize = 9.sp,
                    fontFamily = MsFonts.mono,
                    letterSpacing = 1.sp,
                )
            } else {
                Column(Modifier.padding(end = 2.dp)) {
                    Text(
                        clip.name.ifBlank { if (clip.isText) "TITLE" else "Clip" },
                        color = MsColors.accentSoft,
                        fontSize = 11.sp,
                        fontWeight = FontWeight.SemiBold,
                        maxLines = 1,
                    )
                    Text(
                        "%s \u2192 %s  %s".format(
                            EditorEngine.formatTimecode(clip.start, engine.fps),
                            EditorEngine.formatTimecode(clip.end, engine.fps),
                            track?.name?.uppercase() ?: "",
                        ),
                        color = MsColors.textMuted,
                        fontSize = 8.sp,
                        fontFamily = MsFonts.mono,
                        maxLines = 1,
                    )
                }
                Chip("SPLIT", false) { engine.splitAtPlayhead() }
                Chip("FX ${clip.effects.size}", clip.effects.isNotEmpty()) { onOpenEffects() }
                Chip("MOTION", false) { onOpenMotion() }
                Chip("MASK", false) { onOpenMask() }
                Chip("INSPECT", false) { onInspect() }
                Chip("DEL", false) { engine.removeSelectedClip() }
            }
        }
    }
}

@Composable
private fun TrackHeader(
    track: NativeTrack,
    selected: Boolean,
    onMute: () -> Unit,
    onVisible: () -> Unit,
    onRemove: () -> Unit,
) {
    val audio = track.type == 1
    Row(
        Modifier
            .fillMaxWidth()
            .height(LaneHeight)
            .background(if (selected) MsColors.surfaceHighest else Color.Transparent)
            .border(0.5.dp, MsColors.borderSoft),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(
            Modifier.width(3.dp).fillMaxHeight().padding(vertical = 12.dp)
                .clip(RoundedCornerShape(2.dp))
                .background(if (track.muted) MsColors.textMuted else trackColor(track, audio))
        )
        Column(Modifier.weight(1f).padding(start = 7.dp)) {
            Text(
                track.name,
                color = if (track.muted) MsColors.textMuted else MsColors.textPrimary,
                fontSize = 11.sp,
                maxLines = 1,
            )
            Text(
                when {
                    track.muted -> "MUTED"
                    audio -> "${track.clips.size} AUD"
                    else -> "${track.clips.size} VID"
                },
                color = if (track.muted) MsColors.warning else MsColors.textMuted,
                fontSize = 8.sp,
                fontFamily = MsFonts.mono,
            )
        }
        // The eye is always there, because "which layer can I see" is the first
        // question on a stacked timeline. Mute and remove only appear on the
        // selected track, so an unselected row stays quiet and leaves the name
        // the width it needs.
        Row(
            Modifier.padding(end = 2.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Box(
                Modifier.size(26.dp).clickable { onVisible() },
                contentAlignment = Alignment.Center,
            ) {
                Icon(
                    if (track.visible) Icons.Filled.Visibility else Icons.Filled.VisibilityOff,
                    if (track.visible) "Hide ${track.name}" else "Show ${track.name}",
                    tint = if (track.visible) MsColors.textSecondary else MsColors.textMuted,
                    modifier = Modifier.size(16.dp),
                )
            }
            if (selected) {
                Box(
                    Modifier.size(24.dp).clickable { onMute() },
                    contentAlignment = Alignment.Center,
                ) {
                    Icon(
                        if (track.muted) Icons.AutoMirrored.Filled.VolumeOff
                        else Icons.AutoMirrored.Filled.VolumeUp,
                        if (track.muted) "Unmute ${track.name}" else "Mute ${track.name}",
                        tint = if (track.muted) MsColors.warning else MsColors.textSecondary,
                        modifier = Modifier.size(15.dp),
                    )
                }
                Box(
                    Modifier.size(20.dp).clickable { onRemove() },
                    contentAlignment = Alignment.Center,
                ) {
                    Text("×", color = MsColors.textMuted, fontSize = 14.sp)
                }
            }
        }
    }
}

@Composable
private fun Lane(
    engine: EditorEngine,
    track: NativeTrack,
    top: Dp,
    span: Double,
    contentWidth: Dp,
    gridStep: Double,
    secondsPerDp: Double,
    snapping: Boolean,
    even: Boolean,
    onSnapChange: (Double?) -> Unit,
    onInspect: () -> Unit,
) {
    Box(
        Modifier
            .offset(y = top)
            .width(contentWidth)
            .height(LaneHeight)
            .background(
                when {
                    !track.visible -> MsColors.viewerBg
                    even -> MsColors.surface
                    else -> MsColors.background
                }
            )
    ) {
        // Grid lines: time orientation across the lane, at the ruler's spacing.
        Canvas(Modifier.fillMaxSize()) {
            var t = 0.0
            while (t <= span + 1e-6) {
                val x = (t / span).toFloat() * size.width
                drawLine(MsColors.guide.copy(alpha = 0.35f), Offset(x, 0f), Offset(x, size.height), 1f)
                t += gridStep
            }
        }

        // Transitions sit on the cut between two clips.
        track.transitions.forEach { transition ->
            val outgoing = track.clips.firstOrNull { it.id == transition.outgoing } ?: return@forEach
            val x = ((outgoing.end / span).toFloat() * contentWidth.value).dp
            val w = ((transition.duration / span).toFloat() * contentWidth.value).dp
            Box(
                Modifier.offset(x = x - w / 2, y = 2.dp).width(w.coerceAtLeast(4.dp)).height(4.dp)
                    .background(MsColors.accentDim, RoundedCornerShape(2.dp))
            )
        }

        track.clips.forEach { clip ->
            ClipBox(
                engine = engine,
                track = track,
                clip = clip,
                span = span,
                contentWidth = contentWidth,
                secondsPerDp = secondsPerDp,
                selected = clip.id == engine.selectedClipId,
                snapping = snapping,
                onSnapChange = onSnapChange,
                onInspect = onInspect,
            )
        }
    }
}

@Composable
private fun ClipBox(
    engine: EditorEngine,
    track: NativeTrack,
    clip: NativeClip,
    span: Double,
    contentWidth: Dp,
    secondsPerDp: Double,
    selected: Boolean,
    snapping: Boolean,
    onSnapChange: (Double?) -> Unit,
    onInspect: () -> Unit,
) {
    val audio = track.type == 1
    var start by remember(clip.id, clip.start) { mutableFloatStateOf(clip.start.toFloat()) }
    var length by remember(clip.id, clip.duration) { mutableFloatStateOf(clip.duration.toFloat()) }
    var dragging by remember(clip.id) { mutableStateOf(false) }

    val leftDp = (start / span).toFloat() * contentWidth.value
    val widthDp = ((length / span).toFloat() * contentWidth.value).coerceAtLeast(2f)
    val wideEnoughForGrips = widthDp > GripWidth.value * 2.4f

    Box(
        Modifier
            .offset(x = leftDp.dp, y = ClipInset)
            .width(widthDp.dp)
            .height(LaneHeight - ClipInset * 2)
            .clip(RoundedCornerShape(3.dp))
            .background(
                trackColor(track, audio).copy(
                    alpha = when {
                        !track.visible -> 0.14f
                        selected -> 0.58f
                        dragging -> 0.5f
                        else -> 0.32f
                    }
                )
            )
            .border(
                width = if (selected) 2.dp else if (dragging) 1.5.dp else 0.5.dp,
                color = when {
                    selected -> MsColors.accent
                    dragging -> MsColors.accentSoft
                    else -> trackColor(track, audio).copy(alpha = 0.7f)
                },
                shape = RoundedCornerShape(3.dp),
            )
            .pointerInput(clip.id) {
                detectTapGestures(
                    onTap = { engine.select(track.id, clip.id) },
                    // Double tap opens the inspector: the shortest path from
                    // "that clip" to "edit this clip" on a phone.
                    onDoubleTap = {
                        engine.select(track.id, clip.id)
                        onInspect()
                    },
                )
            },
    ) {
        // Selected clips get a solid cap line so the selection reads across the
        // whole timeline at a glance, not only from the tint behind the name.
        if (selected) {
            Box(
                Modifier.align(Alignment.TopCenter).fillMaxWidth().height(3.dp)
                    .background(MsColors.accent)
            )
        }
        Row(Modifier.fillMaxSize(), verticalAlignment = Alignment.CenterVertically) {
            if (wideEnoughForGrips) {
                TrimGrip(
                    selected = selected,
                    onStart = { dragging = true; engine.select(track.id, clip.id) },
                    onEnd = {
                        dragging = false
                        engine.trimClip(track.id, clip.id, start.toDouble(), length.toDouble())
                    },
                    onDrag = { dx ->
                        val end = start + length
                        val newStart = (start + (dx * secondsPerDp).toFloat()).coerceIn(0f, end - 0.1f)
                        start = newStart
                        length = (end - newStart).coerceAtLeast(0.1f)
                    },
                )
            }

            // The body owns moving; the grips own trimming. Separate nodes mean a
            // single gesture can never do both.
            Box(
                Modifier
                    .weight(1f)
                    .fillMaxHeight()
                    .pointerInput(clip.id, span, snapping) {
                        detectDragGestures(
                            onDragStart = { dragging = true; engine.select(track.id, clip.id) },
                            onDragEnd = {
                                dragging = false
                                onSnapChange(null)
                                engine.moveClip(
                                    track.id, clip.id,
                                    snapTo(snapTargets(engine, clip), start.toDouble(), snapping, onSnapChange),
                                )
                            },
                            onDragCancel = { dragging = false; onSnapChange(null) },
                        ) { change, drag ->
                            change.consume()
                            val maxStart = max(0f, span.toFloat() - length)
                            start = (start + (drag.x * secondsPerDp).toFloat()).coerceIn(0f, maxStart)
                            if (snapping) {
                                start = snapTo(snapTargets(engine, clip), start.toDouble(), true, onSnapChange)
                                    .toFloat().coerceIn(0f, maxStart)
                            }
                        }
                    },
            ) {
                if (audio) {
                    val peaks = remember(track.id, clip.id, clip.mediaId, clip.duration) {
                        engine.peaks(track.id, clip.id, 96)
                    }
                    if (peaks.isNotEmpty()) {
                        Canvas(Modifier.matchParentSize().padding(horizontal = 1.dp, vertical = 3.dp)) {
                            val mid = size.height / 2f
                            val bar = size.width / peaks.size
                            peaks.forEachIndexed { index, value ->
                                val amp = value.coerceIn(0f, 1f) * mid * 0.92f
                                if (amp > 0.2f) {
                                    drawRect(
                                        color = MsColors.audioTrack.copy(alpha = 0.55f),
                                        topLeft = Offset(index * bar, mid - amp),
                                        size = Size((bar * 0.72f).coerceAtLeast(0.6f), amp * 2f),
                                    )
                                }
                            }
                        }
                    }
                }
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        clip.name.ifBlank { if (clip.isText) "TITLE" else "Clip" },
                        color = MsColors.textPrimary,
                        fontSize = 11.sp,
                        maxLines = 1,
                        modifier = Modifier.padding(start = 5.dp, end = 3.dp),
                    )
                    if (clip.effects.isNotEmpty()) {
                        Text(
                            "FX ${clip.effects.count { it.enabled }}",
                            color = MsColors.accentSoft,
                            fontSize = 8.sp,
                            fontFamily = MsFonts.mono,
                        )
                    }
                    if (clip.mute) {
                        Text(" M", color = MsColors.warning, fontSize = 8.sp, fontFamily = MsFonts.mono)
                    }
                }
            }

            if (wideEnoughForGrips) {
                TrimGrip(
                    selected = selected,
                    onStart = { dragging = true; engine.select(track.id, clip.id) },
                    onEnd = {
                        dragging = false
                        engine.trimClip(track.id, clip.id, start.toDouble(), length.toDouble())
                    },
                    onDrag = { dx ->
                        length = (length + (dx * secondsPerDp).toFloat())
                            .coerceIn(0.1f, span.toFloat() - start)
                    },
                )
            }
        }
    }
}

@Composable
private fun TrimGrip(
    selected: Boolean,
    onStart: () -> Unit,
    onEnd: () -> Unit,
    onDrag: (Float) -> Unit,
) {
    Box(
        Modifier
            .width(GripWidth)
            .fillMaxHeight()
            .background(if (selected) MsColors.accentSoft.copy(alpha = 0.3f) else Color.Transparent)
            .pointerInput(selected) {
                detectDragGestures(
                    onDragStart = { onStart() },
                    onDragEnd = { onEnd() },
                    onDragCancel = { onEnd() },
                ) { change, drag ->
                    change.consume()
                    onDrag(drag.x)
                }
            },
        contentAlignment = Alignment.Center,
    ) {
        Box(
            Modifier.width(1.5.dp).height(12.dp)
                .background(if (selected) MsColors.accentSoft else MsColors.guideStrong)
        )
    }
}

/**
 * Two-finger time-axis zoom.
 *
 * Deliberately not `detectTransformGestures`: that claims one-finger pans too and
 * would fight the scroll containers and the clip drags underneath. This detector
 * only acts once a second pointer is down, and consumes on the Initial pass so the
 * scrollable never sees the gesture as a pan.
 */
private fun Modifier.pinchToZoom(onZoom: (factor: Float, focusX: Float) -> Unit): Modifier =
    pointerInput(Unit) {
        awaitEachGesture {
            while (true) {
                val event = awaitPointerEvent(PointerEventPass.Initial)
                val pressed = event.changes.filter { it.pressed }
                if (pressed.isEmpty()) break
                if (pressed.size < 2) continue
                val factor = event.calculateZoom()
                if (!factor.isFinite() || factor == 1f) continue
                val focus = event.calculateCentroid(useCurrent = true).x
                onZoom(factor, focus)
                pressed.forEach { it.consume() }
            }
        }
    }

/**
 * Timeline command chip. 30 dp tall with 10 dp sides: the old 19 dp pill was
 * below the minimum touch target and every strip of controls read as a row of
 * debug labels rather than buttons.
 */
@Composable
private fun Chip(label: String, active: Boolean, onClick: () -> Unit) {
    Box(
        Modifier
            .heightIn(min = 30.dp)
            .clip(RoundedCornerShape(6.dp))
            .background(if (active) MsColors.accentDim.copy(alpha = 0.55f) else MsColors.surfaceHigh)
            .border(
                1.dp,
                if (active) MsColors.accent else MsColors.border,
                RoundedCornerShape(6.dp),
            )
            .clickable { onClick() }
            .padding(horizontal = 10.dp, vertical = 5.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            label,
            color = if (active) MsColors.accentSoft else MsColors.textSecondary,
            fontSize = 10.sp,
            fontFamily = MsFonts.mono,
            fontWeight = if (active) FontWeight.Bold else FontWeight.Normal,
        )
    }
}

/** Snap candidates: the playhead, markers, and every other clip's edges. */
private fun snapTargets(engine: EditorEngine, clip: NativeClip): List<Double> =
    buildList {
        add(engine.time)
        engine.doc.markers.forEach { add(it.time) }
        engine.doc.tracks.forEach { track ->
            track.clips.forEach { other ->
                if (other.id != clip.id) {
                    add(other.start)
                    add(other.end)
                }
            }
        }
    }

private fun snapTo(
    targets: List<Double>,
    candidate: Double,
    enabled: Boolean,
    onSnap: (Double?) -> Unit,
): Double {
    if (!enabled) {
        onSnap(null)
        return candidate
    }
    val nearest = targets.minByOrNull { abs(it - candidate) } ?: return candidate
    return if (abs(nearest - candidate) <= 0.08) {
        onSnap(nearest)
        nearest.coerceAtLeast(0.0)
    } else {
        onSnap(null)
        candidate
    }
}

private fun trackColor(track: NativeTrack, audio: Boolean): Color = when {
    track.muted -> MsColors.textMuted
    audio -> MsColors.audioTrack
    track.name.contains("overlay", ignoreCase = true) -> MsColors.overlayTrack
    else -> MsColors.videoTrack
}

private fun xDp(seconds: Double, span: Double, width: Dp): Dp =
    ((seconds / span).toFloat() * width.value).dp

/** Smallest step whose labels clear ~78dp at the current zoom. */
internal fun rulerStep(span: Double, width: Dp): Double {
    val perDp = span / width.value.coerceAtLeast(1f).toDouble()
    val minimum = perDp * 78.0
    val ladder = listOf(
        1.0 / 30.0, 1.0 / 15.0, 0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 30.0,
        60.0, 120.0, 300.0, 600.0, 1800.0, 3600.0,
    )
    return ladder.firstOrNull { it >= minimum } ?: 7200.0
}

/**
 * One unambiguous unit per zoom level: seconds below a minute, m:ss above it, and
 * a decimal only when ticks are closer together than a second.
 *
 * The old label was mm:ss:ff, so a 15 s tick printed "00:15:00" and the axis
 * looked like it jumped from ten seconds to a minute and nineteen.
 */
internal fun rulerLabel(seconds: Double, step: Double): String {
    val s = seconds.coerceAtLeast(0.0)
    if (step < 1.0) {
        val whole = floor(s).toInt()
        // Locale.US: a decimal comma would read as a separator in a timecode.
        if (whole < 60) return "%.1fs".format(Locale.US, s)
        val frac = ((s - whole) * 10.0).roundToInt().coerceIn(0, 9)
        return "%d:%02d.%d".format(Locale.US, whole / 60, whole % 60, frac)
    }
    val total = s.roundToInt()
    return if (total < 60) "${total}s" else "%d:%02d".format(Locale.US, total / 60, total % 60)
}

/** Grid lines use a quarter of the label step so lanes read as a ruler, not a wall. */
private fun gridStep(secondsPerDp: Double): Double =
    max(secondsPerDp * 54.0, 1.0 / 30.0)



