# Applies PATCH to the working directory unless it is already applied, so a fetch that is populated again does not fail.
execute_process(
  COMMAND git apply --reverse --check ${PATCH}
  RESULT_VARIABLE _already
  OUTPUT_QUIET ERROR_QUIET)
if(NOT
   _already
   EQUAL
   0)
  execute_process(COMMAND git apply ${PATCH} RESULT_VARIABLE _applied)
  if(NOT
     _applied
     EQUAL
     0)
    message(FATAL_ERROR "could not apply ${PATCH}")
  endif()
endif()
