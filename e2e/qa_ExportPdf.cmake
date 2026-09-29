# Exports a deck with the viewer itself and reads the PDF back with poppler, which is independent of the writer.
#
# cmake -DVIEWER=<gr4-present> -DDECK=<talk.md> -DOUTPUT=<pdf> [-DEXPECT_LOG=<words>] -P qa_ExportPdf.cmake
#
# With EXPECT_LOG the viewer's log must say those words: run under GR4_PRESENT_GL32, it is a driver without OpenGL 3.3
# core, which switches the effect shaders off and must still export every page.
#
# The ground truth is the deck's own Markdown: one page per section in `slides` mode, a section being a level-one
# heading outside fenced code, and each heading's words on its page as text that can be searched and copied.

execute_process(
  COMMAND ${VIEWER} --export slides --export-to ${OUTPUT}
  RESULT_VARIABLE exported
  OUTPUT_VARIABLE log
  ERROR_VARIABLE log
  TIMEOUT 240)
if(NOT
   exported
   EQUAL
   0)
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
    # the title alone: a sub-title after <br> is set on a line of its own, and the anchor is not shown
    string(
      REGEX
      REPLACE "<br>.*$"
              ""
              title
              "${CMAKE_MATCH_1}")
    string(
      REGEX
      REPLACE " *\\{#[^}]*\\} *$"
              ""
              title
              "${title}")
    string(STRIP "${title}" title)
    list(APPEND headings "${title}")
  endif()
endforeach()
list(LENGTH headings sections)

execute_process(COMMAND pdfinfo ${OUTPUT} OUTPUT_VARIABLE info)
if(NOT
   info
   MATCHES
   "Pages: +([0-9]+)")
  message(FATAL_ERROR "pdfinfo could not read ${OUTPUT}:\n${info}")
endif()
if(NOT
   CMAKE_MATCH_1
   EQUAL
   sections)
  message(FATAL_ERROR "${CMAKE_MATCH_1} pages for ${sections} sections")
endif()
if(NOT
   info
   MATCHES
   "Page size: +960 x 540 pts")
  message(FATAL_ERROR "pages are not 13.33 x 7.5 in:\n${info}")
endif()

execute_process(COMMAND pdftotext -enc UTF-8 ${OUTPUT} - OUTPUT_VARIABLE text)
string(ASCII 12 page_break) # pdftotext ends every page with one
string(
  REGEX
  REPLACE "[ \t\n\r${page_break}]+"
          " "
          text
          "${text}")
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
message(STATUS "${sections} pages, every heading readable as text")
