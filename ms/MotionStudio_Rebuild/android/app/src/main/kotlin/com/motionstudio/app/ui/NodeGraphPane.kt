package com.motionstudio.app.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
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
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.motionstudio.app.editor.CompOps
import com.motionstudio.app.editor.CompNode
import com.motionstudio.app.editor.EditorEngine
import com.motionstudio.app.editor.Knob
import com.motionstudio.app.ui.theme.MsColors
import com.motionstudio.app.ui.theme.MsFonts

private val NodeWidth = 148.dp
private val NodeHeight = 62.dp
private val CanvasWidth = 1400.dp
private val CanvasHeight = 760.dp

/**
 * Comp network editor.
 *
 * This is a real dependency graph, not a chain: nodes are `ms::NodeOp` values,
 * wires are `GraphEdge`s, and the viewport cooks from the node marked Viewer
 * whenever the comp is active. Dragging moves a node (layout only), tapping
 * selects, and tapping a node's IN port connects the selected node into it. The
 * evaluator rejects cycles, so a bad link simply reports and does not apply.
 */
@Composable
fun NodeGraphPane(engine: EditorEngine, modifier: Modifier = Modifier) {
    val nodes = engine.compNodes
    val edges = engine.compEdges
    var selected by remember { mutableIntStateOf(0) }
    var picking by remember { mutableStateOf<String?>(null) }
    val density = LocalDensity.current
    val hScroll = rememberScrollState()
    val vScroll = rememberScrollState()

    val selectedNode = nodes.firstOrNull { it.id == selected }

    Column(modifier.background(MsColors.background)) {
        PanelHeader(
            "COMP NETWORK",
            "${nodes.size} nodes · ${edges.size} wires · " +
                if (engine.compActive) "COOKING" else "TIMELINE VIEW",
        )

        // ---- toolbar --------------------------------------------------------
        Row(
            Modifier.fillMaxWidth().height(40.dp).background(MsColors.surface)
                .horizontalScroll(rememberScrollState()).padding(horizontal = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(5.dp),
        ) {
            Box(
                Modifier
                    .heightIn(min = 30.dp)
                    .border(
                        1.dp,
                        if (engine.compActive) MsColors.accent else MsColors.border,
                        RoundedCornerShape(3.dp),
                    )
                    .clickable { engine.activateComp(!engine.compActive) }
                    .padding(horizontal = 10.dp, vertical = 6.dp)
            ) {
                Text(
                    if (engine.compActive) "COMP ON" else "COMP OFF",
                    color = if (engine.compActive) MsColors.accent else MsColors.textTertiary,
                    fontSize = 10.sp,
                    fontFamily = MsFonts.mono,
                    fontWeight = FontWeight.Bold,
                )
            }
            listOf(
                CompOps.CONSTANT to "CONST",
                CompOps.BLUR to "BLUR",
                CompOps.GRADE to "GRADE",
                CompOps.SATURATION to "SAT",
                CompOps.INVERT to "INVERT",
                CompOps.TRANSFORM to "XFORM",
                CompOps.MERGE to "MERGE",
                CompOps.OVER to "OVER",
                CompOps.VIEWER to "VIEWER",
            ).forEach { (op, label) ->
                Box(
                    Modifier
                        .heightIn(min = 30.dp)
                        .border(1.dp, MsColors.border, RoundedCornerShape(3.dp))
                        .clickable {
                            // New nodes land in a free column so they never stack.
                            val x = 40f + (nodes.size % 4) * 170f
                            val y = 60f + (nodes.size / 4) * 110f
                            selected = engine.addCompNode(op, x, y)
                        }
                        .padding(horizontal = 9.dp, vertical = 6.dp)
                ) {
                    Text(
                        label,
                        color = MsColors.textSecondary,
                        fontSize = 10.sp,
                        fontFamily = MsFonts.mono,
                    )
                }
            }
        }
        Box(Modifier.fillMaxWidth().height(1.dp).background(MsColors.border))

        // ---- graph ----------------------------------------------------------
        Box(
            Modifier
                .fillMaxWidth()
                .weight(1f)
                .background(MsColors.viewerBg)
                .horizontalScroll(hScroll)
                .verticalScroll(vScroll)
        ) {
            Box(Modifier.size(CanvasWidth, CanvasHeight)) {
                // Wires first so nodes draw on top of them.
                Canvas(Modifier.fillMaxSize()) {
                    val byId = nodes.associateBy { it.id }
                    edges.forEach { edge ->
                        val from = byId[edge.from] ?: return@forEach
                        val to = byId[edge.to] ?: return@forEach
                        val startX = with(density) { (from.x.dp + NodeWidth).toPx() }
                        val startY = with(density) { (from.y.dp + NodeHeight / 2).toPx() }
                        val endX = with(density) { to.x.dp.toPx() }
                        val endY = with(density) { (to.y.dp + NodeHeight / 2).toPx() }
                        val midX = (startX + endX) / 2f
                        // Cubic-ish wire: two straight hints read better than a
                        // bezier at this size and cost nothing to hit-test.
                        drawLine(MsColors.wire, Offset(startX, startY), Offset(midX, startY), 1.5f)
                        drawLine(MsColors.wire, Offset(midX, startY), Offset(midX, endY), 1.5f)
                        drawLine(MsColors.wire, Offset(midX, endY), Offset(endX, endY), 1.5f)
                    }
                }

                nodes.forEach { node ->
                    CompNodeCard(
                        node = node,
                        isSelected = node.id == selected,
                        isSink = node.id == engine.compSink,
                        onSelect = { selected = node.id },
                        onMove = { dx, dy ->
                            val step = with(density) { 1.dp.toPx() }
                            engine.moveCompNode(node.id, node.x + dx / step, node.y + dy / step)
                        },
                        onConnect = { slot ->
                            if (selected != 0 && selected != node.id) {
                                engine.connectComp(selected, node.id, slot)
                            }
                        },
                    )
                }
            }
        }

        // ---- inspector ------------------------------------------------------
        Column(
            Modifier
                .fillMaxWidth()
                .weight(0.55f)
                .background(MsColors.surface)
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 12.dp, vertical = 8.dp)
        ) {
            if (selectedNode == null) {
                Text(
                    "No node selected. Add one above, then tap a node's IN port to wire the selected node into it.",
                    color = MsColors.textTertiary,
                    fontSize = 12.sp,
                )
                return@Column
            }
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(
                    "#${selectedNode.id}  ${CompOps.name(selectedNode.op).uppercase()}",
                    color = MsColors.textPrimary,
                    fontSize = 13.sp,
                    fontFamily = MsFonts.mono,
                    fontWeight = FontWeight.Bold,
                )
                Spacer(Modifier.width(10.dp))
                Text(
                    if (selectedNode.enabled) "ACTIVE" else "BYPASSED",
                    color = if (selectedNode.enabled) MsColors.success else MsColors.textMuted,
                    fontSize = 10.sp,
                    fontFamily = MsFonts.mono,
                )
                Spacer(Modifier.weight(1f))
                TextButton(onClick = { engine.setCompEnabled(selectedNode.id, !selectedNode.enabled) }) {
                    Text(
                        if (selectedNode.enabled) "Bypass" else "Enable",
                        color = MsColors.accentSoft,
                        fontSize = 12.sp,
                    )
                }
                TextButton(
                    onClick = { engine.viewCompNode(selectedNode.id) },
                    enabled = selectedNode.id != engine.compSink,
                ) {
                    Text(
                        if (selectedNode.id == engine.compSink) "Viewing" else "View this",
                        color = if (selectedNode.id == engine.compSink) MsColors.textMuted
                        else MsColors.info,
                        fontSize = 12.sp,
                    )
                }
                TextButton(onClick = {
                    engine.removeCompNode(selectedNode.id)
                    selected = 0
                }) {
                    Text("Delete", color = MsColors.error, fontSize = 12.sp)
                }
            }

            if (selectedNode.op == CompOps.READ) {
                val current = engine.compMedia(selectedNode.id)
                Text(
                    "SOURCE  " + if (current.isEmpty()) "timeline sequence" else current.substringAfterLast('/'),
                    color = MsColors.textTertiary,
                    fontSize = 10.sp,
                    fontFamily = MsFonts.mono,
                    modifier = Modifier.padding(bottom = 3.dp),
                )
                Row(
                    Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()),
                    horizontalArrangement = Arrangement.spacedBy(5.dp),
                ) {
                    SourceChip("TIMELINE", current.isEmpty()) {
                        engine.setCompMedia(selectedNode.id, "")
                    }
                    engine.doc.media.forEach { media ->
                        SourceChip(media.name.ifBlank { media.path.substringAfterLast('/') },
                            current == media.path) {
                            engine.setCompMedia(selectedNode.id, media.path)
                        }
                    }
                }
            }

            val knobs = CompOps.knobs(selectedNode.op)
            if (knobs.isEmpty()) {
                Text(
                    "This node has no editable parameters.",
                    color = MsColors.textMuted,
                    fontSize = 11.sp,
                )
            } else {
                knobs.forEach { knob ->
                    KnobRow(
                        engine = engine,
                        node = selectedNode,
                        knob = knob,
                        picking = picking == knob.name,
                        onPickToggle = {
                            picking = if (picking == knob.name) null else knob.name
                        },
                    )
                }
                // Link picker: any numeric knob on any other node can drive this one.
                val target = picking
                if (target != null) {
                    val candidates = engine.compNodes
                        .filter { it.id != selectedNode.id }
                        .flatMap { other ->
                            CompOps.knobs(other.op).map { k ->
                                Triple(other, k, "#${other.id} ${CompOps.name(other.op)}.${k.label}")
                            }
                        }
                    if (candidates.isEmpty()) {
                        Text(
                            "Add another node with a numeric knob to link against.",
                            color = MsColors.textMuted,
                            fontSize = 11.sp,
                        )
                    } else {
                        Row(
                            Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()),
                            horizontalArrangement = Arrangement.spacedBy(5.dp),
                        ) {
                            candidates.forEach { (other, k, label) ->
                                SourceChip(label, false) {
                                    engine.linkCompParam(selectedNode.id, target, other.id, k.name)
                                    picking = null
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun SourceChip(label: String, selected: Boolean, onClick: () -> Unit) {
    Box(
        Modifier
            .heightIn(min = 30.dp)
            .border(1.dp, if (selected) MsColors.accent else MsColors.border, RoundedCornerShape(3.dp))
            .clickable { onClick() }
            .padding(horizontal = 9.dp, vertical = 6.dp)
    ) {
        Text(
            label.uppercase(),
            color = if (selected) MsColors.accent else MsColors.textSecondary,
            fontSize = 10.sp,
            fontFamily = MsFonts.mono,
        )
    }
}

@Composable
private fun KnobRow(
    engine: EditorEngine,
    node: CompNode,
    knob: Knob,
    picking: Boolean,
    onPickToggle: () -> Unit,
) {
    val link = engine.linkFor(node.id, knob.name)
    val value = if (link != null) engine.compParam(link.src, link.srcParam, knob.default)
    else engine.compParam(node.id, knob.name, knob.default)
    Column(Modifier.fillMaxWidth()) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(
                knob.label.uppercase(),
                color = MsColors.textTertiary,
                fontSize = 10.sp,
                fontFamily = MsFonts.mono,
                modifier = Modifier.width(84.dp),
            )
            Slider(
                // A linked knob is driven by its source, so dragging it would be a lie.
                value = value.toFloat().coerceIn(knob.min.toFloat(), knob.max.toFloat()),
                onValueChange = { engine.setCompParam(node.id, knob.name, it.toDouble()) },
                valueRange = knob.min.toFloat()..knob.max.toFloat(),
                enabled = link == null,
                colors = SliderDefaults.colors(
                    thumbColor = if (link == null) MsColors.accent else MsColors.info,
                    activeTrackColor = if (link == null) MsColors.accent else MsColors.info,
                    inactiveTrackColor = MsColors.border,
                ),
                modifier = Modifier.weight(1f),
            )
            Text(
                "%.2f".format(value),
                color = MsColors.textSecondary,
                fontSize = 11.sp,
                fontFamily = MsFonts.mono,
                modifier = Modifier.width(52.dp),
            )
            if (link == null) {
                SourceChip(if (picking) "PICK…" else "LINK", picking) { onPickToggle() }
            } else {
                SourceChip("#${link.src}.${link.srcParam}", true) {
                    engine.unlinkCompParam(node.id, knob.name)
                }
            }
        }
    }
}

@Composable
private fun CompNodeCard(
    node: CompNode,
    isSelected: Boolean,
    isSink: Boolean,
    onSelect: () -> Unit,
    onMove: (Float, Float) -> Unit,
    onConnect: (Int) -> Unit,
) {
    val capacity = CompOps.capacity(node.op)
    Box(
        Modifier
            .offset(node.x.dp, node.y.dp)
            .size(NodeWidth, NodeHeight)
            .background(MsColors.nodeBg, RoundedCornerShape(3.dp))
            .border(
                1.dp,
                when {
                    isSelected -> MsColors.accent
                    isSink -> MsColors.info
                    else -> MsColors.nodeBorder
                },
                RoundedCornerShape(3.dp),
            )
            // Tap and drag must be separate pointerInput modifiers: a single
            // detector cannot report both gestures.
            .pointerInput(node.id) {
                detectTapGestures { onSelect() }
            }
            .pointerInput(node.id, node.x, node.y) {
                detectDragGestures { change, drag ->
                    change.consume()
                    onMove(drag.x, drag.y)
                }
            }
    ) {
        Column(Modifier.fillMaxSize().padding(start = 22.dp, end = 8.dp, top = 7.dp, bottom = 7.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(
                    CompOps.name(node.op).uppercase(),
                    color = if (isSelected) MsColors.accent else MsColors.textSecondary,
                    fontSize = 11.sp,
                    fontFamily = MsFonts.mono,
                    fontWeight = FontWeight.Bold,
                )
                Spacer(Modifier.weight(1f))
                Box(
                    Modifier.size(6.dp).background(
                        if (node.enabled) MsColors.success else MsColors.textMuted,
                        CircleShape,
                    )
                )
            }
            Text(
                "#${node.id}" + if (isSink) "  VIEW" else "",
                color = if (isSink) MsColors.info else MsColors.textMuted,
                fontSize = 10.sp,
                fontFamily = MsFonts.mono,
            )
        }

        // IN ports: tapping one wires the currently selected node into this slot.
        Column(
            Modifier.align(Alignment.CenterStart).padding(start = 2.dp),
            verticalArrangement = Arrangement.spacedBy(2.dp),
        ) {
            repeat(capacity) { slot ->
                Box(
                    Modifier
                        .size(18.dp)
                        .background(MsColors.border, RoundedCornerShape(3.dp))
                        .clickable { onConnect(slot) },
                    contentAlignment = Alignment.Center,
                ) {
                    Text(
                        "${slot + 1}",
                        color = MsColors.textSecondary,
                        fontSize = 9.sp,
                        fontFamily = MsFonts.mono,
                    )
                }
            }
        }
    }
}
