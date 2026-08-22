package com.rsvpnano.android.ui

import android.Manifest
import android.content.Context
import android.content.Intent
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.content.pm.PackageManager
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import com.rsvpnano.android.FirmwareUpdateJobService
import com.rsvpnano.ui.RsvpNanoSharedApp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

private enum class PermissionFallback {
    WifiSettings,
    AppSettings,
}

@Composable
fun CompanionApp(
    shareIntent: Intent? = null,
    onShareIntentHandled: () -> Unit = {},
) {
    val context = LocalContext.current
    val lifecycleOwner = LocalLifecycleOwner.current
    val viewModel: CompanionViewModel = viewModel(
        factory = CompanionViewModel.Factory(context),
    )
    val nanoNetworkController = viewModel.nanoNetworkController
    val presenter = viewModel.presenter
    val uiState by presenter.uiState.collectAsStateWithLifecycle()
    var permissionRequestAttempted by remember { mutableStateOf(false) }
    var permissionBlockedFallback by remember { mutableStateOf(PermissionFallback.WifiSettings) }

    val nanoWifiPermissionLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.RequestPermission(),
    ) { permissionGranted ->
        val granted = permissionGranted || nanoNetworkController.hasRequiredPermissions()
        if (granted) {
            presenter.connectNanoScan()
        } else {
            val permission = nanoWifiPermission()
            val canAskAgain = context.findActivity()?.shouldShowRequestPermissionRationale(permission) == true
            if (canAskAgain) {
                presenter.scanPermissionDenied()
            } else if (permissionBlockedFallback == PermissionFallback.AppSettings) {
                presenter.wifiPermissionsBlocked()
                context.openAppSettings()
            } else {
                presenter.scanPermissionDenied()
                context.openWifiSettings()
            }
        }
    }
    val firmwareNotificationLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.RequestPermission(),
    ) { granted ->
        presenter.setFirmwareNotificationsEnabled(granted)
    }

    fun setFirmwareNotifications(enabled: Boolean) {
        if (!enabled) {
            presenter.setFirmwareNotificationsEnabled(false)
            return
        }
        if (context.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) == PackageManager.PERMISSION_GRANTED) {
            presenter.setFirmwareNotificationsEnabled(true)
        } else {
            firmwareNotificationLauncher.launch(Manifest.permission.POST_NOTIFICATIONS)
        }
    }

    fun connectNanoFromApp(openWifiSettingsOnBlocked: Boolean) {
        presenter.connectNanoScan {
            val permission = nanoWifiPermission()
            val canAskAgain = !permissionRequestAttempted ||
                context.findActivity()?.shouldShowRequestPermissionRationale(permission) == true
            presenter.requestWifiPermissions()
            if (canAskAgain) {
                permissionRequestAttempted = true
                permissionBlockedFallback = if (openWifiSettingsOnBlocked) {
                    PermissionFallback.WifiSettings
                } else {
                    PermissionFallback.AppSettings
                }
                nanoWifiPermissionLauncher.launch(permission)
            } else if (openWifiSettingsOnBlocked) {
                presenter.scanPermissionDenied()
                context.openWifiSettings()
            } else {
                presenter.wifiPermissionsBlocked()
                context.openAppSettings()
            }
        }
    }

    DisposableEffect(lifecycleOwner, viewModel) {
        val observer = LifecycleEventObserver { _, event ->
            if (event == Lifecycle.Event.ON_RESUME) {
                presenter.recheckConnectionAfterResume()
            }
        }
        lifecycleOwner.lifecycle.addObserver(observer)
        onDispose { lifecycleOwner.lifecycle.removeObserver(observer) }
    }

    DisposableEffect(context, viewModel) {
        val connectivityManager = context.getSystemService(Context.CONNECTIVITY_SERVICE) as? ConnectivityManager
        if (connectivityManager == null) {
            onDispose { }
        } else {
            val callback = object : ConnectivityManager.NetworkCallback() {
                override fun onAvailable(network: Network) {
                    presenter.recheckConnectionAfterNetworkChange()
                    presenter.fetchPendingArticlesWhenOnline()
                }

                override fun onLost(network: Network) {
                    presenter.recheckConnectionAfterNetworkChange()
                }

                override fun onCapabilitiesChanged(network: Network, networkCapabilities: NetworkCapabilities) {
                    if (networkCapabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)) {
                        presenter.fetchPendingArticlesWhenOnline()
                    }
                }
            }
            connectivityManager.registerDefaultNetworkCallback(callback)
            onDispose { connectivityManager.unregisterNetworkCallback(callback) }
        }
    }

    LaunchedEffect(shareIntent) {
        val intent = shareIntent ?: return@LaunchedEffect
        val imports = withContext(Dispatchers.IO) { context.sharedImportsFrom(intent) }
        if (imports.isNotEmpty() || intent.isAndroidShareIntent()) {
            presenter.saveSharedImports(imports)
        }
        onShareIntentHandled()
    }

    LaunchedEffect(uiState.firmwareNotificationsEnabled) {
        val canNotify =
            context.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) == PackageManager.PERMISSION_GRANTED
        val enabled = uiState.firmwareNotificationsEnabled && canNotify
        if (uiState.firmwareNotificationsEnabled && !canNotify) {
            presenter.setFirmwareNotificationsEnabled(false)
        }
        FirmwareUpdateJobService.configure(context, enabled)
        if (enabled) {
            FirmwareUpdateJobService.checkNow(context)
        }
    }

    RsvpNanoSharedApp(
        uiState = uiState,
        presenter = presenter,
        hasPermissions = nanoNetworkController.hasRequiredPermissions(),
        onConnect = { connectNanoFromApp(openWifiSettingsOnBlocked = true) },
        onFirmwareNotificationsChange = ::setFirmwareNotifications,
        onGrantPermissions = { connectNanoFromApp(openWifiSettingsOnBlocked = false) },
    )
}
