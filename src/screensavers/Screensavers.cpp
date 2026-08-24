#include "screensavers/Screensaver.h"

#include <new>

namespace standby {

    void ScreensaverSlot::select(Kind kind, uint16_t columns, uint16_t rows) {
        reset();

        switch (kind) {
        case Kind::maze: {
            auto saver = std::unique_ptr<MazeScreensaver>{new (std::nothrow) MazeScreensaver};
            if (!saver)
                return;
            saver->reset(columns, rows);
            storage_ = std::move(saver);
            kind_ = Kind::maze;
            break;
        }

        case Kind::voronoi: {
            auto saver = std::unique_ptr<VoronoiScreensaver>{new (std::nothrow) VoronoiScreensaver};
            if (!saver)
                return;
            saver->reset(columns, rows);
            storage_ = std::move(saver);
            kind_ = Kind::voronoi;
            break;
        }

        case Kind::reaction: {
            auto saver = std::unique_ptr<ReactionScreensaver>{new (std::nothrow) ReactionScreensaver};
            if (!saver)
                return;
            saver->reset(columns, rows);
            storage_ = std::move(saver);
            kind_ = Kind::reaction;
            break;
        }

        case Kind::screenOff:
            kind_ = Kind::screenOff;
            break;

        case Kind::life:
        case Kind::Count:
        default: {
            auto saver = std::unique_ptr<LifeScreensaver>{new (std::nothrow) LifeScreensaver};
            if (!saver)
                return;
            saver->reset(columns, rows);
            storage_ = std::move(saver);
            kind_ = Kind::life;
            break;
        }
        }
    }

    void ScreensaverSlot::reset() {
        storage_.emplace<std::monostate>();
        kind_ = Kind::life;
    }

    void ScreensaverSlot::seed(uint32_t rngSeed) {
        if (!*this) {
            return;
        }

        switch (kind_) {
        case Kind::maze:
            std::get<std::unique_ptr<MazeScreensaver>>(storage_)->seed(rngSeed);
            break;
        case Kind::voronoi:
            std::get<std::unique_ptr<VoronoiScreensaver>>(storage_)->seed(rngSeed);
            break;
        case Kind::reaction:
            std::get<std::unique_ptr<ReactionScreensaver>>(storage_)->seed(rngSeed);
            break;
        case Kind::screenOff:
            break;
        case Kind::life:
        default:
            std::get<std::unique_ptr<LifeScreensaver>>(storage_)->seed(rngSeed);
            break;
        }
    }

    void ScreensaverSlot::step() {
        if (!*this) {
            return;
        }

        switch (kind_) {
        case Kind::maze:
            std::get<std::unique_ptr<MazeScreensaver>>(storage_)->step();
            break;
        case Kind::voronoi:
            std::get<std::unique_ptr<VoronoiScreensaver>>(storage_)->step();
            break;
        case Kind::reaction:
            std::get<std::unique_ptr<ReactionScreensaver>>(storage_)->step();
            break;
        case Kind::screenOff:
            break;
        case Kind::life:
        default:
            std::get<std::unique_ptr<LifeScreensaver>>(storage_)->step();
            break;
        }
    }

    Frame ScreensaverSlot::frame() const {
        if (!*this) {
            return {};
        }

        switch (kind_) {
        case Kind::maze:
            return std::get<std::unique_ptr<MazeScreensaver>>(storage_)->frame();
        case Kind::voronoi:
            return std::get<std::unique_ptr<VoronoiScreensaver>>(storage_)->frame();
        case Kind::reaction:
            return std::get<std::unique_ptr<ReactionScreensaver>>(storage_)->frame();
        case Kind::screenOff:
            return {};
        case Kind::life:
        default:
            return std::get<std::unique_ptr<LifeScreensaver>>(storage_)->frame();
        }
    }

} // namespace standby
