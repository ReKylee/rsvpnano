#pragma once

#include <cstdint>
#include <span>

#include "book/BookMetadata.h"
#include "reader/ReaderSettings.h"
#include "reader/ReadingLoop.h"
#include "ui/Ui.h"
#include "ui/screens/Screens.h"

namespace screens {

    class ChaptersScreen {
    public:
        Action draw(ui::Context& ui, std::span<const ChapterMarker> chapters, ReadingLoop& reader,
                    const ReaderSettings& settings, uint32_t nowMs, Screen& screen);

    private:
        const ChapterMarker* source_ = nullptr;
        size_t sourceCount_ = 0;
        size_t centeredIndex_ = 0;
        size_t dragStartIndex_ = 0;
        int16_t offset_ = 0;
        uint16_t lastY_ = 0;
        uint16_t dragDistance_ = 0;
        uint32_t lastTickMs_ = 0;
        int32_t velocity_ = 0;
        int32_t scrollRemainder_ = 0;
        bool dragging_ = false;
    };

} // namespace screens
