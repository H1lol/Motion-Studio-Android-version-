package com.motionstudio.app.export

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Rect
import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.media.MediaMuxer
import com.motionstudio.app.diag.Diagnostics
import com.motionstudio.app.editor.EditorEngine
import com.motionstudio.app.native.NativeEngine
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Renders the timeline out of the C++ engine and encodes it with the device
 * codec, muxing the engine's mixed PCM as AAC — no ffmpeg CLI.
 *
 * Video frames and audio spans are fed from one loop. Both output formats must
 * be known before the muxer can start, so any sample produced earlier is
 * buffered briefly rather than dropped.
 *
 * [ExportSettings] decides the delivery: size (the composited frame is scaled,
 * never stretched), frame rate, bitrate, codec and in/out range. The project
 * canvas itself is untouched, so exporting 60p from a 24p project is safe.
 */
class VideoExporter(private val engine: EditorEngine) {

    data class Result(
        val file: File?,
        val frames: Int,
        val hasAudio: Boolean,
        val error: String?,
        val note: String? = null,
        /** The caller asked to stop; the partial file was deleted. */
        val cancelled: Boolean = false,
    )

    private class Pending(val track: Int, val data: ByteArray, val ptsUs: Long, val flags: Int)

    /**
     * [isCancelled] is polled once per frame. A cancel stops the loop, deletes
     * the partial file and still runs the cleanup that restores proxies, the
     * preview scale and the playhead — an abandoned encode must not leave the
     * editor in export mode.
     */
    fun export(
        outFile: File,
        settings: ExportSettings,
        onProgress: (Float) -> Unit = {},
        isCancelled: () -> Boolean = { false },
    ): Result {
        val width = settings.evenWidth
        val height = settings.evenHeight
        val fps = settings.saneFps
        val projectW = (engine.doc.width / 2 * 2).coerceAtLeast(2)
        val projectH = (engine.doc.height / 2 * 2).coerceAtLeast(2)
        val timelineEnd = engine.doc.timelineEnd
        val start = settings.startSec.coerceIn(0.0, timelineEnd)
        val end = settings.resolvedEnd(timelineEnd)
        val duration = (end - start).coerceAtLeast(1.0 / fps)
        val totalFrames = (duration * fps).toInt().coerceIn(1, 240 * 60 * 60)
        val sampleRate = 48_000
        val channels = 2
        val videoBitrate = (settings.bitrateKbps.coerceIn(500, 120_000)) * 1000
        val audioBitrate = settings.audioBitrateKbps.coerceIn(64, 320) * 1000

        // Resolve the encoder first: a device without HEVC still exports H.264
        // instead of failing the whole job.
        var codec = settings.codec
        var note: String? = null
        val firstEncoder = runCatching { MediaCodec.createEncoderByType(codec.mime) }.getOrElse {
            if (codec == ExportSettings.Codec.HEVC) {
                codec = ExportSettings.Codec.H264
                note = "This device has no HEVC encoder — encoded H.264 instead."
                MediaCodec.createEncoderByType(codec.mime)
            } else {
                throw it
            }
        }

        val wasPlaying = engine.playing
        val resumeTime = engine.time
        if (wasPlaying) engine.togglePlay()
        // Delivery must come from the source files, never from a preview proxy.
        // The engine reopens its decoders when this flips, and the finally block
        // puts proxies back for the preview.
        val proxiesWereOn = NativeEngine.available && NativeEngine.nativeProxySetEnabled(false)
        // Delivery is never reduced by the preview-quality setting; the finally
        // block puts the editor's preview scale back.
        val previewScaleWas = NativeEngine.nativePreviewScale()
        if (previewScaleWas < 0.999f) NativeEngine.nativePreviewScaleSet(1f)

        var videoCodec: MediaCodec? = firstEncoder
        var audioCodec: MediaCodec? = null
        var muxer: MediaMuxer? = null
        var videoTrack = -1
        var audioTrack = -1
        var muxerStarted = false
        var framesWritten = 0
        var audioMuxed = false
        val pending = ArrayList<Pending>()
        val info = MediaCodec.BufferInfo()

        try {
            val videoFormat = MediaFormat.createVideoFormat(codec.mime, width, height).apply {
                setInteger(
                    MediaFormat.KEY_COLOR_FORMAT,
                    MediaCodecInfo.CodecCapabilities.COLOR_FormatYUV420SemiPlanar,
                )
                setInteger(MediaFormat.KEY_BIT_RATE, videoBitrate)
                setInteger(MediaFormat.KEY_FRAME_RATE, fps.toInt().coerceAtLeast(1))
                setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 1)
            }
            firstEncoder.configure(videoFormat, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
            firstEncoder.start()

            val audioFormat = MediaFormat.createAudioFormat(
                MediaFormat.MIMETYPE_AUDIO_AAC, sampleRate, channels,
            ).apply {
                setInteger(MediaFormat.KEY_AAC_PROFILE, MediaCodecInfo.CodecProfileLevel.AACObjectLC)
                setInteger(MediaFormat.KEY_BIT_RATE, audioBitrate)
                setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, 32_768)
            }
            audioCodec = runCatching {
                MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_AUDIO_AAC).also {
                    it.configure(audioFormat, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
                    it.start()
                }
            }.getOrNull()

            muxer = MediaMuxer(outFile.absolutePath, MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4)

            val sourceBitmap = Bitmap.createBitmap(projectW, projectH, Bitmap.Config.ARGB_8888)
            val outBitmap = if (projectW == width && projectH == height) sourceBitmap
            else Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
            val outCanvas = if (outBitmap === sourceBitmap) null else Canvas(outBitmap)
            val scalePaint = Paint(Paint.FILTER_BITMAP_FLAG or Paint.ANTI_ALIAS_FLAG)
            val srcRect = Rect(0, 0, projectW, projectH)
            val dstRect = Rect(0, 0, width, height)
            val pixels = IntArray(width * height)
            val yuv = ByteArray(width * height * 3 / 2)
            val frameDurationUs = (1_000_000.0 / fps).toLong().coerceAtLeast(1L)
            val audioChunk = (sampleRate / fps).toInt().coerceAtLeast(256)

            fun renderFrame(t: Double): Boolean {
                engine.seek(t)
                if (!NativeEngine.nativeRenderFrame()) return false
                if (!NativeEngine.nativeCopyFrame(sourceBitmap)) return false
                if (outCanvas != null) {
                    outCanvas.drawColor(Color.BLACK)
                    outCanvas.drawBitmap(sourceBitmap, srcRect, dstRect, scalePaint)
                }
                outBitmap.getPixels(pixels, 0, width, 0, 0, width, height)
                return true
            }

            fun writeSample(track: Int, codec: MediaCodec, index: Int) {
                val buffer = codec.getOutputBuffer(index)
                if (buffer == null || info.size <= 0) return
                if ((info.flags and MediaCodec.BUFFER_FLAG_CODEC_CONFIG) != 0) return
                buffer.position(info.offset)
                buffer.limit(info.offset + info.size)
                if (muxerStarted) {
                    muxer.writeSampleData(track, buffer, info)
                } else {
                    val bytes = ByteArray(info.size)
                    buffer.get(bytes)
                    pending.add(Pending(track, bytes, info.presentationTimeUs, info.flags))
                }
            }

            /** Drains everything available. Returns true when end-of-stream was seen. */
            fun drain(codec: MediaCodec, isVideo: Boolean, track: () -> Int): Boolean {
                var eos = false
                while (true) {
                    val index = codec.dequeueOutputBuffer(info, 0L)
                    if (index == MediaCodec.INFO_TRY_AGAIN_LATER) break
                    if (index == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                        if (!muxerStarted) {
                            val added = muxer.addTrack(codec.outputFormat)
                            if (isVideo) videoTrack = added else audioTrack = added
                        }
                        continue
                    }
                    if (index < 0) continue
                    writeSample(track(), codec, index)
                    if (info.size > 0) {
                        if (isVideo) framesWritten++ else audioMuxed = true
                    }
                    eos = eos || (info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0
                    codec.releaseOutputBuffer(index, false)
                }
                return eos
            }

            fun startMuxerIfReady() {
                if (muxerStarted || videoTrack < 0) return
                if (audioCodec != null && audioTrack < 0) return
                muxer.start()
                muxerStarted = true
                for (p in pending) {
                    val direct = ByteBuffer.allocateDirect(p.data.size)
                    direct.put(p.data).flip()
                    val bi = MediaCodec.BufferInfo()
                    bi.set(0, p.data.size, p.ptsUs, p.flags)
                    muxer.writeSampleData(p.track, direct, bi)
                }
                pending.clear()
            }

            var framesQueued = 0
            var audioFed = 0L
            var audioEos = audioCodec == null
            var videoEosQueued = false
            var videoDone = false
            val pcm = FloatArray(audioChunk * channels)
            val audioBytes = ByteArray(audioChunk * channels * 2)

            while (!videoDone) {
                if (isCancelled()) {
                    runCatching { outFile.delete() }
                    Diagnostics.note("export cancelled after $framesWritten frame(s)")
                    return Result(
                        null, framesWritten, audioMuxed, null, note, cancelled = true,
                    )
                }

                // --- video input
                if (framesQueued < totalFrames) {
                    val inIndex = firstEncoder.dequeueInputBuffer(5_000L)
                    if (inIndex >= 0) {
                        val buffer = firstEncoder.getInputBuffer(inIndex)!!
                        val t = start + framesQueued.toDouble() / fps
                        if (!renderFrame(t)) {
                            return Result(
                                null, framesQueued, audioMuxed,
                                "Engine could not render frame $framesQueued", note,
                            )
                        }
                        rgbaToNv12(pixels, width, height, yuv)
                        buffer.clear()
                        buffer.put(yuv)
                        firstEncoder.queueInputBuffer(
                            inIndex, 0, yuv.size, framesQueued * frameDurationUs, 0,
                        )
                        framesQueued++
                        onProgress(0.7f * framesQueued / totalFrames)
                    }
                } else if (!videoEosQueued) {
                    val inIndex = firstEncoder.dequeueInputBuffer(5_000L)
                    if (inIndex >= 0) {
                        firstEncoder.queueInputBuffer(
                            inIndex, 0, 0, framesQueued * frameDurationUs,
                            MediaCodec.BUFFER_FLAG_END_OF_STREAM,
                        )
                        videoEosQueued = true
                    }
                }

                // --- audio input (one span per video frame, aligned to the range)
                if (!audioEos && audioCodec != null) {
                    val audioTime = audioFed.toDouble() / sampleRate
                    if (start + audioTime >= end) {
                        val aIn = audioCodec.dequeueInputBuffer(5_000L)
                        if (aIn >= 0) {
                            audioCodec.queueInputBuffer(
                                aIn, 0, 0, framesQueued * frameDurationUs,
                                MediaCodec.BUFFER_FLAG_END_OF_STREAM,
                            )
                            audioEos = true
                        }
                    } else {
                        engine.seek(start + audioTime)
                        val read = if (NativeEngine.nativeReadAudioPcm(pcm, audioChunk, channels)) audioChunk else 0
                        val aIn = audioCodec.dequeueInputBuffer(5_000L)
                        if (aIn >= 0 && read > 0) {
                            val sb = ByteBuffer.wrap(audioBytes).order(ByteOrder.LITTLE_ENDIAN)
                            var s = 0
                            while (s < read * channels) {
                                sb.putShort(s, (pcm[s].coerceIn(-1f, 1f) * 32767f).toInt().toShort())
                                s++
                            }
                            val ab = audioCodec.getInputBuffer(aIn)!!
                            ab.clear()
                            ab.put(audioBytes, 0, read * channels * 2)
                            audioCodec.queueInputBuffer(
                                aIn, 0, read * channels * 2,
                                audioFed * 1_000_000L / sampleRate, 0,
                            )
                            audioFed += read
                        }
                    }
                }

                val vDone = drain(firstEncoder, true) { videoTrack }
                if (audioCodec != null) drain(audioCodec, false) { audioTrack }
                startMuxerIfReady()
                if (vDone && videoEosQueued) videoDone = true
            }

            // Flush the audio encoder tail.
            if (audioCodec != null && !audioEos) {
                val aIn = audioCodec.dequeueInputBuffer(5_000L)
                if (aIn >= 0) {
                    audioCodec.queueInputBuffer(
                        aIn, 0, 0, framesQueued * frameDurationUs,
                        MediaCodec.BUFFER_FLAG_END_OF_STREAM,
                    )
                    audioEos = true
                }
            }
            if (audioCodec != null && audioTrack >= 0) {
                var guard = 0
                while (guard++ < 500) {
                    val index = audioCodec.dequeueOutputBuffer(info, 5_000L)
                    if (index == MediaCodec.INFO_TRY_AGAIN_LATER) break
                    if (index == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) continue
                    if (index < 0) continue
                    writeSample(audioTrack, audioCodec, index)
                    if (info.size > 0) audioMuxed = true
                    val eos = (info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0
                    audioCodec.releaseOutputBuffer(index, false)
                    if (eos) break
                }
            }

            if (outBitmap !== sourceBitmap) outBitmap.recycle()
            sourceBitmap.recycle()
            return Result(outFile, framesWritten, audioMuxed, null, note)
        } catch (t: Throwable) {
            runCatching { outFile.delete() }
            return Result(null, framesWritten, audioMuxed, t.message ?: t.javaClass.simpleName, note)
        } finally {
            runCatching { videoCodec?.stop() }
            runCatching { videoCodec?.release() }
            runCatching { audioCodec?.stop() }
            runCatching { audioCodec?.release() }
            if (muxerStarted) runCatching { muxer?.stop() }
            runCatching { muxer?.release() }
            if (proxiesWereOn) NativeEngine.nativeProxySetEnabled(true)
            if (previewScaleWas < 0.999f) NativeEngine.nativePreviewScaleSet(previewScaleWas)
            engine.seek(resumeTime)
            if (wasPlaying) engine.togglePlay()
        }
    }

    /** ARGB ints → NV12 (BT.601, full range). */
    private fun rgbaToNv12(pixels: IntArray, width: Int, height: Int, out: ByteArray) {
        val frameSize = width * height
        var yIndex = 0
        var uvIndex = frameSize
        var i = 0
        while (i < pixels.size) {
            val p = pixels[i]
            val r = (p shr 16) and 0xFF
            val g = (p shr 8) and 0xFF
            val b = p and 0xFF
            val y = ((66 * r + 129 * g + 25 * b + 128) shr 8) + 16
            out[yIndex++] = y.coerceIn(16, 235).toByte()
            if ((i / width) % 2 == 0 && (i % width) % 2 == 0 && uvIndex + 1 < out.size) {
                val u = ((-38 * r - 74 * g + 112 * b + 128) shr 8) + 128
                val v = ((112 * r - 94 * g - 18 * b + 128) shr 8) + 128
                out[uvIndex++] = u.coerceIn(0, 255).toByte()
                out[uvIndex++] = v.coerceIn(0, 255).toByte()
            }
            i++
        }
    }
}
