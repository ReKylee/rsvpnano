package com.rsvpnano.converters

sealed class RsvpEvent {
    data object VerticalWriting : RsvpEvent()
    data class Chapter(val title: String) : RsvpEvent()
    data class Text(val text: String, val startsParagraph: Boolean = true) : RsvpEvent()
    data class Language(val locale: String) : RsvpEvent()
    data class Direction(val value: String) : RsvpEvent()
}
