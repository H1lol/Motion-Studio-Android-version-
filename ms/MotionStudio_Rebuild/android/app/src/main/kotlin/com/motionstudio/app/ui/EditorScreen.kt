package com.motionstudio.app.ui

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
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
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.Redo
import androidx.compose.material.icons.automirrored.filled.Undo
import androidx.compose.material.icons.filled.AutoFixHigh
import androidx.compose.material.icons.filled.BlurOn
import androidx.compose.material.icons.filled.Code
import androidx.compose.material.icons.filled.GraphicEq
import androidx.compose.material.icons.filled.MoreHoriz
import androidx.compose.material.icons.filled.OpenWith
import androidx.compose.material.icons.filled.Palette
import androidx.compose.material.icons.filled.Pause
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.filled.SkipNext
import androidx.compose.material.icons.filled.SkipPrevious
import androidx.compose.material.icons.filled.TextFields
import androidx.compose.material.icons.filled.Tune
import androidx.compose.material.icons.filled.VideoLibrary
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import android.media.MediaMetadataRetriever
import com.motionstudio.app.editor.EditorEngine
import com.motionstudio.app.editor.TitleOptions
import com.motionstudio.app.native.NativeEngine
import com.motionstudio.app.ui.theme.MsColors
import com.motionstudio.app.ui.theme.MsFonts
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File

/**
 * Editor workspaces. Each tab swaps the panel in the lower half; none of them is
 * decorative — TIMELINE is the timeline, NODES is the clip's render chain,
 * KEYS reads keyframe counts from the engine, GRADE drives the master grade,
 * CLIP inspects the selection.
 */
private enum class Workspace(val label: String) {
    Timeline("TIMELINE"),
    Clip("CLIP"),
    Keys("KEYS"),
    Script("SCRIPT"),
    Grade("GRADE"),
    Nodes("NODES"),
}

