package com.motionstudio.app.ui

import android.widget.Toast
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalClipboardManager
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.motionstudio.app.data.PreviewQuality
import com.motionstudio.app.data.PreviewQualityStore
import com.motionstudio.app.data.SettingsStore
import com.motionstudio.app.diag.Diagnostics
import com.motionstudio.app.native.NativeEngine
import com.motionstudio.app.ui.theme.MsColors
import com.motionstudio.app.ui.theme.MsFonts

private val accents = listOf(
    Color(0xFF9476F5),
    Color(0xFF58B997),
    Color(0xFFE8B45F),
    Color(0xFFE87979),
    Color(0xFF6FA8F5),
)

@Composable
fun SettingsScreen(settings: SettingsStore, onBack: () -> Unit) {
    val context = LocalContext.current
    val clipboard = LocalClipboardManager.current
    var report by remember { mutableStateOf(Diagnostics.report(context)) }
    var lastCrash by remember { mutableStateOf(Diagnostics.lastCrash()) }
    val nativeCrash = remember { Diagnostics.nativeCrash() }

    Column(
        Modifier
            .fillMaxSize()
            .background(MsColors.background)
            .verticalScroll(rememberScrollState()),
    ) {
        Row(
            Modifier.fillMaxWidth().height(60.dp).padding(horizontal = 6.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            IconButton(onClick = onBack) {
                Icon(Icons.AutoMirrored.Filled.ArrowBack, "Back", tint = MsColors.textSecondary)
            }
            Text("Settings", color = MsColors.textPrimary, fontSize = 18.sp, fontWeight = FontWeight.SemiBold)
        }

        Section("Accent") {
            Row(Modifier.padding(horizontal = 16.dp)) {
                accents.forEach { color ->
                    Box(
                        Modifier
                            .padding(end = 12.dp)
                            .size(42.dp)
                            .background(color, CircleShape)
                            .then(
                                if (settings.accent == color) {
                                    Modifier.padding(0.dp)
                                } else Modifier
                            )
                            .clickable { settings.updateAccent(color) },
                        contentAlignment = Alignment.Center,
                    ) {
                        if (settings.accent == color) {
                            Box(Modifier.size(14.dp).background(Color.White, CircleShape))
                        }
                    }
                }
            }
        }

        Section("Projects") {
            Row(
                Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 10.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Column(Modifier.weight(1f)) {
                    Text("Autosave every 10 minutes", color = MsColors.textPrimary, fontSize = 14.sp)
                    Text(
                        "Snapshots are written only while the project has unsaved changes.",
                        color = MsColors.textTertiary,
                        fontSize = 11.sp,
                    )
                }
                Switch(
                    checked = settings.autosaveEnabled,
                    onCheckedChange = { settings.setAutosave(it) },
                )
            }
        }

        Section("Preview") {
            Column(Modifier.padding(horizontal = 16.dp)) {
                Row(
                    Modifier.horizontalScroll(rememberScrollState()),
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    PreviewQuality.entries.forEach { quality ->
                        QualityChip(
                            label = quality.label,
                            selected = PreviewQualityStore.quality == quality,
                            onClick = { PreviewQualityStore.set(quality) },
                        )
                    }
                }
                Spacer(Modifier.height(6.dp))
                Text(
                    PreviewQualityStore.quality.detail +
                        ". Preview quality only changes what you see while editing — export " +
                        "always renders the project canvas in full.",
                    color = MsColors.textTertiary,
                    fontSize = 11.sp,
                )
            }
        }

        Section("Diagnostics") {
            Column(Modifier.padding(horizontal = 16.dp)) {
                InfoRow(
                    "Native engine",
                    if (NativeEngine.available) "loaded" else "FAILED",
                    if (NativeEngine.available) MsColors.textPrimary else MsColors.danger,
                )
                InfoRow("Decoder", Diagnostics.decoderBackend, MsColors.textPrimary)
                InfoRow(
                    "Last crash",
                    if (lastCrash.isBlank()) "none" else "recorded ${lastCrash.lineSequence().count()} lines",
                    if (lastCrash.isBlank()) MsColors.textSecondary else MsColors.warning,
                )
                if (nativeCrash.isNotBlank()) {
                    InfoRow("Native crash", "recorded", MsColors.danger)
                }

                Spacer(Modifier.height(6.dp))
                Text(
                    "If the app crashes or misbehaves, copy this report and send it over. " +
                        "It carries the device, the ABI, whether the engine loaded, and the last " +
                        "things the editor did.",
                    color = MsColors.textMuted,
                    fontSize = 11.sp,
                )
                Spacer(Modifier.height(10.dp))

                Row {
                    Chip("COPY REPORT") {
                        clipboard.setText(AnnotatedString(report))
                        Toast.makeText(context, "Diagnostics copied", Toast.LENGTH_SHORT).show()
                    }
                    Spacer(Modifier.size(10.dp))
                    Chip("REFRESH") {
                        report = Diagnostics.report(context)
                        lastCrash = Diagnostics.lastCrash()
                    }
                    Spacer(Modifier.size(10.dp))
                    Chip("CLEAR CRASHES") {
                        Diagnostics.clearCrashes()
                        report = Diagnostics.report(context)
                        lastCrash = Diagnostics.lastCrash()
                    }
                }

                if (lastCrash.isNotBlank()) {
                    Spacer(Modifier.height(10.dp))
                    Text(
                        lastCrash.take(2500),
                        color = MsColors.textSecondary,
                        fontSize = 10.sp,
                        fontFamily = MsFonts.mono,
                        modifier = Modifier
                            .fillMaxWidth()
                            .heightIn(max = 200.dp)
                            .verticalScroll(rememberScrollState()),
                    )
                }
            }
        }

        Section("Device") {
            Text(
                "Android 10 (API 29) or newer · 4 GB RAM minimum",
                color = MsColors.textSecondary,
                fontSize = 12.sp,
                modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp),
            )
            Text(
                "Composition, effects and audio mixing run in the native C++ engine; " +
                    "the UI is Jetpack Compose.",
                color = MsColors.textMuted,
                fontSize = 11.sp,
                modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp),
            )
        }

        Spacer(Modifier.height(24.dp))
        Text(
            "Motion Studio 2026.1.0",
            color = MsColors.textMuted,
            fontSize = 11.sp,
            modifier = Modifier.padding(horizontal = 16.dp),
        )
        Spacer(Modifier.height(32.dp))
    }
}

