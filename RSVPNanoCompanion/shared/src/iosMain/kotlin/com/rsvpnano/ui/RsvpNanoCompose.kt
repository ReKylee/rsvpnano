package com.rsvpnano.ui

import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.window.ComposeUIViewController
import com.rsvpnano.app.createIosCompanionPresenter
import kotlinx.coroutines.MainScope
import kotlinx.coroutines.cancel
import platform.UIKit.UIViewController
import platform.UserNotifications.UNAuthorizationOptionAlert
import platform.UserNotifications.UNAuthorizationOptionSound
import platform.UserNotifications.UNUserNotificationCenter

fun RsvpNanoComposeViewController(): UIViewController = ComposeUIViewController {
    val scope = remember { MainScope() }
    val presenter = remember(scope) { createIosCompanionPresenter(scope) }
    DisposableEffect(presenter) {
        onDispose {
            scope.cancel()
            presenter.close()
        }
    }
    RsvpNanoComposeApp(presenter)
}

@Composable
private fun RsvpNanoComposeApp(presenter: CompanionPresenter) {
    val uiState by presenter.uiState.collectAsState()
    RsvpNanoSharedApp(
        uiState = uiState,
        presenter = presenter,
        hasPermissions = true,
        onConnect = presenter::connectNanoScan,
        onFirmwareNotificationsChange = { enabled ->
            if (!enabled) {
                presenter.setFirmwareNotificationsEnabled(false)
            } else {
                UNUserNotificationCenter.currentNotificationCenter()
                    .requestAuthorizationWithOptions(UNAuthorizationOptionAlert or UNAuthorizationOptionSound) { granted, _ ->
                        presenter.setFirmwareNotificationsEnabled(granted)
                    }
            }
        },
        onGrantPermissions = presenter::requestWifiPermissions,
    )
}
