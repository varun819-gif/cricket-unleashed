cmake_minimum_required(VERSION 3.20)
project(CricketUnleashed LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
  add_compile_options(/W4 /utf-8)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Engine-independent gameplay core: rules, scoring, toss, fields, AI,
# commentary and the server-authoritative session. Shared by the Unreal
# client module, the dedicated server and the tests.
add_library(cricket_core
  core/src/MatchRules.cpp
  core/src/Innings.cpp
  core/src/Match.cpp
  core/src/Toss.cpp
  core/src/Lbw.cpp
  core/src/FieldPlacement.cpp
  core/src/AutoCaptain.cpp
  core/src/Commentary.cpp
  core/src/MatchSession.cpp
  core/src/TeamFactory.cpp
)
target_include_directories(cricket_core PUBLIC core/include)

# Text-mode match simulator / demo.
add_executable(cricket_sim tools/sim/main.cpp)
target_link_libraries(cricket_sim PRIVATE cricket_core)

enable_testing()
add_executable(cricket_tests
  core/tests/TestMain.cpp
  core/tests/InningsTests.cpp
  core/tests/MatchTests.cpp
  core/tests/FieldTests.cpp
  core/tests/SessionTests.cpp
)
target_link_libraries(cricket_tests PRIVATE cricket_core)
add_test(NAME cricket_tests COMMAND cricket_tests)
