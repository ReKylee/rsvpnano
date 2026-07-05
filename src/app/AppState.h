#pragma once

enum class AppState {
    Booting,
    ReaderPaused,
    ReaderPlaying,
    Menu,
    Sync,
    UsbTransfer,
    FocusTimer,
    Ota,
    Standby,
    Sleeping,
};
