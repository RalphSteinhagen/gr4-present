# What a slide drew, in the terms a PDF page needs. Header-only, so the viewer can record without linking the writer.
add_library(gr4-present-recording INTERFACE)
target_include_directories(gr4-present-recording INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(gr4-present-recording INTERFACE gr4-present-core)

if(GR4_PRESENT_ENABLE_EXPORT)
  # the PDF a deck exports to, written from recordings; libharu does the file format
  add_library(gr4-present-export STATIC JpegEncoder.cpp PdfWriter.cpp)
  set_source_files_properties(JpegEncoder.cpp PROPERTIES COMPILE_OPTIONS "-w") # stb_image_write is not written to our
                                                                               # warnings
  target_link_libraries(
    gr4-present-export
    PUBLIC gr4-present-recording
    PRIVATE gr4-present-options hpdf stb)
endif()
