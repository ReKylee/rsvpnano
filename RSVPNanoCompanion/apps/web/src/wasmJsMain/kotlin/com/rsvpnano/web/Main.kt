package com.rsvpnano.web

import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.animation.togetherWith
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsHoveredAsState
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.outlined.ArrowForward
import androidx.compose.material.icons.outlined.AutoStories
import androidx.compose.material.icons.outlined.Build
import androidx.compose.material.icons.outlined.ColorLens
import androidx.compose.material.icons.outlined.DarkMode
import androidx.compose.material.icons.outlined.Devices
import androidx.compose.material.icons.outlined.LightMode
import androidx.compose.material.icons.outlined.Link
import androidx.compose.material.icons.outlined.Settings
import androidx.compose.material.icons.outlined.Timer
import androidx.compose.material.icons.outlined.Usb
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.draw.clip
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.ExperimentalComposeUiApi
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.compositeOver
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.luminance
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.ComposeViewport
import androidx.compose.foundation.Image
import com.rsvpnano.app.NanoConnectionState
import com.rsvpnano.app.NanoConnectionTransport
import com.rsvpnano.app.NanoEndpoint
import com.rsvpnano.models.RememberedNano
import com.rsvpnano.ui.CompanionPresenter
import com.rsvpnano.ui.CompanionUiState
import kotlinx.browser.document
import kotlinx.browser.window
import org.jetbrains.compose.resources.painterResource
import com.rsvpnano.web.resources.Res
import com.rsvpnano.web.resources.rsvp_nano_foreground
import com.rsvpnano.web.resources.rsvp_nano_foreground_light

private val LightColors = lightColorScheme(
    primary = Color(0xff35675f),
    onPrimary = Color.White,
    secondary = Color(0xff9a5b24),
    tertiary = Color(0xffd84315),
    background = Color(0xfffaf8f2),
    surface = Color(0xfffffdf7),
    onSurface = Color(0xff252724),
    outline = Color(0xff747874),
)

private val DarkColors = darkColorScheme(
    primary = Color(0xffa5cec4),
    onPrimary = Color(0xff0b3731),
    secondary = Color(0xffffb77a),
    tertiary = Color(0xffff7043),
    background = Color(0xff191c1a),
    surface = Color(0xff202421),
    onSurface = Color(0xffe2e3df),
    outline = Color(0xff8d938e),
)

internal enum class WebTheme { System, Light, Dark }

internal enum class WebRoute(val hash: String, val label: String, val icon: ImageVector) {
    Setup("#/setup", "Setup", Icons.Outlined.Build),
    Device("#/device", "Device", Icons.Outlined.Devices),
    Library("#/library", "Library", Icons.Outlined.AutoStories),
    Appearance("#/appearance/themes", "Appearance", Icons.Outlined.ColorLens),
    Settings("#/settings/reading", "Settings", Icons.Outlined.Settings),
    Feeds("#/feeds", "Feeds", Icons.Outlined.Link),
    Timers("#/timers", "Timers", Icons.Outlined.Timer),
}

@OptIn(ExperimentalComposeUiApi::class)
fun main() {
    ComposeViewport(document.body!!) { WebApp() }
}

@Composable
private fun WebApp() {
    val scope = rememberCoroutineScope()
    val presenter = remember { createBrowserCompanionPresenter(scope) }
    val state by presenter.uiState.collectAsState()
    var theme by remember {
        mutableStateOf(
            runCatching { WebTheme.valueOf(window.localStorage.getItem("rsvpnano.web.theme") ?: "System") }
                .getOrDefault(WebTheme.System),
        )
    }
    val dark = when (theme) {
        WebTheme.System -> window.matchMedia("(prefers-color-scheme: dark)").matches
        WebTheme.Light -> false
        WebTheme.Dark -> true
    }

    DisposableEffect(presenter) { onDispose { presenter.close() } }

    MaterialTheme(colorScheme = if (dark) DarkColors else LightColors) {
        val pageBackground = MaterialTheme.colorScheme.background
        Box(
            Modifier.fillMaxSize().background(
                Brush.linearGradient(
                    listOf(
                        pageBackground,
                        MaterialTheme.colorScheme.primary.copy(alpha = 0.055f).compositeOver(pageBackground),
                        pageBackground,
                        MaterialTheme.colorScheme.tertiary.copy(alpha = 0.035f).compositeOver(pageBackground),
                    ),
                ),
            ),
        ) {
            EditorialShell(
                presenter = presenter,
                state = state,
                theme = theme,
                onThemeChange = {
                    theme = it
                    window.localStorage.setItem("rsvpnano.web.theme", it.name)
                },
            )
        }
    }
}

