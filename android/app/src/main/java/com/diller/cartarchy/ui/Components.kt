package com.diller.cartarchy.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp

@Composable
fun MacButton(
    title: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
) {
    Box(modifier = modifier.padding(end = 3.dp, bottom = 3.dp)) {
        Box(
            Modifier
                .matchParentSize()
                .offset(3.dp, 3.dp)
                .background(if (enabled) Color.Black else DarkGray),
        )
        Box(
            Modifier
                .fillMaxWidth()
                .background(if (enabled) Color.White else MediumGray)
                .border(2.dp, Color.Black)
                .clickable(enabled = enabled, role = Role.Button, onClick = onClick)
                .padding(horizontal = 10.dp, vertical = 9.dp),
            contentAlignment = Alignment.Center,
        ) {
            Text(
                title,
                style = MaterialTheme.typography.labelLarge,
                fontWeight = FontWeight.Bold,
                color = if (enabled) Color.Black else DarkGray,
            )
        }
    }
}
