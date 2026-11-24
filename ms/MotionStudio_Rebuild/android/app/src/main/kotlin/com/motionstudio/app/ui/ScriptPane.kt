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
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.motionstudio.app.editor.EditorEngine
import com.motionstudio.app.ui.theme.MsColors
import com.motionstudio.app.ui.theme.MsFonts

private val ScriptProps = listOf(
    0 to "Opacity",
    1 to "Scale",
    2 to "X",
    3 to "Y",
)

/**
 * SCRIPT — expressions over the selected clip's motion channels.
 *
 * Type an expression in `t` (seconds inside the clip) and bake it: the value is
 * sampled once per frame and written as keyframes, so the picture, the export
 * and the saved project all agree without a scripting runtime in the renderer.
 * The presets are the shapes a phone editor is actually asked for — fade, pulse,
 * drift — written out so they double as examples of the syntax.
 */
@Composable
fun ScriptPane(engine: EditorEngine, modifier: Modifier = Modifier) {
    var prop by remember { mutableStateOf(0) }
    var expression by remember { mutableStateOf("clamp(t / 1.0, 0, 1)") }
    var message by remember { mutableStateOf<String?>(null) }

    val clip = engine.doc.clipById(engine.selectedTrackId, engine.selectedClipId)
    val keys = if (clip == null) 0 else engine.keyframeCount(prop)

    Column(modifier.background(MsColors.background)) {
        PanelHeader("EXPRESSIONS", if (clip == null) "NO CLIP" else "${clip.duration.formatShort()}s CLIP")

        Column(
            Modifier.fillMaxWidth().weight(1f).verticalScroll(rememberScrollState())
                .padding(horizontal = 12.dp, vertical = 6.dp)
        ) {
            if (clip == null) {
                Text(
                    "Select a clip on the timeline, then describe how it should move.",
                    color = MsColors.textTertiary,
                    fontSize = 12.sp,
                )
                return@Column
            }

            Row(
                Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()),
                horizontalArrangement = Arrangement.spacedBy(6.dp),
            ) {
                ScriptProps.forEach { (id, label) ->
                    OptionChip(label, id == prop) {
                        prop = id
                        message = null
                    }
                }
            }
            Spacer(Modifier.height(8.dp))

            OutlinedTextField(
                value = expression,
                onValueChange = {
                    expression = it
                    message = null
                },
                modifier = Modifier.fillMaxWidth().heightIn(min = 54.dp),
                textStyle = androidx.compose.ui.text.TextStyle(
                    fontFamily = MsFonts.mono,
                    fontSize = 13.sp,
                    color = MsColors.textPrimary,
                ),
                placeholder = {
                    Text(
                        "0.5 + 0.5*sin(t*2*pi)",
                        color = MsColors.textMuted,
                        fontSize = 12.sp,
                        fontFamily = MsFonts.mono,
                    )
                },
                singleLine = true,
                label = {
                    Text("EXPRESSION · t in seconds", color = MsColors.textTertiary, fontSize = 10.sp)
                },
            )

            Spacer(Modifier.height(6.dp))
            Row(
                Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()),
                horizontalArrangement = Arrangement.spacedBy(6.dp),
            ) {
                Preset("Fade in", "clamp(t / 1.0, 0, 1)") { expression = it }
                Preset("Pulse", "0.5 + 0.5*sin(t*2*pi*1.5)") { expression = it }
                Preset("Drift up", "-0.15*t") { expression = it }
                Preset("Shake", "0.02*sin(t*42)") { expression = it }
                Preset("Pop", "1 + 0.25*sin(t*pi)") { expression = it }
            }

            Spacer(Modifier.height(8.dp))
            Row(verticalAlignment = Alignment.CenterVertically) {
                Action(
                    label = "Bake keys",
                    primary = true,
                    enabled = expression.isNotBlank(),
                ) {
                    val written = engine.bakeExpression(prop, expression)
                    message = if (written > 0) {
                        "$written keys on ${ScriptProps.first { it.first == prop }.second.lowercase()}"
                    } else {
                        engine.status
                    }
                }
                Spacer(Modifier.width(8.dp))
                Action(label = "Clear channel", primary = false, enabled = keys > 0) {
                    if (engine.clearKeyframes(prop)) {
                        message = "Channel cleared"
                    }
                }
                Spacer(Modifier.weight(1f))
                Text(
                    if (keys > 0) "$keys keys" else "no keys",
                    color = if (keys > 0) MsColors.accentSoft else MsColors.textMuted,
                    fontSize = 10.sp,
                    fontFamily = MsFonts.mono,
                )
            }

            Spacer(Modifier.height(6.dp))
            Text(
                message ?: "t = seconds into the clip · fps · pi · sin cos abs min max clamp pow",
                color = if (message == null) MsColors.textMuted else MsColors.info,
                fontSize = 10.sp,
                lineHeight = 14.sp,
            )
        }
    }
}

@Composable
private fun OptionChip(label: String, active: Boolean, onClick: () -> Unit) {
    Box(
        Modifier
            .height(30.dp)
            .clip(RoundedCornerShape(8.dp))
            .background(if (active) MsColors.accentDim else MsColors.surfaceHigh)
            .border(1.dp, if (active) MsColors.accent else MsColors.border, RoundedCornerShape(8.dp))
            .clickable(onClick = onClick)
            .padding(horizontal = 12.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            label,
            color = if (active) MsColors.accentSoft else MsColors.textSecondary,
            fontSize = 11.sp,
            fontWeight = if (active) FontWeight.SemiBold else FontWeight.Normal,
        )
    }
}

@Composable
private fun Preset(label: String, value: String, onPick: (String) -> Unit) {
    Text(
        label,
        color = MsColors.accentSoft,
        fontSize = 10.sp,
        fontFamily = MsFonts.mono,
        modifier = Modifier
            .clip(RoundedCornerShape(6.dp))
            .background(MsColors.surfaceHigh)
            .clickable { onPick(value) }
            .padding(horizontal = 8.dp, vertical = 5.dp),
    )
}

@Composable
private fun Action(
    label: String,
    primary: Boolean,
    enabled: Boolean,
    onClick: () -> Unit,
) {
    Box(
        Modifier
            .heightIn(min = 34.dp)
            .clip(RoundedCornerShape(8.dp))
            .background(if (primary) MsColors.accent else MsColors.surfaceHigh)
            .clickable(enabled = enabled, onClick = onClick)
            .padding(horizontal = 14.dp, vertical = 7.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            label,
            color = when {
                !enabled -> MsColors.textMuted
                primary -> MsColors.background
                else -> MsColors.accentSoft
            },
            fontSize = 12.sp,
            fontWeight = FontWeight.SemiBold,
        )
    }
}

/** "3" for whole seconds, "3.4" otherwise — the panel header is tight. */
private fun Double.formatShort(): String =
    if (this >= 10.0 || this % 1.0 < 0.05) "%.0f".format(this) else "%.1f".format(this)