@Composable
private fun EditorialShell(
    presenter: CompanionPresenter,
    state: CompanionUiState,
    theme: WebTheme,
    onThemeChange: (WebTheme) -> Unit,
) {
    var route by remember { mutableStateOf(routeFromHash()) }
    var endpoint by remember { mutableStateOf(window.localStorage.getItem(EndpointStorageKey).orEmpty()) }

    LaunchedEffect(presenter) {
        if (endpoint.isNotBlank()) presenter.connectNanoScan()
    }
    LaunchedEffect(state.isConnected, state.nanoSsid, state.connectionState.transport) {
        if (state.isConnected) {
            val ssid = state.nanoSsid.orEmpty()
            if (ssid.isNotBlank()) window.localStorage.setItem(NanoNameStorageKey, ssid)
            if (state.connectionState.transport == NanoConnectionTransport.Usb && ssid.startsWith("RSVP-Nano-", ignoreCase = true)) {
                endpoint = "http://${ssid.lowercase()}.local"
                window.localStorage.setItem(EndpointStorageKey, endpoint)
            }
        }
    }

    DisposableEffect(Unit) {
        window.onhashchange = { route = routeFromHash() }
        if (window.location.hash.isBlank()) window.location.hash = route.hash
        onDispose { window.onhashchange = null }
    }

    Column(Modifier.fillMaxSize()) {
        ConnectionToolbar(
            state = state,
            endpoint = endpoint,
            onEndpointChange = { endpoint = it },
            onConnect = {
                val entered = endpoint.trim().trimEnd('/')
                if (entered.isNotEmpty()) {
                    val normalized = if ("://" in entered) entered else "http://$entered"
                    endpoint = normalized
                    window.localStorage.setItem(EndpointStorageKey, normalized)
                    presenter.connectEndpoint(
                        NanoEndpoint(
                            normalized,
                            RememberedNano(normalized.removePrefix("http://").removePrefix("https://")),
                        ),
                    )
                }
            },
            onUsbConnect = {
                if (supportsWebSerial()) requestUsbConnection(presenter)
                else presenter.reportConnectionFailure("USB connection is not available here. Try Chrome or Edge on a computer.")
            },
            theme = theme,
            onThemeChange = onThemeChange,
        )

        BoxWithConstraints(Modifier.fillMaxSize()) {
            val wide = maxWidth >= 1100.dp
            if (wide) {
                Row(Modifier.fillMaxSize()) {
                    NavigationRail(route, Modifier.width(210.dp).fillMaxHeight())
                    Workspace(route, presenter, state, Modifier.weight(1f))
                }
            } else {
                Column(Modifier.fillMaxSize()) {
                    NavigationStrip(route)
                    Workspace(route, presenter, state, Modifier.weight(1f))
                }
            }
        }
    }
}

@Composable
private fun ConnectionToolbar(
    state: CompanionUiState,
    endpoint: String,
    onEndpointChange: (String) -> Unit,
    onConnect: () -> Unit,
    onUsbConnect: () -> Unit,
    theme: WebTheme,
    onThemeChange: (WebTheme) -> Unit,
) {
    BoxWithConstraints(
        Modifier.fillMaxWidth().background(MaterialTheme.colorScheme.surface)
            .border(1.dp, MaterialTheme.colorScheme.outline.copy(alpha = 0.25f)).padding(horizontal = 14.dp, vertical = 8.dp),
    ) {
        val compact = maxWidth < 720.dp
        if (compact) {
            Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    BrandAndStatus(state)
                    Spacer(Modifier.weight(1f))
                    ThemeButton(theme, onThemeChange)
                }
                EndpointControls(endpoint, onEndpointChange, onConnect, onUsbConnect, Modifier.fillMaxWidth())
            }
        } else {
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                BrandAndStatus(state)
                Spacer(Modifier.weight(1f))
                EndpointControls(endpoint, onEndpointChange, onConnect, onUsbConnect)
                ThemeButton(theme, onThemeChange)
            }
        }
    }
}

