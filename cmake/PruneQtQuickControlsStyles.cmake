# Framehouse fixes Qt Quick Controls to the Basic style at compile time. Qt's
# QML scanner still discovers the generic Controls dependency used internally
# by Qt Quick Dialogs, whose qmldir advertises every optional style. Remove
# those optional modules before macdeployqt follows their binary dependencies.

function(framehouse_prune_deployed_qt_quick_controls_styles bundle_relative_path plugins_variable)
  if(NOT DEFINED QT_DEPLOY_PREFIX)
    message(FATAL_ERROR "QT_DEPLOY_PREFIX is required while pruning deployed QML styles.")
  endif()

  set(bundle_root "${QT_DEPLOY_PREFIX}/${bundle_relative_path}")
  if(NOT IS_DIRECTORY "${bundle_root}")
    message(FATAL_ERROR "Deployed app bundle was not found: ${bundle_root}")
  endif()

  set(non_basic_style_names
    FluentWinUI3
    Fusion
    Imagine
    Material
    Universal
    Windows
    iOS
    macOS)
  foreach(style_name IN LISTS non_basic_style_names)
    file(REMOVE_RECURSE
      "${bundle_root}/Contents/Resources/qml/QtQuick/Controls/${style_name}")
  endforeach()
  file(REMOVE_RECURSE
    "${bundle_root}/Contents/Resources/qml/QtQuick/NativeStyle")

  # Qt Quick Dialogs ships selector-specific QML copies in +Style folders.
  # Basic uses the unqualified files, so the other selectors are unreachable.
  file(GLOB_RECURSE dialog_style_directories LIST_DIRECTORIES true
    "${bundle_root}/Contents/Resources/qml/QtQuick/Dialogs/+*")
  foreach(style_path IN LISTS dialog_style_directories)
    if(IS_DIRECTORY "${style_path}")
      get_filename_component(style_directory_name "${style_path}" NAME)
      if(style_directory_name MATCHES "^\\+(FluentWinUI3|Fusion|Imagine|Material|Universal|Windows|iOS|macOS)$")
        file(REMOVE_RECURSE "${style_path}")
      endif()
    endif()
  endforeach()

  set(filtered_plugins "")
  foreach(plugin_path IN LISTS ${plugins_variable})
    get_filename_component(plugin_name "${plugin_path}" NAME)
    string(TOLOWER "${plugin_name}" plugin_name_lower)
    if(plugin_name_lower MATCHES
        "qtquickcontrols2(fluentwinui3|fusion|imagine|material|universal|windows|ios|macos|native)style")
      if(IS_ABSOLUTE "${plugin_path}")
        set(deployed_plugin "${plugin_path}")
      else()
        set(deployed_plugin "${QT_DEPLOY_PREFIX}/${plugin_path}")
      endif()
      file(REMOVE "${deployed_plugin}")
    else()
      list(APPEND filtered_plugins "${plugin_path}")
    endif()
  endforeach()

  set(${plugins_variable} "${filtered_plugins}" PARENT_SCOPE)
endfunction()
