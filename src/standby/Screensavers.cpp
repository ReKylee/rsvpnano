#include "standby/Screensaver.h"

#include <new>

namespace standby {

    ScreensaverSlot::~ScreensaverSlot() {
        reset();
    }

    LifeScreensaver& ScreensaverSlot::life() {
        return *reinterpret_cast<LifeScreensaver*>(&storage_);
    }

    const LifeScreensaver& ScreensaverSlot::life() const {
        return *reinterpret_cast<const LifeScreensaver*>(&storage_);
    }

    MazeScreensaver& ScreensaverSlot::maze() {
        return *reinterpret_cast<MazeScreensaver*>(&storage_);
    }

    const MazeScreensaver& ScreensaverSlot::maze() const {
        return *reinterpret_cast<const MazeScreensaver*>(&storage_);
    }

    ReactionScreensaver& ScreensaverSlot::reaction() {
        return *reinterpret_cast<ReactionScreensaver*>(&storage_);
    }

    const ReactionScreensaver& ScreensaverSlot::reaction() const {
        return *reinterpret_cast<const ReactionScreensaver*>(&storage_);
    }

    VoronoiScreensaver& ScreensaverSlot::voronoi() {
        return *reinterpret_cast<VoronoiScreensaver*>(&storage_);
    }

    const VoronoiScreensaver& ScreensaverSlot::voronoi() const {
        return *reinterpret_cast<const VoronoiScreensaver*>(&storage_);
    }

    void ScreensaverSlot::select(Kind kind, uint16_t columns, uint16_t rows) {
        reset();
        kind_ = kind;
        if (kind == Kind::ScreenOff)
            return;
        active_ = true;

        switch (kind_) {
        case Kind::Maze:
            new (&storage_) MazeScreensaver();
            maze().reset(columns, rows);
            break;
        case Kind::Voronoi:
            new (&storage_) VoronoiScreensaver();
            voronoi().reset(columns, rows);
            break;
        case Kind::Reaction:
            new (&storage_) ReactionScreensaver();
            reaction().reset(columns, rows);
            break;
        case Kind::ScreenOff:
            break;
        case Kind::Life:
        default:
            new (&storage_) LifeScreensaver();
            life().reset(columns, rows);
            break;
        }
    }

    void ScreensaverSlot::reset() {
        if (!active_) {
            return;
        }

        switch (kind_) {
        case Kind::Maze:
            maze().~MazeScreensaver();
            break;
        case Kind::Voronoi:
            voronoi().~VoronoiScreensaver();
            break;
        case Kind::Reaction:
            reaction().~ReactionScreensaver();
            break;
        case Kind::ScreenOff:
            break;
        case Kind::Life:
        default:
            life().~LifeScreensaver();
            break;
        }

        active_ = false;
        kind_ = Kind::Life;
    }

    void ScreensaverSlot::seed(uint32_t rngSeed) {
        if (!active_) {
            return;
        }

        switch (kind_) {
        case Kind::Maze:
            maze().seed(rngSeed);
            break;
        case Kind::Voronoi:
            voronoi().seed(rngSeed);
            break;
        case Kind::Reaction:
            reaction().seed(rngSeed);
            break;
        case Kind::ScreenOff:
            break;
        case Kind::Life:
        default:
            life().seed(rngSeed);
            break;
        }
    }

    void ScreensaverSlot::step() {
        if (!active_) {
            return;
        }

        switch (kind_) {
        case Kind::Maze:
            maze().step();
            break;
        case Kind::Voronoi:
            voronoi().step();
            break;
        case Kind::Reaction:
            reaction().step();
            break;
        case Kind::ScreenOff:
            break;
        case Kind::Life:
        default:
            life().step();
            break;
        }
    }

    Frame ScreensaverSlot::frame() const {
        if (!active_) {
            return {};
        }

        switch (kind_) {
        case Kind::Maze:
            return maze().frame();
        case Kind::Voronoi:
            return voronoi().frame();
        case Kind::Reaction:
            return reaction().frame();
        case Kind::ScreenOff:
            return {};
        case Kind::Life:
        default:
            return life().frame();
        }
    }

} // namespace standby
