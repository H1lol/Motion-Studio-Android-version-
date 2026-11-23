package com.motionstudio.app.model

import org.json.JSONObject

data class NativeMedia(
    val id: Int,
    val name: String,
    val path: String,
    val duration: Double,
    val type: Int,
)

data class NativeEffect(
    val id: String,
    val enabled: Boolean,
    val params: Map<String, Double>,
)

data class NativeClip(
    val id: Int,
    val mediaId: Int,
    val name: String,
    val start: Double,
    val duration: Double,
    val opacity: Double,
    val mute: Boolean,
    /** Play speed; the clip's duration on the timeline already accounts for it. */
    val speed: Double = 1.0,
    /** Adjustment layer: no pixels of its own, effects run on the tracks below. */
    val isAdjustment: Boolean = false,
    /** Control layer: invisible, exists to drive the clips parented to it. */
    val isNull: Boolean = false,
    /** Parent clip id (0 = none). Parent translation and scale compose into this clip. */
    val parent: Int = 0,
    val effects: List<NativeEffect> = emptyList(),
    val keyframes: Map<String, Int> = emptyMap(),
    val isText: Boolean = false,
    val text: String = "",
    val textFont: String = "",
    val textSize: Double = 0.0,
) {
    val end: Double get() = start + duration
}

data class NativeTransition(
    val id: Int,
    val outgoing: Int,
    val incoming: Int,
    val type: Int,
    val duration: Double,
)

data class NativeMarker(
    val id: Int,
    val time: Double,
    val name: String,
)

data class NativeTrack(
    val id: Int,
    val name: String,
    val type: Int,
    val muted: Boolean,
    val locked: Boolean,
    val visible: Boolean,
    val clips: List<NativeClip>,
    val transitions: List<NativeTransition> = emptyList(),
    /** Timeline fader, linear (1 = unity). Read by the C++ audio mix. */
    val gain: Double = 1.0,
    val solo: Boolean = false,
)

/**
 * Read-only mirror of the native project JSON. Kotlin never owns the document:
 * it is serialized by C++ and re-parsed here for drawing the timeline. Saves go
 * back through the native engine, so there is exactly one source of truth.
 */
