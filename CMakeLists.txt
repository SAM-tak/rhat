cmake_minimum_required(VERSION 3.20)
project(rhat VERSION 0.1.0 LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
find_package(Threads REQUIRED)
add_library(rhat_json STATIC vendor/cjson/cJSON.c)
target_include_directories(rhat_json PUBLIC vendor/cjson)
target_compile_definitions(rhat_json PUBLIC CJSON_HIDE_SYMBOLS)
add_executable(rhat src/main.c src/build.c src/llvm.c src/llvm_builtins.c
  src/front.c src/modules.c src/sema.c src/types.c src/errors.c src/text.c src/statements.c src/check.c src/digest.c
  src/numeric.c src/stacks.c src/arrays.c src/ranges.c src/tooling.c src/overload.c
  lsp/server.c lsp/analyze.c lsp/rpc.c lsp/transport.c lsp/uri.c lsp/position.c lsp/port/thread.c)
target_include_directories(rhat PRIVATE src lsp)
target_link_libraries(rhat PRIVATE rhat_json Threads::Threads)
if(NOT WIN32)
  target_link_libraries(rhat PRIVATE m)
endif()
target_compile_definitions(rhat PRIVATE RH_STDLIB_PATH="${CMAKE_CURRENT_SOURCE_DIR}/stdlib")
target_compile_definitions(rhat PRIVATE RH_RANDOM_PATH="${CMAKE_CURRENT_SOURCE_DIR}/runtime/rhat_random.c")
target_compile_definitions(rhat PRIVATE RH_RUNTIME_PATH="${CMAKE_CURRENT_SOURCE_DIR}/runtime/rhat_runtime.c")
string(REGEX REPLACE ",.*$" "" RH_VS_ROOT "${CMAKE_GENERATOR_INSTANCE}")
find_program(RH_CLANG NAMES clang HINTS
  "${RH_VS_ROOT}/VC/Tools/Llvm/x64/bin"
  "$ENV{ProgramFiles}/LLVM/bin")
if(RH_CLANG)
  target_compile_definitions(rhat PRIVATE RH_DEFAULT_CLANG="${RH_CLANG}")
endif()
if(MSVC)
  target_compile_options(rhat PRIVATE /W4 /utf-8)
  target_compile_definitions(rhat PRIVATE _CRT_SECURE_NO_WARNINGS)
  # Inference recurses once per call in a chain of untyped functions, and
  # unoptimized frames are large; Windows reserves only 1MB by default.
  target_link_options(rhat PRIVATE /STACK:67108864)
else()
  target_compile_options(rhat PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_definitions(rhat PRIVATE _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE)
endif()
# The editor runs a copy of rhat (rhat lsp) so that building does not fight a
# running language server for the executable; a locked copy only warns.
set(RHAT_SERVER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.rhat-ls" CACHE PATH "Where rhat is copied for the editor's language server")
add_custom_command(TARGET rhat POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -DSOURCE=$<TARGET_FILE:rhat> -DDIRECTORY=${RHAT_SERVER_DIR}
    -P "${CMAKE_CURRENT_SOURCE_DIR}/scripts/copy-server.cmake"
  VERBATIM)
include(CTest)
if(BUILD_TESTING)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  if(NOT RH_CLANG)
    message(FATAL_ERROR "Clang is required for integration tests; set RH_CLANG to its executable path")
  endif()
  option(RHAT_TEST_ALL_OPTS "Also build executable test cases at -O0" OFF)
  set(RHAT_TEST_ARGS --clang "${RH_CLANG}")
  if(RHAT_TEST_ALL_OPTS)
    list(APPEND RHAT_TEST_ARGS --all-opts)
  endif()
  add_test(NAME integration COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/run.py"
    --compiler $<TARGET_FILE:rhat> ${RHAT_TEST_ARGS} --work "${CMAKE_CURRENT_BINARY_DIR}/tests")
  # Hundreds of native builds run at -O2 (and -O0 with RHAT_TEST_ALL_OPTS).
  # Each suite runs its cases on all cores (tests/harness.py), so run ctest
  # without -j; child commands retain their own short timeouts there.
  set_tests_properties(integration PROPERTIES TIMEOUT 3600)
  add_test(NAME modules COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/modules.py"
    --compiler $<TARGET_FILE:rhat> ${RHAT_TEST_ARGS} --work "${CMAKE_CURRENT_BINARY_DIR}/module-tests")
  add_test(NAME syntax COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/syntax.py"
    --compiler $<TARGET_FILE:rhat> ${RHAT_TEST_ARGS} --work "${CMAKE_CURRENT_BINARY_DIR}/syntax-tests")
  add_test(NAME nil COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/nil.py"
    --compiler $<TARGET_FILE:rhat> ${RHAT_TEST_ARGS} --work "${CMAKE_CURRENT_BINARY_DIR}/nil-tests")
  add_test(NAME fits COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/fits.py"
    --compiler $<TARGET_FILE:rhat> ${RHAT_TEST_ARGS} --work "${CMAKE_CURRENT_BINARY_DIR}/fits-tests")
  add_test(NAME lsp COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/lsp.py"
    --server $<TARGET_FILE:rhat> --work "${CMAKE_CURRENT_BINARY_DIR}/lsp-tests")
  add_test(NAME inference COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/inference.py"
    --compiler $<TARGET_FILE:rhat> ${RHAT_TEST_ARGS} --work "${CMAKE_CURRENT_BINARY_DIR}/inference-tests")
  add_test(NAME errors COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/errors.py"
    --compiler $<TARGET_FILE:rhat> ${RHAT_TEST_ARGS} --work "${CMAKE_CURRENT_BINARY_DIR}/error-tests")
  add_test(NAME ffi COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/ffi.py"
    --compiler $<TARGET_FILE:rhat> ${RHAT_TEST_ARGS} --work "${CMAKE_CURRENT_BINARY_DIR}/ffi-tests")
  add_test(NAME variadic COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/variadic.py"
    --compiler $<TARGET_FILE:rhat> ${RHAT_TEST_ARGS} --work "${CMAKE_CURRENT_BINARY_DIR}/variadic-tests")
  add_test(NAME dyn COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/dyn.py"
    --compiler $<TARGET_FILE:rhat> ${RHAT_TEST_ARGS} --work "${CMAKE_CURRENT_BINARY_DIR}/dyn-tests")
  add_test(NAME flex COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/flex.py"
    --compiler $<TARGET_FILE:rhat> ${RHAT_TEST_ARGS} --work "${CMAKE_CURRENT_BINARY_DIR}/flex-tests")
  add_test(NAME overloads COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/overloads.py"
    --compiler $<TARGET_FILE:rhat> ${RHAT_TEST_ARGS} --work "${CMAKE_CURRENT_BINARY_DIR}/overload-tests")
endif()
install(TARGETS rhat RUNTIME DESTINATION bin)
install(FILES runtime/rhat_runtime.c DESTINATION share/rhat)
install(FILES runtime/rhat_random.c DESTINATION share/rhat)
install(DIRECTORY stdlib/ DESTINATION share/rhat/stdlib)
install(FILES LICENSE DESTINATION share/rhat/licenses RENAME rhat.txt)
install(FILES vendor/cjson/LICENSE DESTINATION share/rhat/licenses RENAME cJSON.txt)
install(FILES lsp/LICENSE.lhat DESTINATION share/rhat/licenses RENAME lhat.txt)
