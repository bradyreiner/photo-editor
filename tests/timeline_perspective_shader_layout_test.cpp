#include <algorithm>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <iterator>
#include <regex>
#include <string>
#include <utility>
#include <vector>

namespace {

struct Member final {
  std::string type;
  std::string name;
  std::size_t offset = 0;
};

std::string read_file(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return {};
  }
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::size_t align_up(std::size_t value, std::size_t alignment) {
  const auto remainder = value % alignment;
  return remainder == 0 ? value : value + alignment - remainder;
}

std::vector<Member> uniform_members(const std::string& source) {
  static const std::regex block_pattern(
      R"(layout\s*\(\s*std140\s*,\s*binding\s*=\s*0\s*\)\s*uniform\s+buf\s*\{([^}]*)\})");
  static const std::regex member_pattern(
      R"(\b(mat4|vec4|vec2|float)\s+([A-Za-z_][A-Za-z0-9_]*)\s*;)");

  std::smatch block_match;
  if (!std::regex_search(source, block_match, block_pattern)) {
    return {};
  }

  std::vector<Member> result;
  std::size_t offset = 0;
  const auto block = block_match[1].str();
  for (std::sregex_iterator it(block.begin(), block.end(), member_pattern), end;
       it != end; ++it) {
    const auto type = (*it)[1].str();
    const auto name = (*it)[2].str();
    const auto alignment = type == "mat4" || type == "vec4"
        ? std::size_t{16}
        : type == "vec2" ? std::size_t{8} : std::size_t{4};
    const auto size = type == "mat4" ? std::size_t{64}
        : type == "vec4" ? std::size_t{16}
        : type == "vec2" ? std::size_t{8} : std::size_t{4};
    offset = align_up(offset, alignment);
    result.push_back(Member{type, name, offset});
    offset += size;
  }
  return result;
}

bool same_layout(const std::vector<Member>& vertex,
                 const std::vector<Member>& fragment,
                 std::string* mismatch) {
  if (vertex.empty() || fragment.empty()) {
    *mismatch = "a shader is missing the std140 buf uniform block";
    return false;
  }
  if (vertex.size() != fragment.size()) {
    *mismatch = "vertex and fragment uniform member counts differ";
    return false;
  }
  for (std::size_t index = 0; index < vertex.size(); ++index) {
    const auto& lhs = vertex[index];
    const auto& rhs = fragment[index];
    if (lhs.type != rhs.type || lhs.name != rhs.name || lhs.offset != rhs.offset) {
      *mismatch = "member " + std::to_string(index) + " differs: vertex " +
          lhs.type + " " + lhs.name + " @" + std::to_string(lhs.offset) +
          ", fragment " + rhs.type + " " + rhs.name + " @" +
          std::to_string(rhs.offset);
      return false;
    }
  }
  return true;
}

bool has_expected_offsets(const std::vector<Member>& members,
                          std::string* mismatch) {
  const std::pair<const char*, std::size_t> expected[] = {
      {"qt_Matrix", 0},
      {"qt_Opacity", 64},
      {"matteEnabled", 68},
      {"matteFillColor", 96},
      {"itemWidth", 112},
      {"frameQuadEnabled", 128},
      {"frameQuadTop", 144},
      {"frameQuadBottom", 160},
      {"fadeColor", 176},
      {"clipMaskEnabled", 200},
      {"clipMaskFrameRotationDegrees", 252},
  };
  for (const auto& [name, offset] : expected) {
    const auto found = std::find_if(members.cbegin(), members.cend(),
                                    [name](const Member& member) {
      return member.name == name;
    });
    if (found == members.cend() || found->offset != offset) {
      *mismatch = std::string(name) + " must be at std140 byte offset " +
          std::to_string(offset);
      return false;
    }
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "Usage: timeline_perspective_shader_layout_test <vertex.glsl> <fragment.glsl>\n";
    return 2;
  }
  const auto vertex_source = read_file(argv[1]);
  const auto fragment_source = read_file(argv[2]);
  if (vertex_source.empty() || fragment_source.empty()) {
    std::cerr << "Could not read both perspective shader sources\n";
    return 2;
  }

  const auto vertex_members = uniform_members(vertex_source);
  const auto fragment_members = uniform_members(fragment_source);
  std::string mismatch;
  if (!same_layout(vertex_members, fragment_members, &mismatch) ||
      !has_expected_offsets(vertex_members, &mismatch)) {
    std::cerr << "Perspective shader uniform blocks must have identical std140 layout: "
              << mismatch << '\n';
    return 1;
  }
  return 0;
}
