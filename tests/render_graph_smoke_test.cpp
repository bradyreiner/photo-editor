#include <cstdlib>
#include <iostream>

#include "photo_editor/pipeline/render_graph.h"

int main() {
  const photo_editor::pipeline::RenderGraph graph;
  const auto& stages = graph.stages();

  if (stages.size() < 7) {
    std::cerr << "expected full render graph scaffold\n";
    return EXIT_FAILURE;
  }

  if (stages.front().name != "raw_decode") {
    std::cerr << "unexpected first stage\n";
    return EXIT_FAILURE;
  }

  if (stages[1].name != "input_transform") {
    std::cerr << "unexpected second stage\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
