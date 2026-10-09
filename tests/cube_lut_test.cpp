#include <cmath>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/color_lut_program.h"
#include "photo_editor/pipeline/color_transform.h"

namespace {

bool almost_equal(double left, double right, double tolerance = 1.0e-6) {
  return std::abs(left - right) <= tolerance;
}

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

void use_linear_srgb_working(photo_editor::pipeline::ColorAdjustmentState* state) {
  if (state == nullptr) {
    return;
  }
  state->color_pipeline.working_profile.profile_name = "Linear sRGB";
  state->color_pipeline.working_profile.color_space =
      photo_editor::pipeline::make_linear_srgb_color_space();
}

}  // namespace

int main() {
  const auto cube_text = std::string{
      "# simple channel swap\n"
      "TITLE \"Swap Red Blue\"\n"
      "LUT_3D_SIZE 2\n"
      "DOMAIN_MIN 0 0 0\n"
      "DOMAIN_MAX 1 1 1\n"
      "0 0 0\n"
      "1 0 0\n"
      "0 1 0\n"
      "1 1 0\n"
      "0 0 1\n"
      "1 0 1\n"
      "0 1 1\n"
      "1 1 1\n"};

  const auto parsed = photo_editor::pipeline::parse_cube_lut(cube_text);
  if (!parsed.ok() || parsed.title != "Swap Red Blue" || parsed.lut.size != 2 ||
      parsed.lut.table.size() != 8) {
    return fail("cube LUT parser should load title, size, and table rows");
  }

  const auto swapped = photo_editor::pipeline::apply_lut(
      parsed.lut,
      photo_editor::pipeline::ColorPixel{
          .red = 1.0F,
          .green = 0.0F,
          .blue = 0.0F,
          .alpha = 0.75F,
      });
  if (!almost_equal(swapped.red, 0.0) || !almost_equal(swapped.green, 0.0) ||
      !almost_equal(swapped.blue, 1.0) || !almost_equal(swapped.alpha, 0.75)) {
    return fail("imported cube LUT should apply with trilinear table indexing and preserve alpha");
  }

  const auto interpolated = photo_editor::pipeline::apply_lut(
      parsed.lut,
      photo_editor::pipeline::ColorPixel{
          .red = 0.5F,
          .green = 0.0F,
          .blue = 0.0F,
          .alpha = 1.0F,
      });
  if (!almost_equal(interpolated.blue, 0.5)) {
    return fail("imported cube LUT should interpolate between grid points");
  }

  const auto solid_blue_lut = photo_editor::pipeline::parse_cube_lut(
      "LUT_3D_SIZE 2\n"
      "0 0 1\n0 0 1\n0 0 1\n0 0 1\n"
      "0 0 1\n0 0 1\n0 0 1\n0 0 1\n");
  if (!solid_blue_lut.ok()) {
    return fail("solid-blue CUBE fixture should parse");
  }
  const photo_editor::pipeline::ColorPixel neutral_pixel{};
  const auto trilinear_neutral = photo_editor::pipeline::apply_lut(
      solid_blue_lut.lut, neutral_pixel);
  const auto tetrahedral_neutral = photo_editor::pipeline::apply_lut_tetrahedral(
      solid_blue_lut.lut, neutral_pixel);
  if (!almost_equal(trilinear_neutral.red, 0.0) ||
      !almost_equal(trilinear_neutral.green, 0.0) ||
      !almost_equal(trilinear_neutral.blue, 1.0) ||
      !almost_equal(tetrahedral_neutral.red, 0.0) ||
      !almost_equal(tetrahedral_neutral.green, 0.0) ||
      !almost_equal(tetrahedral_neutral.blue, 1.0)) {
    return fail("trilinear and tetrahedral CUBE sampling should preserve intentional neutral tinting");
  }

  const auto one_d_text = std::string{
      "# independent per-channel shaper\n"
      "TITLE \"Extended 1D shaper\"\n"
      "LUT_1D_SIZE 2\n"
      "LUT_1D_INPUT_RANGE -1 1\n"
      "0 0 0\n"
      "2 1 0.5\n"};
  const auto parsed_one_d = photo_editor::pipeline::parse_cube_lut(one_d_text);
  if (!parsed_one_d.ok() || parsed_one_d.lut.size != 0 ||
      parsed_one_d.lut.shaper_size != 2 ||
      parsed_one_d.lut.shaper_table.size() != 2) {
    return fail("cube LUT parser should retain a 1D shaper without expanding it");
  }
  const auto shaped = photo_editor::pipeline::apply_lut(
      parsed_one_d.lut,
      photo_editor::pipeline::ColorPixel{
          .red = 0.0F,
          .green = 0.0F,
          .blue = 0.0F,
          .alpha = 0.6F,
      });
  if (!almost_equal(shaped.red, 1.0) || !almost_equal(shaped.green, 0.5) ||
      !almost_equal(shaped.blue, 0.25) || !almost_equal(shaped.alpha, 0.6)) {
    return fail("1D CUBE shaper should honor its input range and preserve extended outputs");
  }
  const auto one_d_round_trip = photo_editor::pipeline::parse_cube_lut(
      photo_editor::pipeline::serialize_cube_lut(parsed_one_d.lut, parsed_one_d.title));
  if (!one_d_round_trip.ok() || one_d_round_trip.lut.shaper_size != 2 ||
      one_d_round_trip.lut.shaper_input_range !=
          std::array<double, 2>{-1.0, 1.0}) {
    return fail("1D CUBE serialization should preserve the shaper and input range");
  }

  const auto combined_text = std::string{
      "# shaper followed by a 3D table\n"
      "LUT_1D_SIZE 2\n"
      "LUT_3D_SIZE 2\n"
      "0 0 0\n"
      "1 1 1\n"
      "0 0 0\n"
      "1 0 0\n"
      "0 1 0\n"
      "1 1 0\n"
      "0 0 1\n"
      "1 0 1\n"
      "0 1 1\n"
      "1 1 1\n"};
  const auto parsed_combined = photo_editor::pipeline::parse_cube_lut(combined_text);
  if (!parsed_combined.ok() || parsed_combined.lut.shaper_size != 2 ||
      parsed_combined.lut.size != 2 || parsed_combined.lut.table.size() != 8) {
    return fail("cube LUT parser should retain ordered 1D shaper and 3D stages");
  }
  const auto combined_pixel = photo_editor::pipeline::apply_lut(
      parsed_combined.lut,
      photo_editor::pipeline::ColorPixel{
          .red = 0.5F,
          .green = 0.0F,
          .blue = 0.0F,
          .alpha = 1.0F,
      });
  if (!almost_equal(combined_pixel.blue, 0.5)) {
    return fail("combined CUBE should apply its shaper before its 3D table");
  }
  const auto combined_round_trip = photo_editor::pipeline::parse_cube_lut(
      photo_editor::pipeline::serialize_cube_lut(parsed_combined.lut, "Combined"));
  if (!combined_round_trip.ok() || combined_round_trip.lut.shaper_size != 2 ||
      combined_round_trip.lut.size != 2 ||
      combined_round_trip.lut.shaper_table.size() != 2 ||
      combined_round_trip.lut.table.size() != 8) {
    return fail("combined CUBE serialization should preserve both ordered stages");
  }

  photo_editor::pipeline::ColorAdjustmentState one_d_state;
  use_linear_srgb_working(&one_d_state);
  one_d_state.lut_enabled = true;
  one_d_state.lut_input_color_space = "Linear sRGB";
  one_d_state.lut_output_color_space = "Linear sRGB";
  one_d_state.lut_shaper_table = parsed_one_d.lut.shaper_table;
  one_d_state.lut_shaper_size = parsed_one_d.lut.shaper_size;
  one_d_state.lut_shaper_input_range = parsed_one_d.lut.shaper_input_range;
  const auto one_d_program =
      photo_editor::pipeline::compile_user_lut_stage_program(one_d_state);
  const auto one_d_program_pixel =
      photo_editor::pipeline::apply_compiled_user_lut_stage_program(
          one_d_program,
          photo_editor::pipeline::ColorPixel{0.0F, 0.0F, 0.0F, 0.6F});
  if (!one_d_program.ready() || !almost_equal(one_d_program_pixel.red, 1.0) ||
      !almost_equal(one_d_program_pixel.green, 0.5) ||
      !almost_equal(one_d_program_pixel.blue, 0.25)) {
    return fail("compiled color-engine user LUT should execute a standalone 1D CUBE");
  }
  photo_editor::pipeline::LutAdjustment one_d_stack_entry;
  one_d_stack_entry.name = "1D stack";
  one_d_stack_entry.input_color_space = "Linear sRGB";
  one_d_stack_entry.output_color_space = "Linear sRGB";
  one_d_stack_entry.shaper_table = parsed_one_d.lut.shaper_table;
  one_d_stack_entry.shaper_size = parsed_one_d.lut.shaper_size;
  one_d_stack_entry.shaper_input_range = parsed_one_d.lut.shaper_input_range;
  photo_editor::pipeline::ColorAdjustmentState one_d_stack_state;
  use_linear_srgb_working(&one_d_stack_state);
  one_d_stack_state.lut_stack = {one_d_stack_entry};
  const auto one_d_stack_program =
      photo_editor::pipeline::compile_user_lut_stage_program(one_d_stack_state);
  const auto one_d_stack_pixel =
      photo_editor::pipeline::apply_compiled_user_lut_stage_program(
          one_d_stack_program,
          photo_editor::pipeline::ColorPixel{0.0F, 0.0F, 0.0F, 0.6F});
  if (!one_d_stack_program.ready() ||
      !almost_equal(one_d_stack_pixel.red, 1.0) ||
      !almost_equal(one_d_stack_pixel.green, 0.5) ||
      !almost_equal(one_d_stack_pixel.blue, 0.25)) {
    return fail("compiled LUT stacks should retain and execute a standalone 1D CUBE");
  }

  const auto preview_pixels = photo_editor::pipeline::render_lut_preview_thumbnail(parsed.lut, 4, 4);
  if (preview_pixels.size() != 16) {
    return fail("LUT preview thumbnail renderer should produce requested pixel dimensions");
  }
  if (!(preview_pixels[3].blue > preview_pixels[3].red)) {
    return fail("LUT preview thumbnail should visualize the LUT transform");
  }

  photo_editor::pipeline::ColorAdjustmentState lut_strength_state;
  use_linear_srgb_working(&lut_strength_state);
  lut_strength_state.lut_enabled = true;
  lut_strength_state.lut_strength = 50.0;
  lut_strength_state.lut_size = parsed.lut.size;
  lut_strength_state.lut_table = parsed.lut.table;
  lut_strength_state.lut_domain_min = parsed.lut.domain_min;
  lut_strength_state.lut_domain_max = parsed.lut.domain_max;
  std::vector<photo_editor::pipeline::ColorPixel> lut_strength_pixels = {
      photo_editor::pipeline::ColorPixel{
          .red = 1.0F,
          .green = 0.0F,
          .blue = 0.0F,
          .alpha = 1.0F,
      },
  };
  photo_editor::pipeline::apply_color_adjustments(&lut_strength_pixels, 1, 1, lut_strength_state);
  if (!almost_equal(lut_strength_pixels[0].red, 0.5, 1.0e-5) ||
      !almost_equal(lut_strength_pixels[0].green, 0.0, 1.0e-5) ||
      !almost_equal(lut_strength_pixels[0].blue, 0.5, 1.0e-5)) {
    return fail("LUT strength should blend the imported LUT with the ungraded source");
  }

  photo_editor::pipeline::LutAdjustment blue_to_green_lut;
  blue_to_green_lut.size = 2;
  blue_to_green_lut.table.resize(8);
  for (int red = 0; red < blue_to_green_lut.size; ++red) {
    for (int green = 0; green < blue_to_green_lut.size; ++green) {
      for (int blue = 0; blue < blue_to_green_lut.size; ++blue) {
        const auto index = static_cast<std::size_t>(
            (red * blue_to_green_lut.size + green) * blue_to_green_lut.size + blue);
        blue_to_green_lut.table[index] = photo_editor::pipeline::ColorPixel{
            .red = static_cast<float>(red),
            .green = static_cast<float>(blue),
            .blue = static_cast<float>(green),
            .alpha = 1.0F,
        };
      }
    }
  }
  photo_editor::pipeline::ColorAdjustmentState lut_stack_state;
  use_linear_srgb_working(&lut_stack_state);
  lut_stack_state.lut_stack = {
      photo_editor::pipeline::LutAdjustment{
          .enabled = true,
          .strength = 100.0,
          .name = "swap",
          .table = parsed.lut.table,
          .size = parsed.lut.size,
          .domain_min = parsed.lut.domain_min,
          .domain_max = parsed.lut.domain_max,
      },
      blue_to_green_lut,
  };
  std::vector<photo_editor::pipeline::ColorPixel> lut_stack_pixels = {
      photo_editor::pipeline::ColorPixel{
          .red = 1.0F,
          .green = 0.0F,
          .blue = 0.0F,
          .alpha = 1.0F,
      },
  };
  photo_editor::pipeline::apply_color_adjustments(&lut_stack_pixels, 1, 1, lut_stack_state);
  if (!almost_equal(lut_stack_pixels[0].red, 0.0, 1.0e-5) ||
      !almost_equal(lut_stack_pixels[0].green, 1.0, 1.0e-5) ||
      !almost_equal(lut_stack_pixels[0].blue, 0.0, 1.0e-5)) {
    return fail("LUT stack should apply enabled LUTs sequentially");
  }

  photo_editor::pipeline::LutAdjustment encoded_square_lut;
  encoded_square_lut.name = "sRGB encoded square";
  encoded_square_lut.size = 3;
  encoded_square_lut.input_color_space = "sRGB";
  encoded_square_lut.output_color_space = "sRGB";
  for (int red = 0; red < encoded_square_lut.size; ++red) {
    for (int green = 0; green < encoded_square_lut.size; ++green) {
      for (int blue = 0; blue < encoded_square_lut.size; ++blue) {
        const auto normalized_red = static_cast<double>(red) / 2.0;
        const auto normalized_green = static_cast<double>(green) / 2.0;
        const auto normalized_blue = static_cast<double>(blue) / 2.0;
        encoded_square_lut.table.push_back(photo_editor::pipeline::ColorPixel{
            .red = static_cast<float>(normalized_red * normalized_red),
            .green = static_cast<float>(normalized_green * normalized_green),
            .blue = static_cast<float>(normalized_blue * normalized_blue),
            .alpha = 1.0F,
        });
      }
    }
  }
  photo_editor::pipeline::ColorAdjustmentState encoded_lut_state;
  use_linear_srgb_working(&encoded_lut_state);
  encoded_lut_state.lut_stack = {encoded_square_lut};
  std::vector<photo_editor::pipeline::ColorPixel> encoded_lut_pixels = {
      photo_editor::pipeline::ColorPixel{
          .red = 0.18F,
          .green = 0.18F,
          .blue = 0.18F,
          .alpha = 0.75F,
      },
  };
  photo_editor::pipeline::apply_color_adjustments(
      &encoded_lut_pixels, 1, 1, encoded_lut_state);
  const auto encoded_input = photo_editor::pipeline::linear_to_srgb(0.18);
  const auto expected_encoded_output = encoded_input * 0.5;
  const auto expected_linear_output =
      photo_editor::pipeline::srgb_to_linear(expected_encoded_output);
  if (!almost_equal(encoded_lut_pixels[0].red, expected_linear_output, 1.0e-5) ||
      !almost_equal(encoded_lut_pixels[0].green, expected_linear_output, 1.0e-5) ||
      !almost_equal(encoded_lut_pixels[0].blue, expected_linear_output, 1.0e-5) ||
      !almost_equal(encoded_lut_pixels[0].alpha, 0.75, 1.0e-6)) {
    return fail("sRGB LUTs should encode working-linear input and decode LUT output exactly once");
  }

  auto unsupported_lut = encoded_square_lut;
  unsupported_lut.input_color_space = "Unknown LUT Input";
  photo_editor::pipeline::ColorAdjustmentState unsupported_lut_state;
  use_linear_srgb_working(&unsupported_lut_state);
  unsupported_lut_state.lut_stack = {unsupported_lut};
  auto unsupported_lut_pixels = std::vector<photo_editor::pipeline::ColorPixel>{
      photo_editor::pipeline::ColorPixel{
          .red = 0.18F,
          .green = 0.18F,
          .blue = 0.18F,
          .alpha = 1.0F,
      },
  };
  photo_editor::pipeline::apply_color_adjustments(
      &unsupported_lut_pixels, 1, 1, unsupported_lut_state);
  if (!almost_equal(unsupported_lut_pixels[0].red, 0.18, 1.0e-6) ||
      !almost_equal(unsupported_lut_pixels[0].green, 0.18, 1.0e-6) ||
      !almost_equal(unsupported_lut_pixels[0].blue, 0.18, 1.0e-6)) {
    return fail("unsupported LUT color spaces should fail closed instead of using the working domain");
  }

  const auto exported = photo_editor::pipeline::serialize_cube_lut(parsed.lut, "Round Trip");
  if (exported.find("TITLE \"Round Trip\"") == std::string::npos ||
      exported.find("LUT_3D_SIZE 2") == std::string::npos) {
    return fail("cube LUT exporter should write title and size metadata");
  }
  const auto reparsed = photo_editor::pipeline::parse_cube_lut(exported);
  if (!reparsed.ok() || reparsed.title != "Round Trip" || reparsed.lut.size != parsed.lut.size ||
      reparsed.lut.table.size() != parsed.lut.table.size()) {
    return fail("exported cube LUT should parse back into an equivalent LUT");
  }
  if (!almost_equal(reparsed.lut.table[4].blue, parsed.lut.table[4].blue)) {
    return fail("exported cube LUT should preserve table values");
  }
  auto tagged_lut = parsed.lut;
  tagged_lut.input_color_space = "Linear sRGB";
  tagged_lut.output_color_space = "Display P3";
  const auto tagged_export = photo_editor::pipeline::serialize_cube_lut(tagged_lut, "Tagged");
  const auto tagged_reparse = photo_editor::pipeline::parse_cube_lut(tagged_export);
  if (!tagged_reparse.ok() || tagged_reparse.lut.input_color_space != "Linear sRGB" ||
      tagged_reparse.lut.output_color_space != "Display P3") {
    return fail("cube LUT import and export should preserve color-space tags");
  }

  const auto temp_path = std::filesystem::temp_directory_path() /
                         ("photo-editor-cube-lut-test-" +
                          std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".cube");
  std::string save_error;
  if (!photo_editor::pipeline::save_cube_lut_file(temp_path.string(), parsed.lut, "Disk Round Trip", &save_error)) {
    std::cerr << save_error << '\n';
    return fail("cube LUT exporter should write files");
  }
  const auto loaded = photo_editor::pipeline::load_cube_lut_file(temp_path.string());
  std::filesystem::remove(temp_path);
  if (!loaded.ok() || loaded.title != "Disk Round Trip" || loaded.lut.table.size() != parsed.lut.table.size()) {
    return fail("cube LUT importer should read files exported by the engine");
  }

  const auto invalid = photo_editor::pipeline::parse_cube_lut("LUT_3D_SIZE 2\n0 0 0\n");
  if (invalid.ok() || invalid.error_message.empty()) {
    return fail("cube LUT parser should reject incomplete table data");
  }

  photo_editor::pipeline::ColorLut3D hald_lut;
  hald_lut.size = 4;
  hald_lut.valid = true;
  hald_lut.table.resize(64);
  for (int red = 0; red < hald_lut.size; ++red) {
    for (int green = 0; green < hald_lut.size; ++green) {
      for (int blue = 0; blue < hald_lut.size; ++blue) {
        const auto index = static_cast<std::size_t>((red * hald_lut.size + green) * hald_lut.size + blue);
        hald_lut.table[index] = photo_editor::pipeline::ColorPixel{
            .red = static_cast<float>(blue) / 3.0F,
            .green = static_cast<float>(green) / 3.0F,
            .blue = static_cast<float>(red) / 3.0F,
            .alpha = 1.0F,
        };
      }
    }
  }

  std::vector<photo_editor::pipeline::ColorPixel> hald_pixels;
  int hald_width = 0;
  int hald_height = 0;
  std::string hald_error;
  if (!photo_editor::pipeline::serialize_hald_lut(
          hald_lut, &hald_pixels, &hald_width, &hald_height, &hald_error)) {
    std::cerr << hald_error << '\n';
    return fail("HALD LUT exporter should convert square cube LUTs to image pixels");
  }
  if (hald_width != 8 || hald_height != 8 || hald_pixels.size() != 64) {
    return fail("HALD LUT exporter should produce level^3 square image dimensions");
  }
  const auto parsed_hald = photo_editor::pipeline::parse_hald_lut(hald_pixels, hald_width, hald_height);
  if (!parsed_hald.ok() || parsed_hald.lut.size != hald_lut.size ||
      parsed_hald.lut.table.size() != hald_lut.table.size()) {
    return fail("HALD LUT parser should reconstruct the 3D LUT from image pixels");
  }
  const auto hald_applied = photo_editor::pipeline::apply_lut(
      parsed_hald.lut,
      photo_editor::pipeline::ColorPixel{
          .red = 1.0F,
          .green = 0.0F,
          .blue = 0.0F,
          .alpha = 1.0F,
      });
  if (!almost_equal(hald_applied.red, 0.0) || !almost_equal(hald_applied.green, 0.0) ||
      !almost_equal(hald_applied.blue, 1.0)) {
    return fail("HALD LUT parser should preserve table layout for application");
  }
  const auto invalid_hald = photo_editor::pipeline::parse_hald_lut(hald_pixels, 4, 16);
  if (invalid_hald.ok() || invalid_hald.error_message.empty()) {
    return fail("HALD LUT parser should reject non-square image dimensions");
  }

  const auto library_path = std::filesystem::temp_directory_path() /
                            ("photo-editor-lut-library-test-" +
                             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(library_path / "Looks");
  {
    std::ofstream(library_path / "Clean.cube") << cube_text;
    std::ofstream(library_path / "ignore.txt") << "not a LUT";
    std::ofstream(library_path / "Looks" / "Warm.PNG") << "placeholder image bytes";
  }
  const auto recursive_entries = photo_editor::pipeline::scan_lut_folder(library_path.string(), true);
  if (recursive_entries.size() != 2) {
    std::filesystem::remove_all(library_path);
    return fail("LUT folder scanner should discover supported LUT formats recursively");
  }
  const auto top_level_entries = photo_editor::pipeline::scan_lut_folder(library_path.string(), false);
  if (top_level_entries.size() != 1 || top_level_entries[0].format != "cube") {
    std::filesystem::remove_all(library_path);
    return fail("LUT folder scanner should respect non-recursive scans");
  }
  const auto warm_matches = photo_editor::pipeline::search_lut_entries(recursive_entries, "warm");
  if (warm_matches.size() != 1 || warm_matches[0].format != "hald") {
    std::filesystem::remove_all(library_path);
    return fail("LUT library search should match names and formats case-insensitively");
  }
  std::filesystem::remove_all(library_path);

  return EXIT_SUCCESS;
}
