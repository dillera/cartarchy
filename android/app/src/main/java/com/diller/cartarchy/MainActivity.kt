package com.diller.cartarchy

import android.content.Intent
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.ViewModelProvider
import com.diller.cartarchy.server.ServerViewModel
import com.diller.cartarchy.ui.CartarchyApp
import com.diller.cartarchy.ui.CartarchyTheme

class MainActivity : ComponentActivity() {
    private var deepLink by mutableStateOf<String?>(null)
    private val serverViewModel: ServerViewModel by lazy { ViewModelProvider(this)[ServerViewModel::class.java] }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        deepLink = intent?.dataString
        setContent {
            CartarchyTheme {
                CartarchyApp(deepLink = deepLink, serverViewModel = serverViewModel)
            }
        }
    }

    override fun onStart() {
        super.onStart()
        serverViewModel.setApplicationActive(true)
    }

    override fun onStop() {
        serverViewModel.setApplicationActive(false)
        super.onStop()
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        deepLink = intent.dataString
    }
}
