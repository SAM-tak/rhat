# Copy the built rhat into DIRECTORY for the editor's language server. The
# editor may be running the previous copy, which the OS will not let us replace;
# that is a warning, not a build failure.
get_filename_component(name "${SOURCE}" NAME)
file(MAKE_DIRECTORY "${DIRECTORY}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${SOURCE}" "${DIRECTORY}/${name}"
  RESULT_VARIABLE status ERROR_QUIET)
if(status)
  message(WARNING "could not update ${DIRECTORY}/${name}; stop the language server to refresh it")
endif()
