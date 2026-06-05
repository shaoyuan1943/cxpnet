# User-facing examples. Keep this list explicit so temporary test directories or
# generated files do not become part of the public example build.
set(CXPNET_EXAMPLES
  echo_server
  echo_client
  manual_conn_client
  all_one_thread_server
  http_server
  http_client
  file_server
  file_client
  timer_poll
)

foreach(example_target ${CXPNET_EXAMPLES})
  add_subdirectory(${example_target})
  set_target_properties(${example_target} PROPERTIES DEBUG_POSTFIX d)
endforeach()
