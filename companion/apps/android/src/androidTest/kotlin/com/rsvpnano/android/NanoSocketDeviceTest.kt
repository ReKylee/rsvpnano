package com.rsvpnano.android

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.rsvpnano.app.CompanionNotice
import com.rsvpnano.app.NanoEndpoint
import com.rsvpnano.app.NanoWifiConnector
import com.rsvpnano.app.NanoWifiEvent
import com.rsvpnano.app.NanoWifiRequestResult
import com.rsvpnano.app.NanoWifiSnapshot
import com.rsvpnano.app.createAndroidCompanionPresenter
import com.rsvpnano.models.RememberedNano
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.emptyFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class NanoSocketDeviceTest {
    @Test
    fun testDeviceEndpointThroughAndroidUrlConnection() {
        val arguments = InstrumentationRegistry.getArguments()
        val baseUrl = arguments.getString("deviceUrl") ?: "http://192.168.1.154"
        val path = arguments.getString("path") ?: "device"
        requestThroughAndroidUrlConnection(baseUrl, path)
    }

    private fun requestThroughAndroidUrlConnection(baseUrl: String, path: String) {
        val connection = URL("$baseUrl/api/v2/$path").openConnection() as HttpURLConnection
        connection.connectTimeout = 10_000
        connection.readTimeout = 10_000
        connection.requestMethod = "GET"
        connection.setRequestProperty("Connection", "close")
        val startedAt = System.nanoTime()

        try {
            val status = connection.responseCode
            val body = connection.inputStream.bufferedReader().use { it.readText() }
            assertTrue("/$path returned HTTP $status: $body", status in 200..299)
        } catch (error: Throwable) {
            val elapsedMs = (System.nanoTime() - startedAt) / 1_000_000
            throw AssertionError("/$path failed after $elapsedMs ms: ${error.message}", error)
        } finally {
            connection.disconnect()
        }
    }

    @Test
    fun testConnectsThroughProductionAndroidClient() = runBlocking {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val baseUrl = InstrumentationRegistry.getArguments().getString("deviceUrl") ?: "http://192.168.1.154"
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
        val presenter = createAndroidCompanionPresenter(
            appFilesDir = File(instrumentation.targetContext.cacheDir, "nano-socket-test").apply { mkdirs() },
            nanoWifiConnector = FixedNanoConnector(baseUrl),
            scope = scope,
        )

        try {
            presenter.connectNanoScan()
            val state = withTimeout(60_000) {
                presenter.uiState.first {
                    it.isConnected ||
                        (it.notice is CompanionNotice.Error && it.status.startsWith("Found "))
                }
            }
            assertTrue(state.status, state.isConnected)
        } finally {
            presenter.close()
            scope.cancel()
        }
    }
}

private class FixedNanoConnector(baseUrl: String) : NanoWifiConnector {
    private val endpoint = NanoEndpoint(baseUrl, RememberedNano("RSVP-Nano-device-test"))
    private val state = MutableStateFlow(NanoWifiSnapshot())

    override val snapshot: StateFlow<NanoWifiSnapshot> = state
    override val events: Flow<NanoWifiEvent> = emptyFlow()

    override fun start() = Unit
    override fun stop() = Unit
    override fun refreshSnapshot() = Unit
    override suspend fun discoverNanos(): List<NanoEndpoint> = listOf(endpoint)
    override fun requestNanoNetwork(rememberedNano: RememberedNano?) =
        NanoWifiRequestResult.Failed("Fixed-address test does not switch Wi-Fi networks.")

}