data class ProjectDoc(
    val name: String,
    val width: Int,
    val height: Int,
    val fps: Double,
    val duration: Double,
    val media: List<NativeMedia>,
    val tracks: List<NativeTrack>,
    val raw: String,
    val markers: List<NativeMarker> = emptyList(),
) {
    val aspectRatio: Float
        get() = if (height <= 0) 16f / 9f else width.toFloat() / height.toFloat()

    val frameCount: Int
        get() = (timelineEnd * fps).toInt().coerceAtLeast(1)

    /** Longest of the declared duration and the last clip end. */
    val timelineEnd: Double
        get() {
            val lastClip = tracks.flatMap { it.clips }.maxOfOrNull { it.end } ?: 0.0
            return maxOf(duration, lastClip, 1.0)
        }

    fun clipById(trackId: Int, clipId: Int): NativeClip? =
        tracks.firstOrNull { it.id == trackId }?.clips?.firstOrNull { it.id == clipId }

    companion object {
        private fun parseKeyframeCounts(clip: JSONObject): Map<String, Int> {
            val motion = clip.optJSONObject("motion") ?: return emptyMap()
            val out = LinkedHashMap<String, Int>()
            listOf("keys_opacity", "keys_scale", "keys_pos_x", "keys_pos_y").forEach { key ->
                val arr = motion.optJSONArray(key)
                if (arr != null && arr.length() > 0) out[key] = arr.length()
            }
            return out
        }

        private fun parseEffects(clip: JSONObject): List<NativeEffect> {
            val arr = clip.optJSONArray("effects") ?: return emptyList()
            val out = ArrayList<NativeEffect>(arr.length())
            for (i in 0 until arr.length()) {
                val e = arr.optJSONObject(i) ?: continue
                val id = e.optString("id")
                if (id.isEmpty()) continue
                val params = LinkedHashMap<String, Double>()
                val parr = e.optJSONArray("params")
                if (parr != null) {
                    for (p in 0 until parr.length()) {
                        val po = parr.optJSONObject(p) ?: continue
                        val name = po.optString("name")
                        if (name.isNotEmpty()) params[name] = po.optDouble("value", 0.0)
                    }
                }
                out.add(NativeEffect(id = id, enabled = e.optBoolean("enabled", true), params = params))
            }
            return out
        }

        fun empty(name: String = "Untitled"): ProjectDoc = ProjectDoc(
            name = name,
            width = 1920,
            height = 1080,
            fps = 30.0,
            duration = 15.0,
            media = emptyList(),
            tracks = emptyList(),
            raw = "{}",
        )

        fun parse(json: String): ProjectDoc {
            if (json.isBlank()) return empty()
            return try {
                val root = JSONObject(json)
                val mediaArray = root.optJSONArray("media")
                val media = buildList {
                    if (mediaArray != null) {
                        for (i in 0 until mediaArray.length()) {
                            val m = mediaArray.optJSONObject(i) ?: continue
                            add(
                                NativeMedia(
                                    id = m.optInt("id"),
                                    name = m.optString("name"),
                                    path = m.optString("path"),
                                    duration = m.optDouble("duration", 0.0),
                                    type = m.optInt("type"),
                                )
                            )
                        }
                    }
                }
                val markers = buildList {
                    val arr = root.optJSONArray("markers")
                    if (arr != null) {
                        for (i in 0 until arr.length()) {
                            val m = arr.optJSONObject(i) ?: continue
                            add(
                                NativeMarker(
                                    id = m.optInt("id"),
                                    time = m.optDouble("time", 0.0),
                                    name = m.optString("name"),
                                )
                            )
                        }
                    }
                }
                val tracksArray = root.optJSONArray("tracks")
                val tracks = buildList {
                    if (tracksArray != null) {
                        for (i in 0 until tracksArray.length()) {
                            val t = tracksArray.optJSONObject(i) ?: continue
                            val clipsArray = t.optJSONArray("clips")
                            val clips = buildList {
                                if (clipsArray != null) {
                                    for (c in 0 until clipsArray.length()) {
                                        val clip = clipsArray.optJSONObject(c) ?: continue
                                        add(
                                            NativeClip(
                                                id = clip.optInt("id"),
                                                mediaId = clip.optInt("media_id"),
                                                name = clip.optString("name"),
                                                start = clip.optDouble("start", 0.0),
                                                duration = clip.optDouble("duration", 0.0),
                                                opacity = clip.optDouble("opacity", 1.0),
                                                mute = clip.optBoolean("mute", false),
                                                speed = clip.optDouble("speed", 1.0),
                                                isAdjustment = clip.optBoolean("adj", false),
                                                isNull = clip.optBoolean("null", false),
                                                parent = clip.optInt("parent", 0),
                                                effects = parseEffects(clip),
                                                keyframes = parseKeyframeCounts(clip),
                                                isText = clip.optBoolean("is_text", false),
                                                text = clip.optString("tx_text"),
                                                textFont = clip.optString("tx_font"),
                                                textSize = clip.optDouble("tx_size", 0.0),
                                            )
                                        )
                                    }
                                }
                            }
                            val transitions = buildList {
                                val arr = t.optJSONArray("transitions")
                                if (arr != null) {
                                    for (x in 0 until arr.length()) {
                                        val xo = arr.optJSONObject(x) ?: continue
                                        add(
                                            NativeTransition(
                                                id = xo.optInt("id"),
                                                outgoing = xo.optInt("outgoing"),
                                                incoming = xo.optInt("incoming"),
                                                type = xo.optInt("type", 1),
                                                duration = xo.optDouble("duration", 0.5),
                                            )
                                        )
                                    }
                                }
                            }
                            add(
                                NativeTrack(
                                    id = t.optInt("id"),
                                    name = t.optString("name", "Track"),
                                    type = t.optInt("type"),
                                    muted = t.optBoolean("muted", false),
                                    locked = t.optBoolean("locked", false),
                                    visible = t.optBoolean("visible", true),
                                    clips = clips,
                                    transitions = transitions,
                                    gain = t.optDouble("gain", 1.0),
                                    solo = t.optBoolean("solo", false),
                                )
                            )
                        }
                    }
                }
                ProjectDoc(
                    name = root.optString("name", "Untitled"),
                    width = root.optInt("width", 1920),
                    height = root.optInt("height", 1080),
                    fps = root.optDouble("fps", 30.0),
                    duration = root.optDouble("duration", 15.0),
                    media = media,
                    tracks = tracks,
                    raw = json,
                    markers = markers,
                )
            } catch (t: Throwable) {
                empty()
            }
        }
    }
}
