#include "standby/LifeScreensaver.h"

#include <algorithm>

namespace standby {
    namespace {

        constexpr LifePoint kGlider[] = {
            {1, 0}, {2, 1}, {0, 2}, {1, 2}, {2, 2},
        };
        constexpr LifePoint kBlinker[] = {
            {0, 1},
            {1, 1},
            {2, 1},
        };
        constexpr LifePoint kToad[] = {
            {1, 0}, {2, 0}, {3, 0}, {0, 1}, {1, 1}, {2, 1},
        };
        constexpr LifePoint kBeacon[] = {
            {0, 0}, {1, 0}, {0, 1}, {3, 2}, {2, 3}, {3, 3},
        };
        constexpr LifePoint kBlock[] = {
            {0, 0},
            {1, 0},
            {0, 1},
            {1, 1},
        };
        constexpr LifePoint kEater1[] = {
            {0, 0}, {1, 0}, {0, 1}, {2, 1}, {2, 2}, {2, 3}, {3, 3},
        };
        constexpr LifePoint kRpentomino[] = {
            {1, 0}, {2, 0}, {0, 1}, {1, 1}, {1, 2},
        };
        constexpr LifePoint kAcorn[] = {
            {1, 0}, {3, 1}, {0, 2}, {1, 2}, {4, 2}, {5, 2}, {6, 2},
        };
        constexpr LifePoint kDiehard[] = {
            {6, 0}, {0, 1}, {1, 1}, {1, 2}, {5, 2}, {6, 2}, {7, 2},
        };
        constexpr LifePoint kSmallExploder[] = {
            {1, 0}, {0, 1}, {1, 1}, {2, 1}, {0, 2}, {2, 2}, {1, 3},
        };
        constexpr LifePoint kLightweightSpaceship[] = {
            {1, 0}, {4, 0}, {0, 1}, {0, 2}, {4, 2}, {0, 3}, {1, 3}, {2, 3}, {3, 3},
        };
        constexpr LifePoint kPentadecathlon[] = {
            {2, 0}, {2, 1}, {1, 2}, {3, 2}, {2, 3}, {2, 4}, {2, 5}, {2, 6}, {1, 7}, {3, 7}, {2, 8}, {2, 9},
        };
        constexpr LifePoint kPulsar[] = {
            {2, 0}, {3, 0},  {4, 0},  {8, 0},  {9, 0},  {10, 0},  {0, 2},  {5, 2},  {7, 2},  {12, 2}, {0, 3},  {5, 3},
            {7, 3}, {12, 3}, {0, 4},  {5, 4},  {7, 4},  {12, 4},  {2, 5},  {3, 5},  {4, 5},  {8, 5},  {9, 5},  {10, 5},
            {2, 7}, {3, 7},  {4, 7},  {8, 7},  {9, 7},  {10, 7},  {0, 8},  {5, 8},  {7, 8},  {12, 8}, {0, 9},  {5, 9},
            {7, 9}, {12, 9}, {0, 10}, {5, 10}, {7, 10}, {12, 10}, {2, 12}, {3, 12}, {4, 12}, {8, 12}, {9, 12}, {10, 12},
        };
        constexpr LifePoint kGosperGliderGun[] = {
            {24, 0}, {22, 1}, {24, 1}, {12, 2}, {13, 2}, {20, 2}, {21, 2}, {34, 2}, {35, 2}, {11, 3}, {15, 3}, {20, 3},
            {21, 3}, {34, 3}, {35, 3}, {0, 4},  {1, 4},  {10, 4}, {16, 4}, {20, 4}, {21, 4}, {0, 5},  {1, 5},  {10, 5},
            {14, 5}, {16, 5}, {17, 5}, {22, 5}, {24, 5}, {10, 6}, {16, 6}, {24, 6}, {11, 7}, {15, 7}, {12, 8}, {13, 8},
        };

        struct LifePattern {
            const LifePoint* points;
            size_t pointCount;
            uint8_t width;
            uint8_t height;
        };

        enum class LifeTransform : uint8_t {
            Normal,
            MirrorX,
            MirrorY,
            Rotate180,
        };

        template<size_t N>
        constexpr LifePattern pattern(const LifePoint (&points)[N], uint8_t width, uint8_t height) {
            return LifePattern{points, N, width, height};
        }

