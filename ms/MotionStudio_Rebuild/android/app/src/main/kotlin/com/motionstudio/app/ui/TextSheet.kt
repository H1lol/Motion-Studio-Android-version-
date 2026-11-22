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
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.OutlinedTextField
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
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.motionstudio.app.editor.EditorEngine
import com.motionstudio.app.editor.TextFonts
import com.motionstudio.app.editor.TitleOptions
import com.motionstudio.app.ui.theme.MsColors
import com.motionstudio.app.ui.theme.MsFonts

/** Swatches offered for fill and outline colour. */
private val TitleColors = listOf(
    "#FFFFFF" to "White",
    "#0E0E10" to "Ink",
    "#FFC857" to "Amber",
    "#FF6B57" to "Coral",
    "#57E0A0" to "Mint",
    "#57B8FF" to "Sky",
)

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun TextSheet(engine: EditorEngine, onDismiss: () -> Unit) {
    val context = LocalContext.current
    val sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)

    // Font families are unpacked from assets the first time this sheet opens.
    LaunchedEffect(Unit) { TextFonts.ensure(context) }

    val editing = engine.selectedIsText()
    var text by remember {
        mutableStateOf(if (editing) engine.selectedTitleText() else "")
    }
    var opts by remember {
        mutableStateOf(if (editing) engine.selectedTitleOptions() else TitleOptions())
    }
    var duration by remember { mutableStateOf(3.0) }

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
                .padding(bottom = 28.dp),
        ) {
            Text("Titles", color = MsColors.textPrimary, fontSize = 16.sp, fontWeight = FontWeight.SemiBold)
            Spacer(Modifier.height(2.dp))
            Text(
                "Rasterised by stb_truetype inside the engine, then composited like any other clip — " +
                    "keyframed transform, effects, grade and transitions all apply.",
                color = MsColors.textTertiary,
                fontSize = 12.sp,
            )
            Spacer(Modifier.height(12.dp))

            OutlinedTextField(
                value = text,
                onValueChange = { next ->
                    text = next
                    if (engine.selectedIsText()) engine.updateSelectedTitle(next, opts)
                },
                label = { Text("Text", color = MsColors.textTertiary, fontSize = 12.sp) },
                textStyle = androidx.compose.ui.text.TextStyle(
                    color = MsColors.textPrimary,
                    fontSize = 15.sp,
                    fontFamily = MsFonts.mono,
                ),
                modifier = Modifier.fillMaxWidth(),
            )

            Spacer(Modifier.height(12.dp))
            Text("Typeface", color = MsColors.textSecondary, fontSize = 12.sp)
            Spacer(Modifier.height(4.dp))
            Row(
                Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(6.dp),
            ) {
                TextButton(
                    onClick = {
                        opts = opts.copy(font = "inter")
                        if (engine.selectedIsText()) engine.updateSelectedTitle(text, opts)
                    },
                    contentPadding = androidx.compose.foundation.layout.PaddingValues(horizontal = 6.dp),
                ) { Text("Inter", color = MsColors.textSecondary, fontSize = 11.sp) }
            }
            Spacer(Modifier.height(2.dp))
            TextFonts.families.chunked(3).forEach { row ->
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    row.forEach { (family, label) ->
                        val selected = opts.font == family
                        Box(
                            Modifier
                                .weight(1f)
                                .padding(vertical = 3.dp)
                                .background(
                                    if (selected) MsColors.accentSoft.copy(alpha = 0.16f)
                                    else MsColors.surfaceHigh,
                                    RoundedCornerShape(8.dp),
                                )
                                .border(
                                    1.dp,
                                    if (selected) MsColors.accentSoft else Color.Transparent,
                                    RoundedCornerShape(8.dp),
                                )
                                .clickable {
                                    opts = opts.copy(font = family)
                                    if (engine.selectedIsText()) engine.updateSelectedTitle(text, opts)
                                }
                                .padding(vertical = 8.dp),
                            contentAlignment = Alignment.Center,
                        ) {
                            Text(
                                label,
                                color = if (selected) MsColors.accentSoft else MsColors.textSecondary,
                                fontSize = 12.sp,
                            )
                        }
                    }
                    repeat(3 - row.size) { Spacer(Modifier.weight(1f)) }
                }
            }

            Spacer(Modifier.height(10.dp))
            SliderRow("Size", opts.size, 24.0, 480.0) {
                opts = opts.copy(size = it)
                if (engine.selectedIsText()) engine.updateSelectedTitle(text, opts)
            }
            SliderRow("Tracking", opts.tracking, -10.0, 40.0) {
                opts = opts.copy(tracking = it)
                if (engine.selectedIsText()) engine.updateSelectedTitle(text, opts)
            }
            SliderRow("Line spacing", opts.lineSpacing, 0.8, 2.0) {
                opts = opts.copy(lineSpacing = it)
                if (engine.selectedIsText()) engine.updateSelectedTitle(text, opts)
            }
            SliderRow("Outline", opts.outline, 0.0, 24.0) {
                opts = opts.copy(outline = it)
                if (engine.selectedIsText()) engine.updateSelectedTitle(text, opts)
            }
            SliderRow("Shadow", opts.shadow, 0.0, 40.0) {
                opts = opts.copy(shadow = it)
                if (engine.selectedIsText()) engine.updateSelectedTitle(text, opts)
            }
            SliderRow("Wrap width (0 = off)", opts.boxWidth, 0.0, 2000.0) {
                opts = opts.copy(boxWidth = it)
                if (engine.selectedIsText()) engine.updateSelectedTitle(text, opts)
            }

            Spacer(Modifier.height(6.dp))
            Text("Fill", color = MsColors.textSecondary, fontSize = 12.sp)
            Spacer(Modifier.height(4.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                TitleColors.forEach { (hex, label) ->
                    val selected = opts.color.equals(hex, ignoreCase = true)
                    Box(
                        Modifier
                            .size(30.dp)
                            .background(parseHex(hex), CircleShape)
                            .border(
                                if (selected) 2.dp else 1.dp,
                                if (selected) MsColors.accentSoft else MsColors.surfaceHigh,
                                CircleShape,
                            )
                            .clickable {
                                opts = opts.copy(color = hex)
                                if (engine.selectedIsText()) engine.updateSelectedTitle(text, opts)
                            },
                    )
                    if (selected) {
                        Text(label, color = MsColors.textTertiary, fontSize = 10.sp)
                    }
                }
            }

            Spacer(Modifier.height(10.dp))
            Text("Outline colour", color = MsColors.textSecondary, fontSize = 12.sp)
            Spacer(Modifier.height(4.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                TitleColors.forEach { (hex, _) ->
                    Box(
                        Modifier
                            .size(26.dp)
                            .background(parseHex(hex), CircleShape)
                            .border(
                                if (opts.outlineColor.equals(hex, ignoreCase = true)) 2.dp else 1.dp,
                                if (opts.outlineColor.equals(hex, ignoreCase = true)) MsColors.accentSoft
                                else MsColors.surfaceHigh,
                                CircleShape,
                            )
                            .clickable {
                                opts = opts.copy(outlineColor = hex)
                                if (engine.selectedIsText()) engine.updateSelectedTitle(text, opts)
                            },
                    )
                }
            }

            Spacer(Modifier.height(10.dp))
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("Align", color = MsColors.textSecondary, fontSize = 12.sp, modifier = Modifier.width(56.dp))
                listOf("Left", "Centre", "Right").forEachIndexed { index, label ->
                    TextButton(onClick = {
                        opts = opts.copy(align = index)
                        if (engine.selectedIsText()) engine.updateSelectedTitle(text, opts)
                    }) {
                        Text(
                            label,
                            color = if (opts.align == index) MsColors.accentSoft else MsColors.textSecondary,
                            fontSize = 12.sp,
                        )
                    }
                }
            }
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("Place", color = MsColors.textSecondary, fontSize = 12.sp, modifier = Modifier.width(56.dp))
                listOf("Top", "Middle", "Bottom").forEachIndexed { index, label ->
                    TextButton(onClick = {
                        opts = opts.copy(anchor = index)
                        if (engine.selectedIsText()) engine.updateSelectedTitle(text, opts)
                    }) {
                        Text(
                            label,
                            color = if (opts.anchor == index) MsColors.accentSoft else MsColors.textSecondary,
                            fontSize = 12.sp,
                        )
                    }
                }
            }
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("Uppercase", color = MsColors.textSecondary, fontSize = 12.sp, modifier = Modifier.weight(1f))
                Switch(
                    checked = opts.uppercase,
                    onCheckedChange = {
                        opts = opts.copy(uppercase = it)
                        if (engine.selectedIsText()) engine.updateSelectedTitle(text, opts)
                    },
                )
            }

            if (editing) {
                // Reveal animates the title on: the text is truncated as the clip
                // plays, so the finished frame matches an un-revealed title.
                var revealMode by remember {
                    mutableIntStateOf(
                        engine.textReveal(engine.selectedTrackId, engine.selectedClipId)?.first ?: 0
                    )
                }
                var revealSec by remember {
                    mutableFloatStateOf(
                        engine.textReveal(engine.selectedTrackId, engine.selectedClipId)?.second ?: 1f
                    )
                }
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        "Reveal",
                        color = MsColors.textSecondary,
                        fontSize = 12.sp,
                        modifier = Modifier.width(56.dp),
                    )
                    listOf("None", "Type on", "Word", "Line").forEachIndexed { index, label ->
                        TextButton(onClick = {
                            revealMode = index
                            engine.setTextReveal(
                                engine.selectedTrackId,
                                engine.selectedClipId,
                                index,
                                revealSec,
                            )
                        }) {
                            Text(
                                label,
                                color = if (revealMode == index) MsColors.accentSoft
                                else MsColors.textSecondary,
                                fontSize = 12.sp,
                            )
                        }
                    }
                }
                if (revealMode > 0) {
                    SliderRow("Reveal time", revealSec.toDouble(), 0.2, 6.0) {
                        revealSec = it.toFloat()
                        engine.setTextReveal(
                            engine.selectedTrackId,
                            engine.selectedClipId,
                            revealMode,
                            revealSec,
                        )
                    }
                }
            }

            if (!editing) {
                SliderRow("Title length", duration, 0.5, 15.0) { duration = it }
            }

            Spacer(Modifier.height(14.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                TextButton(
                    enabled = !engine.selectedIsText() && text.isNotBlank(),
                    onClick = {
                        engine.addTitle(text, opts, duration)
                        onDismiss()
                    },
                ) {
                    Text(
                        "Add at playhead",
                        color = if (text.isNotBlank()) MsColors.accentSoft else MsColors.textMuted,
                        fontSize = 13.sp,
                        fontWeight = FontWeight.SemiBold,
                    )
                }
                if (engine.selectedIsText()) {
                    TextButton(onClick = { engine.updateSelectedTitle(text, opts) }) {
                        Text("Re-render", color = MsColors.accentSoft, fontSize = 13.sp)
                    }
                }
            }
        }
    }
}

@Composable
private fun SliderRow(label: String, value: Double, min: Double, max: Double, onChange: (Double) -> Unit) {
    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
        Text(
            label,
            color = MsColors.textSecondary,
            fontSize = 11.sp,
            modifier = Modifier.width(130.dp),
        )
        Slider(
            value = value.coerceIn(min, max).toFloat(),
            valueRange = min.toFloat()..max.toFloat(),
            onValueChange = { onChange(it.toDouble()) },
            modifier = Modifier.weight(1f),
        )
        Text(
            if (max <= 10.0) "%.2f".format(value) else value.toInt().toString(),
            color = MsColors.textTertiary,
            fontSize = 11.sp,
            fontFamily = FontFamily.Monospace,
            modifier = Modifier.width(46.dp),
        )
    }
}

private fun parseHex(hex: String): Color {
    val s = hex.removePrefix("#")
    val v = s.toLongOrNull(16) ?: return Color.White
    return if (s.length == 8) Color(v) else Color(0xFF000000L or v)
}