@Composable
private fun InfoRow(label: String, value: String, valueColor: Color) {
    Row(
        Modifier.fillMaxWidth().padding(vertical = 3.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(label, color = MsColors.textSecondary, fontSize = 12.sp, modifier = Modifier.weight(1f))
        Text(value, color = valueColor, fontSize = 12.sp, fontFamily = MsFonts.mono)
    }
}

@Composable
private fun QualityChip(label: String, selected: Boolean, onClick: () -> Unit) {
    Text(
        label,
        color = if (selected) MsColors.accentSoft else MsColors.textSecondary,
        fontSize = 12.sp,
        fontWeight = if (selected) FontWeight.Bold else FontWeight.Normal,
        modifier = Modifier
            .heightIn(min = 36.dp)
            .background(if (selected) MsColors.accentDim.copy(alpha = 0.55f) else MsColors.surfaceHigh, RoundedCornerShape(6.dp))
            .border(
                1.dp,
                if (selected) MsColors.accent else MsColors.border,
                RoundedCornerShape(6.dp),
            )
            .clickable(onClick = onClick)
            .padding(horizontal = 14.dp, vertical = 9.dp),
    )
}

@Composable
private fun Chip(label: String, onClick: () -> Unit) {
    Text(
        label,
        color = MsColors.accent,
        fontSize = 11.sp,
        fontWeight = FontWeight.Bold,
        fontFamily = MsFonts.mono,
        modifier = Modifier
            .heightIn(min = 36.dp)
            .clickable(onClick = onClick)
            .background(MsColors.surfaceHigh, CircleShape)
            .padding(horizontal = 14.dp, vertical = 9.dp),
    )
}

@Composable
private fun Section(title: String, content: @Composable () -> Unit) {
    Column(Modifier.fillMaxWidth().padding(horizontal = 16.dp)) {
        SheetSection(title)
    }
    content()
}