        constexpr LifePattern kPatternGlider = pattern(kGlider, 3, 3);
        constexpr LifePattern kPatternBlinker = pattern(kBlinker, 3, 3);
        constexpr LifePattern kPatternToad = pattern(kToad, 4, 2);
        constexpr LifePattern kPatternBeacon = pattern(kBeacon, 4, 4);
        constexpr LifePattern kPatternBlock = pattern(kBlock, 2, 2);
        constexpr LifePattern kPatternEater1 = pattern(kEater1, 4, 4);
        constexpr LifePattern kPatternRpentomino = pattern(kRpentomino, 3, 3);
        constexpr LifePattern kPatternAcorn = pattern(kAcorn, 7, 3);
        constexpr LifePattern kPatternDiehard = pattern(kDiehard, 8, 3);
        constexpr LifePattern kPatternSmallExploder = pattern(kSmallExploder, 3, 4);
        constexpr LifePattern kPatternLightweightSpaceship = pattern(kLightweightSpaceship, 5, 4);
        constexpr LifePattern kPatternPentadecathlon = pattern(kPentadecathlon, 5, 10);
        constexpr LifePattern kPatternPulsar = pattern(kPulsar, 13, 13);
        constexpr LifePattern kPatternGosperGliderGun = pattern(kGosperGliderGun, 36, 9);

        int clampOrigin(uint16_t limit, uint8_t size, int origin) {
            const int maxOrigin = std::max(0, static_cast<int>(limit) - static_cast<int>(size));
            return std::clamp(origin, 0, maxOrigin);
        }

        int centeredOrigin(uint16_t limit, uint8_t size) {
            return clampOrigin(limit, size, (static_cast<int>(limit) - static_cast<int>(size)) / 2);
        }

        void stampPattern(IncrementalLifeGrid& life, const LifePattern& pattern, int originX, int originY,
                          LifeTransform transform = LifeTransform::Normal) {
            for (size_t i = 0; i < pattern.pointCount; ++i) {
                int x = pattern.points[i].x;
                int y = pattern.points[i].y;
                switch (transform) {
                case LifeTransform::MirrorX:
                    x = static_cast<int>(pattern.width) - 1 - x;
                    break;
                case LifeTransform::MirrorY:
                    y = static_cast<int>(pattern.height) - 1 - y;
                    break;
                case LifeTransform::Rotate180:
                    x = static_cast<int>(pattern.width) - 1 - x;
                    y = static_cast<int>(pattern.height) - 1 - y;
                    break;
                case LifeTransform::Normal:
                default:
                    break;
                }
                life.setAliveAt(originX + x, originY + y, true);
            }
        }

    } // namespace

    void LifeScreensaver::reset(uint16_t columns, uint16_t rows) {
        columns_ = std::min<uint16_t>(columns, kMaxStandbyColumns);
        rows_ = std::min<uint16_t>(rows, kMaxStandbyRows);
        life_.reset(columns_, rows_);
    }

    void LifeScreensaver::seed(uint32_t rngSeed) {
        rng_ = rngSeed == 0 ? 1U : rngSeed;
        generation_ = 0;
        stableFrames_ = 0;
        life_.reset(columns_, rows_);
        life_.clear();

        static constexpr uint8_t kSceneCount = 3;
        sceneIndex_ = static_cast<uint8_t>((advanceRng(rng_) >> 24) % kSceneCount);
        switch (sceneIndex_) {
        case 0:
            seedCrossfireScene();
            break;
        case 1:
            seedOscillatorGardenScene();
            break;
        case 2:
        default:
            seedMethuselahBurstScene();
            break;
        }

        life_.finishSeed();
    }

    void LifeScreensaver::step() {
        life_.clearFullRedraw();
        const size_t before = life_.liveCount();
        const size_t aliveCount = life_.step();
        ++generation_;

        if (aliveCount == before && !hasDirtyCells()) {
            ++stableFrames_;
        } else {
            stableFrames_ = 0;
        }

        const size_t cellCount = static_cast<size_t>(columns_) * rows_;
        if (aliveCount == 0 || aliveCount > (cellCount * 3) / 4 || stableFrames_ > 90 || generation_ > 1200) {
            seed(advanceRng(rng_) ^ static_cast<uint32_t>(sceneIndex_ + 1U));
        }
    }

    Frame LifeScreensaver::frame() const {
        return Frame{life_.liveCells(), {}, life_.dirtyCells(), generation_, life_.fullRedraw()};
    }

    void LifeScreensaver::seedCrossfireScene() {
        stampPattern(life_, kPatternGosperGliderGun, clampOrigin(columns_, kPatternGosperGliderGun.width, 4),
                     clampOrigin(rows_, kPatternGosperGliderGun.height, 4));
        stampPattern(life_, kPatternGosperGliderGun,
                     clampOrigin(columns_, kPatternGosperGliderGun.width, static_cast<int>(columns_) - 40),
                     clampOrigin(rows_, kPatternGosperGliderGun.height, 4), LifeTransform::MirrorX);

        if (rows_ > 30) {
            stampPattern(life_, kPatternLightweightSpaceship, 8,
                         clampOrigin(rows_, kPatternLightweightSpaceship.height, static_cast<int>(rows_) - 8));
            stampPattern(life_, kPatternLightweightSpaceship,
                         clampOrigin(columns_, kPatternLightweightSpaceship.width, static_cast<int>(columns_) - 14),
                         clampOrigin(rows_, kPatternLightweightSpaceship.height, static_cast<int>(rows_) - 8),
                         LifeTransform::MirrorX);
        }

        const int centerX = centeredOrigin(columns_, 18);
        const int centerY = centeredOrigin(rows_, 10);
        stampPattern(life_, kPatternGlider, centerX, centerY);
        stampPattern(life_, kPatternGlider, centerX + 14, centerY + 7, LifeTransform::Rotate180);
    }

