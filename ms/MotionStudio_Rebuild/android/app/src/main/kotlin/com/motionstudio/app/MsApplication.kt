package com.motionstudio.app

import android.app.Application
import com.motionstudio.app.data.DeviceProfile
import com.motionstudio.app.data.PreviewQualityStore
import com.motionstudio.app.diag.Diagnostics
import com.motionstudio.app.media.ProxyStore

/**
 * Earliest hook in the process. The crash handler has to exist before the
 * native library is loaded or the first activity inflates, otherwise a failure
 * in either of those leaves nothing on disk to look at.
 */
class MsApplication : Application() {

    override fun onCreate() {
        super.onCreate()
        Diagnostics.install(this)
        // Proxy files live under filesDir, so the storage root is known before
        // the editor can open a project.
        ProxyStore.init(this)
        // The device class decides what a first run previews at, so it is read
        // before the preview setting is restored.
        DeviceProfile.init(this)
        // Preview quality is a device setting, so it is restored before the
        // first frame is composited.
        PreviewQualityStore.init(this)
    }
}
