if(NOT IS_DIRECTORY "${SOURCE}" OR NOT EXISTS "${SOURCE}/framehouse-search-runtime.json")
  message(FATAL_ERROR "A verified search runtime source is required.")
endif()
if(NOT DESTINATION MATCHES "/Resources/search-runtime$")
  message(FATAL_ERROR "Search runtime destination must be the app's Resources/search-runtime directory.")
endif()
# Preserve relative framework/library links. Post-copy verification rejects
# unexpected leftovers; this never recursively deletes an existing directory.
file(COPY "${SOURCE}/" DESTINATION "${DESTINATION}" USE_SOURCE_PERMISSIONS)
