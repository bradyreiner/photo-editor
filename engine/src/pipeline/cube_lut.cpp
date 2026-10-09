#include "photo_editor/pipeline/color_adjustments.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <span>

namespace photo_editor::pipeline {

namespace {

constexpr int kMinCubeLutSize = 2;
constexpr int kMaxCubeLutSize = 128;

[[nodiscard]] std::string trim_copy(std::string_view value) {
  auto first = std::size_t{0};
  while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])) != 0) {
    ++first;
  }
  auto last = value.size();
  while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1])) != 0) {
    --last;
  }
  return std::string(value.substr(first, last - first));
}

[[nodiscard]] std::string strip_comment(std::string_view line) {
  if (const auto comment = line.find('#'); comment != std::string_view::npos) {
    line = line.substr(0, comment);
  }
  return trim_copy(line);
}

[[nodiscard]] std::string upper_copy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::toupper(ch));
  });
  return value;
}

[[nodiscard]] std::string lower_copy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

[[nodiscard]] std::string unquote_title(std::string value) {
  value = trim_copy(value);
  if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
    value = value.substr(1, value.size() - 2);
  }
  std::string output;
  output.reserve(value.size());
  auto escaping = false;
  for (const auto ch : value) {
    if (escaping) {
      output.push_back(ch);
      escaping = false;
    } else if (ch == '\\') {
      escaping = true;
    } else {
      output.push_back(ch);
    }
  }
  if (escaping) {
    output.push_back('\\');
  }
  return output;
}

[[nodiscard]] std::string quote_title(std::string_view value) {
  std::string output;
  output.reserve(value.size() + 2);
  output.push_back('"');
  for (const auto ch : value) {
    if (ch == '"' || ch == '\\') {
      output.push_back('\\');
    }
    output.push_back(ch);
  }
  output.push_back('"');
  return output;
}

[[nodiscard]] bool read_three_doubles(
    const std::string& text,
    std::array<double, 3>* values,
    std::string* error_message) {
  std::istringstream stream(text);
  double red = 0.0;
  double green = 0.0;
  double blue = 0.0;
  std::string extra;
  if (!(stream >> red >> green >> blue) || (stream >> extra)) {
    if (error_message != nullptr) {
      *error_message = "Expected three numeric values.";
    }
    return false;
  }
  if (!std::isfinite(red) || !std::isfinite(green) || !std::isfinite(blue)) {
    if (error_message != nullptr) {
      *error_message = "Cube LUT values must be finite.";
    }
    return false;
  }
  *values = {red, green, blue};
  return true;
}

[[nodiscard]] bool read_two_doubles(
    const std::string& text,
    std::array<double, 2>* values,
    std::string* error_message) {
  std::istringstream stream(text);
  double minimum = 0.0;
  double maximum = 0.0;
  std::string extra;
  if (!(stream >> minimum >> maximum) || (stream >> extra)) {
    if (error_message != nullptr) {
      *error_message = "Expected two numeric values.";
    }
    return false;
  }
  if (!std::isfinite(minimum) || !std::isfinite(maximum) || maximum <= minimum) {
    if (error_message != nullptr) {
      *error_message = "The 1D input range must be finite and increasing.";
    }
    return false;
  }
  *values = {minimum, maximum};
  return true;
}

[[nodiscard]] std::string line_error(int line_number, std::string_view message) {
  return "Line " + std::to_string(line_number) + ": " + std::string(message);
}

