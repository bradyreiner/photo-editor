# macdeployqt deliberately deploys a broad default plug-in set. Framehouse has
# narrower release capabilities on macOS:
#
# - PDF is an export format (QPdfWriter), not an accepted image input format.
# - The desktop UI does not import QtQuick.VirtualKeyboard.
# - Qt's native Apple network-information backend is bundled, so the GLib
#   backend is redundant. Its GIO/GObject/GModule dependency branch is otherwise
#   unreachable from the staged application.
# - SecureTransport is the supported macOS TLS backend. The parallel OpenSSL
#   plug-in duplicates platform TLS functionality. QtNetwork still links the
#   packaged OpenSSL libraries directly, so those libraries remain required;
#   PostgreSQL keeps its own isolated runtime under Resources/postgres.
# - ICNS, ICO, JPEG 2000, MNG, TGA, and WBMP are absent from the canonical
#   catalog/import capability list. Their image-reader plug-ins are not product
#   functionality; GIF remains because it is explicitly supported.
#
# Keep this list explicit. Any future PDF-import, virtual-keyboard, or GLib-only
# feature must update the capability contract, this pruner, and its release
# proof together.

function(framehouse_prune_deployed_qt_macos_runtime bundle_relative_path)
  if(NOT DEFINED QT_DEPLOY_PREFIX)
    message(FATAL_ERROR "QT_DEPLOY_PREFIX is required while pruning the deployed Qt runtime.")
  endif()

  set(bundle_root "${QT_DEPLOY_PREFIX}/${bundle_relative_path}")
  if(NOT IS_DIRECTORY "${bundle_root}")
    message(FATAL_ERROR "Deployed app bundle was not found: ${bundle_root}")
  endif()

  set(unreachable_runtime_paths
    "Contents/PlugIns/imageformats/libqpdf.dylib"
    "Contents/Frameworks/QtPdf.framework"
    "Contents/PlugIns/imageformats/libqicns.dylib"
    "Contents/PlugIns/imageformats/libqico.dylib"
    "Contents/PlugIns/imageformats/libqjp2.dylib"
    "Contents/PlugIns/imageformats/libqmng.dylib"
    "Contents/PlugIns/imageformats/libqtga.dylib"
    "Contents/PlugIns/imageformats/libqwbmp.dylib"
    "Contents/Frameworks/libmng.2.dylib"
    "Contents/PlugIns/platforminputcontexts/libqtvirtualkeyboardplugin.dylib"
    "Contents/Frameworks/QtVirtualKeyboard.framework"
    "Contents/Frameworks/QtVirtualKeyboardQml.framework"
    "Contents/PlugIns/networkinformation/libqglib.dylib"
    "Contents/Frameworks/libgio-2.0.0.dylib"
    "Contents/Frameworks/libgobject-2.0.0.dylib"
    "Contents/Frameworks/libgmodule-2.0.0.dylib"
    "Contents/PlugIns/tls/libqopensslbackend.dylib")

  foreach(relative_path IN LISTS unreachable_runtime_paths)
    file(REMOVE_RECURSE "${bundle_root}/${relative_path}")
  endforeach()
endfunction()

function(framehouse_reseal_macos_bundle_at_path bundle_root)
  if(NOT IS_DIRECTORY "${bundle_root}")
    message(FATAL_ERROR "Deployed app bundle was not found: ${bundle_root}")
  endif()
  set(bundle_signer "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../scripts/sign-macos-bundle.sh")
  if(NOT EXISTS "${bundle_signer}")
    message(FATAL_ERROR "macOS bundle signer was not found: ${bundle_signer}")
  endif()

  # macdeployqt can return after warning that an install_name_tool or strip
  # QProcess was destroyed while still running. Re-sign every Mach-O leaf and
  # nested bundle inner-to-outer so stale child signatures cannot escape into
  # the stage. Release signing reuses this same routine with Developer ID.
  execute_process(
    COMMAND "${bundle_signer}" "${bundle_root}" -
    RESULT_VARIABLE codesign_result
    OUTPUT_VARIABLE codesign_stdout
    ERROR_VARIABLE codesign_stderr)
  if(NOT codesign_result EQUAL 0)
    message(FATAL_ERROR
      "Could not reseal pruned app bundle inner-to-outer (${codesign_result}):\n"
      "${codesign_stdout}${codesign_stderr}")
  endif()
endfunction()

function(framehouse_reseal_deployed_macos_bundle bundle_relative_path)
  if(NOT DEFINED QT_DEPLOY_PREFIX)
    message(FATAL_ERROR "QT_DEPLOY_PREFIX is required while resealing the deployed app bundle.")
  endif()
  framehouse_reseal_macos_bundle_at_path("${QT_DEPLOY_PREFIX}/${bundle_relative_path}")
endfunction()
