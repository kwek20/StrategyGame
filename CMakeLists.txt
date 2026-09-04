cmake_minimum_required(VERSION 3.25)

project(StrategyGame VERSION 0.1.0 LANGUAGES CXX)

option(STRATEGY_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(STRATEGY_BUILD_TESTS "Build lightweight terrain tests" ON)

find_package(SDL3 CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)
find_package(RapidJSON CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(Freetype REQUIRED)

add_executable(strategy_game
    src/main.cpp
    src/app/Application.cpp
    src/assets/Model.cpp
    src/assets/ResourceManager.cpp
    src/game/PlayState.cpp
    src/game/BuildState.cpp
    src/game/RtsCamera.cpp
    src/game/StartMenuState.cpp
    src/game/SettingsState.cpp
    src/game/ThirdPersonCamera.cpp
    src/gameplay/GameplayCatalogue.cpp
    src/localization/Text.cpp
    src/render/FontRenderer.cpp
    src/persistence/GameConfig.cpp
    src/persistence/SaveGame.cpp
    src/players/PlayerRegistry.cpp
    src/players/CountryCatalogue.cpp
    src/render/Renderer.cpp
    src/simulation/GameSession.cpp
    src/terrain/Terrain.cpp
    src/world/World.cpp
    src/world/Collision.cpp
    src/world/WorldGeneration.cpp
    src/world/Navigation.cpp
)

target_include_directories(strategy_game PRIVATE src)
target_include_directories(strategy_game PRIVATE ${Stb_INCLUDE_DIR})
target_compile_features(strategy_game PRIVATE cxx_std_20)
target_link_libraries(strategy_game PRIVATE
    SDL3::SDL3
    glad::glad
    glm::glm
    assimp::assimp
    RapidJSON
    Freetype::Freetype
)

if(MSVC)
    target_compile_options(strategy_game PRIVATE /W4 /permissive- /Zc:__cplusplus)
    if(STRATEGY_WARNINGS_AS_ERRORS)
        target_compile_options(strategy_game PRIVATE /WX)
    endif()
else()
    target_compile_options(strategy_game PRIVATE -Wall -Wextra -Wpedantic)
    if(STRATEGY_WARNINGS_AS_ERRORS)
        target_compile_options(strategy_game PRIVATE -Werror)
    endif()
endif()

set_target_properties(strategy_game PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

if(STRATEGY_BUILD_TESTS)
    enable_testing()
    add_executable(terrain_tests
        tests/TerrainTests.cpp
        src/terrain/Terrain.cpp
    )
    target_include_directories(terrain_tests PRIVATE src)
    target_compile_features(terrain_tests PRIVATE cxx_std_20)
    target_link_libraries(terrain_tests PRIVATE glm::glm)
    add_test(NAME terrain_queries COMMAND terrain_tests)

    add_executable(game_system_tests
        tests/GameSystemTests.cpp
        src/game/RtsCamera.cpp
        src/game/ThirdPersonCamera.cpp
        src/gameplay/GameplayCatalogue.cpp
        src/localization/Text.cpp
        src/players/CountryCatalogue.cpp
        src/players/PlayerRegistry.cpp
        src/simulation/GameSession.cpp
        src/terrain/Terrain.cpp
        src/world/World.cpp
        src/world/Collision.cpp
        src/world/WorldGeneration.cpp
        src/world/Navigation.cpp
    )
    target_include_directories(game_system_tests PRIVATE src)
    target_compile_features(game_system_tests PRIVATE cxx_std_20)
    target_link_libraries(game_system_tests PRIVATE glm::glm RapidJSON)
    add_test(NAME game_systems COMMAND game_system_tests)
    set_tests_properties(game_systems PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )

    add_executable(persistence_tests
        tests/PersistenceTests.cpp
        src/persistence/GameConfig.cpp
        src/persistence/SaveGame.cpp
        src/gameplay/GameplayCatalogue.cpp
        src/players/PlayerRegistry.cpp
        src/world/World.cpp
    )
    target_include_directories(persistence_tests PRIVATE src)
    target_compile_features(persistence_tests PRIVATE cxx_std_20)
    target_link_libraries(persistence_tests PRIVATE glm::glm RapidJSON)
    add_test(NAME persistence_round_trip COMMAND persistence_tests)
    set_tests_properties(persistence_round_trip PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )

    add_executable(asset_import_tests tests/AssetImportTests.cpp)
    target_compile_features(asset_import_tests PRIVATE cxx_std_20)
    target_link_libraries(asset_import_tests PRIVATE assimp::assimp)
    add_test(NAME asset_imports COMMAND asset_import_tests)
    set_tests_properties(asset_imports PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
endif()
