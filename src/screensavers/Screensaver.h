#pragma once

#include <cstdint>
#include <memory>
#include <variant>

#include "screensavers/LifeScreensaver.h"
#include "screensavers/MazeScreensaver.h"
#include "screensavers/ReactionScreensaver.h"
#include "screensavers/ScreensaverTypes.h"
#include "screensavers/VoronoiScreensaver.h"

namespace standby {

    class ScreensaverSlot {
    public:
        ScreensaverSlot() = default;
        ~ScreensaverSlot() = default;

        ScreensaverSlot(const ScreensaverSlot&) = delete;
        ScreensaverSlot& operator=(const ScreensaverSlot&) = delete;

        void select(Kind kind, uint16_t columns, uint16_t rows);
        void reset();
        void seed(uint32_t rngSeed);
        void step();
        Frame frame() const;

        explicit operator bool() const {
            return !std::holds_alternative<std::monostate>(storage_);
        }

    private:
        using Storage = std::variant<std::monostate, std::unique_ptr<LifeScreensaver>,
                                     std::unique_ptr<MazeScreensaver>, std::unique_ptr<ReactionScreensaver>,
                                     std::unique_ptr<VoronoiScreensaver>>;

        Storage storage_;
        Kind kind_ = Kind::life;
    };

} // namespace standby
