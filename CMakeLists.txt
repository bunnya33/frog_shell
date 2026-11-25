cmake_minimum_required(VERSION 3.24)
project(RabbitShell VERSION 0.1.54 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets Network Test Core5Compat)
include(FetchContent)
include(cmake/Scintilla.cmake)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
option(RABBITSHELL_BUILD_TESTS "Build RabbitShell tests" ON)
set(BUILD_TESTING OFF)
set(ENABLE_ZLIB_COMPRESSION OFF CACHE BOOL "" FORCE)
set(CRYPTO_BACKEND OpenSSL CACHE STRING "SSH crypto provider" FORCE)
find_package(OpenSSL 3 REQUIRED)
FetchContent_Declare(libssh2 URL https://www.libssh2.org/download/libssh2-1.11.1.tar.gz
  URL_HASH SHA256=d9ec76cbe34db98eec3539fe2c899d26b0c837cb3eb466a56b0f109cabf658f7 DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(libssh2)
FetchContent_Declare(libvterm URL https://www.leonerd.org.uk/code/libvterm/libvterm-0.3.3.tar.gz
  URL_HASH SHA256=09156f43dd2128bd347cbeebe50d9a571d32c64e0cf18d211197946aff7226e0 DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(libvterm)
set(BUILD_TESTING OFF CACHE BOOL "Dependency tests disabled" FORCE)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
FetchContent_Declare(libyaml URL https://codeload.github.com/yaml/libyaml/tar.gz/refs/tags/0.2.5
  URL_HASH SHA256=fa240dbf262be053f3898006d502d514936c818e422afdcf33921c63bed9bf2e DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(tomlplusplus URL https://codeload.github.com/marzer/tomlplusplus/tar.gz/refs/tags/v3.4.0
  URL_HASH SHA256=8517f65938a4faae9ccf8ebb36631a38c1cadfb5efa85d9a72e15b9e97d25155 DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(libyaml tomlplusplus)
file(GLOB VTERM_SOURCES CONFIGURE_DEPENDS ${libvterm_SOURCE_DIR}/src/*.c)
include(cmake/PatchLibvterm.cmake)
add_library(vterm STATIC ${VTERM_SOURCES})
target_include_directories(vterm PUBLIC ${libvterm_SOURCE_DIR}/include PRIVATE ${libvterm_SOURCE_DIR}/src)
set_target_properties(vterm PROPERTIES AUTOMOC OFF)
if(MSVC)
  target_compile_definitions(vterm PRIVATE _CRT_SECURE_NO_WARNINGS)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/utf-8> $<$<COMPILE_LANGUAGE:CXX>:/W4>)
endif()
add_library(rabbitshell_core STATIC src/core/session.cpp src/core/session.h src/core/profile.cpp src/core/profile.h
  src/core/credentials.cpp src/core/credentials.h src/core/tunnel.cpp src/core/tunnel.h src/core/sshtrust.h)
target_sources(rabbitshell_core PRIVATE src/core/services.cpp src/core/services.h)
target_sources(rabbitshell_core PRIVATE src/core/diagnostics.cpp src/core/diagnostics.h src/core/bookmarks.cpp src/core/bookmarks.h)
target_sources(rabbitshell_core PRIVATE src/core/fileoperationjournal.cpp src/core/fileoperationjournal.h src/core/security.cpp src/core/security.h src/core/singleinstance.cpp src/core/singleinstance.h src/core/operationgate.h)
target_sources(rabbitshell_core PRIVATE src/core/listeningports.cpp src/core/listeningports.h)
target_sources(rabbitshell_core PRIVATE src/core/documentsearch.cpp src/core/documentsearch.h)
target_sources(rabbitshell_core PRIVATE src/core/textdiff.cpp src/core/textdiff.h)
target_sources(rabbitshell_core PRIVATE src/core/storage.cpp src/core/storage.h)
target_sources(rabbitshell_core PRIVATE src/core/syntaxdiagnostic.cpp src/core/syntaxdiagnostic.h)
target_sources(rabbitshell_core PRIVATE src/core/serverclock.cpp src/core/serverclock.h src/core/editorlimits.cpp src/core/editorlimits.h)
target_sources(rabbitshell_core PRIVATE src/core/storageusage.cpp src/core/storageusage.h)
target_sources(rabbitshell_core PRIVATE src/core/cachecleanup.cpp src/core/cachecleanup.h)
target_sources(rabbitshell_core PRIVATE src/core/releaseupdate.cpp src/core/releaseupdate.h src/core/selfupdate.cpp src/core/selfupdate.h)
target_link_libraries(rabbitshell_core PRIVATE yaml tomlplusplus::tomlplusplus)
target_compile_definitions(rabbitshell_core PRIVATE RABBITSHELL_DIAGNOSTIC_VERSION="${PROJECT_VERSION}")
target_link_libraries(rabbitshell_core PUBLIC Qt6::Core Qt6::Network libssh2_static OpenSSL::Crypto)
if(WIN32)
  target_link_libraries(rabbitshell_core PRIVATE advapi32 iphlpapi)
elseif(APPLE)
  target_link_libraries(rabbitshell_core PRIVATE "-framework Security" "-framework CoreFoundation")
elseif(UNIX)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(SECRET QUIET IMPORTED_TARGET libsecret-1)
  endif()
  if(SECRET_FOUND)
    target_link_libraries(rabbitshell_core PRIVATE PkgConfig::SECRET)
    target_compile_definitions(rabbitshell_core PRIVATE RABBITSHELL_LIBSECRET)
  else()
    message(STATUS "libsecret-1 unavailable: password saving disabled (no plaintext fallback)")
  endif()
endif()
add_library(rabbitshell_ui STATIC src/ui/theme.cpp src/ui/theme.h src/ui/terminal.cpp src/ui/terminal.h
  src/ui/editor.cpp src/ui/editor.h src/ui/workspace.cpp src/ui/workspace.h src/ui/mainwindow.cpp src/ui/mainwindow.h
  src/ui/windowstyle.cpp src/ui/windowstyle.h
  src/ui/editorarea.cpp src/ui/editorarea.h src/ui/icons.cpp src/ui/icons.h)
target_sources(rabbitshell_ui PRIVATE src/ui/animatedtree.cpp src/ui/animatedtree.h)
target_sources(rabbitshell_ui PRIVATE src/ui/promptstyle.cpp src/ui/promptstyle.h)
target_sources(rabbitshell_ui PRIVATE src/ui/layoutmetrics.h src/ui/serverstatus.h src/ui/processtable.cpp src/ui/processtable.h src/ui/safetree.h src/ui/bookmarkpanel.cpp src/ui/bookmarkpanel.h src/ui/workspace_bookmarks.cpp)
target_sources(rabbitshell_ui PRIVATE src/ui/listeningtable.cpp src/ui/listeningtable.h src/ui/workspace_listening.cpp)
target_sources(rabbitshell_ui PRIVATE src/ui/workspace_commands.cpp src/ui/mainwindow_menus.cpp)
target_sources(rabbitshell_ui PRIVATE src/ui/connectiondelegate.cpp src/ui/connectiondelegate.h)
target_sources(rabbitshell_ui PRIVATE src/ui/documentsearch.cpp src/ui/documentsearch.h)
target_sources(rabbitshell_ui PRIVATE src/ui/editorsurface.cpp src/ui/editorsurface.h src/ui/editorlexer.cpp src/ui/editorlexer.h)
target_sources(rabbitshell_ui PRIVATE src/ui/editorminimap.cpp src/ui/editorminimap.h src/ui/editorcompare.cpp src/ui/editorcompare.h)
target_sources(rabbitshell_ui PRIVATE src/ui/serverclock.cpp src/ui/serverclock.h src/ui/wheeleditguard.cpp src/ui/wheeleditguard.h)
target_sources(rabbitshell_ui PRIVATE src/ui/storageusagecontroller.cpp src/ui/storageusagecontroller.h)
target_sources(rabbitshell_ui PRIVATE src/ui/cachecleanupcontroller.cpp src/ui/cachecleanupcontroller.h)
target_sources(rabbitshell_ui PRIVATE src/ui/updatecontroller.cpp src/ui/updatecontroller.h)
target_compile_definitions(rabbitshell_ui PRIVATE RABBITSHELL_VERSION="${PROJECT_VERSION}")
target_sources(rabbitshell_ui PRIVATE src/ui/messagebox.cpp src/ui/messagebox.h)
target_sources(rabbitshell_ui PRIVATE src/ui/styledcombobox.cpp src/ui/styledcombobox.h src/ui/roundedsurface.cpp src/ui/roundedsurface.h)
target_sources(rabbitshell_ui PRIVATE src/ui/fileicons.cpp src/ui/fileicons.h src/ui/filecontextmenu.cpp src/ui/filecontextmenu.h)
target_sources(rabbitshell_ui PRIVATE src/ui/fileclipboard.cpp src/ui/fileclipboard.h src/ui/workspace_clipboard.cpp)
target_sources(rabbitshell_ui PRIVATE src/ui/ports.cpp src/ui/ports.h src/ui/sshprompts.cpp src/ui/sshprompts.h)
target_sources(rabbitshell_ui PRIVATE src/ui/workspace_tunnels.cpp)
target_sources(rabbitshell_ui PRIVATE src/ui/services.cpp src/ui/services.h src/ui/workspace_services.cpp)
target_sources(rabbitshell_ui PRIVATE src/ui/contentfont.cpp src/ui/contentfont.h src/ui/vterm_extensions.h src/ui/filedrag.cpp src/ui/filedrag.h src/ui/filedrag_win.cpp src/ui/filedrag_win.h)
target_sources(rabbitshell_ui PRIVATE src/ui/fontsettings.cpp src/ui/fontsettings.h)
target_sources(rabbitshell_ui PRIVATE src/ui/uizoom.cpp src/ui/uizoom.h src/ui/uipreferences.cpp src/ui/uipreferences.h src/ui/connectionnotice.cpp src/ui/connectionnotice.h)
target_sources(rabbitshell_ui PRIVATE src/ui/uploadconflictdialog.cpp src/ui/uploadconflictdialog.h src/ui/applock.cpp src/ui/applock.h src/ui/sessionlockevents.cpp src/ui/sessionlockevents.h)
qt_add_resources(rabbitshell_ui contentfont PREFIX "/fonts"
  BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts"
  FILES resources/fonts/CascadiaCodeNF.ttf)
qt_add_resources(rabbitshell_ui fileicons PREFIX "/fileicons"
  BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/seti"
  FILES resources/seti/vs-seti-icon-theme.json resources/seti/outlines.json resources/seti/languages.json)
qt_add_resources(rabbitshell_ui branding PREFIX "/branding"
  BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/branding" FILES resources/branding/frogshell.png resources/branding/frogshell.ico resources/branding/frogshell-update.png)
target_link_libraries(rabbitshell_ui PUBLIC rabbitshell_core Qt6::Widgets vterm frogshell_scintilla)
if(WIN32)
  target_link_libraries(rabbitshell_ui PRIVATE dwmapi ole32 shell32 shlwapi uuid gdi32 user32 wtsapi32)
endif()
if(APPLE)
  target_link_libraries(rabbitshell_ui PRIVATE "-framework CoreFoundation")
elseif(UNIX)
  find_package(Qt6 QUIET COMPONENTS DBus)
  if(TARGET Qt6::DBus)
    target_link_libraries(rabbitshell_ui PRIVATE Qt6::DBus)
    target_compile_definitions(rabbitshell_ui PRIVATE RABBITSHELL_LOCK_DBUS)
  endif()
endif()
qt_add_executable(RabbitShell WIN32 MACOSX_BUNDLE src/main.cpp src/ui/diagnosticapplication.h)
target_link_libraries(RabbitShell PRIVATE rabbitshell_ui)
target_compile_definitions(RabbitShell PRIVATE RABBITSHELL_VERSION="${PROJECT_VERSION}")
set_target_properties(RabbitShell PROPERTIES MACOSX_BUNDLE_GUI_IDENTIFIER io.rabbitshell.desktop)
set_target_properties(RabbitShell PROPERTIES OUTPUT_NAME FrogShell MACOSX_BUNDLE_BUNDLE_NAME FrogShell)
if(WIN32)
  configure_file(resources/frogshell.rc.in "${CMAKE_CURRENT_BINARY_DIR}/frogshell.rc" @ONLY)
  target_sources(RabbitShell PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/frogshell.rc")
elseif(APPLE)
  target_sources(RabbitShell PRIVATE resources/branding/frogshell.icns)
  set_source_files_properties(resources/branding/frogshell.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  set_target_properties(RabbitShell PROPERTIES MACOSX_BUNDLE_ICON_FILE frogshell.icns)
endif()
include(CTest)
if(RABBITSHELL_BUILD_TESTS)
  qt_add_executable(frogshell_editor_engine_tests tests/test_editorengine.cpp)
  set_target_properties(frogshell_editor_engine_tests PROPERTIES WIN32_EXECUTABLE FALSE)
  target_link_libraries(frogshell_editor_engine_tests PRIVATE rabbitshell_ui Qt6::Test)
  add_test(NAME editor_engine COMMAND ${CMAKE_COMMAND}
    -DTEST_EXECUTABLE=$<TARGET_FILE:frogshell_editor_engine_tests>
    -DTEST_REPORT=${CMAKE_CURRENT_BINARY_DIR}/editor-engine-tests.txt
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_qtest.cmake)
  set_tests_properties(editor_engine PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  qt_add_executable(frogshell_selection_probe tests/probe_editor_selection.cpp)
  set_target_properties(frogshell_selection_probe PROPERTIES WIN32_EXECUTABLE FALSE)
  target_link_libraries(frogshell_selection_probe PRIVATE rabbitshell_ui Qt6::Test)
  qt_add_executable(rabbitshell_animatedtree_tests tests/test_animatedtree.cpp)
  set_target_properties(rabbitshell_animatedtree_tests PROPERTIES WIN32_EXECUTABLE FALSE)
  target_link_libraries(rabbitshell_animatedtree_tests PRIVATE rabbitshell_ui Qt6::Test)
  add_test(NAME animatedtree COMMAND ${CMAKE_COMMAND}
    -DTEST_EXECUTABLE=$<TARGET_FILE:rabbitshell_animatedtree_tests>
    -DTEST_REPORT=${CMAKE_CURRENT_BINARY_DIR}/animatedtree-$<CONFIG>.txt
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_qtest.cmake)
  set_tests_properties(animatedtree PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;TREE_EVIDENCE_DIR=${CMAKE_CURRENT_BINARY_DIR}/animatedtree-evidence")
  if(WIN32)
    # Fixture-only entry points are never present in rabbitshell_core or the app.
    qt_add_executable(rabbitshell_selfupdate_tests tests/test_selfupdate.cpp src/core/selfupdate.cpp src/core/selfupdate.h)
    set_target_properties(rabbitshell_selfupdate_tests PROPERTIES WIN32_EXECUTABLE FALSE)
    target_compile_definitions(rabbitshell_selfupdate_tests PRIVATE SELFUPDATE_FIXTURE_TESTS)
    target_link_libraries(rabbitshell_selfupdate_tests PRIVATE Qt6::Core Qt6::Test advapi32)
    add_test(NAME self_update COMMAND ${CMAKE_COMMAND}
      -DTEST_EXECUTABLE=$<TARGET_FILE:rabbitshell_selfupdate_tests>
      -DTEST_REPORT=${CMAKE_CURRENT_BINARY_DIR}/self-update-tests.txt
      -DTEST_TIMEOUT=180
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_qtest.cmake)
    set_tests_properties(self_update PROPERTIES TIMEOUT 190 ENVIRONMENT "SELFUPDATE_EVIDENCE_DIR=${CMAKE_CURRENT_BINARY_DIR}/self-update-evidence")
  endif()
  qt_add_executable(rabbitshell_releaseupdate_tests tests/test_releaseupdate.cpp)
  set_target_properties(rabbitshell_releaseupdate_tests PROPERTIES WIN32_EXECUTABLE FALSE)
  target_link_libraries(rabbitshell_releaseupdate_tests PRIVATE rabbitshell_core Qt6::Test)
  add_test(NAME release_update COMMAND ${CMAKE_COMMAND}
    -DTEST_EXECUTABLE=$<TARGET_FILE:rabbitshell_releaseupdate_tests>
    -DTEST_REPORT=${CMAKE_CURRENT_BINARY_DIR}/release-update-tests.txt
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_qtest.cmake)
  set_tests_properties(release_update PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  qt_add_executable(rabbitshell_updateui_tests tests/test_updateui.cpp)
  set_target_properties(rabbitshell_updateui_tests PROPERTIES WIN32_EXECUTABLE FALSE)
  target_link_libraries(rabbitshell_updateui_tests PRIVATE rabbitshell_ui Qt6::Test)
  add_test(NAME update_ui COMMAND ${CMAKE_COMMAND}
    -DTEST_EXECUTABLE=$<TARGET_FILE:rabbitshell_updateui_tests>
    -DTEST_REPORT=${CMAKE_CURRENT_BINARY_DIR}/update-ui-tests.txt
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_qtest.cmake)
  set_tests_properties(update_ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;RABBITSHELL_TEST_REPORT_DIR=${CMAKE_CURRENT_BINARY_DIR}/update-ui-evidence")
  enable_testing()
  qt_add_executable(rabbitshell_cachecleanup_tests tests/test_cachecleanup.cpp)
  set_target_properties(rabbitshell_cachecleanup_tests PROPERTIES WIN32_EXECUTABLE FALSE)
  target_link_libraries(rabbitshell_cachecleanup_tests PRIVATE rabbitshell_ui Qt6::Test)
  add_test(NAME cache_cleanup COMMAND ${CMAKE_COMMAND}
    -DTEST_EXECUTABLE=$<TARGET_FILE:rabbitshell_cachecleanup_tests>
    -DTEST_REPORT=${CMAKE_CURRENT_BINARY_DIR}/cache_cleanup-$<CONFIG>.txt
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_qtest.cmake)
  set_tests_properties(cache_cleanup PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;RABBITSHELL_TEST_REPORT_DIR=${CMAKE_CURRENT_BINARY_DIR}/cache-cleanup-evidence")
  qt_add_executable(rabbitshell_editorlimits_wheel_tests tests/test_editorlimits_wheel.cpp)
  set_target_properties(rabbitshell_editorlimits_wheel_tests PROPERTIES WIN32_EXECUTABLE FALSE)
  target_link_libraries(rabbitshell_editorlimits_wheel_tests PRIVATE rabbitshell_ui Qt6::Test)
  add_test(NAME editor_limits_wheel COMMAND ${CMAKE_COMMAND}
    -DTEST_EXECUTABLE=$<TARGET_FILE:rabbitshell_editorlimits_wheel_tests>
    -DTEST_REPORT=${CMAKE_CURRENT_BINARY_DIR}/editor_limits_wheel-$<CONFIG>.txt
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_qtest.cmake)
  set_tests_properties(editor_limits_wheel PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;RABBITSHELL_TEST_REPORT_DIR=${CMAKE_CURRENT_BINARY_DIR}/editor-limits-wheel-evidence")
  qt_add_executable(rabbitshell_syntaxdiagnostic_tests tests/test_syntaxdiagnostic.cpp)
  set_target_properties(rabbitshell_syntaxdiagnostic_tests PROPERTIES WIN32_EXECUTABLE FALSE)
  target_link_libraries(rabbitshell_syntaxdiagnostic_tests PRIVATE rabbitshell_ui Qt6::Test)
  add_test(NAME syntax_diagnostics COMMAND ${CMAKE_COMMAND}
    -DTEST_EXECUTABLE=$<TARGET_FILE:rabbitshell_syntaxdiagnostic_tests>
    -DTEST_REPORT=${CMAKE_CURRENT_BINARY_DIR}/syntax_diagnostics-$<CONFIG>.txt
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_qtest.cmake)
  set_tests_properties(syntax_diagnostics PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;RABBITSHELL_TEST_REPORT_DIR=${CMAKE_CURRENT_BINARY_DIR}/syntax-diagnostics-evidence")
  qt_add_executable(rabbitshell_serverclock_tests tests/test_serverclock.cpp)
  set_target_properties(rabbitshell_serverclock_tests PROPERTIES WIN32_EXECUTABLE FALSE)
  target_link_libraries(rabbitshell_serverclock_tests PRIVATE rabbitshell_ui Qt6::Test)
  add_test(NAME server_clock COMMAND ${CMAKE_COMMAND}
    -DTEST_EXECUTABLE=$<TARGET_FILE:rabbitshell_serverclock_tests>
    -DTEST_REPORT=${CMAKE_CURRENT_BINARY_DIR}/server_clock-$<CONFIG>.txt
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_qtest.cmake)
  set_tests_properties(server_clock PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;RABBITSHELL_TEST_REPORT_DIR=${CMAKE_CURRENT_BINARY_DIR}/server-clock-evidence")
  qt_add_executable(rabbitshell_editor_highlights_tests tests/test_editorhighlights.cpp)
  set_target_properties(rabbitshell_editor_highlights_tests PROPERTIES WIN32_EXECUTABLE FALSE)
  target_link_libraries(rabbitshell_editor_highlights_tests PRIVATE rabbitshell_ui Qt6::Test)
  add_test(NAME editor_highlights COMMAND ${CMAKE_COMMAND}
    -DTEST_EXECUTABLE=$<TARGET_FILE:rabbitshell_editor_highlights_tests>
    -DTEST_REPORT=${CMAKE_CURRENT_BINARY_DIR}/editor_highlights-$<CONFIG>.txt
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_qtest.cmake)
  set_tests_properties(editor_highlights PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;RABBITSHELL_TEST_REPORT_DIR=${CMAKE_CURRENT_BINARY_DIR}/editor-highlights")
  qt_add_executable(rabbitshell_search_tests tests/test_documentsearch.cpp)
  set_target_properties(rabbitshell_search_tests PROPERTIES WIN32_EXECUTABLE FALSE)
  target_link_libraries(rabbitshell_search_tests PRIVATE rabbitshell_ui Qt6::Test)
  add_test(NAME document_search COMMAND ${CMAKE_COMMAND}
    -DTEST_EXECUTABLE=$<TARGET_FILE:rabbitshell_search_tests>
    -DTEST_REPORT=${CMAKE_CURRENT_BINARY_DIR}/document_search-$<CONFIG>.txt
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_qtest.cmake)
  set_tests_properties(document_search PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  qt_add_executable(rabbitshell_rounded_tests tests/test_roundedsurfaces.cpp)
  set_target_properties(rabbitshell_rounded_tests PROPERTIES WIN32_EXECUTABLE FALSE)
  target_link_libraries(rabbitshell_rounded_tests PRIVATE rabbitshell_ui Qt6::Test)
  if(WIN32)
    target_link_libraries(rabbitshell_rounded_tests PRIVATE dwmapi)
  endif()
  add_test(NAME rounded_surfaces COMMAND ${CMAKE_COMMAND}
    -DTEST_EXECUTABLE=$<TARGET_FILE:rabbitshell_rounded_tests>
    -DTEST_REPORT=${CMAKE_CURRENT_BINARY_DIR}/rounded_surfaces-$<CONFIG>.txt
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_qtest.cmake)
  set_tests_properties(rounded_surfaces PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  qt_add_executable(rabbitshell_menu_tests tests/test_menus.cpp)
  set_target_properties(rabbitshell_menu_tests PROPERTIES WIN32_EXECUTABLE FALSE)
  target_link_libraries(rabbitshell_menu_tests PRIVATE rabbitshell_ui Qt6::Test)
  add_test(NAME menus COMMAND ${CMAKE_COMMAND}
    -DTEST_EXECUTABLE=$<TARGET_FILE:rabbitshell_menu_tests>
    -DTEST_REPORT=${CMAKE_CURRENT_BINARY_DIR}/menus-$<CONFIG>.txt
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_qtest.cmake)
  set_tests_properties(menus PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  qt_add_executable(rabbitshell_lock_tests tests/test_applock.cpp)
  set_target_properties(rabbitshell_lock_tests PROPERTIES WIN32_EXECUTABLE FALSE)
  target_link_libraries(rabbitshell_lock_tests PRIVATE rabbitshell_ui Qt6::Test)
  add_test(NAME app_lock COMMAND ${CMAKE_COMMAND}
    -DTEST_EXECUTABLE=$<TARGET_FILE:rabbitshell_lock_tests>
    -DTEST_REPORT=${CMAKE_CURRENT_BINARY_DIR}/app_lock-$<CONFIG>.txt
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_qtest.cmake)
  set_tests_properties(app_lock PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  qt_add_executable(rabbitshell_bookmark_tests tests/test_bookmarks.cpp)
  set_target_properties(rabbitshell_bookmark_tests PROPERTIES WIN32_EXECUTABLE FALSE)
  target_link_libraries(rabbitshell_bookmark_tests PRIVATE rabbitshell_ui Qt6::Test)
  add_test(NAME bookmarks COMMAND ${CMAKE_COMMAND}
    -DTEST_EXECUTABLE=$<TARGET_FILE:rabbitshell_bookmark_tests>
    -DTEST_REPORT=${CMAKE_CURRENT_BINARY_DIR}/bookmarks-$<CONFIG>.txt
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_qtest.cmake)
  set_tests_properties(bookmarks PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  qt_add_executable(rabbitshell_diagnostic_tests tests/test_diagnostics.cpp)
  set_target_properties(rabbitshell_diagnostic_tests PROPERTIES WIN32_EXECUTABLE FALSE)
  target_link_libraries(rabbitshell_diagnostic_tests PRIVATE rabbitshell_core Qt6::Widgets Qt6::Test)
  add_test(NAME diagnostics COMMAND ${CMAKE_COMMAND}
    -DTEST_EXECUTABLE=$<TARGET_FILE:rabbitshell_diagnostic_tests>
    -DTEST_REPORT=${CMAKE_CURRENT_BINARY_DIR}/diagnostics-$<CONFIG>.txt
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_qtest.cmake)
  set_tests_properties(diagnostics PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  if(WIN32)
    qt_add_executable(rabbitshell_nativedrag_tests tests/test_nativefiledrag.cpp)
    set_target_properties(rabbitshell_nativedrag_tests PROPERTIES WIN32_EXECUTABLE FALSE)
    target_link_libraries(rabbitshell_nativedrag_tests PRIVATE rabbitshell_ui Qt6::Test)
    add_test(NAME native_drag COMMAND ${CMAKE_COMMAND}
      -DTEST_EXECUTABLE=$<TARGET_FILE:rabbitshell_nativedrag_tests>
      -DTEST_REPORT=${CMAKE_CURRENT_BINARY_DIR}/native_drag-$<CONFIG>.txt
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_qtest.cmake)
    set_tests_properties(native_drag PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  endif()
  qt_add_executable(rabbitshell_filetransfer_tests tests/test_filetransfer.cpp)
  set_target_properties(rabbitshell_filetransfer_tests PROPERTIES WIN32_EXECUTABLE FALSE)
  target_link_libraries(rabbitshell_filetransfer_tests PRIVATE rabbitshell_ui Qt6::Test)
  # Test the platform -> QWindow -> QWidget drag routing, not only direct viewport events.
  target_link_libraries(rabbitshell_filetransfer_tests PRIVATE Qt6::GuiPrivate)
  add_test(NAME file_transfers COMMAND ${CMAKE_COMMAND}
    -DTEST_EXECUTABLE=$<TARGET_FILE:rabbitshell_filetransfer_tests>
    -DTEST_REPORT=${CMAKE_CURRENT_BINARY_DIR}/file_transfers-$<CONFIG>.txt
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_qtest.cmake)
  set_tests_properties(file_transfers PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  qt_add_executable(rabbitshell_appearance_tests tests/test_appearance.cpp)
  set_target_properties(rabbitshell_appearance_tests PROPERTIES WIN32_EXECUTABLE FALSE)
  target_link_libraries(rabbitshell_appearance_tests PRIVATE rabbitshell_ui Qt6::Test)
  add_test(NAME appearance COMMAND ${CMAKE_COMMAND}
    -DTEST_EXECUTABLE=$<TARGET_FILE:rabbitshell_appearance_tests>
    -DTEST_REPORT=${CMAKE_CURRENT_BINARY_DIR}/appearance-$<CONFIG>.txt
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_qtest.cmake)
  set_tests_properties(appearance PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  qt_add_executable(rabbitshell_services_tests tests/test_services.cpp)
  set_target_properties(rabbitshell_services_tests PROPERTIES WIN32_EXECUTABLE FALSE)
  target_link_libraries(rabbitshell_services_tests PRIVATE rabbitshell_ui Qt6::Test)
  add_test(NAME services COMMAND ${CMAKE_COMMAND}
    -DTEST_EXECUTABLE=$<TARGET_FILE:rabbitshell_services_tests>
    -DTEST_REPORT=${CMAKE_CURRENT_BINARY_DIR}/services-$<CONFIG>.txt
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_qtest.cmake)
  set_tests_properties(services PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  qt_add_executable(rabbitshell_tests tests/test_core.cpp)
  set_target_properties(rabbitshell_tests PROPERTIES WIN32_EXECUTABLE FALSE)
  target_link_libraries(rabbitshell_tests PRIVATE rabbitshell_ui Qt6::Test)
  add_test(NAME core COMMAND ${CMAKE_COMMAND}
    -DTEST_EXECUTABLE=$<TARGET_FILE:rabbitshell_tests>
    -DTEST_REPORT=${CMAKE_CURRENT_BINARY_DIR}/core-$<CONFIG>.txt
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_qtest.cmake)
  set_tests_properties(core PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  qt_add_executable(rabbitshell_editorarea_tests tests/test_editorarea.cpp)
  set_target_properties(rabbitshell_editorarea_tests PROPERTIES WIN32_EXECUTABLE FALSE)
  target_link_libraries(rabbitshell_editorarea_tests PRIVATE rabbitshell_ui Qt6::Test)
  add_test(NAME editor_groups COMMAND ${CMAKE_COMMAND}
    -DTEST_EXECUTABLE=$<TARGET_FILE:rabbitshell_editorarea_tests>
    -DTEST_REPORT=${CMAKE_CURRENT_BINARY_DIR}/editor_groups-$<CONFIG>.txt
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_qtest.cmake)
  set_tests_properties(editor_groups PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  qt_add_executable(rabbitshell_terminal_tests tests/test_terminal.cpp)
  set_target_properties(rabbitshell_terminal_tests PROPERTIES WIN32_EXECUTABLE FALSE)
  target_link_libraries(rabbitshell_terminal_tests PRIVATE rabbitshell_ui Qt6::Test)
  add_test(NAME terminal COMMAND ${CMAKE_COMMAND}
    -DTEST_EXECUTABLE=$<TARGET_FILE:rabbitshell_terminal_tests>
    -DTEST_REPORT=${CMAKE_CURRENT_BINARY_DIR}/terminal-$<CONFIG>.txt
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_qtest.cmake)
  set_tests_properties(terminal PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  qt_add_executable(rabbitshell_process_tests tests/test_processes.cpp)
  set_target_properties(rabbitshell_process_tests PROPERTIES WIN32_EXECUTABLE FALSE)
  target_link_libraries(rabbitshell_process_tests PRIVATE rabbitshell_ui Qt6::Test)
  add_test(NAME processes COMMAND ${CMAKE_COMMAND}
    -DTEST_EXECUTABLE=$<TARGET_FILE:rabbitshell_process_tests>
    -DTEST_REPORT=${CMAKE_CURRENT_BINARY_DIR}/processes-$<CONFIG>.txt
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_qtest.cmake)
  set_tests_properties(processes PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  qt_add_executable(rabbitshell_control_tests tests/test_controls.cpp)
  set_target_properties(rabbitshell_control_tests PROPERTIES WIN32_EXECUTABLE FALSE)
  target_link_libraries(rabbitshell_control_tests PRIVATE rabbitshell_ui Qt6::Test)
  add_test(NAME controls COMMAND ${CMAKE_COMMAND}
    -DTEST_EXECUTABLE=$<TARGET_FILE:rabbitshell_control_tests>
    -DTEST_REPORT=${CMAKE_CURRENT_BINARY_DIR}/controls-$<CONFIG>.txt
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_qtest.cmake)
  set_tests_properties(controls PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  qt_add_executable(rabbitshell_workspace_tests tests/test_workspace.cpp)
  set_target_properties(rabbitshell_workspace_tests PROPERTIES WIN32_EXECUTABLE FALSE)
  target_link_libraries(rabbitshell_workspace_tests PRIVATE rabbitshell_ui Qt6::Test)
  add_test(NAME workspace COMMAND ${CMAKE_COMMAND}
    -DTEST_EXECUTABLE=$<TARGET_FILE:rabbitshell_workspace_tests>
    -DTEST_REPORT=${CMAKE_CURRENT_BINARY_DIR}/workspace-$<CONFIG>.txt
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_qtest.cmake)
  set_tests_properties(workspace PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  # Explicit isolated benchmark; excluded from routine CTest because allocator trends are diagnostic.
  qt_add_executable(rabbitshell_memory_probe tests/probe_editor_memory.cpp)
  set_target_properties(rabbitshell_memory_probe PROPERTIES WIN32_EXECUTABLE FALSE)
  target_link_libraries(rabbitshell_memory_probe PRIVATE rabbitshell_ui Qt6::Test)
  if(WIN32)
    target_link_libraries(rabbitshell_memory_probe PRIVATE psapi)
  endif()
  qt_add_executable(rabbitshell_session_tests tests/test_session.cpp)
  set_target_properties(rabbitshell_session_tests PROPERTIES WIN32_EXECUTABLE FALSE)
  target_link_libraries(rabbitshell_session_tests PRIVATE rabbitshell_core Qt6::Test)
  add_test(NAME ssh_sftp COMMAND ${CMAKE_COMMAND}
    -DTEST_EXECUTABLE=$<TARGET_FILE:rabbitshell_session_tests>
    -DTEST_REPORT=${CMAKE_CURRENT_BINARY_DIR}/ssh_sftp-$<CONFIG>.txt
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_qtest.cmake)
  set_tests_properties(ssh_sftp PROPERTIES SKIP_REGULAR_EXPRESSION "Set RABBITSHELL_TEST_PORT")
  qt_add_executable(rabbitshell_credential_tests tests/test_credentials.cpp)
  set_target_properties(rabbitshell_credential_tests PROPERTIES WIN32_EXECUTABLE FALSE)
  target_link_libraries(rabbitshell_credential_tests PRIVATE rabbitshell_ui Qt6::Test)
  add_test(NAME credentials COMMAND ${CMAKE_COMMAND}
    -DTEST_EXECUTABLE=$<TARGET_FILE:rabbitshell_credential_tests>
    -DTEST_REPORT=${CMAKE_CURRENT_BINARY_DIR}/credentials-$<CONFIG>.txt
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_qtest.cmake)
  set_tests_properties(credentials PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  qt_add_executable(rabbitshell_tunnel_tests tests/test_tunnel.cpp)
  qt_add_executable(rabbitshell_storage_worker_tests tests/test_storage_worker.cpp)
  set_target_properties(rabbitshell_storage_worker_tests PROPERTIES WIN32_EXECUTABLE FALSE)
  target_link_libraries(rabbitshell_storage_worker_tests PRIVATE rabbitshell_core Qt6::Widgets Qt6::Test)
  add_test(NAME storage_worker COMMAND ${CMAKE_COMMAND}
    -DTEST_EXECUTABLE=$<TARGET_FILE:rabbitshell_storage_worker_tests>
    -DTEST_REPORT=${CMAKE_CURRENT_BINARY_DIR}/storage-worker-$<CONFIG>.txt
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_qtest.cmake)
  set_tests_properties(storage_worker PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  set_target_properties(rabbitshell_tunnel_tests PROPERTIES WIN32_EXECUTABLE FALSE)
  target_link_libraries(rabbitshell_tunnel_tests PRIVATE rabbitshell_core Qt6::Test)
  add_test(NAME tunnels COMMAND ${CMAKE_COMMAND}
    -DTEST_EXECUTABLE=$<TARGET_FILE:rabbitshell_tunnel_tests>
    -DTEST_REPORT=${CMAKE_CURRENT_BINARY_DIR}/tunnels-$<CONFIG>.txt
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_qtest.cmake)
  set_tests_properties(tunnels PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  qt_add_executable(rabbitshell_ports_tests tests/test_ports.cpp)
  set_target_properties(rabbitshell_ports_tests PROPERTIES WIN32_EXECUTABLE FALSE)
  target_link_libraries(rabbitshell_ports_tests PRIVATE rabbitshell_ui Qt6::Test)
  add_test(NAME ports COMMAND ${CMAKE_COMMAND}
    -DTEST_EXECUTABLE=$<TARGET_FILE:rabbitshell_ports_tests>
    -DTEST_REPORT=${CMAKE_CURRENT_BINARY_DIR}/ports-$<CONFIG>.txt
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_qtest.cmake)
  set_tests_properties(ports PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  qt_add_executable(rabbitshell_sshtrust_tests tests/test_sshtrust.cpp)
  set_target_properties(rabbitshell_sshtrust_tests PROPERTIES WIN32_EXECUTABLE FALSE)
  target_link_libraries(rabbitshell_sshtrust_tests PRIVATE rabbitshell_core Qt6::Test)
  add_test(NAME sshtrust COMMAND ${CMAKE_COMMAND}
    -DTEST_EXECUTABLE=$<TARGET_FILE:rabbitshell_sshtrust_tests>
    -DTEST_REPORT=${CMAKE_CURRENT_BINARY_DIR}/sshtrust-$<CONFIG>.txt
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_qtest.cmake)
  set_tests_properties(sshtrust PROPERTIES SKIP_REGULAR_EXPRESSION "Set RABBITSHELL_TUNNEL_FIXTURE")
  qt_add_executable(frogshell_update_probe tests/probe_release_download.cpp)
  set_target_properties(frogshell_update_probe PROPERTIES WIN32_EXECUTABLE FALSE)
  target_link_libraries(frogshell_update_probe PRIVATE rabbitshell_core Qt6::Network)
  add_custom_target(frogshell_editor_verification DEPENDS RabbitShell frogshell_selection_probe
    frogshell_editor_engine_tests rabbitshell_editor_highlights_tests rabbitshell_search_tests
    rabbitshell_syntaxdiagnostic_tests rabbitshell_appearance_tests rabbitshell_editorarea_tests
    rabbitshell_workspace_tests rabbitshell_menu_tests rabbitshell_memory_probe)
endif()
install(TARGETS RabbitShell RUNTIME DESTINATION . BUNDLE DESTINATION .)
qt_generate_deploy_app_script(TARGET RabbitShell OUTPUT_SCRIPT deploy_script NO_UNSUPPORTED_PLATFORM_ERROR)
install(SCRIPT ${deploy_script})
