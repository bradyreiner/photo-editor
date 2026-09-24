include_guard(GLOBAL)

include(FetchContent)

function(photo_editor_add_pinned_lcms2)
  if(TARGET photo_editor_lcms2_pinned)
    return()
  endif()

  FetchContent_Declare(
    photo_editor_lcms2_source
    URL https://github.com/mm2/Little-CMS/releases/download/lcms2.18/lcms2-2.18.tar.gz
    URL_HASH SHA256=ee67be3566f459362c1ee094fde2c159d33fa0390aa4ed5f5af676f9e5004347
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR cmake-source-is-built-by-photo-editor)
  FetchContent_MakeAvailable(photo_editor_lcms2_source)

  file(GLOB photo_editor_lcms2_sources CONFIGURE_DEPENDS
    "${photo_editor_lcms2_source_SOURCE_DIR}/src/*.c")
  if(NOT photo_editor_lcms2_sources)
    message(FATAL_ERROR "Pinned Little CMS 2.18 archive did not contain its expected C sources")
  endif()

  add_library(photo_editor_lcms2_pinned STATIC ${photo_editor_lcms2_sources})
  add_library(photo_editor::lcms2_pinned ALIAS photo_editor_lcms2_pinned)
  target_include_directories(
    photo_editor_lcms2_pinned
    PUBLIC "${photo_editor_lcms2_source_SOURCE_DIR}/include")
  target_compile_features(photo_editor_lcms2_pinned PRIVATE c_std_99)
  set_target_properties(
    photo_editor_lcms2_pinned
    PROPERTIES
      POSITION_INDEPENDENT_CODE ON
      C_VISIBILITY_PRESET hidden)
  if(WIN32)
    target_compile_definitions(photo_editor_lcms2_pinned PRIVATE HAVE_GMTIME_S=1)
  else()
    target_compile_definitions(
      photo_editor_lcms2_pinned
      PRIVATE
        HAVE_FUNC_ATTRIBUTE_VISIBILITY=1
        HAVE_GMTIME_R=1)
  endif()
  if(UNIX AND NOT APPLE)
    target_link_libraries(photo_editor_lcms2_pinned PUBLIC m)
  endif()
endfunction()