[[nodiscard]] bool valid_lut_shape(const ColorLut3D& lut) {
  if (!lut.valid) {
    return false;
  }
  const auto has_3d = lut.size > 0;
  const auto has_1d = lut.shaper_size > 0;
  const auto valid_3d = has_3d
                            ? lut.size >= kMinCubeLutSize &&
                                  lut.size <= kMaxCubeLutSize &&
                                  lut.table.size() ==
                                      static_cast<std::size_t>(lut.size) *
                                          static_cast<std::size_t>(lut.size) *
                                          static_cast<std::size_t>(lut.size)
                            : lut.table.empty();
  const auto valid_1d = has_1d
                            ? lut.shaper_size >= 2 && lut.shaper_size <= 65536 &&
                                  lut.shaper_table.size() ==
                                      static_cast<std::size_t>(lut.shaper_size) &&
                                  std::isfinite(lut.shaper_input_range[0]) &&
                                  std::isfinite(lut.shaper_input_range[1]) &&
                                  lut.shaper_input_range[1] > lut.shaper_input_range[0]
                            : lut.shaper_table.empty();
  return (has_3d || has_1d) && valid_3d && valid_1d;
}

[[nodiscard]] bool write_cube_lut_stream(
    std::ostream& output,
    const ColorLut3D& lut,
    std::string_view title) {
  if (!valid_lut_shape(lut)) {
    return false;
  }
  output << "# PhotoEditor cube LUT\n";
  if (!title.empty()) {
    output << "TITLE " << quote_title(title) << '\n';
  }
  if (!lut.input_color_space.empty()) {
    output << "# PHOTO_EDITOR_INPUT_COLOR_SPACE " << lut.input_color_space << '\n';
  }
  if (!lut.output_color_space.empty()) {
    output << "# PHOTO_EDITOR_OUTPUT_COLOR_SPACE " << lut.output_color_space << '\n';
  }
  output << std::setprecision(9);
  if (lut.shaper_size > 0) {
    output << "LUT_1D_SIZE " << lut.shaper_size << '\n';
    output << "LUT_1D_INPUT_RANGE " << lut.shaper_input_range[0] << ' '
           << lut.shaper_input_range[1] << '\n';
  }
  if (lut.size > 0) {
    output << "LUT_3D_SIZE " << lut.size << '\n';
    output << "DOMAIN_MIN " << lut.domain_min[0] << ' ' << lut.domain_min[1] << ' '
           << lut.domain_min[2] << '\n';
    output << "DOMAIN_MAX " << lut.domain_max[0] << ' ' << lut.domain_max[1] << ' '
           << lut.domain_max[2] << '\n';
  }
  for (const auto& entry : lut.shaper_table) {
    output << entry.red << ' ' << entry.green << ' ' << entry.blue << '\n';
  }
  for (const auto& entry : lut.table) {
    output << entry.red << ' ' << entry.green << ' ' << entry.blue << '\n';
  }
  return output.good();
}

[[nodiscard]] std::string lut_format_for_extension(std::string extension) {
  extension = lower_copy(std::move(extension));
  if (extension == ".cube") {
    return "cube";
  }
  if (extension == ".png" || extension == ".tif" || extension == ".tiff") {
    return "hald";
  }
  return {};
}

[[nodiscard]] int hald_level_for_dimensions(int width, int height) {
  if (width <= 0 || width != height) {
    return 0;
  }
  for (int level = 2; level * level <= kMaxCubeLutSize; ++level) {
    if (level * level * level == width) {
      return level;
    }
  }
  return 0;
}

}  // namespace

