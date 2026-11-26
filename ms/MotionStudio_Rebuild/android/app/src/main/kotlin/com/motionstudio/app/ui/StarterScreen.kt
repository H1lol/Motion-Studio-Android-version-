package com.motionstudio.app.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.DeleteOutline
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableDoubleStateOf
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
import com.motionstudio.app.data.ProjectStore
import com.motionstudio.app.native.NativeEngine
import com.motionstudio.app.ui.theme.MsColors
import com.motionstudio.app.ui.theme.MsFonts
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

private val resolutions = listOf(
    Triple("1920 × 1080", 1920 to 1080, "Landscape"),
    Triple("1080 × 1920", 1080 to 1920, "Portrait"),
    Triple("1080 × 1080", 1080 to 1080, "Square"),
    Triple("3840 × 2160", 3840 to 2160, "4K UHD"),
)

@Composable
fun StarterScreen(
    store: ProjectStore,
    onOpenProject: (File) -> Unit,
    onRestoreProject: (File) -> Unit,
    onCreateProject: (String, Int, Int, Double) -> Unit,
    onSettings: () -> Unit,
) {
    var showNew by remember { mutableStateOf(false) }
    var refreshKey by remember { mutableIntStateOf(0) }
    val projects = remember(refreshKey) { store.projects() }
    // Autosaves newer than their project file: the work the OS killed before it
    // could be saved. They are offered back before the project list.
    val recovered = remember(refreshKey) { store.recoverable() }

    Column(
        Modifier
            .fillMaxSize()
            .background(MsColors.background),
    ) {
        // ---- identity strip -------------------------------------------------
        Row(
            Modifier.fillMaxWidth().height(52.dp).background(MsColors.surface)
                .padding(horizontal = 14.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            // The mark, not a swatch: same ring and letters as the launcher icon.
            Box(
                Modifier.size(26.dp)
                    .background(MsColors.background, RoundedCornerShape(7.dp))
                    .border(1.5.dp, MsColors.accent, RoundedCornerShape(7.dp)),
                contentAlignment = Alignment.Center,
            ) {
                Text(
                    "MS",
                    color = MsColors.accent,
                    fontSize = 11.sp,
                    fontFamily = MsFonts.sans,
                    fontWeight = FontWeight.Bold,
                )
            }
            Spacer(Modifier.width(10.dp))
            Column(Modifier.weight(1f)) {
                Text(
                    "MOTION STUDIO",
                    color = MsColors.textPrimary,
                    fontSize = 12.sp,
                    fontFamily = MsFonts.mono,
                    fontWeight = FontWeight.Bold,
                    letterSpacing = 1.5.sp,
                )
                Text(
                    "COMPOSITOR  ·  KOTLIN + C++  ·  ${NativeEngine.nativeDecoderBackend().uppercase()}",
                    color = MsColors.textMuted,
                    fontSize = 8.sp,
                    fontFamily = MsFonts.mono,
                    letterSpacing = 0.8.sp,
                )
            }
            IconButton(onClick = onSettings, modifier = Modifier.size(36.dp)) {
                Icon(Icons.Filled.Settings, "Settings", tint = MsColors.textSecondary)
            }
        }
        Box(Modifier.fillMaxWidth().height(1.dp).background(MsColors.border))

        Column(
            Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 14.dp, vertical = 14.dp),
        ) {
            Text(
                "Motion design and\ncompositing, on device.",
                color = MsColors.textPrimary,
                fontSize = 26.sp,
                lineHeight = 30.sp,
                fontWeight = FontWeight.Bold,
            )
            Spacer(Modifier.height(6.dp))
            Text(
                "Node chain, keyframes, grade and scopes — computed by the native engine, " +
                    "no cloud render.",
                color = MsColors.textTertiary,
                fontSize = 12.sp,
                lineHeight = 17.sp,
            )

            Spacer(Modifier.height(18.dp))
            SectionLabel("NEW COMPOSITION")
            Spacer(Modifier.height(6.dp))
            Row(
                Modifier
                    .fillMaxWidth()
                    .background(MsColors.surface, RoundedCornerShape(2.dp))
                    .border(1.dp, MsColors.border, RoundedCornerShape(2.dp))
                    .clickable { showNew = true }
                    .padding(horizontal = 12.dp, vertical = 14.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Box(
                    Modifier.size(26.dp).border(1.dp, MsColors.accent, RoundedCornerShape(2.dp)),
                    contentAlignment = Alignment.Center,
                ) {
                    Icon(Icons.Filled.Add, "New project", tint = MsColors.accent, modifier = Modifier.size(16.dp))
                }
                Spacer(Modifier.width(12.dp))
                Column(Modifier.weight(1f)) {
                    Text(
                        "New project",
                        color = MsColors.textPrimary,
                        fontSize = 14.sp,
                        fontWeight = FontWeight.Medium,
                    )
                    Text(
                        "1080p · 4K  ·  24 / 30 / 60 fps",
                        color = MsColors.textMuted,
                        fontSize = 9.sp,
                        fontFamily = MsFonts.mono,
                    )
                }
                Text("›", color = MsColors.accent, fontSize = 18.sp)
            }

            if (recovered.isNotEmpty()) {
                Spacer(Modifier.height(20.dp))
                SectionLabel("UNSAVED WORK", "${recovered.size}")
                Spacer(Modifier.height(6.dp))
                recovered.forEach { entry ->
                    Row(
                        Modifier
                            .fillMaxWidth()
                            .padding(bottom = 6.dp)
                            .background(MsColors.surface, RoundedCornerShape(2.dp))
                            .border(1.dp, MsColors.warning, RoundedCornerShape(2.dp))
                            .padding(horizontal = 12.dp, vertical = 10.dp),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Column(Modifier.weight(1f)) {
                            Text(
                                entry.name,
                                color = MsColors.textPrimary,
                                fontSize = 13.sp,
                                fontWeight = FontWeight.Medium,
                            )
                            Text(
                                "autosaved " + SimpleDateFormat(
                                    "d MMM HH:mm", Locale.getDefault(),
                                ).format(Date(entry.savedAt)),
                                color = MsColors.textMuted,
                                fontSize = 9.sp,
                                fontFamily = MsFonts.mono,
                            )
                        }
                        TextButton(onClick = {
                            refreshKey++
                            onRestoreProject(entry.file)
                        }) {
                            Text("Recover", color = MsColors.accentSoft, fontSize = 12.sp)
                        }
                        TextButton(onClick = {
                            store.clearAutosave(entry.name)
                            refreshKey++
                        }) {
                            Text("Discard", color = MsColors.textTertiary, fontSize = 12.sp)
                        }
                    }
                }
            }

            Spacer(Modifier.height(20.dp))
            SectionLabel("PROJECTS", if (projects.isEmpty()) "0" else "${projects.size}")
            Spacer(Modifier.height(6.dp))

            if (projects.isEmpty()) {
                Box(
                    Modifier
                        .fillMaxWidth()
                        .border(1.dp, MsColors.borderSoft, RoundedCornerShape(2.dp))
                        .padding(horizontal = 12.dp, vertical = 16.dp),
                ) {
                    Text(
                        "No projects yet. Saved projects live in app storage and never leave the device.",
                        color = MsColors.textMuted,
                        fontSize = 11.sp,
                    )
                }
            } else {
                projects.forEach { file ->
                    ProjectRow(
                        file = file,
                        name = store.projectName(file),
                        onOpen = { onOpenProject(file) },
                        onDelete = {
                            store.delete(file)
                            refreshKey++
                        },
                    )
                }
            }

            Spacer(Modifier.height(28.dp))
            Text(
                "MOTION STUDIO 2026.1.0  ·  ENGINE native-cpp  ·  " +
                    NativeEngine.nativeDecoderBackend(),
                color = MsColors.textMuted,
                fontSize = 8.sp,
                fontFamily = MsFonts.mono,
                letterSpacing = 0.6.sp,
            )
        }
    }

    if (showNew) {
        NewProjectDialog(
            onDismiss = { showNew = false },
            onCreate = { name, width, height, fps ->
                showNew = false
                onCreateProject(name, width, height, fps)
            },
        )
    }
}

@Composable
private fun SectionLabel(text: String, trailing: String? = null) {
    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
        Text(
            text,
            color = MsColors.textMuted,
            fontSize = 9.sp,
            fontFamily = MsFonts.mono,
            letterSpacing = 1.2.sp,
        )
        Spacer(Modifier.width(8.dp))
        Box(Modifier.weight(1f).height(1.dp).background(MsColors.borderSoft))
        if (trailing != null) {
            Spacer(Modifier.width(8.dp))
            Text(trailing, color = MsColors.textMuted, fontSize = 9.sp, fontFamily = MsFonts.mono)
        }
    }
}

