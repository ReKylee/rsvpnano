#pragma once

#include <cstdint>
#include <type_traits>

#include "standby/LifeScreensaver.h"
#include "standby/MazeScreensaver.h"
#include "standby/ScreensaverTypes.h"
#include "standby/VoronoiScreensaver.h"

namespace standby {

class ScreensaverSlot {
 public:
  ScreensaverSlot() = default;
  ~ScreensaverSlot();

  ScreensaverSlot(const ScreensaverSlot&) = delete;
  ScreensaverSlot& operator=(const ScreensaverSlot&) = delete;

  void select(Kind kind, uint16_t columns, uint16_t rows);
  void reset();
  void seed(uint32_t rngSeed);
  void step();
  Frame frame() const;

  Kind kind() const { return kind_; }
  explicit operator bool() const { return active_; }

 private:
  using Storage = std::aligned_union_t<0, LifeScreensaver, MazeScreensaver, VoronoiScreensaver>;

  LifeScreensaver& life();
  const LifeScreensaver& life() const;
  MazeScreensaver& maze();
  const MazeScreensaver& maze() const;
  VoronoiScreensaver& voronoi();
  const VoronoiScreensaver& voronoi() const;

  Storage storage_{};
  Kind kind_ = Kind::Life;
  bool active_ = false;
};

}  // namespace standby