@Composable
private fun BrandAndStatus(state: CompanionUiState) {
    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(10.dp)) {
        val logo = if (MaterialTheme.colorScheme.background.luminance() > 0.5f) {
            Res.drawable.rsvp_nano_foreground_light
        } else {
            Res.drawable.rsvp_nano_foreground
        }
        Image(
            painter = painterResource(logo),
            contentDescription = "RSVP Nano",
            modifier = Modifier.width(112.dp).height(52.dp),
            contentScale = ContentScale.Fit,
        )
        Surface(
            shape = RoundedCornerShape(50),
            color = MaterialTheme.colorScheme.primary.copy(alpha = 0.11f),
        ) {
            Text(
                when (state.connectionState) {
                    is NanoConnectionState.CheckingReader -> "Looking for your Nano…"
                    is NanoConnectionState.ReaderConnected -> "Connected"
                    else -> "Ready to connect"
                },
                Modifier.padding(horizontal = 12.dp, vertical = 7.dp),
                style = MaterialTheme.typography.labelMedium,
            )
        }
    }
}

@Composable
private fun ThemeButton(theme: WebTheme, onThemeChange: (WebTheme) -> Unit) {
    IconButton(onClick = { onThemeChange(WebTheme.entries[(theme.ordinal + 1) % WebTheme.entries.size]) }) {
        Icon(if (theme == WebTheme.Dark) Icons.Outlined.DarkMode else Icons.Outlined.LightMode, "Change theme")
    }
}

@Composable
private fun EndpointControls(
    endpoint: String,
    onEndpointChange: (String) -> Unit,
    onConnect: () -> Unit,
    onUsbConnect: () -> Unit,
    modifier: Modifier = Modifier,
) {
    var showAddress by remember(endpoint) { mutableStateOf(endpoint.isNotBlank()) }
    Row(
        modifier,
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp, Alignment.End),
    ) {
        OutlinedButton(onClick = onUsbConnect, modifier = Modifier.height(40.dp)) {
            Icon(Icons.Outlined.Usb, null, Modifier.size(18.dp))
            Spacer(Modifier.width(6.dp))
            Text("USB")
        }
        if (showAddress) {
            Row(
                Modifier.height(40.dp).widthIn(min = 180.dp, max = 240.dp)
                    .border(1.dp, MaterialTheme.colorScheme.outline, RoundedCornerShape(10.dp))
                    .padding(start = 12.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                BasicTextField(
                    value = endpoint,
                    onValueChange = onEndpointChange,
                    modifier = Modifier.weight(1f),
                    singleLine = true,
                    textStyle = MaterialTheme.typography.bodyMedium.copy(color = MaterialTheme.colorScheme.onSurface),
                    decorationBox = { inner ->
                        if (endpoint.isBlank()) Text("Nano address", color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.5f))
                        inner()
                    },
                )
                IconButton(onClick = onConnect, modifier = Modifier.size(38.dp)) {
                    Icon(Icons.AutoMirrored.Outlined.ArrowForward, "Connect", Modifier.size(18.dp))
                }
            }
        } else {
            OutlinedButton(onClick = { showAddress = true }, modifier = Modifier.height(40.dp)) {
                Icon(Icons.Outlined.Link, null, Modifier.size(17.dp))
                Spacer(Modifier.width(6.dp))
                Text("Address")
            }
        }
    }
}

@Composable
private fun NavigationRail(selected: WebRoute, modifier: Modifier = Modifier) {
    Column(modifier.background(MaterialTheme.colorScheme.surface).padding(18.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        WebRoute.entries.forEach { NavigationItem(it, selected == it, Modifier.fillMaxWidth()) }
    }
}

@Composable
private fun NavigationStrip(selected: WebRoute) {
    Row(
        Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).background(MaterialTheme.colorScheme.surface)
            .padding(horizontal = 10.dp, vertical = 8.dp),
        horizontalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        WebRoute.entries.forEach { NavigationItem(it, selected == it) }
    }
}