@Composable
fun EditorScreen(
    engine: EditorEngine,
    onBack: () -> Unit,
    onExportFrame: () -> Unit,
) {
    val context = LocalContext.current
    val snackbar = remember { SnackbarHostState() }
    var workspace by remember { mutableStateOf(Workspace.Timeline) }
    var showMore by remember { mutableStateOf(false) }
    var showLooks by remember { mutableStateOf(false) }
    var showEffects by remember { mutableStateOf(false) }
    var showColor by remember { mutableStateOf(false) }
    var showMotion by remember { mutableStateOf(false) }
    var showText by remember { mutableStateOf(false) }
    var showMask by remember { mutableStateOf(false) }
    var showAudio by remember { mutableStateOf(false) }
    var showExport by remember { mutableStateOf(false) }

    val importScope = rememberCoroutineScope()
    val picker = rememberLauncherForActivityResult(
        ActivityResultContracts.OpenDocument()
    ) { uri ->
        if (uri == null) return@rememberLauncherForActivityResult
        // Copying a video is disk-bound; doing it on the main thread froze the
        // editor for as long as the file took to copy.
        importScope.launch {
            engine.note("Importing…")
            val imported = withContext(Dispatchers.IO) {
                runCatching {
                    val raw = uri.lastPathSegment?.substringAfterLast('/') ?: "media"
                    val name = raw.ifBlank { "media" }
                    // Media lives in filesDir: the cache can be purged by the OS
                    // and the project would then point at a missing path.
                    val target = File(context.filesDir, "imports").apply { mkdirs() }
                    // Never overwrite an earlier import that happens to share a name.
                    val file = File(target, uniqueName(target, name))
                    val input = context.contentResolver.openInputStream(uri)
                        ?: return@runCatching false
                    input.use { source ->
                        file.outputStream().use { output -> source.copyTo(output) }
                    }
                    if (file.length() <= 0L) {
                        file.delete()
                        return@runCatching false
                    }
                    // The platform's metadata reader knows the container length
                    // even when the native decoder's track format does not. A
                    // clip cut to five seconds used to make the whole sequence
                    // look like it stopped at the project's default length.
                    val fallbackSeconds = runCatching {
                        MediaMetadataRetriever().use { retriever ->
                            retriever.setDataSource(file.absolutePath)
                            val ms = retriever
                                .extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION)
                                ?.toLongOrNull() ?: 0L
                            ms / 1000.0
                        }
                    }.getOrDefault(0.0)
                    engine.importMedia(
                        file.absolutePath,
                        name,
                        appendToEnd = engine.appendImportsAtEnd,
                        fallbackDuration = fallbackSeconds,
                    )
                }.getOrDefault(false)
            }
            if (!imported) engine.note("Could not import that file")
        }
    }

    LaunchedEffect(engine.status) {
        snackbar.showSnackbar(engine.status)
    }
    LaunchedEffect(engine.lastError) {
        engine.lastError?.let { snackbar.showSnackbar(it) }
    }

    Scaffold(
        snackbarHost = { SnackbarHost(snackbar) },
        containerColor = MsColors.background,
    ) { padding ->
        BoxWithConstraints(Modifier.fillMaxSize().padding(padding)) {
            val landscape = maxWidth > maxHeight
            Column(Modifier.fillMaxSize()) {
                EditorHeader(
                    engine = engine,
                    onBack = onBack,
                    onSettings = { showMore = true },
                    onExport = { showExport = true },
                )
                // One rail, one row of tabs, and the picture gets everything
                // else. The previous layout stacked a header, a full transport
                // strip, a tab row, the panel AND a labelled dock between the
                // viewer and the timeline edge — about 220 dp of chrome, which
                // is most of a 640 dp-tall phone.
                val pickMedia = {
                    picker.launch(arrayOf("video/*", "audio/*", "image/*"))
                }
                if (landscape) {
                    Row(Modifier.weight(1f).fillMaxWidth()) {
                        Column(Modifier.weight(0.62f).fillMaxHeight()) {
                            Viewport(engine, modifier = Modifier.weight(1f))
                            TransportBar(engine)
                        }
                        Column(
                            Modifier.weight(0.38f).fillMaxHeight()
                                .border(1.dp, MsColors.borderSoft)
                        ) {
                            ToolRail(
                                onImport = pickMedia,
                                onText = {
                                    engine.addTitle("TITLE", TitleOptions())
                                    showText = true
                                },
                                onEffects = { showEffects = true },
                                onLook = { showLooks = true },
                                onColor = { showColor = true },
                                onMotion = { showMotion = true },
                                onMask = { showMask = true },
                                onAudio = { showAudio = true },
                                onScript = { workspace = Workspace.Script },
                                onMore = { showMore = true },
                            )
                            WorkspaceTabs(selected = workspace, onSelect = { workspace = it })
                            WorkspacePanel(
                                engine = engine,
                                workspace = workspace,
                                modifier = Modifier.weight(1f),
                                onImport = pickMedia,
                                onInspect = { workspace = Workspace.Clip },
                                onOpenEffects = { showEffects = true },
                                onOpenText = { showText = true },
                                onOpenMask = { showMask = true },
                                onOpenMotion = { showMotion = true },
                            )
                        }
                    }
                } else {
                    Viewport(engine, modifier = Modifier.weight(1f))
                    TransportBar(engine)
                    ToolRail(
                        onImport = pickMedia,
                        onText = {
                            engine.addTitle("TITLE", TitleOptions())
                            showText = true
                        },
                        onEffects = { showEffects = true },
                        onLook = { showLooks = true },
                        onColor = { showColor = true },
                        onMotion = { showMotion = true },
                        onMask = { showMask = true },
                        onAudio = { showAudio = true },
                        onScript = { workspace = Workspace.Script },
                        onMore = { showMore = true },
                    )
                    WorkspaceTabs(selected = workspace, onSelect = { workspace = it })
                    WorkspacePanel(
                        engine = engine,
                        workspace = workspace,
                        // The timeline is where the work happens; give it the room.
                        modifier = Modifier.weight(1f),
                        onImport = pickMedia,
                        onInspect = { workspace = Workspace.Clip },
                        onOpenEffects = { showEffects = true },
                        onOpenText = { showText = true },
                        onOpenMask = { showMask = true },
                        onOpenMotion = { showMotion = true },
                    )
                }
            }

            if (showMore) {
                MoreSheet(
                    engine = engine,
                    onDismiss = { showMore = false },
                    onExportFrame = onExportFrame,
                    onOpenExport = {
                        showMore = false
                        showExport = true
                    },
                )
            }
            if (showExport) {
                ExportSheet(engine = engine, onDismiss = { showExport = false })
            }
            if (showLooks) LooksSheet(engine = engine, onDismiss = { showLooks = false })
            if (showEffects) EffectsSheet(engine = engine, onDismiss = { showEffects = false })
            if (showColor) ColorSheet(engine = engine, onDismiss = { showColor = false })
            if (showMotion) MotionSheet(engine = engine, onDismiss = { showMotion = false })
            if (showText) TextSheet(engine = engine, onDismiss = { showText = false })
            if (showMask) MaskSheet(engine = engine, onDismiss = { showMask = false })
            if (showAudio) AudioSheet(engine = engine, onDismiss = { showAudio = false })
        }
    }
}

