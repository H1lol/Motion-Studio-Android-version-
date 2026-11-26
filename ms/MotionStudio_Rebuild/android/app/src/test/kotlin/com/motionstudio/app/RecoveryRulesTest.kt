package com.motionstudio.app

import com.motionstudio.app.data.autosaveIsRecoverable
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Whether an autosave is offered back on launch. Getting this wrong either hides
 * work the user lost or nags them with a copy of work already saved.
 */
class RecoveryRulesTest {

    @Test
    fun anAutosaveOlderThanTheSaveIsNotOffered() {
        // Saved at 2000, autosaved at 1000: the project file already has this work.
        assertFalse(
            autosaveIsRecoverable(
                autosaveModified = 1_000L,
                savedModified = 2_000L,
                savedExists = true,
            ),
        )
    }

    @Test
    fun anAutosaveNewerThanTheSaveIsOffered() {
        assertTrue(
            autosaveIsRecoverable(
                autosaveModified = 3_000L,
                savedModified = 2_000L,
                savedExists = true,
            ),
        )
    }

    @Test
    fun aSaveAtTheSameInstantCoversTheAutosave() {
        // The save wrote the whole document, so the copy adds nothing.
        assertFalse(
            autosaveIsRecoverable(
                autosaveModified = 4_000L,
                savedModified = 4_000L,
                savedExists = true,
            ),
        )
    }

    @Test
    fun anAutosaveWithoutAProjectFileIsAlwaysOffered() {
        assertTrue(
            autosaveIsRecoverable(
                autosaveModified = 5_000L,
                savedModified = null,
                savedExists = false,
            ),
        )
    }
}
