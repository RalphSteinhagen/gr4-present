# Applies PATCH to the working directory unless it is already applied, so a fetch that is populated again does not fail.
function(apply_patch patch_file)
  execute_process(
    COMMAND git apply --reverse --check ${patch_file}
    RESULT_VARIABLE _already
    OUTPUT_QUIET ERROR_QUIET)
  if(NOT
     _already
     EQUAL
     0)
    execute_process(COMMAND git apply ${patch_file} RESULT_VARIABLE _applied)
    if(NOT
       _applied
       EQUAL
       0)
      message(FATAL_ERROR "could not apply ${patch_file}")
    endif()
  endif()
endfunction()

apply_patch(${PATCH})
if(DEFINED PATCH_EXTRA)
  apply_patch(${PATCH_EXTRA})
endif()