@Composable
private fun WorkspacePanel(
    engine: EditorEngine,
    workspace: Workspace,
    modifier: Modifier,
    onImport: () -> Unit,
    onInspect: () -> Unit,
    onOpenEffects: () -> Unit,
    onOpenText: () -> Unit,
    onOpenMask: () -> Unit,
    onOpenMotion: () -> Unit,
) {
    when (workspace) {
        Workspace.Timeline -> TimelinePane(
            engine = engine,
            modifier = modifier.fillMaxWidth().background(MsColors.surface),
            onImport = onImport,
            onOpenEffects = onOpenEffects,
            onOpenMotion = onOpenMotion,
            onOpenMask = onOpenMask,
            onInspect = onInspect,
        )
        Workspace.Nodes -> NodeGraphPane(engine, modifier.fillMaxWidth())
        Workspace.Keys -> KeyframePane(engine, modifier.fillMaxWidth())
        Workspace.Script -> ScriptPane(engine, modifier.fillMaxWidth())
        Workspace.Grade -> GradePane(engine, modifier.fillMaxWidth())
        Workspace.Clip -> ClipPane(
            engine = engine,
            onOpenEffects = onOpenEffects,
            onOpenText = onOpenText,
            onOpenMask = onOpenMask,
            onOpenMotion = onOpenMotion,
            modifier = modifier.fillMaxWidth(),
        )
    }
}

// --------------------------------------------------------------- chrome ----

/**
 * Header: project identity on the left, the delivery button on the right.
 * Export is the one filled control on the screen because it is the thing a
 * phone editor is ultimately for; everything else is an icon.
 */
@Composable
private fun EditorHeader(
    engine: EditorEngine,
    onBack: () -> Unit,
    onSettings: () -> Unit,
    onExport: () -> Unit,
) {
    Column {
        Row(
            Modifier.fillMaxWidth().height(48.dp).background(MsColors.surface)
                .padding(start = 2.dp, end = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            IconButton(onClick = onBack, modifier = Modifier.size(36.dp)) {
                Icon(
                    Icons.AutoMirrored.Filled.ArrowBack,
                    "Back to projects",
                    tint = MsColors.textSecondary,
                )
            }
            // One line for identity, one for the clock: a two-storey title block
            // costs 10 dp of picture on every phone.
            Column(Modifier.weight(1f)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        engine.doc.name.ifBlank { "Untitled" },
                        color = MsColors.textPrimary,
                        fontSize = 14.sp,
                        fontWeight = FontWeight.SemiBold,
                        maxLines = 1,
                        modifier = Modifier.weight(1f, fill = false),
                    )
                    Spacer(Modifier.width(6.dp))
                    Box(
                        Modifier.size(4.dp).background(
                            if (engine.dirty) MsColors.warning else MsColors.success,
                            CircleShape,
                        )
                    )
                }
                HeaderClock(engine)
            }
            IconButton(onClick = { engine.undo() }, modifier = Modifier.size(34.dp)) {
                Icon(Icons.AutoMirrored.Filled.Undo, "Undo", tint = MsColors.textSecondary)
            }
            IconButton(onClick = { engine.redo() }, modifier = Modifier.size(34.dp)) {
                Icon(Icons.AutoMirrored.Filled.Redo, "Redo", tint = MsColors.textSecondary)
            }
            IconButton(onClick = onSettings, modifier = Modifier.size(34.dp)) {
                Icon(Icons.Filled.Settings, "Settings", tint = MsColors.textSecondary)
            }
            Box(
                Modifier
                    .height(32.dp)
                    .clip(RoundedCornerShape(9.dp))
                    .background(MsColors.accent)
                    .clickable { onExport() }
                    .padding(horizontal = 14.dp),
                contentAlignment = Alignment.Center,
            ) {
                Text(
                    "Export",
                    color = MsColors.background,
                    fontSize = 12.sp,
                    fontWeight = FontWeight.Bold,
                )
            }
        }
        Box(Modifier.fillMaxWidth().height(1.dp).background(MsColors.border))
    }
}

