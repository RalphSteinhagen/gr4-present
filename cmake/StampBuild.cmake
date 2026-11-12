# Writes the header the viewer names its build by: its version, its commit and when that commit was built. Run on every
# build: the header changes only when the commit or its local changes do, so building the same tree again relinks
# nothing. cmake -DSOURCE_DIR=<repository> -DVERSION=<x.y.z> -DTARGET=<BuildStamp.hpp> -P StampBuild.cmake
execute_process(COMMAND git -C ${SOURCE_DIR} rev-parse --short=7 HEAD OUTPUT_VARIABLE _commit
                OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE _failed)
if(_failed AND DEFINED ENV{GITHUB_SHA}) # a checkout git will not read, as in a container of another owner
  string(SUBSTRING "$ENV{GITHUB_SHA}" 0 7 _commit)
elseif(_failed)
  set(_commit "unknown")
else()
  execute_process(COMMAND git -C ${SOURCE_DIR} status --porcelain --untracked-files=no OUTPUT_VARIABLE _changes
                  ERROR_QUIET)
  if(NOT _changes STREQUAL "")
    string(APPEND _commit "+changes")
  endif()
endif()

set(_built "")
if(EXISTS ${TARGET})
  file(READ ${TARGET} _before)
  string(FIND "${_before}" "kCommit = \"${_commit}\"" _same) # literally: a commit with local changes holds a +
  if(_same GREATER -1 AND _before MATCHES "kBuiltAt = \"([^\"]*)\"")
    set(_built ${CMAKE_MATCH_1})
  endif()
endif()
if(_built STREQUAL "")
  string(TIMESTAMP _built "%Y-%m-%d %H:%M UTC" UTC) # SOURCE_DATE_EPOCH, when set, makes it reproducible
endif()

file(
  CONFIGURE
  OUTPUT
  ${TARGET}
  CONTENT
  "#ifndef GR4_PRESENT_BUILDSTAMP_HPP
#define GR4_PRESENT_BUILDSTAMP_HPP

#include <string_view>

namespace gr::present::build {
inline constexpr std::string_view kVersion = \"${VERSION}\";
inline constexpr std::string_view kCommit = \"${_commit}\";
inline constexpr std::string_view kBuiltAt = \"${_built}\";
} // namespace gr::present::build

#endif // GR4_PRESENT_BUILDSTAMP_HPP
")
