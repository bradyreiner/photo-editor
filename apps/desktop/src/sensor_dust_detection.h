#pragma once

#include <vector>

#include <QByteArray>

class QImage;

namespace photo_editor::desktop {

struct DustSpotCandidate final {
  int x = 0;
  int y = 0;
  double radius = 0.0;
  double score = 0.0;
};

[[nodiscard]] std::vector<DustSpotCandidate> detect_dust_spots(
    const QImage& image,
    int maximum_spots);

[[nodiscard]] QByteArray dust_spot_mask_data(
    int width,
    int height,
    const std::vector<DustSpotCandidate>& spots);

}  // namespace photo_editor::desktop
