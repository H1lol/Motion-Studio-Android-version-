package com.motionstudio.app

import com.motionstudio.app.data.DeviceProfile
import com.motionstudio.app.data.PreviewQuality
import org.junit.Assert.assertEquals
import org.junit.Test

/** The preview a device class starts with, exercised off-device. */
class DeviceProfileTest {

    @Test
    fun aLowRamDeviceStartsAtQuarter() {
        // Android Go and 2 GB phones report the low-RAM flag whatever their core count.
        assertEquals(PreviewQuality.Quarter, DeviceProfile.defaultPreviewQuality(true, 512, 8))
    }

    @Test
    fun aSmallHeapClassStartsAtQuarter() {
        assertEquals(PreviewQuality.Quarter, DeviceProfile.defaultPreviewQuality(false, 128, 8))
        assertEquals(PreviewQuality.Quarter, DeviceProfile.defaultPreviewQuality(false, 192, 8))
    }

    @Test
    fun aTwoCorePhoneStartsAtQuarter() {
        assertEquals(PreviewQuality.Quarter, DeviceProfile.defaultPreviewQuality(false, 512, 2))
    }

    @Test
    fun aRoomierPhoneStartsAtHalf() {
        assertEquals(PreviewQuality.Half, DeviceProfile.defaultPreviewQuality(false, 256, 8))
        assertEquals(PreviewQuality.Half, DeviceProfile.defaultPreviewQuality(false, 512, 4))
    }
}
