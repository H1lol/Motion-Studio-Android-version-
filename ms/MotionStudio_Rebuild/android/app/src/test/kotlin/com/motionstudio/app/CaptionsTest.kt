package com.motionstudio.app

import com.motionstudio.app.model.CaptionCue
import com.motionstudio.app.model.Captions
import com.motionstudio.app.model.NativeClip
import com.motionstudio.app.model.NativeTrack
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class CaptionsTest {

    @Test
    fun parsesPlainSrt() {
        val srt = """
            1
            00:00:01,000 --> 00:00:02,500
            First line

            2
            00:00:03,250 --> 00:00:04,000
            Second line
        """.trimIndent()
        val cues = Captions.parse(srt, "a.srt")
        assertEquals(2, cues.size)
        assertEquals(1.0, cues[0].start, 1e-6)
        assertEquals(2.5, cues[0].end, 1e-6)
        assertEquals("First line", cues[0].text)
        assertEquals(3.25, cues[1].start, 1e-6)
    }

    @Test
    fun keepsMultiLineTextAndStripsTags() {
        val srt = """
            1
            00:00:00,500 --> 00:00:02,000
            <i>Hello</i>
            world &amp; all
        """.trimIndent()
        val cues = Captions.parse(srt)
        assertEquals(1, cues.size)
        assertEquals("Hello\nworld & all", cues[0].text)
    }

    @Test
    fun copesWithMissingBlankLineBetweenBlocks() {
        val srt = "1\n00:00:01,000 --> 00:00:02,000\nOne\n2\n00:00:03,000 --> 00:00:04,000\nTwo"
        val cues = Captions.parse(srt)
        assertEquals(2, cues.size)
        assertEquals("One", cues[0].text)
        assertEquals("Two", cues[1].text)
    }

    @Test
    fun parsesVttWithHeaderIdentifiersNotesAndSettings() {
        val vtt = """
            WEBVTT - demo

            NOTE this block is not a cue
            it has two lines

            intro
            00:00:01.000 --> 00:00:02.250 align:middle line:90%
            Spoken line

            00:00:03.000 --> 00:00:04.000
            Second cue
        """.trimIndent()
        assertTrue(Captions.looksLikeVtt(vtt, "x.vtt"))
        val cues = Captions.parse(vtt, "x.vtt")
        assertEquals(2, cues.size)
        assertEquals(1.0, cues[0].start, 1e-6)
        assertEquals(2.25, cues[0].end, 1e-6)
        assertEquals("Spoken line", cues[0].text)
        assertEquals("Second cue", cues[1].text)
    }

    @Test
    fun acceptsHoursAndShortFormTimestamps() {
        assertEquals(3723.5, Captions.parse_timestamp("01:02:03,500")!!, 1e-6)
        assertEquals(63.25, Captions.parse_timestamp("01:03.250")!!, 1e-6)
        assertEquals(1.5, Captions.parse_timestamp("00:00:01.5")!!, 1e-6)
    }

    @Test
    fun rejectsBrokenTimingLines() {
        val junk = """
            1
            not a timestamp
            Hello

            2
            00:00:05,000 --> 00:00:04,000
            Backwards

            3
            00:00:06,000 --> 00:00:07,000
        """.trimIndent()
        val cues = Captions.parse(junk)
        assertTrue("no cue may survive bad timing or empty text", cues.isEmpty())
    }

    @Test
    fun formatsBothTimestampStyles() {
        assertEquals("00:00:01,050", Captions.format_timestamp(1.05, ','))
        assertEquals("01:02:03.500", Captions.format_timestamp(3723.5, '.'))
        assertEquals("00:00:00,000", Captions.format_timestamp(-4.0, ','))
    }

    @Test
    fun srtRoundTripsThroughParser() {
        val cues = listOf(
            CaptionCue(0.5, 2.0, "One"),
            CaptionCue(2.0, 4.25, "Two\nlines"),
        )
        val parsed = Captions.parse(Captions.toSrt(cues), "out.srt")
        assertEquals(cues, parsed)
    }

    @Test
    fun vttRoundTripsThroughParser() {
        val cues = listOf(CaptionCue(1.0, 3.5, "Only"))
        val text = Captions.toVtt(cues)
        assertTrue(text.startsWith("WEBVTT"))
        assertEquals(cues, Captions.parse(text, "out.vtt"))
    }

    @Test
    fun exportsTitlesFromEveryOverlayTrackInTimeOrder() {
        fun title(id: Int, start: Double, duration: Double, text: String) = NativeClip(
            id = id,
            mediaId = 0,
            name = "title",
            start = start,
            duration = duration,
            opacity = 1.0,
            mute = true,
            isText = true,
            text = text,
        )
        val tracks = listOf(
            NativeTrack(
                id = 2, name = "Overlay 1", type = 1, muted = false, locked = false,
                visible = true,
                clips = listOf(title(1, 4.0, 2.0, "Later"), title(2, 0.0, 1.0, "  ")),
            ),
            NativeTrack(
                id = 3, name = "Subtitles", type = 1, muted = false, locked = false,
                visible = true,
                clips = listOf(title(3, 1.0, 1.5, "Earlier")),
            ),
        )
        val cues = Captions.fromTitles(tracks)
        assertEquals(2, cues.size)
        assertEquals("Earlier", cues[0].text)
        assertEquals(1.0, cues[0].start, 1e-6)
        assertEquals(2.5, cues[0].end, 1e-6)
        assertEquals("Later", cues[1].text)
    }
}