@Composable
private fun ProjectRow(file: File, name: String, onOpen: () -> Unit, onDelete: () -> Unit) {
    val stamp = remember(file.lastModified()) {
        SimpleDateFormat("yyyy-MM-dd HH:mm", Locale.getDefault()).format(Date(file.lastModified()))
    }
    val size = remember(file.length()) { "%.0f KB".format(file.length() / 1024.0) }
    Row(
        Modifier
            .fillMaxWidth()
            .padding(vertical = 3.dp)
            .background(MsColors.surface, RoundedCornerShape(2.dp))
            .border(1.dp, MsColors.borderSoft, RoundedCornerShape(2.dp))
            .clickable { onOpen() },
        verticalAlignment = Alignment.CenterVertically,
    ) {
        // Accent spine: reads as a selected-layer marker rather than a card.
        Box(Modifier.width(2.dp).height(46.dp).background(MsColors.accent))
        Spacer(Modifier.width(12.dp))
        Column(Modifier.weight(1f)) {
            Text(
                name,
                color = MsColors.textPrimary,
                fontSize = 13.sp,
                fontWeight = FontWeight.Medium,
                maxLines = 1,
            )
            Text(
                "$stamp  ·  $size",
                color = MsColors.textMuted,
                fontSize = 9.sp,
                fontFamily = MsFonts.mono,
            )
        }
        IconButton(onClick = onDelete, modifier = Modifier.size(38.dp)) {
            Icon(
                Icons.Filled.DeleteOutline,
                "Delete project",
                tint = MsColors.textMuted,
                modifier = Modifier.size(18.dp),
            )
        }
    }
}

