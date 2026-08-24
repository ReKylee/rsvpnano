@file:OptIn(ExperimentalWasmJsInterop::class, kotlin.io.encoding.ExperimentalEncodingApi::class)

package com.rsvpnano.web

import kotlin.io.encoding.Base64
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.withContext
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException

class WebSerialNanoApiTest {
    @Test
    fun bootloaderRestartUsesNative1200BaudTouch() = runTest {
        installBootloaderResetSerial(openFails = false)
        restartInBootloader()

        assertEquals("1200|1|0", bootloaderResetState())
    }

    @Test
    fun bootloaderRestartAcceptsDisconnectDuringOpen() = runTest {
        installBootloaderResetSerial(openFails = true)
        restartInBootloader()

        assertEquals("1200|0|0", bootloaderResetState())
    }

    @Test
    fun companionConnectionReleasesPortForInstaller() = runTest {
        withContext(Dispatchers.Default.limitedParallelism(1)) {
            val deviceBody = """{"ssid":"RSVP-Nano","firmwareVersion":"0.0.9","otaAsset":"nano-ota.bin"}""".encodeToByteArray()
            val responseMetadata = """{"status":200,"contentType":"application/json","totalBytes":${deviceBody.size}}"""
            val response =
                SerialFrameCodec.encode(SerialFrame(SerialFrameType.Response, 1u, payload = responseMetadata.encodeToByteArray())) +
                    SerialFrameCodec.encode(SerialFrame(SerialFrameType.Data, 1u, payload = deviceBody)) +
                    SerialFrameCodec.encode(SerialFrame(SerialFrameType.End, 1u))
            val repairBody = """{"healthy":true,"checked":8,"moved":1,"removed":0,"diagnosticSummary":"Storage OK","diagnosticDetail":"FAT32","actions":[],"issues":[]}""".encodeToByteArray()
            val repairMetadata = """{"status":200,"contentType":"application/json","totalBytes":${repairBody.size}}"""
            val repairResponse =
                SerialFrameCodec.encode(SerialFrame(SerialFrameType.Response, 2u, payload = repairMetadata.encodeToByteArray())) +
                    SerialFrameCodec.encode(SerialFrame(SerialFrameType.Data, 2u, payload = repairBody)) +
                    SerialFrameCodec.encode(SerialFrame(SerialFrameType.End, 2u))
            val reads = listOf("RSVPNANO/COMPANION/1 READY\n".encodeToByteArray(), response, repairResponse)
                .joinToString("|") { Base64.encode(it) }
            installFakeSerial(reads)
            val api = WebSerialNanoApi()

            assertTrue(api.open())
            assertFalse(installerCanOpenFakeSerial())

            val device = api.fetchDevice("usb://active")
            assertEquals("RSVP-Nano", device.ssid)
            assertEquals("0.0.9", device.firmwareVersion)
            assertEquals(1, api.repairStorage("usb://active").moved)

            api.release()

            assertTrue(installerCanOpenFakeSerial())
            assertEquals(2, fakeSerialOpenCount())
            assertEquals(2, fakeSerialCloseCount())
            assertEquals(SerialFrameType.Close, fakeSerialFrames().last().type)
        }
    }

    private suspend fun restartInBootloader() {
        suspendCancellableCoroutine { continuation ->
            restartNanoInBootloader(
                { if (continuation.isActive) continuation.resume(Unit) },
                { message -> if (continuation.isActive) continuation.resumeWithException(IllegalStateException(message)) },
            )
        }
    }
}

@JsFun("""(openFails) => { const state = { baudRate: 0, closes: 0, signals: 0 }; const listeners = new Set(); let port; const emitDisconnect = () => listeners.forEach(listener => listener({ target: port, port })); const serial = { requestPort: async () => port, addEventListener: (type, listener) => { if (type === 'disconnect') listeners.add(listener); }, removeEventListener: (type, listener) => { if (type === 'disconnect') listeners.delete(listener); } }; port = { getInfo: () => ({ usbVendorId: 0x303a, usbProductId: 0x1001 }), open: async options => { state.baudRate = options.baudRate; if (openFails) { setTimeout(emitDisconnect, 0); throw new Error('Failed to open port'); } }, close: async () => { state.closes++; emitDisconnect(); }, setSignals: async () => { state.signals++; } }; globalThis.rsvpNanoBootloaderReset = state; Object.defineProperty(navigator, 'serial', { configurable: true, value: serial }); }""")
private external fun installBootloaderResetSerial(openFails: Boolean)

@JsFun("""() => { const state = globalThis.rsvpNanoBootloaderReset; return state.baudRate + '|' + state.closes + '|' + state.signals; }""")
private external fun bootloaderResetState(): String

@JsFun("""(encodedReads) => { const decode = encoded => { const text = atob(encoded); const bytes = new Uint8Array(text.length); for (let i = 0; i < text.length; i++) bytes[i] = text.charCodeAt(i); return bytes; }; const reads = encodedReads.split('|').map(decode); const state = { opened: false, opens: 0, closes: 0, reads, writes: [], pendingRead: null }; const port = { getInfo: () => ({ usbVendorId: 0x303a, usbProductId: 0x1001 }), open: async () => { if (state.opened) throw new Error('Port is already open'); state.opened = true; state.opens++; }, close: async () => { state.opened = false; state.closes++; }, readable: { getReader: () => ({ read: async () => state.reads.length ? { value: state.reads.shift(), done: false } : new Promise(resolve => { state.pendingRead = resolve; }), cancel: async () => { state.pendingRead?.({ done: true }); state.pendingRead = null; }, releaseLock: () => {} }) }, writable: { getWriter: () => ({ write: async data => { state.writes.push(new Uint8Array(data)); }, close: async () => {}, releaseLock: () => {} }) } }; state.port = port; globalThis.rsvpNanoFakeSerial = state; Object.defineProperty(navigator, 'serial', { configurable: true, value: { getPorts: async () => [port], requestPort: async () => port } }); localStorage.removeItem('rsvpnano.web.usbDevice'); }""")
private external fun installFakeSerial(encodedReads: String)

private suspend fun installerCanOpenFakeSerial(): Boolean = suspendCancellableCoroutine { continuation ->
    tryOpenFakeSerial { opened ->
        if (continuation.isActive) continuation.resume(opened)
    }
}

@JsFun("""(done) => { (async () => { const state = globalThis.rsvpNanoFakeSerial; try { await state.port.open({ baudRate: 115200 }); await state.port.close(); done(true); } catch (_) { done(false); } })(); }""")
private external fun tryOpenFakeSerial(done: (Boolean) -> Unit)

@JsFun("""() => globalThis.rsvpNanoFakeSerial.opens""")
private external fun fakeSerialOpenCount(): Int

@JsFun("""() => globalThis.rsvpNanoFakeSerial.closes""")
private external fun fakeSerialCloseCount(): Int

private fun fakeSerialFrames(): List<SerialFrame> {
    val decoder = SerialFrameCodec.Decoder()
    return fakeSerialWrites()
        .split('|')
        .filter { it.isNotEmpty() }
        .flatMap { decoder.feed(Base64.decode(it)) }
}

@JsFun("""() => globalThis.rsvpNanoFakeSerial.writes.map(bytes => { let text = ''; for (let i = 0; i < bytes.length; i++) text += String.fromCharCode(bytes[i]); return btoa(text); }).join('|')""")
private external fun fakeSerialWrites(): String
