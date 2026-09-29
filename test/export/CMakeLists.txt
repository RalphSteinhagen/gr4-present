# poppler's tools read the pages back; without them there is nothing independent to check a PDF against
find_program(GR4_PRESENT_PDFTOTEXT pdftotext)
find_program(GR4_PRESENT_PDFINFO pdfinfo)
find_program(GR4_PRESENT_PDFFONTS pdffonts)
if(GR4_PRESENT_PDFTOTEXT
   AND GR4_PRESENT_PDFINFO
   AND GR4_PRESENT_PDFFONTS
   AND NOT EMSCRIPTEN)
  add_executable(qa_PdfWriter qa_PdfWriter.cpp)
  target_link_libraries(qa_PdfWriter PRIVATE ut gr4-present-export gr4-present-options)
  target_compile_definitions(
    qa_PdfWriter PRIVATE GR4_PRESENT_LIBERATION_DIRECTORY="${CMAKE_SOURCE_DIR}/assets/liberation"
                         GR4_PRESENT_PACKAGE_DIRECTORY="${CMAKE_SOURCE_DIR}/presentations/default")
  add_test(NAME qa_PdfWriter COMMAND qa_PdfWriter)
else()
  message(STATUS "qa_PdfWriter disabled: poppler's pdftotext, pdfinfo and pdffonts are needed to read the pages back")
endif()