@Composable
private fun WorkspaceTabs(selected: Workspace, onSelect: (Workspace) -> Unit) {
    Row(
        Modifier.fillMaxWidth().height(34.dp).background(MsColors.surface)
            .horizontalScroll(rememberScrollState())
            .padding(horizontal = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(2.dp),
    ) {
        Workspace.entries.forEach { tab ->
            val active = tab == selected
            Box(
                Modifier
                    .height(26.dp)
                    .clip(RoundedCornerShape(7.dp))
                    .background(if (active) MsColors.accentDim else Color.Transparent)
                    .clickable { onSelect(tab) }
                    .padding(horizontal = 11.dp),
                contentAlignment = Alignment.Center,
            ) {
                Text(
                    tab.label,
                    color = if (active) MsColors.accentSoft else MsColors.textTertiary,
                    fontSize = 10.sp,
                    fontFamily = MsFonts.mono,
                    fontWeight = if (active) FontWeight.Bold else FontWeight.Normal,
                )
            }
        }
    }
    Box(Modifier.fillMaxWidth().height(1.dp).background(MsColors.border))
}

@Composable
private fun Viewport(engine: EditorEngine, modifier: Modifier = Modifier) {
    // The explicit pan/zoom mode from the Alight reference: with it on, two
    // fingers always move the canvas; with it off and a layer selected, they
    // move the layer instead.
    var navigate by remember { mutableStateOf(false) }
    // Read here, outside the AndroidView update lambda, so a selection change
    // recomposes the viewport and pushes the new gesture mode into the view.
    val layerSelected = engine.selectedClipId > 0

    Column(modifier.fillMaxWidth().background(MsColors.viewerBg)) {
        Box(Modifier.fillMaxWidth().weight(1f)) {
            AndroidView(
                factory = { context ->
                    PreviewView(context).also { view ->
                        view.attach(engine)
                        view.onLayerTransform = { dx, dy, scale ->
                            engine.transformSelectedLayer(dx, dy, scale)
                        }
                        // Tap-to-select: the view reports a point on the
                        // composite and the engine picks the layer drawn there.
                        view.onPickLayer = { nx, ny -> engine.pickAt(nx, ny) }
                    }
                },
                update = { view ->
                    view.setNavigateMode(navigate)
                    view.setLayerTransformEnabled(layerSelected)
                },
                modifier = Modifier.fillMaxSize(),
            )
            // Framing marks, not a box: four corner rules say where the canvas
            // edge is without drawing a rectangle across the picture you are
            // judging. The previous full-inset border read as a stray selection.
            Canvas(
                Modifier.fillMaxSize().padding(horizontal = 22.dp, vertical = 14.dp)
            ) {
                val arm = 16.dp.toPx()
                val ink = MsColors.guideStrong.copy(alpha = 0.45f)
                fun corner(x: Float, y: Float, dx: Float, dy: Float) {
                    drawLine(ink, Offset(x, y), Offset(x + dx * arm, y), 1f)
                    drawLine(ink, Offset(x, y), Offset(x, y + dy * arm), 1f)
                }
                corner(0f, 0f, 1f, 1f)
                corner(size.width, 0f, -1f, 1f)
                corner(0f, size.height, 1f, -1f)
                corner(size.width, size.height, -1f, -1f)
            }
            Box(
                Modifier
                    .align(Alignment.TopStart)
                    .padding(8.dp)
                    .background(MsColors.background.copy(alpha = 0.72f))
                    .padding(horizontal = 6.dp, vertical = 2.dp)
            ) {
                // A JNI call returning a String on every recomposition of the
                // viewport (one per rendered frame) is pure waste: the backend
                // cannot change while the app runs.
                val backend = remember { NativeEngine.nativeDecoderBackend().uppercase() }
                Text(
                    "$backend  ·  " +
                        "${engine.doc.width}×${engine.doc.height}  ·  ${engine.fps.toInt()}P",
                    color = MsColors.textTertiary,
                    fontSize = 9.sp,
                    fontFamily = MsFonts.mono,
                )
            }
            Box(
                Modifier
                    .align(Alignment.TopEnd)
                    .padding(8.dp)
                    .background(MsColors.background.copy(alpha = 0.72f))
                    .padding(horizontal = 6.dp, vertical = 2.dp)
            ) {
                Text(
                    if (engine.playing) "PLAYING" else "PAUSED",
                    color = if (engine.playing) MsColors.success else MsColors.textMuted,
                    fontSize = 9.sp,
                    fontFamily = MsFonts.mono,
                )
            }
            Box(
                Modifier
                    .align(Alignment.BottomStart)
                    .padding(8.dp)
                    .background(MsColors.background.copy(alpha = 0.72f))
                    .padding(horizontal = 6.dp, vertical = 2.dp)
            ) {
                ViewportClock(engine)
            }
            Row(
                Modifier.align(Alignment.BottomEnd).padding(8.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(6.dp),
            ) {
                engine.lookName?.let { look ->
                    Box(
                        Modifier
                            .background(MsColors.background.copy(alpha = 0.72f))
                            .padding(horizontal = 6.dp, vertical = 2.dp)
                    ) {
                        Text(
                            "LUT ${look.uppercase()}",
                            color = MsColors.info,
                            fontSize = 9.sp,
                            fontFamily = MsFonts.mono,
                        )
                    }
                }
                Box(
                    Modifier
                        .heightIn(min = 30.dp)
                        .clip(RoundedCornerShape(5.dp))
                        .background(
                            if (navigate) MsColors.accentDim.copy(alpha = 0.85f)
                            else MsColors.background.copy(alpha = 0.72f)
                        )
                        .border(
                            1.dp,
                            if (navigate) MsColors.accent else MsColors.border,
                            RoundedCornerShape(5.dp),
                        )
                        .clickable { navigate = !navigate }
                        .padding(horizontal = 9.dp, vertical = 5.dp),
                    contentAlignment = Alignment.Center,
                ) {
                    Text(
                        "PAN / ZOOM",
                        color = if (navigate) MsColors.accentSoft else MsColors.textSecondary,
                        fontSize = 9.sp,
                        fontFamily = MsFonts.mono,
                        fontWeight = if (navigate) FontWeight.Bold else FontWeight.Normal,
                    )
                }
            }
        }
    }
}

/**
 * Transport: previous/next keyframe around a large play button, then undo/redo.
 * The keyframe buttons fall back to a single frame when the selected clip has no
 * keys, so they never feel dead.
 */
@Composable
private fun TransportBar(engine: EditorEngine) {
    Column {
        Box(Modifier.fillMaxWidth().height(1.dp).background(MsColors.border))
        Row(
            Modifier.fillMaxWidth().height(46.dp).background(MsColors.surface)
                .padding(horizontal = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(2.dp),
        ) {
            IconButton(onClick = { engine.jumpToKeyframe(-1) }, modifier = Modifier.size(36.dp)) {
                Icon(Icons.Filled.SkipPrevious, "Previous keyframe", tint = MsColors.textSecondary)
            }
            Box(
                Modifier
                    .size(36.dp)
                    .background(MsColors.accent, CircleShape)
                    .clickable { engine.togglePlay() },
                contentAlignment = Alignment.Center,
            ) {
                Icon(
                    if (engine.playing) Icons.Filled.Pause else Icons.Filled.PlayArrow,
                    if (engine.playing) "Pause" else "Play",
                    tint = Color.Black,
                    modifier = Modifier.size(21.dp),
                )
            }
            IconButton(onClick = { engine.jumpToKeyframe(1) }, modifier = Modifier.size(36.dp)) {
                Icon(Icons.Filled.SkipNext, "Next keyframe", tint = MsColors.textSecondary)
            }
            Spacer(Modifier.weight(1f))
            TransportClock(engine)
        }
    }
}

/**
 * The tool dock: one icon and one word per action, at thumb size.
 * Mono-labelled chips were the previous rail and they read as debug output; a
 * layer tool has to look like something you press.
 */
/**
 * The one action rail: everything you can add to the edit, in thumb reach.
 *
 * It used to be a separate dock under the tabs, so the timeline's own toolbar
 * and the tools competed for the same 66 dp. One rail, directly above the tabs,
 * and the timeline keeps its height.
 */
@Composable
private fun ToolRail(
    onImport: () -> Unit,
    onText: () -> Unit,
    onEffects: () -> Unit,
    onLook: () -> Unit,
    onColor: () -> Unit,
    onMotion: () -> Unit,
    onMask: () -> Unit,
    onAudio: () -> Unit,
    onScript: () -> Unit,
    onMore: () -> Unit,
) {
    Column {
        Box(Modifier.fillMaxWidth().height(1.dp).background(MsColors.border))
        Row(
            Modifier
                .fillMaxWidth()
                .height(58.dp)
                .background(MsColors.surface)
                .horizontalScroll(rememberScrollState())
                .padding(horizontal = 4.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(0.dp),
        ) {
            DockButton(Icons.Filled.VideoLibrary, "Media", onImport)
            DockButton(Icons.Filled.TextFields, "Text", onText)
            DockButton(Icons.Filled.AutoFixHigh, "Effects", onEffects)
            DockButton(Icons.Filled.Palette, "Look", onLook)
            DockButton(Icons.Filled.Tune, "Grade", onColor)
            DockButton(Icons.Filled.OpenWith, "Motion", onMotion)
            DockButton(Icons.Filled.BlurOn, "Mask", onMask)
            DockButton(Icons.Filled.GraphicEq, "Audio", onAudio)
            DockButton(Icons.Filled.Code, "Script", onScript)
            DockButton(Icons.Filled.MoreHoriz, "More", onMore)
        }
    }
}

@Composable
private fun DockButton(icon: ImageVector, label: String, onClick: () -> Unit) {
    Column(
        Modifier
            .width(56.dp)
            .height(52.dp)
            .clip(RoundedCornerShape(8.dp))
            .clickable(onClick = onClick),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        Icon(icon, label, tint = MsColors.accentSoft, modifier = Modifier.size(22.dp))
        Spacer(Modifier.height(4.dp))
        Text(label, color = MsColors.textSecondary, fontSize = 9.sp, maxLines = 1)
    }
}

/**
 * Playhead readouts sit in their own composables.
 *
 * Reading `engine.time` inside [EditorHeader] or [TransportBar] recomposed that
 * whole row once per delivered frame. Compose invalidates only the scope that
 * read the state, so keeping the read in a leaf keeps frame-rate updates to one
 * Text instead of a header, a transport strip and the viewport badges.
 */
@Composable
private fun HeaderClock(engine: EditorEngine) {
    Text(
        EditorEngine.formatTimecode(engine.time, engine.fps) + " / " +
            EditorEngine.formatTimecode(engine.duration, engine.fps),
        color = MsColors.textTertiary,
        fontSize = 11.sp,
        fontFamily = MsFonts.mono,
        maxLines = 1,
    )
}

@Composable
private fun TransportClock(engine: EditorEngine) {
    Text(
        EditorEngine.formatTimecode(engine.time, engine.fps),
        color = MsColors.accentSoft,
        fontSize = 12.sp,
        fontFamily = MsFonts.mono,
        fontWeight = FontWeight.Bold,
    )
}

@Composable
private fun ViewportClock(engine: EditorEngine) {
    Text(
        "TC " + EditorEngine.formatTimecode(engine.time, engine.fps),
        color = MsColors.accentSoft,
        fontSize = 10.sp,
        fontFamily = MsFonts.mono,
    )
}

/** Picks a filename that does not collide with an earlier import. */
private fun uniqueName(dir: File, name: String): String {
    if (!File(dir, name).exists()) return name
    val dot = name.lastIndexOf('.')
    val base = if (dot > 0) name.substring(0, dot) else name
    val ext = if (dot > 0) name.substring(dot) else ""
    var i = 2
    while (File(dir, "$base-$i$ext").exists()) i++
    return "$base-$i$ext"
}
