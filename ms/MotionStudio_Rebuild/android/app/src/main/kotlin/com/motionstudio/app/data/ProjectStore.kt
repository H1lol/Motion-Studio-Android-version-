package com.motionstudio.app.data

import android.content.Context
import java.io.File

/** An autosave newer than the project file it belongs to, offered back on launch. */
data class RecoveredProject(val file: File, val name: String, val savedAt: Long)

/**
 * True when an autosave holds work the project file does not.
 *
 * A save at the same timestamp still covers the copy: the save writes the whole
 * document, so nothing is lost by discarding the autosave beside it. A missing
 * project file means every autosave is the only copy and must be offered back.
 */
internal fun autosaveIsRecoverable(
    autosaveModified: Long,
    savedModified: Long?,
    savedExists: Boolean,
): Boolean = !savedExists || savedModified == null || autosaveModified > savedModified

/** Project documents are the native `.msproj` JSON, stored app-privately. */
class ProjectStore(context: Context) {

    private val root: File = File(context.filesDir, "projects").apply { mkdirs() }

    fun save(name: String, json: String): File {
        val file = File(root, "${safeName(name)}.msproj")
        file.writeText(json)
        return file
    }

    fun autosave(name: String, json: String): File {
        val file = File(root, "autosave_${safeName(name)}.msproj")
        file.writeText(json)
        return file
    }

    fun read(file: File): String = if (file.exists()) file.readText() else ""

    fun projects(): List<File> =
        root.listFiles { f -> f.isFile && f.extension == "msproj" && !f.name.startsWith("autosave_") }
            ?.sortedByDescending { it.lastModified() }
            ?: emptyList()

    fun autosaveFor(name: String): File = File(root, "autosave_${safeName(name)}.msproj")

    /**
     * Autosaves that are newer than the project they were taken from, newest
     * first. Called on the starter screen: after the OS kills the editor, this
     * is the only way back to the work, since autosaves were written but never
     * offered for recovery.
     */
    fun recoverable(): List<RecoveredProject> {
        val autosaves = root.listFiles { f ->
            f.isFile && f.extension == "msproj" && f.name.startsWith("autosave_")
        } ?: return emptyList()
        return autosaves.mapNotNull { file ->
            val name = file.nameWithoutExtension.removePrefix("autosave_").ifBlank { "Untitled" }
            val saved = File(root, "$name.msproj")
            val savedExists = saved.exists()
            // A normal save after the autosave means the work is already on disk.
            if (autosaveIsRecoverable(
                    file.lastModified(),
                    if (savedExists) saved.lastModified() else null,
                    savedExists,
                )
            ) {
                RecoveredProject(file, name, file.lastModified())
            } else {
                null
            }
        }.sortedByDescending { it.savedAt }
    }

    /** Drops the recovery copy once the project has been saved or discarded. */
    fun clearAutosave(name: String) {
        autosaveFor(name).delete()
    }

    fun delete(file: File) {
        file.delete()
    }

    fun clearAutosaves() {
        root.listFiles { f -> f.name.startsWith("autosave_") }?.forEach { it.delete() }
    }

    fun projectName(file: File): String =
        file.nameWithoutExtension.replace('_', ' ').ifBlank { "Untitled" }

    private fun safeName(name: String): String {
        val cleaned = name.trim().replace(Regex("[^A-Za-z0-9 _-]"), "_")
        return cleaned.ifBlank { "Untitled" }.take(64)
    }
}
