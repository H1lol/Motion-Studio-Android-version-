package com.motionstudio.app.model

/**
 * One timed line of dialogue, in project seconds.
 *
 * Cues are the interchange form: SRT and WebVTT both collapse to this, and the
 * timeline collapses to it the other way round (one text clip = one cue).
 */
data class CaptionCue(
    val start: Double,
    val end: Double,
    val text: String,
) {
    val duration: Double get() = end - start
}

/**
 * Subtitle interchange, written from scratch against the two formats' published
 * grammar rather than through a library, because both are small and a phone
 * build should not carry a parser for six lines of text.
 *
 * SRT (SubRip):
 *
 *     1
 *     00:00:01,000 --> 00:00:02,500
 *     Line one
 *     Line two
 *
 * WebVTT:
 *
 *     WEBVTT
 *
 *     00:00:01.000 --> 00:00:02.500 align:middle line:90%
 *     Line one
 *
 * What is tolerated on the way in: a UTF-8 BOM, CRLF or old-Mac line endings,
 * hours omitted (`MM:SS.mmm`), options/settings after the end timestamp, VTT
 * cue identifiers, `NOTE`/`STYLE`/`REGION` blocks, HTML-ish tags inside the cue
 * text (`<i>`, `<c.yellow>`, `<00:00:01.000>`), the five XML entities that
 * actually appear in subtitle files, and blocks with no blank line between
 * them. What is dropped: cues whose end is not after their start, cues with no
 * text, and anything longer than the sanity ceiling.
 *
 * What is not attempted: styling, positioning or karaoke timing. Those do not
 * survive a move into the timeline's text model, and pretending they do would
 * hand back a file that says something the project does not.
 */
object Captions {

    /**
     * Anything past six hours in one cue is a parse error, not a subtitle.
     * 6 h × 3600 s — longer than any programme cut on a phone, short enough that
     * a corrupt timestamp cannot push the timeline end into next week.
     */
    private const val MAX_CUE_SECONDS = 21_600.0

    /** Full form `HH:MM:SS,mmm` (SRT) or `HH:MM:SS.mmm` (VTT). */
    private val FULL_TIMESTAMP =
        Regex("""^(\d{1,3}):(\d{2}):(\d{2})[.,](\d{1,3})$""")

    /** Short form `MM:SS.mmm`, which some VTT writers emit. */
    private val SHORT_TIMESTAMP =
        Regex("""^(\d{1,3}):(\d{2})[.,](\d{1,3})$""")

    private val TAG = Regex("<[^>]*>")

    /** Splits a parsed cue's text on the engine's line break. */
    private const val LINE_BREAK = "\n"

    fun looksLikeVtt(raw: String, fileName: String = ""): Boolean {
        if (fileName.endsWith(".vtt", ignoreCase = true)) return true
        val head = raw.removePrefix("\uFEFF").lineSequence().firstOrNull().orEmpty()
        return head.trim().startsWith("WEBVTT", ignoreCase = true)
    }

    /**
     * Parses SRT or WebVTT into cues in file order.
     *
     * Returns an empty list — never throws — for text that holds no timing line,
     * so a picked `.txt` costs a status message rather than a crash.
     */
    fun parse(raw: String, fileName: String = ""): List<CaptionCue> {
        if (raw.isBlank()) return emptyList()
        val normalized = raw
            .removePrefix("\uFEFF")
            .replace("\r\n", "\n")
            .replace('\r', '\n')

        val cues = ArrayList<CaptionCue>()
        var start = Double.NaN
        var end = Double.NaN
        val body = ArrayList<String>()

        fun flush() {
            val from = start
            val to = end
            if (!from.isNaN() && !to.isNaN() && to > from) {
                val lines = body.map { clean_text(it) }.filter { it.isNotEmpty() }
                if (lines.isNotEmpty()) {
                    val capped = minOf(to, from + MAX_CUE_SECONDS)
                    cues.add(CaptionCue(from, capped, lines.joinToString(LINE_BREAK)))
                }
            }
            start = Double.NaN
            end = Double.NaN
            body.clear()
        }

        val lines = normalized.split('\n')
        lines.forEachIndexed { index, line ->
            val timing = parse_timing_line(line)
            when {
                timing != null -> {
                    flush()
                    start = timing.first
                    end = timing.second
                }
                line.isBlank() -> if (!start.isNaN()) flush()
                // Some writers omit the blank line between blocks. A bare number
                // directly above a timing line is the next cue's index, not the
                // previous cue's last line of dialogue.
                is_index_line(line) && parse_timing_line(lines.getOrNull(index + 1).orEmpty()) != null -> {
                    flush()
                }
                !start.isNaN() -> body.add(line)
                // Before the first timing line: SRT index, WEBVTT header, NOTE /
                // STYLE / REGION blocks, blank separators. Nothing to keep.
                else -> Unit
            }
        }
        flush()
        return cues
    }

