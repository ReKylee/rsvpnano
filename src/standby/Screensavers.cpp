#include "standby/Screensaver.h"

namespace standby {

    void ScreensaverSlot::select(Kind kind, uint16_t columns, uint16_t rows) {
        reset();

        switch (kind) {
        case Kind::maze: {
            kind_ = Kind::maze;
            auto& saver = storage_.emplace<MazeScreensaver>();
            saver.reset(columns, rows);
            break;
        }

        case Kind::voronoi: {
            kind_ = Kind::voronoi;
            auto& saver = storage_.emplace<VoronoiScreensaver>();
            saver.reset(columns, rows);
            break;
        }

        case Kind::reaction: {
            kind_ = Kind::reaction;
            auto& saver = storage_.emplace<ReactionScreensaver>();
            saver.reset(columns, rows);
            break;
        }

        case Kind::screenOff:
            kind_ = Kind::screenOff;
            break;

        case Kind::life:
        case Kind::Count:
        default: {
            kind_ = Kind::life;
            auto& saver = storage_.emplace<LifeScreensaver>();
            saver.reset(columns, rows);
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
            std::get<MazeScreensaver>(storage_).seed(rngSeed);
            break;
        case Kind::voronoi:
            std::get<VoronoiScreensaver>(storage_).seed(rngSeed);
            break;
        case Kind::reaction:
            std::get<ReactionScreensaver>(storage_).seed(rngSeed);
            break;
        case Kind::screenOff:
            break;
        case Kind::life:
        default:
            std::get<LifeScreensaver>(storage_).seed(rngSeed);
            break;
        }
    }

    void ScreensaverSlot::step() {
        if (!*this) {
            return;
        }

        switch (kind_) {
        case Kind::maze:
            std::get<MazeScreensaver>(storage_).step();
            break;
        case Kind::voronoi:
            std::get<VoronoiScreensaver>(storage_).step();
            break;
        case Kind::reaction:
            std::get<ReactionScreensaver>(storage_).step();
            break;
        case Kind::screenOff:
            break;
        case Kind::life:
        default:
            std::get<LifeScreensaver>(storage_).step();
            break;
        }
    }

    Frame ScreensaverSlot::frame() const {
        if (!*this) {
            return {};
        }

        switch (kind_) {
        case Kind::maze:
            return std::get<MazeScreensaver>(storage_).frame();
        case Kind::voronoi:
            return std::get<VoronoiScreensaver>(storage_).frame();
        case Kind::reaction:
            return std::get<ReactionScreensaver>(storage_).frame();
        case Kind::screenOff:
            return {};
        case Kind::life:
        default:
            return std::get<LifeScreensaver>(storage_).frame();
        }
    }

} // namespace standby