static CubeLutParseResult parse_cube_lut_stream(std::istream& lines) {
  CubeLutParseResult result;
  auto domain_min = std::array<double, 3>{0.0, 0.0, 0.0};
  auto domain_max = std::array<double, 3>{1.0, 1.0, 1.0};
  auto shaper_input_range = std::array<double, 2>{0.0, 1.0};
  auto input_color_space = std::string{"sRGB"};
  auto output_color_space = std::string{"sRGB"};
  auto size_1d = 0;
  auto size_3d = 0;
  auto shaper_range_declared = false;
  auto saw_table_data = false;
  std::vector<ColorPixel> shaper_table;
  std::vector<ColorPixel> table;

  std::string raw_line;
  auto line_number = 0;
  while (std::getline(lines, raw_line)) {
    ++line_number;
    const auto trimmed_raw_line = trim_copy(raw_line);
    if (!trimmed_raw_line.empty() && trimmed_raw_line.front() == '#') {
      std::istringstream tag_stream(trim_copy(std::string_view(trimmed_raw_line).substr(1)));
      std::string tag;
      tag_stream >> tag;
      auto tag_value = std::string{};
      std::getline(tag_stream, tag_value);
      tag_value = trim_copy(tag_value);
      const auto upper_tag = upper_copy(tag);
      if ((upper_tag == "PHOTO_EDITOR_INPUT_COLOR_SPACE" || upper_tag == "INPUT_COLOR_SPACE") &&
          !tag_value.empty()) {
        input_color_space = tag_value;
      } else if ((upper_tag == "PHOTO_EDITOR_OUTPUT_COLOR_SPACE" || upper_tag == "OUTPUT_COLOR_SPACE") &&
                 !tag_value.empty()) {
        output_color_space = tag_value;
      }
    }
    const auto line = strip_comment(raw_line);
    if (line.empty()) {
      continue;
    }

    std::istringstream tokens(line);
    std::string keyword;
    tokens >> keyword;
    const auto upper_keyword = upper_copy(keyword);
    if (upper_keyword == "TITLE") {
      auto title_text = std::string{};
      std::getline(tokens, title_text);
      result.title = unquote_title(title_text);
      continue;
    }
    if (upper_keyword == "LUT_1D_SIZE") {
      int parsed_size = 0;
      std::string extra;
      if (saw_table_data) {
        result.error_message = line_error(line_number, "LUT_1D_SIZE must appear before table values.");
        return result;
      }
      if (size_1d != 0) {
        result.error_message = line_error(line_number, "Cube LUT contains more than one LUT_1D_SIZE directive.");
        return result;
      }
      if (!(tokens >> parsed_size) || (tokens >> extra)) {
        result.error_message = line_error(line_number, "Expected LUT_1D_SIZE followed by one integer.");
        return result;
      }
      if (parsed_size < 2 || parsed_size > 65536) {
        result.error_message = line_error(line_number, "LUT_1D_SIZE must be between 2 and 65536.");
        return result;
      }
      size_1d = parsed_size;
      shaper_table.reserve(static_cast<std::size_t>(size_1d));
      continue;
    }
    if (upper_keyword == "LUT_3D_SIZE") {
      int parsed_size = 0;
      std::string extra;
      if (saw_table_data) {
        result.error_message = line_error(line_number, "LUT_3D_SIZE must appear before table values.");
        return result;
      }
      if (size_3d != 0) {
        result.error_message = line_error(line_number, "Cube LUT contains more than one LUT_3D_SIZE directive.");
        return result;
      }
      if (!(tokens >> parsed_size) || (tokens >> extra)) {
        result.error_message = line_error(line_number, "Expected LUT_3D_SIZE followed by one integer.");
        return result;
      }
      if (parsed_size < kMinCubeLutSize || parsed_size > kMaxCubeLutSize) {
        result.error_message = line_error(line_number, "LUT_3D_SIZE must be between 2 and 128.");
        return result;
      }
      size_3d = parsed_size;
      const auto edge = static_cast<std::size_t>(size_3d);
      const auto entry_count = edge * edge * edge;
      table.reserve(entry_count);
      continue;
    }
    if (upper_keyword == "LUT_1D_INPUT_RANGE") {
      auto values_text = std::string{};
      std::getline(tokens, values_text);
      std::string error;
      if (saw_table_data) {
        result.error_message = line_error(line_number, "LUT_1D_INPUT_RANGE must appear before table values.");
        return result;
      }
      if (shaper_range_declared ||
          !read_two_doubles(values_text, &shaper_input_range, &error)) {
        result.error_message = line_error(
            line_number,
            shaper_range_declared ? "Cube LUT contains more than one LUT_1D_INPUT_RANGE directive."
                                  : error);
        return result;
      }
      shaper_range_declared = true;
      continue;
    }
    if (upper_keyword == "DOMAIN_MIN" || upper_keyword == "DOMAIN_MAX") {
      auto values_text = std::string{};
      std::getline(tokens, values_text);
      std::array<double, 3> values{};
      std::string error;
      if (saw_table_data) {
        result.error_message = line_error(line_number, "CUBE domain directives must appear before table values.");
        return result;
      }
      if (!read_three_doubles(values_text, &values, &error)) {
        result.error_message = line_error(line_number, error);
        return result;
      }
      if (upper_keyword == "DOMAIN_MIN") {
        domain_min = values;
      } else {
        domain_max = values;
      }
      continue;
    }

    std::array<double, 3> values{};
    std::string error;
    if (!read_three_doubles(line, &values, &error)) {
      result.error_message = line_error(line_number, "Unsupported cube LUT directive.");
      return result;
    }
    if (size_1d == 0 && size_3d == 0) {
      result.error_message = line_error(line_number, "LUT_1D_SIZE or LUT_3D_SIZE must appear before table values.");
      return result;
    }
    saw_table_data = true;
    const auto edge = static_cast<std::size_t>(size_3d);
    const auto expected_3d_count = size_3d > 0 ? edge * edge * edge : 0U;
    const auto pixel = ColorPixel{
        .red = static_cast<float>(values[0]),
        .green = static_cast<float>(values[1]),
        .blue = static_cast<float>(values[2]),
        .alpha = 1.0F,
    };
    if (size_1d > 0 && shaper_table.size() < static_cast<std::size_t>(size_1d)) {
      shaper_table.push_back(pixel);
      continue;
    }
    if (size_3d == 0) {
      result.error_message = line_error(line_number, "Cube LUT contains more table rows than LUT_1D_SIZE allows.");
      return result;
    }
    if (table.size() >= expected_3d_count) {
      result.error_message = line_error(line_number, "Cube LUT contains more table rows than LUT_3D_SIZE allows.");
      return result;
    }
    table.push_back(pixel);
  }

  if (size_1d == 0 && size_3d == 0) {
    result.error_message = "Cube LUT is missing LUT_1D_SIZE and LUT_3D_SIZE.";
    return result;
  }
  if (shaper_range_declared && size_1d == 0) {
    result.error_message = "LUT_1D_INPUT_RANGE requires LUT_1D_SIZE.";
    return result;
  }
  if (size_1d > 0 && shaper_table.size() != static_cast<std::size_t>(size_1d)) {
    result.error_message = "Cube LUT shaper row count does not match LUT_1D_SIZE.";
    return result;
  }
  for (std::size_t index = 0; index < domain_min.size(); ++index) {
    if (!std::isfinite(domain_min[index]) || !std::isfinite(domain_max[index]) ||
        domain_max[index] <= domain_min[index]) {
      result.error_message = "Cube LUT DOMAIN_MAX values must be greater than DOMAIN_MIN values.";
      return result;
    }
  }
  const auto edge = static_cast<std::size_t>(size_3d);
  const auto expected_3d_count = size_3d > 0 ? edge * edge * edge : 0U;
  if (table.size() != expected_3d_count) {
    result.error_message = "Cube LUT table row count does not match LUT_3D_SIZE.";
    return result;
  }

  result.lut.table = std::move(table);
  result.lut.size = size_3d;
  result.lut.domain_min = domain_min;
  result.lut.domain_max = domain_max;
  result.lut.input_color_space = input_color_space;
  result.lut.output_color_space = output_color_space;
  result.lut.shaper_table = std::move(shaper_table);
  result.lut.shaper_size = size_1d;
  result.lut.shaper_input_range = shaper_input_range;
  result.lut.valid = true;
  if (!valid_lut_shape(result.lut)) {
    result.error_message = "Cube LUT payload is invalid.";
    result.lut.valid = false;
  }
  return result;
}

