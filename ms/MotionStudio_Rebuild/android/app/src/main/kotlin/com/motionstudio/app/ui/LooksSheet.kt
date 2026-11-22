package com.motionstudio.app.ui

import android.content.Context
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.motionstudio.app.editor.EditorEngine
import com.motionstudio.app.ui.theme.MsColors
import com.motionstudio.app.ui.theme.MsFonts
import java.io.File

/** The `.cube` files bundled in `assets/looks/`, baked by scripts/gen_looks.py. */
data class LookPreset(val asset: String, val name: String, val note: String)

private val Looks = listOf(
    LookPreset("teal_orange", "Teal & Orange", "Blockbuster split tone"),
    LookPreset("bleach_bypass", "Bleach Bypass", "Silver-halide contrast"),
    LookPreset("warm_film", "Warm Film", "Golden hour warm-up"),
    LookPreset("cool_noir", "Cool Noir", "Desaturated blue crush"),
    LookPreset("vivid_punch", "Vivid Punch", "Social-ready saturation"),
    LookPreset("fade_matte", "Fade Matte", "Lifted blacks, soft whites"),
)

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun LooksSheet(engine: EditorEngine, onDismiss: () -> Unit) {
    val context = LocalContext.current
    val sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)
    // Master look (whole sequence) or only the selected clip's own look.
    var clipMode by remember { mutableStateOf(false) }

    LaunchedEffect(engine.selectedClipId) { engine.refreshClipLook() }

    ModalBottomSheet(
        onDismissRequest = onDismiss,
        sheetState = sheetState,
        containerColor = MsColors.surface,
    ) {
        Column(
            Modifier
                .fillMaxWidth()
                .fillMaxHeight(0.92f)
                .padding(horizontal = 20.dp)
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f)) {
                    Text(
                        "Looks",
                        color = MsColors.textPrimary,
                        fontSize = 16.sp,
                        fontWeight = FontWeight.SemiBold,
                    )
                    Spacer(Modifier.height(2.dp))
                    Text(
                        when {
                            clipMode && engine.clipLookPath != null -> "Look on the selected clip"
                            clipMode -> "This clip has no look"
                            engine.lookName != null -> "Look on every frame of the sequence"
                            else -> "No look applied"
                        },
                        color = when {
                            clipMode && engine.clipLookPath != null -> MsColors.success
                            !clipMode && engine.lookName != null -> MsColors.success
                            else -> MsColors.textTertiary
                        },
                        fontSize = 11.sp,
                    )
                }
                TextButton(onClick = {
                    if (clipMode) engine.clearClipLook() else engine.clearLook()
                }) {
                    Text("None", color = MsColors.textTertiary, fontSize = 12.sp)
                }
            }

            Spacer(Modifier.height(4.dp))
            Row {
                TextButton(
                    onClick = { clipMode = false },
                    modifier = Modifier
                        .clip(RoundedCornerShape(8.dp))
                        .background(if (!clipMode) MsColors.accentSoft.copy(alpha = 0.16f) else Color.Transparent),
                ) {
                    Text(
                        "Sequence",
                        color = if (!clipMode) MsColors.accentSoft else MsColors.textSecondary,
                        fontSize = 12.sp,
                    )
                }
                TextButton(
                    onClick = {
                        clipMode = true
                        engine.refreshClipLook()
                    },
                    modifier = Modifier
                        .clip(RoundedCornerShape(8.dp))
                        .background(if (clipMode) MsColors.accentSoft.copy(alpha = 0.16f) else Color.Transparent),
                ) {
                    Text(
                        "Selected clip",
                        color = if (clipMode) MsColors.accentSoft else MsColors.textSecondary,
                        fontSize = 12.sp,
                    )
                }
            }

            val strength = if (clipMode) engine.clipLookStrength else engine.lookStrength
            Row(
                Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                Text(
                    "Strength  ${(strength * 100).toInt()}%",
                    color = MsColors.textSecondary,
                    fontSize = 12.sp,
                    fontFamily = MsFonts.mono,
                )
            }
            Slider(
                value = strength,
                onValueChange = {
                    if (clipMode) engine.updateClipLookStrength(it) else engine.updateLookStrength(it)
                },
                valueRange = 0f..1f,
            )

            LazyColumn(Modifier.fillMaxWidth().weight(1f)) {
                items(Looks) { look ->
                    // The clip look is stored as a resolved path, so match on the
                    // file name rather than holding on to an asset name in state.
                    val active = if (clipMode) {
                        engine.clipLookPath?.endsWith("/${look.asset}.cube") == true
                    } else {
                        engine.lookName == look.name
                    }
                    Row(
                        Modifier
                            .fillMaxWidth()
                            .padding(vertical = 3.dp)
                            .background(
                                if (active) MsColors.accent.copy(alpha = 0.16f) else Color.Transparent,
                                RoundedCornerShape(10.dp),
                            )
                            .padding(horizontal = 10.dp, vertical = 10.dp),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Column(Modifier.fillMaxWidth(0.72f)) {
                            Text(
                                look.name,
                                color = if (active) MsColors.accentSoft else MsColors.textPrimary,
                                fontSize = 14.sp,
                                fontWeight = if (active) FontWeight.SemiBold else FontWeight.Medium,
                            )
                            Text(
                                look.note,
                                color = MsColors.textMuted,
                                fontSize = 11.sp,
                            )
                        }
                        Spacer(Modifier.weight(1f))
                        TextButton(
                            onClick = {
                                val path = ensureLookAsset(context, look.asset)
                                if (path != null) {
                                    if (clipMode) {
                                        engine.applyClipLook(path.absolutePath, engine.clipLookStrength)
                                    } else {
                                        engine.applyLook(path.absolutePath, look.name)
                                    }
                                }
                            },
                        ) {
                            Text(
                                if (active) "Applied" else "Apply",
                                color = if (active) MsColors.success else MsColors.accentSoft,
                                fontSize = 12.sp,
                            )
                        }
                    }
                }
            }
            Spacer(Modifier.height(20.dp))
        }
    }
}

/** Copies the bundled cube out of the APK once; the engine only reads files. */
private fun ensureLookAsset(context: Context, assetName: String): File? {
    val dir = File(context.cacheDir, "looks").apply { mkdirs() }
    val out = File(dir, "$assetName.cube")
    if (out.exists() && out.length() > 0) return out
    return runCatching {
        context.assets.open("looks/$assetName.cube").use { input ->
            out.outputStream().use { output -> input.copyTo(output) }
        }
        out
    }.getOrNull()
}
