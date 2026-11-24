package com.motionstudio.app

import com.motionstudio.app.editor.importClipLength
import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * The native probe returns 0 for containers whose track format has no DURATION
 * entry, which used to make every import a five second clip. The platform
 * (MediaMetadataRetriever) duration has to survive that probe result.
 */
class MediaImportRulesTest {

    @Test
    fun fallbackWinsWhenTheProbeReportsZero() {
        assertEquals(37.5, importClipLength(probedDuration = 0.0, fallbackDuration = 37.5), 1e-9)
    }

    @Test
    fun probeWinsWhenItIsLongerThanTheFallback() {
        assertEquals(12.0, importClipLength(probedDuration = 12.0, fallbackDuration = 3.0), 1e-9)
    }

    @Test
    fun fallbackWinsWhenItIsLongerThanTheProbe() {
        assertEquals(9.25, importClipLength(probedDuration = 5.0, fallbackDuration = 9.25), 1e-9)
    }

    @Test
    fun fiveSecondsRemainsTheLastResort() {
        assertEquals(5.0, importClipLength(probedDuration = 0.0, fallbackDuration = 0.0), 1e-9)
        assertEquals(5.0, importClipLength(probedDuration = 0.01, fallbackDuration = 0.02), 1e-9)
    }
}