    void LifeScreensaver::seedOscillatorGardenScene() {
        stampPattern(life_, kPatternPulsar, clampOrigin(columns_, kPatternPulsar.width, 8),
                     centeredOrigin(rows_, kPatternPulsar.height));
        stampPattern(life_, kPatternPulsar,
                     clampOrigin(columns_, kPatternPulsar.width, static_cast<int>(columns_) - 22),
                     centeredOrigin(rows_, kPatternPulsar.height));

        stampPattern(life_, kPatternPentadecathlon, centeredOrigin(columns_, kPatternPentadecathlon.width),
                     clampOrigin(rows_, kPatternPentadecathlon.height, 4));
        stampPattern(life_, kPatternPentadecathlon,
                     clampOrigin(columns_, kPatternPentadecathlon.width, static_cast<int>(columns_ / 2) + 14),
                     clampOrigin(rows_, kPatternPentadecathlon.height, static_cast<int>(rows_) - 15),
                     LifeTransform::MirrorY);

        for (uint8_t i = 0; i < 6; ++i) {
            const int x = 28 + static_cast<int>(i) * 18;
            const int y = (i & 1U) != 0 ? 8 : static_cast<int>(rows_) - 10;
            stampPattern(life_, (i % 3 == 0) ? kPatternToad : ((i % 3 == 1) ? kPatternBeacon : kPatternBlinker),
                         clampOrigin(columns_, 4, x), clampOrigin(rows_, 4, y),
                         (i & 2U) != 0 ? LifeTransform::Rotate180 : LifeTransform::Normal);
        }

        stampPattern(life_, kPatternGlider, clampOrigin(columns_, kPatternGlider.width, 42), 3);
        stampPattern(life_, kPatternGlider,
                     clampOrigin(columns_, kPatternGlider.width, static_cast<int>(columns_) - 46),
                     clampOrigin(rows_, kPatternGlider.height, static_cast<int>(rows_) - 6), LifeTransform::Rotate180);
    }

    void LifeScreensaver::seedMethuselahBurstScene() {
        stampPattern(life_, kPatternRpentomino,
                     clampOrigin(columns_, kPatternRpentomino.width, static_cast<int>(columns_ / 2) - 22),
                     centeredOrigin(rows_, kPatternRpentomino.height));
        stampPattern(life_, kPatternAcorn,
                     clampOrigin(columns_, kPatternAcorn.width, static_cast<int>(columns_ / 2) + 12),
                     centeredOrigin(rows_, kPatternAcorn.height) + 6);
        stampPattern(life_, kPatternDiehard,
                     clampOrigin(columns_, kPatternDiehard.width, static_cast<int>(columns_ / 2) - 4),
                     clampOrigin(rows_, kPatternDiehard.height, static_cast<int>(rows_ / 2) - 13));

        stampPattern(life_, kPatternSmallExploder, 12, clampOrigin(rows_, kPatternSmallExploder.height, 8));
        stampPattern(life_, kPatternSmallExploder,
                     clampOrigin(columns_, kPatternSmallExploder.width, static_cast<int>(columns_) - 16),
                     clampOrigin(rows_, kPatternSmallExploder.height, static_cast<int>(rows_) - 13),
                     LifeTransform::Rotate180);

        const uint16_t maxGliderX = columns_ > 4 ? static_cast<uint16_t>(columns_ - 4) : 1;
        const uint16_t maxGliderY = rows_ > 4 ? static_cast<uint16_t>(rows_ - 4) : 1;
        for (uint8_t i = 0; i < 5; ++i) {
            const int x = static_cast<int>((advanceRng(rng_) >> 8) % maxGliderX);
            const int y = static_cast<int>((advanceRng(rng_) >> 8) % maxGliderY);
            stampPattern(life_, kPatternGlider, x, y, static_cast<LifeTransform>((advanceRng(rng_) >> 30) & 0x03));
        }
    }

    bool LifeScreensaver::hasDirtyCells() const {
        return anyCellAlive(life_.dirtyCells());
    }

} // namespace standby
