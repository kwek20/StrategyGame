cmake_minimum_required(VERSION 3.25)

project(StrategyGame VERSION 0.1.0 LANGUAGES CXX)

option(STRATEGY_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(STRATEGY_BUILD_TESTS "Build lightweight tests" ON)

file(GLOB_RECURSE STRATEGY_FORMAT_FILES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/*.hpp"
    "${CMAKE_SOURCE_DIR}/tests/*.cpp"
)
find_program(STRATEGY_CLANG_FORMAT NAMES clang-format clang-format-20 clang-format-19)
if(STRATEGY_CLANG_FORMAT)
    add_custom_target(format
        COMMAND "${STRATEGY_CLANG_FORMAT}" -i ${STRATEGY_FORMAT_FILES}
        COMMENT "Formatting StrategyGame C++ sources"
        VERBATIM
    )
    add_custom_target(format-check
        COMMAND "${STRATEGY_CLANG_FORMAT}" --dry-run --Werror ${STRATEGY_FORMAT_FILES}
        COMMENT "Checking StrategyGame C++ formatting"
        VERBATIM
    )
endif()

find_package(SDL3 CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)
find_package(RapidJSON CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(Freetype REQUIRED)

function(strategy_target target)
    target_include_directories(${target} PUBLIC "${CMAKE_SOURCE_DIR}/src")
    target_compile_features(${target} PUBLIC cxx_std_20)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /Zc:__cplusplus)
        if(STRATEGY_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(STRATEGY_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

# Foundational data and services.
add_library(strategy_core INTERFACE)
target_include_directories(strategy_core INTERFACE "${CMAKE_SOURCE_DIR}/src")
target_compile_features(strategy_core INTERFACE cxx_std_20)
target_link_libraries(strategy_core INTERFACE glm::glm)

add_library(strategy_diagnostics STATIC src/diagnostics/Logger.cpp)
strategy_target(strategy_diagnostics)
target_link_libraries(strategy_diagnostics PUBLIC strategy_core)

add_library(strategy_localization STATIC src/localization/Text.cpp)
strategy_target(strategy_localization)
target_link_libraries(strategy_localization PUBLIC strategy_core RapidJSON)

add_library(strategy_players STATIC
    src/players/PlayerRegistry.cpp
)
strategy_target(strategy_players)
target_link_libraries(strategy_players PUBLIC strategy_core)

add_library(strategy_terrain STATIC src/terrain/Terrain.cpp)
strategy_target(strategy_terrain)
target_link_libraries(strategy_terrain PUBLIC strategy_core)

# World storage and spatial services.
add_library(strategy_world STATIC
    src/world/World.cpp
    src/world/Collision.cpp
    src/world/Navigation.cpp
    src/world/WorldGeneration.cpp
)
strategy_target(strategy_world)
target_link_libraries(strategy_world
    PUBLIC strategy_core strategy_players strategy_terrain
    PRIVATE strategy_localization
)

add_library(strategy_gameplay STATIC src/gameplay/GameplayCatalogue.cpp)
strategy_target(strategy_gameplay)
target_link_libraries(strategy_gameplay PUBLIC strategy_world RapidJSON)

# Presentation-ready data assembled outside the renderer.
add_library(strategy_ui_model STATIC
    src/ui/EntityHudLayout.cpp
    src/ui/EntityHudModel.cpp
    src/ui/GameHudLayout.cpp
    src/ui/UiController.cpp
    src/ui/UiDocument.cpp
    src/ui/UiLayout.cpp
)
strategy_target(strategy_ui_model)
target_link_libraries(strategy_ui_model
    PUBLIC strategy_gameplay strategy_world
    PRIVATE strategy_localization
)

add_library(strategy_simulation STATIC
    src/simulation/GameSession.cpp
    src/simulation/CommandCodec.cpp
    src/simulation/StateChecksum.cpp
)
strategy_target(strategy_simulation)
target_link_libraries(strategy_simulation
    PUBLIC strategy_gameplay strategy_players strategy_terrain strategy_world
    PRIVATE strategy_localization
)

# Storage and configuration are independent of presentation.
add_library(strategy_persistence STATIC
    src/persistence/GameConfig.cpp
    src/persistence/SaveGame.cpp
)
strategy_target(strategy_persistence)
target_link_libraries(strategy_persistence
    PUBLIC strategy_gameplay strategy_players strategy_world RapidJSON
)

add_library(strategy_audio STATIC src/audio/AudioSystem.cpp)
strategy_target(strategy_audio)
target_link_libraries(strategy_audio PUBLIC strategy_core RapidJSON)

# CPU asset import has no graphics dependency and is safe on worker threads or headless builds.
add_library(strategy_asset_data STATIC src/assets/ModelAsset.cpp)
strategy_target(strategy_asset_data)
target_include_directories(strategy_asset_data PRIVATE ${Stb_INCLUDE_DIR})
target_link_libraries(strategy_asset_data PUBLIC strategy_core assimp::assimp)

# Render-thread ownership of uploaded model resources.
add_library(strategy_assets STATIC
    src/assets/AssetManifest.cpp
    src/assets/IconAtlas.cpp
    src/assets/Model.cpp
    src/assets/ResourceManager.cpp
    src/assets/Texture.cpp
)
strategy_target(strategy_assets)
target_link_libraries(strategy_assets PUBLIC strategy_asset_data strategy_core glad::glad RapidJSON)

add_library(strategy_camera STATIC
    src/game/RtsCamera.cpp
    src/game/ThirdPersonCamera.cpp
)
strategy_target(strategy_camera)
target_link_libraries(strategy_camera PUBLIC strategy_core)

add_library(strategy_render STATIC
    src/render/FontRenderer.cpp
    src/render/MaterialManager.cpp
    src/render/RenderCommandQueue.cpp
    src/render/RenderGraph.cpp
    src/render/RenderPass.cpp
    src/render/Renderer.cpp
    src/render/ShaderManager.cpp
    src/render/UiRenderer.cpp
)
strategy_target(strategy_render)
target_compile_definitions(strategy_render PRIVATE
    $<$<CONFIG:Debug>:STRATEGY_SHADER_HOT_RELOAD=1>
)
target_link_libraries(strategy_render
    PUBLIC strategy_assets strategy_camera strategy_diagnostics strategy_persistence strategy_players
           strategy_terrain strategy_world SDL3::SDL3 glad::glad Freetype::Freetype
    PRIVATE strategy_localization strategy_ui_model
)

# Interactive states compose engine subsystems but own no platform loop.
add_library(strategy_states STATIC
    src/game/PlayState.cpp
    src/game/BuildState.cpp
    src/game/StartMenuState.cpp
    src/game/SettingsState.cpp
)
strategy_target(strategy_states)
target_link_libraries(strategy_states
    PUBLIC strategy_audio strategy_camera strategy_diagnostics strategy_gameplay strategy_persistence
           strategy_players strategy_render strategy_simulation strategy_world SDL3::SDL3
    PRIVATE strategy_localization strategy_ui_model
)

add_library(strategy_app STATIC
    src/app/Application.cpp
    src/app/StateStack.cpp
)
strategy_target(strategy_app)
target_link_libraries(strategy_app
    PUBLIC strategy_audio strategy_diagnostics strategy_persistence strategy_render strategy_states SDL3::SDL3 glad::glad
    PRIVATE strategy_localization
)

add_executable(strategy_game src/main.cpp)
strategy_target(strategy_game)
target_link_libraries(strategy_game PRIVATE strategy_app)
set_target_properties(strategy_game PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

if(STRATEGY_BUILD_TESTS)
    enable_testing()

    add_executable(terrain_tests tests/TerrainTests.cpp)
    strategy_target(terrain_tests)
    target_link_libraries(terrain_tests PRIVATE strategy_terrain)
    add_test(NAME terrain_queries COMMAND terrain_tests)

    add_executable(game_system_tests tests/GameSystemTests.cpp)
    strategy_target(game_system_tests)
    target_link_libraries(game_system_tests
        PRIVATE strategy_camera strategy_gameplay strategy_simulation strategy_world
    )
    add_test(NAME game_systems COMMAND game_system_tests)
    set_tests_properties(game_systems PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

    add_executable(entity_hud_model_tests tests/EntityHudModelTests.cpp)
    strategy_target(entity_hud_model_tests)
    target_link_libraries(entity_hud_model_tests PRIVATE strategy_ui_model)
    add_test(NAME entity_hud_model COMMAND entity_hud_model_tests)
    set_tests_properties(entity_hud_model PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

    add_executable(definition_registry_tests tests/DefinitionRegistryTests.cpp)
    strategy_target(definition_registry_tests)
    target_link_libraries(definition_registry_tests PRIVATE strategy_gameplay)
    add_test(NAME definition_registry_validation COMMAND definition_registry_tests)
    set_tests_properties(definition_registry_validation
        PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

    add_executable(persistence_tests tests/PersistenceTests.cpp)
    strategy_target(persistence_tests)
    target_link_libraries(persistence_tests PRIVATE strategy_persistence)
    add_test(NAME persistence_round_trip COMMAND persistence_tests)
    set_tests_properties(persistence_round_trip PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

    add_executable(asset_import_tests tests/AssetImportTests.cpp)
    strategy_target(asset_import_tests)
    target_link_libraries(asset_import_tests PRIVATE strategy_asset_data)
    add_test(NAME asset_imports COMMAND asset_import_tests)
    set_tests_properties(asset_imports PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

    add_executable(state_stack_tests tests/StateStackTests.cpp)
    strategy_target(state_stack_tests)
    target_link_libraries(state_stack_tests PRIVATE strategy_app)
    add_test(NAME state_stack_transitions COMMAND state_stack_tests)
    set_tests_properties(state_stack_transitions PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

    add_executable(renderer_integration_tests tests/RendererIntegrationTests.cpp)
    strategy_target(renderer_integration_tests)
    target_link_libraries(renderer_integration_tests PRIVATE strategy_render)
    add_test(NAME renderer_integration COMMAND renderer_integration_tests)
    set_tests_properties(renderer_integration PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

    add_executable(render_architecture_tests tests/RenderArchitectureTests.cpp)
    strategy_target(render_architecture_tests)
    target_link_libraries(render_architecture_tests PRIVATE strategy_render)
    add_test(NAME render_architecture COMMAND render_architecture_tests)
endif()