CubeLutParseResult parse_cube_lut(std::string_view text) {
  std::istringstream lines{std::string(text)};
  return parse_cube_lut_stream(lines);
}

CubeLutParseResult load_cube_lut_file(std::string_view file_path) {
  CubeLutParseResult result;
  std::ifstream file(std::string(file_path), std::ios::binary);
  if (!file) {
    result.error_message = "Unable to open cube LUT file.";
    return result;
  }
  return parse_cube_lut_stream(file);
}

std::string serialize_cube_lut(const ColorLut3D& lut, std::string_view title) {
  std::ostringstream output;
  if (!write_cube_lut_stream(output, lut, title)) {
    return {};
  }
  return output.str();
}

bool save_cube_lut_file(
    std::string_view file_path,
    const ColorLut3D& lut,
    std::string_view title,
    std::string* error_message) {
  if (!valid_lut_shape(lut)) {
    if (error_message != nullptr) {
      *error_message = "Cannot export an invalid cube LUT.";
    }
    return false;
  }
  std::ofstream file(std::string(file_path), std::ios::binary | std::ios::trunc);
  if (!file) {
    if (error_message != nullptr) {
      *error_message = "Unable to write cube LUT file.";
    }
    return false;
  }
  if (!write_cube_lut_stream(file, lut, title)) {
    if (error_message != nullptr) {
      *error_message = "Cube LUT file write failed.";
    }
    return false;
  }
  return true;
}

