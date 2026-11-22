package com.motionstudio.app.ui.theme

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.Font
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.sp
import com.motionstudio.app.R

/**
 * Black workspace, one lavender accent — the colour of the logo, and the palette
 * of a compositor rather than a phone gallery. Every surface is a flat panel
 * with a 1 px edge; nothing is rounded or soft, because the numbers carry the
 * colour. Neutrals carry a little violet so the UI reads as one piece instead of
 * grey plastic with a purple highlight pasted on.
 */
object MsColors {
    val background = Color(0xFF07060B)
    val surface = Color(0xFF0D0B14)
    val surfaceHigh = Color(0xFF141020)
    val surfaceHighest = Color(0xFF1C1630)
    val border = Color(0xFF332B4C)
    val borderSoft = Color(0xFF241E34)

    val accent = Color(0xFFA98BFF)
    val accentSoft = Color(0xFFC9B8FF)
    val accentDim = Color(0xFF4B3480)

    val textPrimary = Color(0xFFF0EDF8)
    val textSecondary = Color(0xFFBBB3D0)
    val textTertiary = Color(0xFFA79EBD)
    val textMuted = Color(0xFF857C9C)

    val success = Color(0xFF63D6A2)
    val warning = Color(0xFFF0B429)
    val danger = Color(0xFFE87979)
    val error = Color(0xFFFF6B8A)
    val info = Color(0xFF6FA8F5)

    val viewerBg = Color(0xFF000000)
    val videoTrack = Color(0xFF8A6BE0)
    val audioTrack = Color(0xFF4E9E8C)
    val overlayTrack = Color(0xFFC27AD6)

    // Node graph + viewport overlays.
    val nodeBg = Color(0xFF120E1D)
    val nodeBorder = Color(0xFF332A4A)
    val wire = Color(0xFF544873)
    val guide = Color(0xFF37304A)
    val guideStrong = Color(0xFF5E5480)
}

/**
 * Inter (SIL OFL 1.1) for interface text and JetBrains Mono (SIL OFL 1.1) for
 * anything numeric: timecode, gain, frame stats, track labels.
 */
object MsFonts {
    val sans = FontFamily(
        Font(R.font.inter_regular, FontWeight.Normal),
        Font(R.font.inter_medium, FontWeight.Medium),
        Font(R.font.inter_semibold, FontWeight.SemiBold),
        Font(R.font.inter_bold, FontWeight.Bold),
    )
    val mono = FontFamily(
        Font(R.font.jbmono_regular, FontWeight.Normal),
        Font(R.font.jbmono_medium, FontWeight.Medium),
        Font(R.font.jbmono_bold, FontWeight.Bold),
    )
}

private val MsTypography = Typography(
    displaySmall = TextStyle(fontFamily = MsFonts.sans, fontSize = 34.sp, lineHeight = 40.sp, fontWeight = FontWeight.Bold),
    headlineMedium = TextStyle(fontFamily = MsFonts.sans, fontSize = 24.sp, lineHeight = 30.sp, fontWeight = FontWeight.Bold),
    titleMedium = TextStyle(fontFamily = MsFonts.sans, fontSize = 16.sp, lineHeight = 22.sp, fontWeight = FontWeight.SemiBold),
    titleSmall = TextStyle(fontFamily = MsFonts.sans, fontSize = 14.sp, lineHeight = 20.sp, fontWeight = FontWeight.SemiBold),
    bodyMedium = TextStyle(fontFamily = MsFonts.sans, fontSize = 14.sp, lineHeight = 20.sp),
    bodySmall = TextStyle(fontFamily = MsFonts.sans, fontSize = 12.sp, lineHeight = 16.sp),
    labelMedium = TextStyle(fontFamily = MsFonts.sans, fontSize = 12.sp, lineHeight = 16.sp, fontWeight = FontWeight.Medium),
    labelSmall = TextStyle(fontFamily = MsFonts.sans, fontSize = 11.sp, lineHeight = 14.sp, fontWeight = FontWeight.Medium),
)

@Composable
fun MotionStudioTheme(accent: Color = MsColors.accent, content: @Composable () -> Unit) {
    val scheme = darkColorScheme(
        primary = accent,
        onPrimary = Color(0xFF120A22),
        primaryContainer = accent.copy(alpha = 0.20f),
        onPrimaryContainer = MsColors.accentSoft,
        secondary = MsColors.accentSoft,
        background = MsColors.background,
        onBackground = MsColors.textPrimary,
        surface = MsColors.surface,
        onSurface = MsColors.textPrimary,
        surfaceVariant = MsColors.surfaceHigh,
        onSurfaceVariant = MsColors.textSecondary,
        outline = MsColors.border,
        error = MsColors.error,
    )
    MaterialTheme(
        // The app is a compositor workspace: there is one palette, and it is dark.
        // Following the system light theme would leave Material controls (sliders,
        // switches, text fields) light-on-black.
        colorScheme = scheme,
        typography = MsTypography.copy(
            bodyLarge = MsTypography.bodyMedium,
            labelLarge = MsTypography.labelMedium,
        ),
        content = content,
    )
}
