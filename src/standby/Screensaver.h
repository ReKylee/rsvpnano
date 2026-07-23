#pragma once

#include <cstdint>
#include <variant>

#include "standby/LifeScreensaver.h"
#include "standby/MazeScreensaver.h"
#include "standby/ReactionScreensaver.h"
#include "standby/ScreensaverTypes.h"
#include "standby/VoronoiScreensaver.h"

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
        using Storage =
            std::variant<std::monostate, LifeScreensaver, MazeScreensaver, ReactionScreensaver, VoronoiScreensaver>;

        Storage storage_;
        Kind kind_ = Kind::life;
    };

} // namespace standby