CubeLutParseResult parse_hald_lut(std::span<const ColorPixel> pixels, int width, int height) {
  CubeLutParseResult result;
  const auto level = hald_level_for_dimensions(width, height);
  if (level == 0) {
    result.error_message = "HALD LUT image must be square with width and height equal to level^3.";
    return result;
  }
  const auto size = level * level;
  const auto expected_count =
      static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
  if (pixels.size() != expected_count) {
    result.error_message = "HALD LUT pixel count does not match its image dimensions.";
    return result;
  }

  result.lut.size = size;
  result.lut.table.resize(expected_count);
  const auto cube_size = static_cast<std::size_t>(size);
  const auto image_width = static_cast<std::size_t>(width);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto red_index = static_cast<std::size_t>(x % size);
      const auto green_index = static_cast<std::size_t>((x / size) + (level * (y % level)));
      const auto blue_index = static_cast<std::size_t>(y / level);
      const auto table_index = (red_index * cube_size + green_index) * cube_size + blue_index;
      const auto image_index = static_cast<std::size_t>(y) * image_width + static_cast<std::size_t>(x);
      auto pixel = pixels[image_index];
      pixel.alpha = 1.0F;
      result.lut.table[table_index] = pixel;
    }
  }
  result.lut.valid = true;
  return result;
}

bool serialize_hald_lut(
    const ColorLut3D& lut,
    std::vector<ColorPixel>* pixels,
    int* width,
    int* height,
    std::string* error_message) {
  if (pixels == nullptr || width == nullptr || height == nullptr) {
    if (error_message != nullptr) {
      *error_message = "HALD LUT export requires output pixel and dimension storage.";
    }
    return false;
  }
  if (!valid_lut_shape(lut) || lut.size < kMinCubeLutSize ||
      lut.shaper_size > 0) {
    if (error_message != nullptr) {
      *error_message = "HALD export requires a valid 3D LUT without a 1D shaper.";
    }
    return false;
  }
  const auto level = static_cast<int>(std::sqrt(static_cast<double>(lut.size)));
  if (level * level != lut.size) {
    if (error_message != nullptr) {
      *error_message = "HALD LUT export requires a square cube size such as 16 or 64.";
    }
    return false;
  }

  const auto image_size = level * level * level;
  const auto pixel_count = static_cast<std::size_t>(image_size) * static_cast<std::size_t>(image_size);
  pixels->assign(pixel_count, ColorPixel{});
  *width = image_size;
  *height = image_size;
  const auto cube_size = static_cast<std::size_t>(lut.size);
  const auto image_width = static_cast<std::size_t>(image_size);
  for (int y = 0; y < image_size; ++y) {
    for (int x = 0; x < image_size; ++x) {
      const auto red_index = static_cast<std::size_t>(x % lut.size);
      const auto green_index = static_cast<std::size_t>((x / lut.size) + (level * (y % level)));
      const auto blue_index = static_cast<std::size_t>(y / level);
      const auto table_index = (red_index * cube_size + green_index) * cube_size + blue_index;
      const auto image_index = static_cast<std::size_t>(y) * image_width + static_cast<std::size_t>(x);
      (*pixels)[image_index] = lut.table[table_index];
    }
  }
  return true;
}

