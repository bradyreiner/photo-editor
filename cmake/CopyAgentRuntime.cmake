if(NOT IS_DIRECTORY "${SOURCE}" OR NOT EXISTS "${SOURCE}/framehouse-runtime.json")
  message(FATAL_ERROR "A verified agent runtime source is required.")
endif()
if(NOT DESTINATION MATCHES "/Resources/agent-runtime$")
  message(FATAL_ERROR "Agent runtime copy destination must be the app's Resources/agent-runtime directory.")
endif()
# Unlike cmake -E copy_directory, file(COPY) preserves the pinned relative
# library symlinks rather than expanding them into duplicate binaries.
file(COPY "${SOURCE}/" DESTINATION "${DESTINATION}" USE_SOURCE_PERMISSIONS)
