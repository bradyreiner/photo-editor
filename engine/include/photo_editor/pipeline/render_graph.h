#pragma once

#include <string>
#include <vector>

namespace photo_editor::pipeline {

struct RenderStage final {
  std::string name;
  std::string description;
};

class RenderGraph final {
 public:
  RenderGraph();

  [[nodiscard]] const std::vector<RenderStage>& stages() const noexcept;

 private:
  std::vector<RenderStage> stages_;
};

}  // namespace photo_editor::pipeline