@Composable
private fun NavigationItem(route: WebRoute, selected: Boolean, modifier: Modifier = Modifier) {
    val interactions = remember { MutableInteractionSource() }
    val hovered by interactions.collectIsHoveredAsState()
    val pressed by interactions.collectIsPressedAsState()
    val shape = RoundedCornerShape(8.dp)
    val background by animateColorAsState(
        when {
            selected -> MaterialTheme.colorScheme.primary.copy(alpha = 0.16f)
            hovered -> MaterialTheme.colorScheme.primary.copy(alpha = 0.075f)
            else -> Color.Transparent
        },
        animationSpec = tween(140),
    )
    val scale by animateFloatAsState(
        when {
            pressed -> 0.98f
            hovered -> 1.015f
            else -> 1f
        },
        animationSpec = tween(110),
    )
    Row(
        modifier.graphicsLayer(scaleX = scale, scaleY = scale).clip(shape).background(background)
            .clickable(interactionSource = interactions, indication = null) { window.location.hash = route.hash }
            .padding(horizontal = 12.dp, vertical = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(9.dp),
    ) {
        Icon(route.icon, null, Modifier.size(19.dp), tint = if (selected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurface)
        Text(route.label, fontWeight = if (selected) FontWeight.Bold else FontWeight.Normal)
    }
}

@Composable
private fun Workspace(
    route: WebRoute,
    presenter: CompanionPresenter,
    state: CompanionUiState,
    modifier: Modifier = Modifier,
) {
    val reducedMotion = remember { prefersReducedMotion() }
    AnimatedContent(
        targetState = route,
        modifier = modifier.fillMaxSize(),
        transitionSpec = {
            if (reducedMotion) {
                fadeIn(tween(120)) togetherWith fadeOut(tween(90))
            } else {
                (slideInHorizontally(tween(240, easing = FastOutSlowInEasing)) { it / 12 } + fadeIn(tween(180))) togetherWith
                    (slideOutHorizontally(tween(180, easing = FastOutSlowInEasing)) { -it / 16 } + fadeOut(tween(120)))
            }
        },
        label = "workspace",
    ) { activeRoute ->
        Column(
            Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 24.dp, vertical = 28.dp),
            verticalArrangement = Arrangement.spacedBy(20.dp),
        ) {
            when (activeRoute) {
                WebRoute.Setup -> SetupWizard(presenter, state)
                WebRoute.Device -> DeviceWorkspace(state)
                WebRoute.Library -> LibraryWorkspace(presenter, state)
                WebRoute.Appearance -> AppearanceWorkspace(presenter, state)
                WebRoute.Settings -> SettingsWorkspace(presenter, state)
                WebRoute.Feeds -> FeedsWorkspace(presenter, state)
                WebRoute.Timers -> TimersWorkspace(presenter, state)
            }
            NoticeCard(state)
        }
    }
}

@Composable
private fun DeviceWorkspace(state: CompanionUiState) {
    WorkspaceHeading("Device")
    Card(colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface), shape = RoundedCornerShape(6.dp)) {
        Column(Modifier.fillMaxWidth().padding(22.dp), verticalArrangement = Arrangement.spacedBy(14.dp)) {
            DetailRow("Connection", when (state.connectionState.transport) {
                NanoConnectionTransport.LocalNetwork -> "Local network"
                NanoConnectionTransport.AccessPoint -> "Nano access point"
                NanoConnectionTransport.Usb -> "USB"
                null -> "Not connected"
            })
            DetailRow("Connection address", state.baseUrl)
            DetailRow("Software version", state.firmwareVersion.ifBlank { "—" })
        }
    }
}

@Composable
internal fun WorkspaceHeading(title: String) {
    Text(title, fontSize = 34.sp, fontWeight = FontWeight.Black)
}

@Composable
internal fun DetailRow(label: String, value: String) {
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
        Text(label, fontWeight = FontWeight.Bold)
        Text(value, color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.75f))
    }
}

@Composable
private fun NoticeCard(state: CompanionUiState) {
    Card(colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.primary.copy(alpha = 0.09f)), shape = RoundedCornerShape(4.dp)) {
        Text(state.status, Modifier.fillMaxWidth().padding(14.dp), style = MaterialTheme.typography.bodyMedium)
    }
}

private fun routeFromHash(): WebRoute {
    return routeForHash(window.location.hash)
}

internal fun routeForHash(value: String): WebRoute {
    val hash = value.ifBlank { WebRoute.Setup.hash }
    return WebRoute.entries.firstOrNull { route ->
        hash == route.hash || (route.hash.count { it == '/' } > 1 && hash.startsWith(route.hash.substringBeforeLast('/')))
    } ?: WebRoute.Setup
}