    private fun is_index_line(line: String): Boolean {
        val text = line.trim()
        return text.isNotEmpty() && text.all { it.isDigit() }
    }

    /** `start --> end` with optional settings after the end timestamp. */
    private fun parse_timing_line(line: String): Pair<Double, Double>? {
        val arrow = line.indexOf("-->")
        if (arrow < 0) return null
        val left = line.substring(0, arrow).trim()
        val right = line.substring(arrow + 3).trim()
        val start = parse_timestamp(left) ?: return null
        // A VTT cue may carry `align:middle line:90%` after the end time.
        val endToken = right.split(' ', '\t').firstOrNull { it.isNotBlank() } ?: return null
        val end = parse_timestamp(endToken) ?: return null
        return start to end
    }

    /** Accepts `HH:MM:SS,mmm`, `HH:MM:SS.mmm`, `MM:SS.mmm`; null otherwise. */
    fun parse_timestamp(token: String): Double? {
        val text = token.trim()
        if (text.isEmpty()) return null
        FULL_TIMESTAMP.matchEntire(text)?.let { m ->
            val h = m.groupValues[1].toIntOrNull() ?: return null
            val min = m.groupValues[2].toIntOrNull() ?: return null
            val s = m.groupValues[3].toIntOrNull() ?: return null
            val ms = fraction_to_ms(m.groupValues[4])
            if (min > 59 || s > 59) return null
            return h * 3600.0 + min * 60.0 + s + ms / 1000.0
        }
        SHORT_TIMESTAMP.matchEntire(text)?.let { m ->
            val min = m.groupValues[1].toIntOrNull() ?: return null
            val s = m.groupValues[2].toIntOrNull() ?: return null
            val ms = fraction_to_ms(m.groupValues[3])
            if (s > 59) return null
            return min * 60.0 + s + ms / 1000.0
        }
        return null
    }

    /** `5` is 500 ms in a subtitle file, not 5 ms. */
    private fun fraction_to_ms(digits: String): Int = when (digits.length) {
        1 -> digits.toIntOrNull()?.times(100) ?: 0
        2 -> digits.toIntOrNull()?.times(10) ?: 0
        else -> digits.take(3).toIntOrNull() ?: 0
    }

    private fun clean_text(line: String): String {
        val stripped = TAG.replace(line, "")
        return stripped
            .replace("&nbsp;", " ")
            .replace("&lt;", "<")
            .replace("&gt;", ">")
            .replace("&quot;", "\"")
            .replace("&apos;", "'")
            .replace("&amp;", "&")
            .trim()
    }

    /**
     * Serializes cues as SRT, numbering from 1 and using comma decimals.
     * Cues are written in time order so players that trust file order behave.
     */
    fun toSrt(cues: List<CaptionCue>): String {
        val sb = StringBuilder()
        cues.sortedBy { it.start }.forEachIndexed { index, cue ->
            sb.append(index + 1).append('\n')
            sb.append(format_timestamp(cue.start, ',')).append(" --> ")
                .append(format_timestamp(cue.end, ',')).append('\n')
            sb.append(cue.text.trim()).append('\n').append('\n')
        }
        return sb.toString()
    }

    /** Serializes cues as WebVTT: header, then unnumbered cues with dot decimals. */
    fun toVtt(cues: List<CaptionCue>): String {
        val sb = StringBuilder("WEBVTT\n\n")
        cues.sortedBy { it.start }.forEach { cue ->
            sb.append(format_timestamp(cue.start, '.')).append(" --> ")
                .append(format_timestamp(cue.end, '.')).append('\n')
            sb.append(cue.text.trim()).append('\n').append('\n')
        }
        return sb.toString()
    }

    /** `HH:MM:SS<sep>mmm`, clamped at zero and at 999 hours. */
    fun format_timestamp(seconds: Double, separator: Char): String {
        val total = seconds.coerceIn(0.0, 999 * 3600.0 + 59 * 60.0 + 59.999)
        val whole = total.toInt()
        val hours = whole / 3600
        val minutes = (whole % 3600) / 60
        val secs = whole % 60
        val millis = ((total - whole) * 1000.0).toInt().coerceIn(0, 999)
        return "%02d:%02d:%02d%c%03d".format(hours, minutes, secs, separator, millis)
    }

    /**
     * Timeline titles as cues. Every text clip is a caption regardless of which
     * overlay lane it sits on, which is why exporting never depends on the
     * import having created the lane.
     */
    fun fromTitles(tracks: List<NativeTrack>): List<CaptionCue> =
        tracks.asSequence()
            .flatMap { it.clips.asSequence() }
            .filter { it.isText && it.text.isNotBlank() && it.duration > 0.0 }
            .map { CaptionCue(it.start, it.start + it.duration, it.text.trim()) }
            .sortedBy { it.start }
            .toList()
}
