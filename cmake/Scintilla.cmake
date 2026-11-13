# Official Scintilla Qt widget (not the separately licensed QScintilla wrapper).
FetchContent_Declare(scintilla
  URL https://www.scintilla.org/scintilla557.zip
  URL_HASH SHA256=801faef6a2404ec4cbe6f22c853d9bb63fc5e18f169f302a9c69b9d6f93349dd
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(scintilla)
set(scintilla_qt "${scintilla_SOURCE_DIR}/qt/ScintillaEditBase")
# Preserve Qt 6's numeric/variable font weight rather than reducing it to bold.
# Patch only a verified build copy; downloaded sources remain unchanged.
file(SHA256 "${scintilla_qt}/PlatQt.cpp" scintilla_plat_hash)
if(NOT scintilla_plat_hash STREQUAL "c283ba7f9b01cc3abda0ea2b9f98cf5f2394f09010d914f2e7e593decdb0e1ce")
  message(FATAL_ERROR "Scintilla 5.5.7 PlatQt.cpp differs from pinned source; font patch requires review")
endif()
file(READ "${scintilla_qt}/PlatQt.cpp" scintilla_plat)
set(scintilla_old "pfont->setBold(static_cast<int>(fp.weight) > 500);")
string(FIND "${scintilla_plat}" "${scintilla_old}" scintilla_anchor)
if(scintilla_anchor LESS 0)
  message(FATAL_ERROR "Scintilla font weight patch anchor missing")
endif()
string(REPLACE "${scintilla_old}"
  "pfont->setWeight(QFont::Weight(qBound(1, static_cast<int>(fp.weight), 1000)));\n\tpfont->setVariableAxis(QFont::Tag::fromString(\"wght\").value(), static_cast<float>(fp.weight));\n\tpfont->setHintingPreference(QFont::PreferFullHinting);"
  scintilla_plat "${scintilla_plat}")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/patched-scintilla/PlatQt.cpp" CONTENT "${scintilla_plat}" @ONLY)
file(GLOB scintilla_core CONFIGURE_DEPENDS "${scintilla_SOURCE_DIR}/src/*.cxx")
file(SHA256 "${scintilla_SOURCE_DIR}/src/PositionCache.cxx" scintilla_cache_hash)
if(NOT scintilla_cache_hash STREQUAL "bb071e3d7a3cb1059e67bcbfd9ac84dc8273e9672d081b29d99a0ef98af1e192")
  message(FATAL_ERROR "Scintilla position cache differs from pinned source; patch requires review")
endif()
file(READ "${scintilla_SOURCE_DIR}/src/PositionCache.cxx" scintilla_cache)
string(REPLACE "(sv.length() < 30)" "(sv.length() < 128)" scintilla_cache "${scintilla_cache}")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/patched-scintilla/PositionCache.cxx" CONTENT "${scintilla_cache}" @ONLY)
list(REMOVE_ITEM scintilla_core "${scintilla_SOURCE_DIR}/src/PositionCache.cxx")
list(APPEND scintilla_core "${CMAKE_CURRENT_BINARY_DIR}/patched-scintilla/PositionCache.cxx")
add_library(frogshell_scintilla STATIC ${scintilla_core}
  "${CMAKE_CURRENT_BINARY_DIR}/patched-scintilla/PlatQt.cpp" "${scintilla_qt}/PlatQt.h"
  "${scintilla_qt}/ScintillaQt.cpp" "${scintilla_qt}/ScintillaQt.h"
  "${scintilla_qt}/ScintillaEditBase.cpp" "${scintilla_qt}/ScintillaEditBase.h")
target_include_directories(frogshell_scintilla PUBLIC "${scintilla_SOURCE_DIR}/include" "${scintilla_SOURCE_DIR}/src" "${scintilla_qt}")
target_compile_definitions(frogshell_scintilla PUBLIC SCINTILLA_QT=1 EXPORT_IMPORT_API=)
target_link_libraries(frogshell_scintilla PUBLIC Qt6::Widgets Qt6::Core5Compat)
if(MSVC)
  target_compile_options(frogshell_scintilla PRIVATE /utf-8 /EHsc /MP4)
endif()
