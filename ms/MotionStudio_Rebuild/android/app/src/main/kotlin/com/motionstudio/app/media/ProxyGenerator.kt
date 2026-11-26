package com.motionstudio.app.media

import android.graphics.ImageFormat
import android.media.Image
import android.media.ImageReader
import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaExtractor
import android.media.MediaFormat
import android.media.MediaMetadataRetriever
import android.media.MediaMuxer
import com.motionstudio.app.diag.Diagnostics
import java.io.File

/**
 * Builds a small H.264 stand-in for a large source file using only platform
 * codecs — there is no ffmpeg on Android, and shipping one would mean shipping
 * the whole GPL toolchain.
 *
 * The transcode is deliberately simple: decode to an [ImageReader] surface,
 * nearest-neighbour the Y and UV planes down to the proxy size, feed the encoder
 * NV12 and mux the result. No GPU, no GL context, no audio (preview audio is
 * decoded from the source independently, so a video-only proxy does not mute
 * anything).
 *
 * Runs synchronously; call it from a background thread. The proxy is written to
 * a `.part` file and renamed only on success, so an interrupted generation can
 * never leave a half-written file that later decodes as garbage.
 */
object ProxyGenerator {

    sealed interface Result {
        data class Done(val file: File, val width: Int, val height: Int) : Result
        /** The source is already small enough; a proxy would not help. */
        data class Skipped(val reason: String) : Result
        data class Failed(val message: String) : Result
        /** The caller stopped it; the partial file was removed. */
        data class Cancelled(val frames: Int) : Result
    }