@Composable
private fun NewProjectDialog(
    onDismiss: () -> Unit,
    onCreate: (String, Int, Int, Double) -> Unit,
) {
    var name by remember { mutableStateOf("Untitled Project") }
    var resolution by remember { mutableIntStateOf(0) }
    var fps by remember { mutableDoubleStateOf(30.0) }

    AlertDialog(
        onDismissRequest = onDismiss,
        containerColor = MsColors.surface,
        title = {
            Text(
                "NEW COMPOSITION",
                color = MsColors.textPrimary,
                fontSize = 12.sp,
                fontFamily = MsFonts.mono,
                letterSpacing = 1.sp,
            )
        },
        text = {
            Column {
                OutlinedTextField(
                    value = name,
                    onValueChange = { name = it },
                    label = { Text("Name") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                )
                Spacer(Modifier.height(12.dp))
                Text(
                    "FORMAT",
                    color = MsColors.textMuted,
                    fontSize = 9.sp,
                    fontFamily = MsFonts.mono,
                    letterSpacing = 1.sp,
                )
                Spacer(Modifier.height(4.dp))
                Column {
                    resolutions.forEachIndexed { index, item ->
                        val active = index == resolution
                        Row(
                            Modifier
                                .fillMaxWidth()
                                .padding(vertical = 2.dp)
                                .border(
                                    1.dp,
                                    if (active) MsColors.accent else MsColors.borderSoft,
                                    RoundedCornerShape(2.dp),
                                )
                                .clickable { resolution = index }
                                .padding(horizontal = 10.dp, vertical = 9.dp),
                            verticalAlignment = Alignment.CenterVertically,
                        ) {
                            Text(
                                item.first,
                                color = if (active) MsColors.textPrimary else MsColors.textSecondary,
                                fontSize = 12.sp,
                                fontFamily = MsFonts.mono,
                                modifier = Modifier.weight(1f),
                            )
                            Text(
                                item.third,
                                color = MsColors.textMuted,
                                fontSize = 9.sp,
                                fontFamily = MsFonts.mono,
                            )
                        }
                    }
                }
                Spacer(Modifier.height(12.dp))
                Text(
                    "FRAME RATE",
                    color = MsColors.textMuted,
                    fontSize = 9.sp,
                    fontFamily = MsFonts.mono,
                    letterSpacing = 1.sp,
                )
                Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    listOf(24.0, 30.0, 60.0).forEach { option ->
                        val active = fps == option
                        Box(
                            Modifier
                                .padding(top = 4.dp)
                                .border(
                                    1.dp,
                                    if (active) MsColors.accent else MsColors.borderSoft,
                                    RoundedCornerShape(2.dp),
                                )
                                .clickable { fps = option }
                                .padding(horizontal = 10.dp, vertical = 6.dp),
                        ) {
                            Text(
                                "${option.toInt()} fps",
                                color = if (active) MsColors.accentSoft else MsColors.textTertiary,
                                fontSize = 11.sp,
                                fontFamily = MsFonts.mono,
                            )
                        }
                    }
                }
            }
        },
        confirmButton = {
            TextButton(
                onClick = {
                    val (w, h) = resolutions[resolution].second
                    onCreate(name.trim().ifBlank { "Untitled Project" }, w, h, fps)
                }
            ) { Text("Create", color = MsColors.accent) }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) {
                Text("Cancel", color = MsColors.textTertiary)
            }
        },
    )
}