std::vector<ColorPixel> render_lut_preview_thumbnail(const ColorLut3D& lut, int width, int height) {
  if (!valid_lut_shape(lut) || width <= 0 || height <= 0 || width > 2048 || height > 2048) {
    return {};
  }
  const auto safe_width = static_cast<std::size_t>(width);
  const auto safe_height = static_cast<std::size_t>(height);
  std::vector<ColorPixel> pixels(safe_width * safe_height);
  const auto denom_x = static_cast<double>(std::max(1, width - 1));
  const auto denom_y = static_cast<double>(std::max(1, height - 1));
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto u = static_cast<double>(x) / denom_x;
      const auto v = static_cast<double>(y) / denom_y;
      const auto source = ColorPixel{
          .red = static_cast<float>(u),
          .green = static_cast<float>(v),
          .blue = static_cast<float>(0.20 + (0.60 * (1.0 - std::abs((u + v) - 1.0)))),
          .alpha = 1.0F,
      };
      pixels[static_cast<std::size_t>(y) * safe_width + static_cast<std::size_t>(x)] = apply_lut(lut, source);
    }
  }
  return pixels;
}

std::vector<LutLibraryEntry> scan_lut_folder(std::string_view folder_path, bool recursive) {
  std::vector<LutLibraryEntry> entries;
  const auto root = std::filesystem::path(std::string(folder_path));
  std::error_code error;
  if (!std::filesystem::is_directory(root, error)) {
    return entries;
  }

  const auto add_entry = [&](const std::filesystem::directory_entry& entry) {
    std::error_code status_error;
    if (!entry.is_regular_file(status_error)) {
      return;
    }
    const auto format = lut_format_for_extension(entry.path().extension().string());
    if (format.empty()) {
      return;
    }
    entries.push_back(LutLibraryEntry{
        .name = entry.path().stem().string(),
        .file_path = entry.path().lexically_normal().string(),
        .format = format,
    });
  };

  if (recursive) {
    for (std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied,
                                                          error),
         end;
         it != end && !error;
         it.increment(error)) {
      add_entry(*it);
    }
  } else {
    for (std::filesystem::directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, error),
         end;
         it != end && !error;
         it.increment(error)) {
      add_entry(*it);
    }
  }

  std::sort(entries.begin(), entries.end(), [](const LutLibraryEntry& left, const LutLibraryEntry& right) {
    return lower_copy(left.file_path) < lower_copy(right.file_path);
  });
  return entries;
}

std::vector<LutLibraryEntry> search_lut_entries(
    std::span<const LutLibraryEntry> entries,
    std::string_view query) {
  const auto normalized_query = lower_copy(trim_copy(query));
  if (normalized_query.empty()) {
    return std::vector<LutLibraryEntry>(entries.begin(), entries.end());
  }
  std::vector<LutLibraryEntry> matches;
  for (const auto& entry : entries) {
    const auto haystack = lower_copy(entry.name + " " + entry.file_path + " " + entry.format);
    if (haystack.find(normalized_query) != std::string::npos) {
      matches.push_back(entry);
    }
  }
  return matches;
}

}  // namespace photo_editor::pipeline
