include(GNUInstallDirs)

set(
  PHOTO_EDITOR_RUST_BINARY_DIR
  "${CMAKE_SOURCE_DIR}/rust/target/release"
  CACHE PATH
  "Directory containing built Rust helper binaries for packaging.")

set(
  PHOTO_EDITOR_STRICT_PACKAGING
  OFF
  CACHE BOOL
  "Fail configuration if required packaged helper binaries are missing.")

set(
  PHOTO_EDITOR_POSTGRES_RUNTIME_DIR
  ""
  CACHE PATH
  "Optional PostgreSQL runtime root to bundle. Expected layout includes bin/ and optionally lib/ and share/.")

set(
  PHOTO_EDITOR_SPARKLE_DISTRIBUTION_DIR
  ""
  CACHE PATH
  "Optional Sparkle distribution root that contains Sparkle.framework and bin/generate_appcast.")

set(
  PHOTO_EDITOR_SPARKLE_FRAMEWORK_DIR
  ""
  CACHE PATH
  "Optional direct path to Sparkle.framework.")

set(
  PHOTO_EDITOR_MACOS_APPCAST_URL
  ""
  CACHE STRING
  "Sparkle appcast feed URL embedded into the macOS bundle when the updater is enabled.")

set(
  PHOTO_EDITOR_MACOS_SPARKLE_PUBLIC_KEY
  ""
  CACHE STRING
  "Sparkle public EdDSA key embedded into the macOS bundle when the updater is enabled.")

set(
  PHOTO_EDITOR_MACOS_SPARKLE_ENABLE_AUTOMATIC_CHECKS
  "true"
  CACHE STRING
  "Boolean plist literal for Sparkle automatic update checks (true or false).")

set(
  PHOTO_EDITOR_MACOS_SPARKLE_AUTOMATIC_DOWNLOADS
  "false"
  CACHE STRING
  "Boolean plist literal for Sparkle automatic update installation (true or false).")

function(photo_editor_install_desktop_runtime target_name)
  install(TARGETS ${target_name}
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

  foreach(helper_name IN ITEMS catalog_local_db catalog_migrate catalog_snapshot import_catalog save_edit_recipe)
    set(helper_path "${PHOTO_EDITOR_RUST_BINARY_DIR}/${helper_name}${CMAKE_EXECUTABLE_SUFFIX}")
    if(EXISTS "${helper_path}")
      if(APPLE)
        install(PROGRAMS "${helper_path}" DESTINATION "PhotoEditor.app/Contents/MacOS")
      else()
        install(PROGRAMS "${helper_path}" DESTINATION ${CMAKE_INSTALL_BINDIR})
      endif()
    elseif(PHOTO_EDITOR_STRICT_PACKAGING)
      message(FATAL_ERROR "Required Rust helper not found for packaging: ${helper_path}")
    endif()
  endforeach()

  if(PHOTO_EDITOR_POSTGRES_RUNTIME_DIR)
    if(EXISTS "${PHOTO_EDITOR_POSTGRES_RUNTIME_DIR}/bin")
      if(APPLE)
        install(DIRECTORY "${PHOTO_EDITOR_POSTGRES_RUNTIME_DIR}/" DESTINATION "PhotoEditor.app/Contents/Resources/postgres")
      else()
        install(DIRECTORY "${PHOTO_EDITOR_POSTGRES_RUNTIME_DIR}/" DESTINATION "${CMAKE_INSTALL_BINDIR}/postgres")
      endif()
    else()
      message(WARNING "PHOTO_EDITOR_POSTGRES_RUNTIME_DIR does not contain a bin/ directory: ${PHOTO_EDITOR_POSTGRES_RUNTIME_DIR}")
    endif()
  endif()

  if(APPLE AND PHOTO_EDITOR_ENABLE_MACOS_SPARKLE_UPDATER)
    if(PHOTO_EDITOR_SPARKLE_DISTRIBUTION_DIR AND NOT PHOTO_EDITOR_SPARKLE_FRAMEWORK_DIR)
      set(PHOTO_EDITOR_SPARKLE_FRAMEWORK_DIR "${PHOTO_EDITOR_SPARKLE_DISTRIBUTION_DIR}/Sparkle.framework")
    endif()

    if(PHOTO_EDITOR_SPARKLE_FRAMEWORK_DIR AND EXISTS "${PHOTO_EDITOR_SPARKLE_FRAMEWORK_DIR}")
      install(DIRECTORY "${PHOTO_EDITOR_SPARKLE_FRAMEWORK_DIR}" DESTINATION "PhotoEditor.app/Contents/Frameworks")
    else()
      message(WARNING "Sparkle updater is enabled but Sparkle.framework was not found for packaging.")
    endif()
  endif()

  if(QT_VERSION VERSION_GREATER_EQUAL 6.3)
    qt_generate_deploy_app_script(
      TARGET ${target_name}
      OUTPUT_SCRIPT deploy_script
      NO_UNSUPPORTED_PLATFORM_ERROR)
    install(SCRIPT ${deploy_script})
  endif()
endfunction()

function(photo_editor_configure_packaging)
  if(APPLE)
    set(CPACK_GENERATOR DragNDrop PARENT_SCOPE)
  elseif(WIN32)
    set(CPACK_GENERATOR ZIP PARENT_SCOPE)
  else()
    set(CPACK_GENERATOR TGZ PARENT_SCOPE)
  endif()

  set(CPACK_PACKAGE_NAME "PhotoEditor" PARENT_SCOPE)
  set(CPACK_PACKAGE_VENDOR "PhotoEditor" PARENT_SCOPE)
  set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}" PARENT_SCOPE)
  set(CPACK_PACKAGE_FILE_NAME "PhotoEditor-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}" PARENT_SCOPE)
  include(CPack)
endfunction()
