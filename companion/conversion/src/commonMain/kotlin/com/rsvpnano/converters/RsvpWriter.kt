package com.rsvpnano.converters

internal class RsvpWriter(
    title: String,
    author: String,
    source: String,
) {
    private val title = RsvpTextUtils.directiveValue(title).take(256).ifEmpty { "Untitled" }
    private val author = RsvpTextUtils.directiveValue(author).take(256)
    private val lines = mutableListOf("@rsvp 1", "@title $title")
    private var wordCount = 0
    private var chapterCount = 0
    private var lineWords = mutableListOf<String>()
    private var lineLength = 0
    private var lastChapter = ""
    private var language = "und"
    private var direction = "auto"
    private var verticalWriting = false

    init {
        val cleanedSource = RsvpTextUtils.directiveValue(source).take(512)
        if (cleanedSource.isNotEmpty()) {
            lines += "@source $cleanedSource"
        }
        if (this.author.isNotEmpty()) {
            lines += "@author ${this.author}"
        }
        lines += ""
    }

    fun addChapter(value: String) {
        val chapter = RsvpTextUtils.directiveValue(value).take(256)
        if (chapter.isEmpty() || chapter == lastChapter) {
            return
        }
        flushLine()
        if (lines.lastOrNull() != "") {
            lines += ""
        }
        lines += "@chapter $chapter"
        chapterCount += 1
        lastChapter = chapter
    }

    fun beginParagraph() {
        flushLine()
        if (wordCount > 0) {
            if (lines.lastOrNull() != "") {
                lines += ""
            }
            lines += "@para"
        }
    }

    fun setLanguage(locale: String) {
        val next = RsvpTextUtils.directiveValue(locale).take(64).ifEmpty { "und" }
        if (next == language) return
        language = next
        addDirective("language", next)
    }

    fun setDirection(direction: String) {
        val next = RsvpTextUtils.directiveValue(direction).takeIf { it == "ltr" || it == "rtl" } ?: "auto"
        if (next == this.direction) return
        this.direction = next
        addDirective("direction", next)
    }

    fun setVerticalWriting() {
        if (verticalWriting) return
        verticalWriting = true
        addDirective("writing-mode", "vertical-rl")
    }

    fun addEvents(events: Iterable<RsvpEvent>) {
        events.forEach { event ->
            when (event) {
                RsvpEvent.VerticalWriting -> setVerticalWriting()
                is RsvpEvent.Chapter -> addChapter(event.title)
                is RsvpEvent.Text -> {
                    if (event.startsParagraph) beginParagraph()
                    addText(event.text)
                }
                is RsvpEvent.Language -> setLanguage(event.locale)
                is RsvpEvent.Direction -> setDirection(event.value)
            }
        }
    }

    fun hasReadableText(): Boolean = wordCount > 0

    fun addText(text: String) {
        val readableTokens = RsvpTextUtils.cleanWordTokens(text)
        var readableIndex = 0
        RsvpTextUtils.outputTokens(text).forEach { word ->
            val separated = lineWords.isNotEmpty()
                && !(RsvpTextUtils.isCjkToken(lineWords.last()) && RsvpTextUtils.isCjkToken(word))
            val projected = if (lineWords.isEmpty()) word.length else lineLength + (if (separated) 1 else 0) + word.length
            if (lineWords.isNotEmpty() && projected > RsvpConverter.wrapWidth) {
                flushLine()
            }
            val appendSpace = lineWords.isNotEmpty()
                && !(RsvpTextUtils.isCjkToken(lineWords.last()) && RsvpTextUtils.isCjkToken(word))
            lineWords += word
            lineLength = if (lineWords.size == 1) word.length else lineLength + (if (appendSpace) 1 else 0) + word.length
            if (readableIndex < readableTokens.size && word == readableTokens[readableIndex]) {
                wordCount += 1
                readableIndex += 1
            }
        }
    }

    fun finalize(fallbackChapterTitle: String): RsvpBookFile {
        flushLine()
        if (wordCount == 0) {
            throw RsvpConversionError.emptyText
        }
        if (chapterCount == 0) {
            val chapter = "@chapter ${RsvpTextUtils.directiveValue(fallbackChapterTitle)}"
            val index = lines.indexOfFirst { it.isEmpty() }
            if (index >= 0) {
                lines.add(index, chapter)
            } else {
                lines += chapter
            }
        }

        val body = lines.joinToString("\n").trim() + "\n"
        return RsvpBookFile(
            filename = "${RsvpTextUtils.filenameSafe(title)}.rsvp",
            data = body.encodeToByteArray(),
            title = title,
            wordCount = wordCount,
            chapterCount = maxOf(chapterCount, 1),
        )
    }

    private fun flushLine() {
        if (lineWords.isEmpty()) {
            return
        }
        var line = buildString(lineLength) {
            lineWords.forEachIndexed { index, word ->
                if (index > 0 && !(RsvpTextUtils.isCjkToken(lineWords[index - 1]) && RsvpTextUtils.isCjkToken(word)))
                    append(' ')
                append(word)
            }
        }
        if (line.startsWith("@")) {
            line = "@$line"
        }
        lines += line
        lineWords = mutableListOf()
        lineLength = 0
    }

    private fun addDirective(name: String, value: String) {
        flushLine()
        val directive = "@$name ${RsvpTextUtils.directiveValue(value)}"
        for (index in lines.indices.reversed()) {
            if (!lines[index].startsWith("@language ") &&
                !lines[index].startsWith("@direction ") &&
                !lines[index].startsWith("@writing-mode ")
            ) break
            if (lines[index].startsWith("@$name ")) {
                lines[index] = directive
                return
            }
        }
        if (wordCount == 0) {
            lines.add(lines.indexOf(""), directive)
        } else {
            lines += directive
        }
    }
}