    /** Width/height of the video track, or null when the file has no video. */
    fun sourceSize(path: String): Pair<Int, Int>? {
        val retriever = MediaMetadataRetriever()
        return try {
            retriever.setDataSource(path)
            val w = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_WIDTH)
                ?.toIntOrNull() ?: return null
            val h = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_HEIGHT)
                ?.toIntOrNull() ?: return null
            if (w <= 0 || h <= 0) null else w to h
        } catch (t: Throwable) {
            null
        } finally {
            runCatching { retriever.release() }
        }
    }

    /** True when the source has more pixels than the proxy box. */
    fun needed(width: Int, height: Int): Boolean =
        width > ProxyStore.MAX_WIDTH || height > ProxyStore.MAX_HEIGHT

    /** Fits [sw]x[sh] inside the proxy box, preserving aspect, both edges even. */
    fun targetSize(sw: Int, sh: Int): Pair<Int, Int> {
        if (sw <= 0 || sh <= 0) return ProxyStore.MAX_WIDTH to ProxyStore.MAX_HEIGHT
        val scale = minOf(
            ProxyStore.MAX_WIDTH.toFloat() / sw,
            ProxyStore.MAX_HEIGHT.toFloat() / sh,
            1f,
        )
        val w = (sw * scale).toInt().coerceAtLeast(2) / 2 * 2
        val h = (sh * scale).toInt().coerceAtLeast(2) / 2 * 2
        return w to h
    }

    fun generate(
        mediaPath: String,
        isCancelled: () -> Boolean = { false },
        onProgress: (Float) -> Unit = {},
    ): Result {
        val source = File(mediaPath)
        if (!source.isFile) return Result.Failed("source file is gone: ${source.name}")
        val (sw, sh) = sourceSize(mediaPath)
            ?: return Result.Failed("no video track in ${source.name}")
        if (!needed(sw, sh)) return Result.Skipped("${sw}x$sh is already small enough")
        val (tw, th) = targetSize(sw, sh)

        val out = ProxyStore.fileFor(mediaPath)
        out.parentFile?.mkdirs()
        val temp = File(out.absolutePath + ".part")
        temp.delete()

        var extractor: MediaExtractor? = null
        var reader: ImageReader? = null
        var decoder: MediaCodec? = null
        var encoder: MediaCodec? = null
        var muxer: MediaMuxer? = null
        var muxerStarted = false

        try {
            extractor = MediaExtractor().apply { setDataSource(mediaPath) }
            var track = -1
            var inputFormat: MediaFormat? = null
            for (i in 0 until extractor.trackCount) {
                val f = extractor.getTrackFormat(i)
                val mime = f.getString(MediaFormat.KEY_MIME) ?: continue
                if (mime.startsWith("video/")) {
                    track = i
                    inputFormat = f
                    break
                }
            }
            if (track < 0 || inputFormat == null) return Result.Failed("no video track")
            extractor.selectTrack(track)

            val mime = inputFormat.getString(MediaFormat.KEY_MIME)!!
            val durationUs = if (inputFormat.containsKey(MediaFormat.KEY_DURATION)) {
                inputFormat.getLong(MediaFormat.KEY_DURATION)
            } else {
                0L
            }
            var sourceFps = 30
            if (inputFormat.containsKey(MediaFormat.KEY_FRAME_RATE)) {
                sourceFps = inputFormat.getInteger(MediaFormat.KEY_FRAME_RATE)
            }
            sourceFps = sourceFps.coerceIn(1, 120)

            reader = ImageReader.newInstance(sw, sh, ImageFormat.YUV_420_888, 3)
            decoder = MediaCodec.createDecoderByType(mime)
            // No colour format is set on purpose: output goes to the reader
            // surface, which is what makes plane access possible without GL.
            decoder.configure(inputFormat, reader.surface, null, 0)
            decoder.start()

            val bitrate = 3_000_000
            val encoderFormat = MediaFormat.createVideoFormat("video/avc", tw, th).apply {
                setInteger(
                    MediaFormat.KEY_COLOR_FORMAT,
                    MediaCodecInfo.CodecCapabilities.COLOR_FormatYUV420SemiPlanar,
                )
                setInteger(MediaFormat.KEY_BIT_RATE, bitrate)
                setInteger(MediaFormat.KEY_FRAME_RATE, sourceFps)
                setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 1)
            }
            encoder = MediaCodec.createEncoderByType("video/avc").apply {
                configure(encoderFormat, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
                start()
            }
            muxer = MediaMuxer(temp.absolutePath, MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4)

            val nv12 = ByteArray(tw * th * 3 / 2)
            val bufferInfo = MediaCodec.BufferInfo()
            var videoTrack = -1
            var inputDone = false
            var decoderDone = false
            var encoderDone = false
            var encoderEosQueued = false
            var lastPtsUs = 0L
            var frames = 0

            fun queueEncoderFrame(ptsUs: Long) {
                val index = encoder!!.dequeueInputBuffer(10_000L)
                if (index < 0) return
                val buffer = encoder!!.getInputBuffer(index) ?: return
                buffer.clear()
                buffer.put(nv12)
                encoder!!.queueInputBuffer(index, 0, nv12.size, ptsUs, 0)
                frames++
            }

            fun drainEncoder(waitUs: Long) {
                while (true) {
                    val index = encoder!!.dequeueOutputBuffer(bufferInfo, waitUs)
                    if (index == MediaCodec.INFO_TRY_AGAIN_LATER) return
                    if (index == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                        if (!muxerStarted) {
                            videoTrack = muxer!!.addTrack(encoder!!.outputFormat)
                            muxer!!.start()
                            muxerStarted = true
                        }
                        continue
                    }
                    if (index < 0) continue
                    val buffer = encoder!!.getOutputBuffer(index)
                    if (buffer != null && bufferInfo.size > 0 && muxerStarted) {
                        if ((bufferInfo.flags and MediaCodec.BUFFER_FLAG_CODEC_CONFIG) == 0) {
                            buffer.position(bufferInfo.offset)
                            buffer.limit(bufferInfo.offset + bufferInfo.size)
                            muxer!!.writeSampleData(videoTrack, buffer, bufferInfo)
                        }
                    }
                    if ((bufferInfo.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) {
                        encoderDone = true
                    }
                    encoder!!.releaseOutputBuffer(index, false)
                    if (encoderDone) return
                }
            }

            fun drainImages() {
                while (true) {
                    val image = reader!!.acquireNextImage() ?: return
                    try {
                        downscaleToNv12(image, tw, th, nv12)
                        lastPtsUs = (image.timestamp / 1000L).coerceAtLeast(lastPtsUs + 1)
                        queueEncoderFrame(lastPtsUs)
                    } finally {
                        image.close()
                    }
                }
            }

            while (!encoderDone) {
                if (!inputDone) {
                    val index = decoder!!.dequeueInputBuffer(10_000L)
                    if (index >= 0) {
                        val buffer = decoder!!.getInputBuffer(index)!!
                        val size = extractor.readSampleData(buffer, 0)
                        if (size < 0) {
                            decoder!!.queueInputBuffer(
                                index, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM,
                            )
                            inputDone = true
                        } else {
                            decoder!!.queueInputBuffer(
                                index, 0, size, extractor.sampleTime.coerceAtLeast(0), 0,
                            )
                            extractor.advance()
                        }
                    }
                }

                // Surface output still has to be dequeued, or the decoder stalls.
                while (true) {
                    val index = decoder!!.dequeueOutputBuffer(bufferInfo, 0L)
                    if (index == MediaCodec.INFO_TRY_AGAIN_LATER) break
                    if (index == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) continue
                    if (index < 0) continue
                    val eos = (bufferInfo.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0
                    decoder!!.releaseOutputBuffer(index, true)
                    if (eos) {
                        decoderDone = true
                        break
                    }
                }

                drainImages()

                if (decoderDone && !encoderEosQueued) {
                    // One more pass: the last image may arrive after EOS.
                    drainImages()
                    val index = encoder!!.dequeueInputBuffer(10_000L)
                    if (index >= 0) {
                        encoder!!.queueInputBuffer(
                            index, 0, 0, lastPtsUs + 33_000,
                            MediaCodec.BUFFER_FLAG_END_OF_STREAM,
                        )
                        encoderEosQueued = true
                    }
                }
                drainEncoder(0L)

                if (isCancelled()) {
                    temp.delete()
                    Diagnostics.note("proxy cancelled: ${source.name} after $frames frames")
                    return Result.Cancelled(frames)
                }

                if (durationUs > 0) onProgress((lastPtsUs.toFloat() / durationUs).coerceIn(0f, 1f))
            }

            // Flush whatever the encoder still holds.
            drainEncoder(10_000L)

            muxer!!.stop()
            muxerStarted = false
            if (!temp.renameTo(out)) return Result.Failed("could not move the proxy into place")
            onProgress(1f)
            Diagnostics.note("proxy: ${source.name} → ${tw}x$th, $frames frames")
            return Result.Done(out, tw, th)
        } catch (t: Throwable) {
            temp.delete()
            Diagnostics.note("proxy failed: ${source.name}: ${t.javaClass.simpleName} ${t.message}")
            return Result.Failed(t.message ?: t.javaClass.simpleName)
        } finally {
            runCatching { decoder?.stop() }
            runCatching { decoder?.release() }
            runCatching { encoder?.stop() }
            runCatching { encoder?.release() }
            runCatching { if (muxerStarted) muxer?.stop() }
            runCatching { muxer?.release() }
            runCatching { reader?.close() }
            runCatching { extractor?.release() }
        }
    }

    /**
     * Nearest-neighbour downscale of an image's Y and UV planes into NV12 at
     * [tw]x[th]. Chroma is sampled at half resolution, which is why the loops
     * step by two and write interleaved U/V pairs.
     */
    private fun downscaleToNv12(image: Image, tw: Int, th: Int, out: ByteArray) {
        val sw = image.width
        val sh = image.height
        val frameSize = tw * th
        val yPlane = image.planes[0]
        val uPlane = image.planes[1]
        val vPlane = image.planes[2]
        val yBuf = yPlane.buffer
        val uBuf = uPlane.buffer
        val vBuf = vPlane.buffer

        var di = 0
        for (y in 0 until th) {
            val sy = (y.toLong() * sh / th).toInt().coerceIn(0, sh - 1)
            val row = sy * yPlane.rowStride
            for (x in 0 until tw) {
                val sx = (x.toLong() * sw / tw).toInt().coerceIn(0, sw - 1)
                out[di++] = yBuf.get(row + sx * yPlane.pixelStride)
            }
        }

        var rowUv = frameSize
        var y = 0
        while (y < th) {
            val sy = ((y.toLong() * sh / th).toInt() / 2).coerceIn(0, maxOf(0, sh / 2 - 1))
            val uRow = sy * uPlane.rowStride
            val vRow = sy * vPlane.rowStride
            var x = 0
            while (x < tw) {
                val sx = ((x.toLong() * sw / tw).toInt() / 2).coerceIn(0, maxOf(0, sw / 2 - 1))
                out[rowUv + x] = uBuf.get(uRow + sx * uPlane.pixelStride)
                out[rowUv + x + 1] = vBuf.get(vRow + sx * vPlane.pixelStride)
                x += 2
            }
            rowUv += tw
            y += 2
        }
    }
}
