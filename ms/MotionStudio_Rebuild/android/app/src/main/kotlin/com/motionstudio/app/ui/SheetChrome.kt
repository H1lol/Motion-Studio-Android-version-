package com.motionstudio.app.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.motionstudio.app.ui.theme.MsColors

/**
 * Header for every bottom sheet: a title, one line of "what this does", and a
 * close button.
 *
 * Before this, each sheet opened with a bare 16 sp label and the only way out
 * was the drag handle at the top of a full-height panel — a gesture that is not
 * discoverable on a phone. The button is 40 dp so it can be hit with a thumb.
 */
@Composable
fun SheetHeader(
    title: String,
    subtitle: String? = null,
    onDismiss: () -> Unit,
) {
    Row(
        Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.Top,
    ) {
        Column(Modifier.weight(1f)) {
            Text(
                title,
                color = MsColors.textPrimary,
                fontSize = 18.sp,
                lineHeight = 22.sp,
                fontWeight = FontWeight.SemiBold,
            )
            if (!subtitle.isNullOrBlank()) {
                Spacer(Modifier.height(3.dp))
                Text(
                    subtitle,
                    color = MsColors.textTertiary,
                    fontSize = 12.sp,
                    lineHeight = 16.sp,
                )
            }
        }
        Spacer(Modifier.width(12.dp))
        Box(
            Modifier
                .size(40.dp)
                .clip(CircleShape)
                .background(MsColors.surfaceHigh)
                .clickable(onClick = onDismiss),
            contentAlignment = Alignment.Center,
        ) {
            Text("✕", color = MsColors.textSecondary, fontSize = 14.sp)
        }
    }
}

/**
 * A labelled block divider inside a sheet. The hairline runs to the right of the
 * label so a long list reads as sections instead of one ribbon of text.
 */
@Composable
fun SheetSection(title: String, trailing: String? = null) {
    Row(
        Modifier.fillMaxWidth().padding(top = 14.dp, bottom = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            title,
            color = MsColors.textTertiary,
            fontSize = 10.sp,
            fontFamily = com.motionstudio.app.ui.theme.MsFonts.mono,
            letterSpacing = 1.sp,
        )
        Spacer(Modifier.width(8.dp))
        Box(
            Modifier
                .weight(1f)
                .height(1.dp)
                .background(MsColors.borderSoft)
        )
        if (trailing != null) {
            Spacer(Modifier.width(8.dp))
            Text(
                trailing,
                color = MsColors.textMuted,
                fontSize = 9.sp,
                fontFamily = com.motionstudio.app.ui.theme.MsFonts.mono,
            )
        }
    }
}
