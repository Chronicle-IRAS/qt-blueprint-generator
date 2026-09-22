cmake_minimum_required(VERSION 3.22)

project(BlueprintEditor LANGUAGES CXX)

option(BUILD_MANUAL_AI_INTEGRATION
    "Build the opt-in real-provider manual integration executable"
    OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS LinguistTools Network Widgets)

qt_standard_project_setup()

qt_add_library(BlueprintEditorCore STATIC
    src/ui/theme.cpp
    src/ui/theme.h
    src/ai/ai_connection_tester.cpp
    src/ai/ai_connection_tester.h
    src/ai/ai_client.h
    src/ai/ai_provider_settings.cpp
    src/ai/ai_provider_settings.h
    src/ai/fake_ai_client.cpp
    src/ai/fake_ai_client.h
    src/ai/openai_compatible_client.cpp
    src/ai/openai_compatible_client.h
    src/app/ai_settings_dialog.cpp
    src/app/ai_settings_dialog.h
    src/app/candidate_review_dialog.cpp
    src/app/candidate_review_dialog.h
    src/app/generation_controller.cpp
    src/app/generation_controller.h
    src/app/main_window.cpp
    src/app/main_window.h
    src/blueprint/blueprint_document.cpp
    src/blueprint/blueprint_document.h
    src/blueprint/blueprint_serializer.cpp
    src/blueprint/blueprint_serializer.h
    src/blueprint/blueprint_validator.cpp
    src/blueprint/blueprint_validator.h
    src/editor/blueprint_scene.cpp
    src/editor/blueprint_scene.h
    src/editor/edge_item.cpp
    src/editor/edge_item.h
    src/editor/node_properties_editor.cpp
    src/editor/node_properties_editor.h
    src/editor/node_item.cpp
    src/editor/node_item.h
    src/generation/ir_compiler.cpp
    src/generation/ir_compiler.h
    src/generation/generation_service.cpp
    src/generation/generation_service.h
    src/generation/prompt_compiler.cpp
    src/generation/prompt_compiler.h
    src/generation/project_scaffolder.cpp
    src/generation/project_scaffolder.h
    src/generation/workspace_io.cpp
    src/generation/workspace_io.h
    src/generation/candidate_store.cpp
    src/workspace/external_code_importer.cpp
    src/workspace/external_code_importer.h
    src/workspace/build_service.cpp
    src/workspace/build_service.h
    src/workspace/project_exporter.cpp
    src/workspace/project_exporter.h
    src/workspace/project_exporter_recovery_p.h
)

target_include_directories(BlueprintEditorCore PUBLIC
    ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(BlueprintEditorCore PUBLIC
    Qt6::Widgets
    PRIVATE
    Qt6::Network
)

qt_add_executable(BlueprintEditor
    src/main.cpp
)

target_link_libraries(BlueprintEditor PRIVATE
    BlueprintEditorCore
)

if(BUILD_MANUAL_AI_INTEGRATION)
    qt_add_executable(manual_ai_integration
        tools/manual_ai_integration.cpp
        tools/manual_ai_integration_support.cpp
        tools/manual_ai_integration_support.h
    )
    target_include_directories(manual_ai_integration PRIVATE ${PROJECT_SOURCE_DIR})
    target_link_libraries(manual_ai_integration PRIVATE BlueprintEditorCore Qt6::Network)
endif()

include(CTest)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()

set(translation_targets BlueprintEditor)
if(BUILD_TESTING)
    list(APPEND translation_targets tst_language_switch tst_ai_settings_dialog tst_generation_workflow tst_candidate_review_dialog tst_editor_theme)
endif()

qt_add_translations(
    TARGETS ${translation_targets}
    SOURCE_TARGETS BlueprintEditorCore
    TS_FILES translations/BlueprintEditor_zh_CN.ts
    RESOURCE_PREFIX "/i18n"
)
