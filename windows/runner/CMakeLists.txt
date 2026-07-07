cmake_minimum_required(VERSION 3.14)
project(runner LANGUAGES CXX)

# Define the application target. To change its name, change BINARY_NAME in the
# top-level CMakeLists.txt, not the value here, or `flutter run` will no longer
# work.
#
# Any new source files that you add to the application should be added here.
add_executable(${BINARY_NAME} WIN32
  "flutter_window.cpp"
  "main.cpp"
  "utils.cpp"
  "win32_window.cpp"
  "xo_sole_windows_bertec.cpp"
  "${FLUTTER_MANAGED_DIR}/generated_plugin_registrant.cc"
  "Runner.rc"
  "runner.exe.manifest"
)

# Apply the standard set of build settings. This can be removed for applications
# that need different build settings.
apply_standard_settings(${BINARY_NAME})

# Add preprocessor definitions for the build version.
target_compile_definitions(${BINARY_NAME} PRIVATE "FLUTTER_VERSION=\"${FLUTTER_VERSION}\"")
target_compile_definitions(${BINARY_NAME} PRIVATE "FLUTTER_VERSION_MAJOR=${FLUTTER_VERSION_MAJOR}")
target_compile_definitions(${BINARY_NAME} PRIVATE "FLUTTER_VERSION_MINOR=${FLUTTER_VERSION_MINOR}")
target_compile_definitions(${BINARY_NAME} PRIVATE "FLUTTER_VERSION_PATCH=${FLUTTER_VERSION_PATCH}")
target_compile_definitions(${BINARY_NAME} PRIVATE "FLUTTER_VERSION_BUILD=${FLUTTER_VERSION_BUILD}")

# Disable Windows macros that collide with C++ standard library functions.
target_compile_definitions(${BINARY_NAME} PRIVATE "NOMINMAX")

# Add dependency libraries and include directories. Add any application-specific
# dependencies here.
target_link_libraries(${BINARY_NAME} PRIVATE flutter flutter_wrapper_app)
target_link_libraries(${BINARY_NAME} PRIVATE "dwmapi.lib")
target_include_directories(${BINARY_NAME} PRIVATE "${CMAKE_SOURCE_DIR}")

# Bertec SDK, used directly by the xo_sole native implementation in the runner.
set(BERTEC_SDK_ROOT "${CMAKE_SOURCE_DIR}/../Bertec_Device_SDK_March_2026")
target_include_directories(${BINARY_NAME} PRIVATE "${BERTEC_SDK_ROOT}")
target_link_libraries(${BINARY_NAME} PRIVATE "${BERTEC_SDK_ROOT}/x64/BertecDevice.lib")
# SHGetKnownFolderPath / CoTaskMemFree used by the recording/diagnostic logging.
target_link_libraries(${BINARY_NAME} PRIVATE "Shell32.lib" "Ole32.lib")

# The xo_sole integration pulls in filesystem/thread/fstream STL and includes the
# vendor SDK header; keep its warnings from failing the runner's /WX build without
# relaxing the strict settings applied to the rest of the runner sources.
set_source_files_properties("xo_sole_windows_bertec.cpp" PROPERTIES
  COMPILE_OPTIONS "/WX-")

# Run the Flutter tool portions of the build. This must not be removed.
add_dependencies(${BINARY_NAME} flutter_assemble)
