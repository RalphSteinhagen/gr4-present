# Exports a deck with the viewer and reads the PDF back with poppler, which is independent of the writer.
#
# cmake -DVIEWER=<gr4-present> -DDECK=<talk.md> -DOUTPUT=<pdf> [-DEXPECT_LOG=<words>] [-DOPEN_DECK=ON] [-DMODE=steps]
# [-DEXPECT_PAGES=<n>] [-DEXPECT_IMAGES=<n>] -P qa_ExportPdf.cmake
#
# * EXPECT_LOG: the viewer's log must say those words; under GR4_PRESENT_GL32 effect shaders are off and every page must
#   still export
# * OPEN_DECK: the viewer opens DECK rather than its built-in deck
# * MODE: `slides` (the default) or `steps`
# * EXPECT_PAGES: pages other than one per heading, as a deck with steps has
# * EXPECT_IMAGES: pictures embedded, each counted once however many pages draw it
#
# Ground truth: the deck's own Markdown, one page per level-one heading outside fenced code, each heading's words on its
# page as text

if(NOT DEFINED MODE)
  set(MODE slides)
endif()
set(deck_argument)
if(OPEN_DECK)
  set(deck_argument "--presentation=${DECK}")
endif()
execute_process(
  COMMAND ${VIEWER} --export ${MODE} --export-to ${OUTPUT} ${deck_argument}
  RESULT_VARIABLE exported
  OUTPUT_VARIABLE log
  ERROR_VARIABLE log
  TIMEOUT 240)
if(NOT exported EQUAL 0)
  message(FATAL_ERROR "the viewer failed to export (${exported}):\n${log}")
endif()
if(DEFINED EXPECT_LOG)
  string(FIND "${log}" "${EXPECT_LOG}" said)
  if(said EQUAL -1)
    message(FATAL_ERROR "the viewer's log does not say '${EXPECT_LOG}':\n${log}")
  endif()
endif()

file(STRINGS ${DECK} lines)
set(fenced FALSE)
set(headings)
foreach(line IN LISTS lines)
  if(line MATCHES "^(```|~~~)")
    if(fenced)
      set(fenced FALSE)
    else()
      set(fenced TRUE)
    endif()
  elseif(NOT fenced AND line MATCHES "^# (.*)$")
    # the title alone: a sub-title after <br> is set on its own line and the anchor is not shown
    string(REGEX REPLACE "<br>.*$" "" title "${CMAKE_MATCH_1}")
    string(REGEX REPLACE " *\\{#[^}]*\\} *$" "" title "${title}")
    string(STRIP "${title}" title)
    list(APPEND headings "${title}")
  endif()
endforeach()
list(LENGTH headings sections)
if(DEFINED EXPECT_PAGES)
  set(sections ${EXPECT_PAGES})
endif()

execute_process(COMMAND pdfinfo ${OUTPUT} OUTPUT_VARIABLE info)
if(NOT info MATCHES "Pages: +([0-9]+)")
  message(FATAL_ERROR "pdfinfo could not read ${OUTPUT}:\n${info}")
endif()
if(NOT CMAKE_MATCH_1 EQUAL sections)
  message(FATAL_ERROR "${CMAKE_MATCH_1} pages for ${sections} sections")
endif()
if(NOT info MATCHES "Page size: +960 x 540 pts")
  message(FATAL_ERROR "pages are not 13.33 x 7.5 in:\n${info}")
endif()

execute_process(COMMAND pdftotext -enc UTF-8 ${OUTPUT} - OUTPUT_VARIABLE text)
string(ASCII 12 page_break) # pdftotext ends every page with one
string(REGEX REPLACE "[ \t\n\r${page_break}]+" " " text "${text}")
set(missing)
foreach(title IN LISTS headings)
  string(FIND "${text}" "${title}" at)
  if(at EQUAL -1)
    list(APPEND missing "${title}")
  endif()
endforeach()
if(missing)
  message(FATAL_ERROR "headings missing from the PDF's text: ${missing}")
endif()
if(DEFINED EXPECT_IMAGES)
  execute_process(COMMAND pdfimages -list ${OUTPUT} OUTPUT_VARIABLE listed)
  string(REGEX MATCHALL
               "\n +[0-9]+ +[0-9]+ +image +[0-9]+ +[0-9]+ +[a-z]+ +[0-9]+ +[0-9]+ +[a-z]+ +[a-z]+ +[0-9]+ +[0-9]+" rows
               "${listed}")
  set(objects)
  foreach(row IN LISTS rows)
    string(REGEX REPLACE "^.* ([0-9]+) +([0-9]+)$" "\\1 \\2" object "${row}")
    list(APPEND objects "${object}")
  endforeach()
  list(REMOVE_DUPLICATES objects)
  list(LENGTH objects embedded)
  if(NOT embedded EQUAL EXPECT_IMAGES)
    message(FATAL_ERROR "${embedded} pictures embedded, not ${EXPECT_IMAGES}:\n${listed}")
  endif()
endif()
message(STATUS "${sections} pages, every heading readable as text")
