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
  "PostgreSQL runtime root to bundle. Expected layout includes bin/ and optionally lib/ and share/.")

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
  if(PHOTO_EDITOR_STRICT_PACKAGING AND NOT PHOTO_EDITOR_POSTGRES_RUNTIME_DIR)
    message(FATAL_ERROR "Production packaging requires PHOTO_EDITOR_POSTGRES_RUNTIME_DIR.")
  endif()
  if(PHOTO_EDITOR_STRICT_PACKAGING AND
     (NOT FRAMEHOUSE_LEGAL_PUBLISHER OR
      FRAMEHOUSE_LEGAL_PUBLISHER STREQUAL "Publisher identity pending release approval"))
    message(FATAL_ERROR "Production packaging requires an approved FRAMEHOUSE_LEGAL_PUBLISHER.")
  endif()
  if(APPLE AND PHOTO_EDITOR_STRICT_PACKAGING AND NOT PHOTO_EDITOR_ENABLE_MACOS_SPARKLE_UPDATER)
    message(FATAL_ERROR "Production macOS packaging requires the Sparkle updater.")
  endif()
  if(APPLE AND PHOTO_EDITOR_STRICT_PACKAGING AND PHOTO_EDITOR_ENABLE_MACOS_SPARKLE_UPDATER)
    if(PHOTO_EDITOR_SPARKLE_DISTRIBUTION_DIR AND NOT PHOTO_EDITOR_SPARKLE_FRAMEWORK_DIR)
      set(PHOTO_EDITOR_SPARKLE_FRAMEWORK_DIR "${PHOTO_EDITOR_SPARKLE_DISTRIBUTION_DIR}/Sparkle.framework")
    endif()
    if(NOT PHOTO_EDITOR_SPARKLE_FRAMEWORK_DIR OR
       NOT EXISTS "${PHOTO_EDITOR_SPARKLE_FRAMEWORK_DIR}" OR
       NOT PHOTO_EDITOR_MACOS_APPCAST_URL OR
       NOT PHOTO_EDITOR_MACOS_SPARKLE_PUBLIC_KEY)
      message(FATAL_ERROR
        "Production macOS packaging requires Sparkle.framework, an appcast URL, and a public EdDSA key.")
    endif()
  endif()

  install(TARGETS ${target_name}
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

  if(APPLE)
    install(
      FILES "${CMAKE_SOURCE_DIR}/LICENSE" "${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
      DESTINATION "${FRAMEHOUSE_PRODUCT_SLUG}.app/Contents/Resources/licenses")
  else()
    install(
      FILES "${CMAKE_SOURCE_DIR}/LICENSE" "${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
      DESTINATION "${CMAKE_INSTALL_DATADIR}/${FRAMEHOUSE_PRODUCT_SLUG}/licenses")
  endif()

  file(
    STRINGS "${CMAKE_SOURCE_DIR}/cmake/CatalogHelpers.txt"
    PHOTO_EDITOR_PACKAGED_CATALOG_HELPERS
    REGEX "^[a-z][a-z0-9_]*$")
  if(APPLE)
    # The helpers share the same Rust runtime and catalog dependency graph.
    # Package one multicall Mach-O and preserve the established executable
    # names as relative symlinks; argv[0] selects the command implementation.
    set(catalog_helper_path "${PHOTO_EDITOR_RUST_BINARY_DIR}/catalog_helper")
    if(EXISTS "${catalog_helper_path}")
      set(catalog_helper_install_dir "${FRAMEHOUSE_PRODUCT_SLUG}.app/Contents/MacOS")
      install(PROGRAMS "${catalog_helper_path}" DESTINATION "${catalog_helper_install_dir}")
      foreach(helper_name IN LISTS PHOTO_EDITOR_PACKAGED_CATALOG_HELPERS)
        install(CODE "
set(helper_link \"\${CMAKE_INSTALL_PREFIX}/${catalog_helper_install_dir}/${helper_name}\")
file(REMOVE \"\${helper_link}\")
file(CREATE_LINK \"catalog_helper\" \"\${helper_link}\" SYMBOLIC)")
      endforeach()
    elseif(PHOTO_EDITOR_STRICT_PACKAGING)
      message(FATAL_ERROR "Required Rust multicall helper not found for packaging: ${catalog_helper_path}")
    endif()
  else()
    foreach(helper_name IN LISTS PHOTO_EDITOR_PACKAGED_CATALOG_HELPERS)
      set(helper_path "${PHOTO_EDITOR_RUST_BINARY_DIR}/${helper_name}${CMAKE_EXECUTABLE_SUFFIX}")
      if(EXISTS "${helper_path}")
        install(PROGRAMS "${helper_path}" DESTINATION ${CMAKE_INSTALL_BINDIR})
      elseif(PHOTO_EDITOR_STRICT_PACKAGING)
        message(FATAL_ERROR "Required Rust helper not found for packaging: ${helper_path}")
      endif()
    endforeach()
  endif()

  if(PHOTO_EDITOR_POSTGRES_RUNTIME_DIR)
    get_filename_component(
      PHOTO_EDITOR_RESOLVED_POSTGRES_RUNTIME_DIR
      "${PHOTO_EDITOR_POSTGRES_RUNTIME_DIR}"
      REALPATH)
    if(EXISTS "${PHOTO_EDITOR_RESOLVED_POSTGRES_RUNTIME_DIR}/bin")
      foreach(postgres_tool IN ITEMS initdb pg_ctl pg_dump pg_restore postgres psql)
        if(WIN32)
          set(postgres_tool_path "${PHOTO_EDITOR_RESOLVED_POSTGRES_RUNTIME_DIR}/bin/${postgres_tool}.exe")
        else()
          set(postgres_tool_path "${PHOTO_EDITOR_RESOLVED_POSTGRES_RUNTIME_DIR}/bin/${postgres_tool}")
        endif()
        if(PHOTO_EDITOR_STRICT_PACKAGING AND NOT EXISTS "${postgres_tool_path}")
          message(FATAL_ERROR "Required PostgreSQL runtime tool is missing: ${postgres_tool_path}")
        endif()
      endforeach()
    else()
      message(WARNING "PHOTO_EDITOR_POSTGRES_RUNTIME_DIR does not contain a bin/ directory: ${PHOTO_EDITOR_POSTGRES_RUNTIME_DIR}")
    endif()
  endif()

  if(APPLE AND PHOTO_EDITOR_ENABLE_MACOS_SPARKLE_UPDATER)
    if(PHOTO_EDITOR_SPARKLE_DISTRIBUTION_DIR AND NOT PHOTO_EDITOR_SPARKLE_FRAMEWORK_DIR)
      set(PHOTO_EDITOR_SPARKLE_FRAMEWORK_DIR "${PHOTO_EDITOR_SPARKLE_DISTRIBUTION_DIR}/Sparkle.framework")
    endif()

    if(PHOTO_EDITOR_SPARKLE_FRAMEWORK_DIR AND EXISTS "${PHOTO_EDITOR_SPARKLE_FRAMEWORK_DIR}")
      install(DIRECTORY "${PHOTO_EDITOR_SPARKLE_FRAMEWORK_DIR}" DESTINATION "${FRAMEHOUSE_PRODUCT_SLUG}.app/Contents/Frameworks")
    else()
      message(WARNING "Sparkle updater is enabled but Sparkle.framework was not found for packaging.")
    endif()
  endif()

  if(APPLE AND COMMAND qt6_generate_deploy_script)
    # Homebrew's Qt QML plug-ins are relative symlinks into its Cellar. CMake's
    # QML deployment preserves those links, but they become broken after being
    # relocated into an app bundle. Materialize them before macdeployqt scans
    # and rewrites each plug-in's dependencies.
    qt6_generate_deploy_script(
      TARGET ${target_name}
      NAME framehouse_macos_qml_app
      OUTPUT_SCRIPT deploy_script
      CONTENT "
qt6_deploy_qml_imports(TARGET ${target_name} PLUGINS_FOUND plugins_found)
include(\"${CMAKE_SOURCE_DIR}/cmake/PruneQtQuickControlsStyles.cmake\")
framehouse_prune_deployed_qt_quick_controls_styles(\"${FRAMEHOUSE_PRODUCT_SLUG}.app\" plugins_found)
include(\"${CMAKE_SOURCE_DIR}/cmake/DereferenceQtQmlPlugins.cmake\")
framehouse_dereference_deployed_qml_plugins(\"${FRAMEHOUSE_PRODUCT_SLUG}.app\" \${plugins_found})
qt6_deploy_runtime_dependencies(
  EXECUTABLE \"$<TARGET_FILE_NAME:${target_name}>.app\"
  ADDITIONAL_MODULES \${plugins_found}
  NO_OVERWRITE)
include(\"${CMAKE_SOURCE_DIR}/cmake/PruneQtMacosRuntime.cmake\")
framehouse_prune_deployed_qt_macos_runtime(\"${FRAMEHOUSE_PRODUCT_SLUG}.app\")")
    install(SCRIPT ${deploy_script})
  elseif(COMMAND qt_generate_deploy_qml_app_script)
    qt_generate_deploy_qml_app_script(
      TARGET ${target_name}
      OUTPUT_SCRIPT deploy_script
      DEPLOY_USER_QML_MODULES_ON_UNSUPPORTED_PLATFORM
      NO_UNSUPPORTED_PLATFORM_ERROR)
    install(SCRIPT ${deploy_script})
  elseif(COMMAND qt_generate_deploy_app_script)
    qt_generate_deploy_app_script(
      TARGET ${target_name}
      OUTPUT_SCRIPT deploy_script
      NO_UNSUPPORTED_PLATFORM_ERROR)
    install(SCRIPT ${deploy_script})
  endif()

  # Keep PostgreSQL outside Qt's dependency scan. The database runtime has its
  # own portability verifier and may contain thousands of server support files
  # that are unrelated to Qt deployment.
  if(PHOTO_EDITOR_RESOLVED_POSTGRES_RUNTIME_DIR)
    if(APPLE)
      set(postgres_install_root "${FRAMEHOUSE_PRODUCT_SLUG}.app/Contents/Resources/postgres")
    else()
      set(postgres_install_root "${CMAKE_INSTALL_BINDIR}/postgres")
    endif()
    foreach(postgres_runtime_directory IN ITEMS bin lib share)
      if(EXISTS "${PHOTO_EDITOR_RESOLVED_POSTGRES_RUNTIME_DIR}/${postgres_runtime_directory}")
        install(
          DIRECTORY "${PHOTO_EDITOR_RESOLVED_POSTGRES_RUNTIME_DIR}/${postgres_runtime_directory}"
          DESTINATION "${postgres_install_root}"
          USE_SOURCE_PERMISSIONS
          PATTERN "doc" EXCLUDE
          PATTERN "man" EXCLUDE)
      endif()
    endforeach()
  endif()

  # Seal only after every install rule, including PostgreSQL resources, has
  # populated the app bundle. Any later file addition would invalidate the
  # outer resource envelope.
  if(APPLE)
    install(CODE "
include(\"${CMAKE_SOURCE_DIR}/cmake/PruneQtMacosRuntime.cmake\")
framehouse_reseal_macos_bundle_at_path(\"\${CMAKE_INSTALL_PREFIX}/${FRAMEHOUSE_PRODUCT_SLUG}.app\")")
  endif()
endfunction()

function(photo_editor_configure_packaging)
  if(APPLE)
    set(CPACK_GENERATOR DragNDrop)
  elseif(WIN32)
    set(CPACK_GENERATOR ZIP)
  else()
    set(CPACK_GENERATOR TGZ)
  endif()

  set(CPACK_PACKAGE_NAME "${FRAMEHOUSE_PRODUCT_SLUG}")
  set(CPACK_PACKAGE_VENDOR "${FRAMEHOUSE_LEGAL_PUBLISHER}")
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${FRAMEHOUSE_PRODUCT_NAME} visual workspace")
  set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
  set(CPACK_PACKAGE_FILE_NAME "${FRAMEHOUSE_PRODUCT_SLUG}-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
  include(CPack)
endfunction()
