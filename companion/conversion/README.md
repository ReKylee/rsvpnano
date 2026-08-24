# conversionCore

Kotlin Multiplatform document conversion shared by the Android, iOS, and web companions.

The module owns `.rsvp`, EPUB, text, Markdown, HTML, and XHTML conversion, including EPUB package,
navigation, chapter, direction, language, and vertical-writing metadata. The Compose web companion
links it directly into the Kotlin/Wasm application, so no separate JavaScript converter is generated
or published.

Run the common and browser tests with:

```bash
bash ./gradlew :conversionCore:testDebugUnitTest :conversionCore:wasmJsBrowserTest
```

Reference inputs and expected `.rsvp` files live in `companion/testdata`.
