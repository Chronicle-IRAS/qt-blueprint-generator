set(smoke_moc "${CMAKE_CURRENT_BINARY_DIR}/tst_smoke.moc")

set(end_to_end_moc "${CMAKE_CURRENT_BINARY_DIR}/tst_end_to_end.moc")
add_custom_command(OUTPUT "${end_to_end_moc}"
    COMMAND Qt6::moc -o tst_end_to_end.moc "${CMAKE_CURRENT_SOURCE_DIR}/tst_end_to_end.cpp"
    DEPENDS tst_end_to_end.cpp WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" VERBATIM)
qt_add_executable(tst_end_to_end tst_end_to_end.cpp "${end_to_end_moc}")
set_target_properties(tst_end_to_end PROPERTIES AUTOMOC OFF)
target_include_directories(tst_end_to_end PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(tst_end_to_end PRIVATE BlueprintEditorCore Qt6::Test)
target_compile_definitions(tst_end_to_end PRIVATE
    TASK10_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/login-demo"
    TASK10_CMAKE_COMMAND="${CMAKE_COMMAND}"
    TASK10_CTEST_COMMAND="${CMAKE_CTEST_COMMAND}"
    TASK10_CMAKE_GENERATOR="${CMAKE_GENERATOR}"
    TASK10_MAKE_PROGRAM="${CMAKE_MAKE_PROGRAM}"
    TASK10_CXX_COMPILER="${CMAKE_CXX_COMPILER}"
    TASK10_QT_CMAKE_DIR="${Qt6_DIR}")
add_test(NAME end_to_end COMMAND tst_end_to_end)
set_tests_properties(end_to_end PROPERTIES TIMEOUT 300
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")

set(project_exporter_moc "${CMAKE_CURRENT_BINARY_DIR}/tst_project_exporter.moc")
add_custom_command(OUTPUT "${project_exporter_moc}"
    COMMAND Qt6::moc -o tst_project_exporter.moc "${CMAKE_CURRENT_SOURCE_DIR}/tst_project_exporter.cpp"
    DEPENDS tst_project_exporter.cpp WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" VERBATIM)
qt_add_executable(tst_project_exporter tst_project_exporter.cpp "${project_exporter_moc}")
set_target_properties(tst_project_exporter PROPERTIES AUTOMOC OFF)
target_include_directories(tst_project_exporter PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(tst_project_exporter PRIVATE BlueprintEditorCore Qt6::Test)
target_compile_definitions(tst_project_exporter PRIVATE
    TASK9_CMAKE_COMMAND="${CMAKE_COMMAND}"
    TASK9_CMAKE_GENERATOR="${CMAKE_GENERATOR}"
    TASK9_MAKE_PROGRAM="${CMAKE_MAKE_PROGRAM}"
    TASK9_CXX_COMPILER="${CMAKE_CXX_COMPILER}"
    TASK9_QT_CMAKE_DIR="${Qt6_DIR}")
add_test(NAME project_exporter COMMAND tst_project_exporter)
set_tests_properties(project_exporter PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")

set(external_code_importer_moc "${CMAKE_CURRENT_BINARY_DIR}/tst_external_code_importer.moc")
add_custom_command(OUTPUT "${external_code_importer_moc}"
    COMMAND Qt6::moc -o tst_external_code_importer.moc "${CMAKE_CURRENT_SOURCE_DIR}/tst_external_code_importer.cpp"
    DEPENDS tst_external_code_importer.cpp WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" VERBATIM)
qt_add_executable(tst_external_code_importer tst_external_code_importer.cpp "${external_code_importer_moc}")
set_target_properties(tst_external_code_importer PROPERTIES AUTOMOC OFF)
target_include_directories(tst_external_code_importer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(tst_external_code_importer PRIVATE BlueprintEditorCore Qt6::Test)
add_test(NAME external_code_importer COMMAND tst_external_code_importer)
set_tests_properties(external_code_importer PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")

set(project_scaffolder_moc "${CMAKE_CURRENT_BINARY_DIR}/tst_project_scaffolder.moc")
add_custom_command(OUTPUT "${project_scaffolder_moc}"
    COMMAND Qt6::moc -o tst_project_scaffolder.moc "${CMAKE_CURRENT_SOURCE_DIR}/tst_project_scaffolder.cpp"
    DEPENDS tst_project_scaffolder.cpp WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" VERBATIM)
qt_add_executable(tst_project_scaffolder tst_project_scaffolder.cpp "${project_scaffolder_moc}")
set_target_properties(tst_project_scaffolder PROPERTIES AUTOMOC OFF)
target_include_directories(tst_project_scaffolder PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(tst_project_scaffolder PRIVATE BlueprintEditorCore Qt6::Test)
target_compile_definitions(tst_project_scaffolder PRIVATE
    TASK7_CMAKE_COMMAND="${CMAKE_COMMAND}"
    TASK7_CMAKE_GENERATOR="${CMAKE_GENERATOR}"
    TASK7_MAKE_PROGRAM="${CMAKE_MAKE_PROGRAM}"
    TASK7_CXX_COMPILER="${CMAKE_CXX_COMPILER}"
    TASK7_QT_CMAKE_DIR="${Qt6_DIR}")
add_test(NAME project_scaffolder COMMAND tst_project_scaffolder)
set_tests_properties(project_scaffolder PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")

add_custom_command(
    OUTPUT "${smoke_moc}"
    COMMAND Qt6::moc -o tst_smoke.moc "${CMAKE_CURRENT_SOURCE_DIR}/tst_smoke.cpp"
    DEPENDS tst_smoke.cpp
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    VERBATIM
)

qt_add_executable(tst_smoke
    tst_smoke.cpp
    "${smoke_moc}"
)

set_target_properties(tst_smoke PROPERTIES
    AUTOMOC OFF
)

target_include_directories(tst_smoke PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(tst_smoke PRIVATE
    BlueprintEditorCore
    Qt6::Test
)

add_test(NAME smoke COMMAND tst_smoke)

set_tests_properties(smoke PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

set(blueprint_document_moc "${CMAKE_CURRENT_BINARY_DIR}/tst_blueprint_document.moc")

add_custom_command(
    OUTPUT "${blueprint_document_moc}"
    COMMAND Qt6::moc -o tst_blueprint_document.moc "${CMAKE_CURRENT_SOURCE_DIR}/tst_blueprint_document.cpp"
    DEPENDS tst_blueprint_document.cpp
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    VERBATIM
)

qt_add_executable(tst_blueprint_document
    tst_blueprint_document.cpp
    "${blueprint_document_moc}"
)

set_target_properties(tst_blueprint_document PROPERTIES
    AUTOMOC OFF
)

target_include_directories(tst_blueprint_document PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(tst_blueprint_document PRIVATE
    BlueprintEditorCore
    Qt6::Test
)

add_test(NAME blueprint_document COMMAND tst_blueprint_document)

set_tests_properties(blueprint_document PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

set(blueprint_validator_moc "${CMAKE_CURRENT_BINARY_DIR}/tst_blueprint_validator.moc")

add_custom_command(
    OUTPUT "${blueprint_validator_moc}"
    COMMAND Qt6::moc -o tst_blueprint_validator.moc "${CMAKE_CURRENT_SOURCE_DIR}/tst_blueprint_validator.cpp"
    DEPENDS tst_blueprint_validator.cpp
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    VERBATIM
)

qt_add_executable(tst_blueprint_validator
    tst_blueprint_validator.cpp
    "${blueprint_validator_moc}"
)

set_target_properties(tst_blueprint_validator PROPERTIES
    AUTOMOC OFF
)

target_include_directories(tst_blueprint_validator PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(tst_blueprint_validator PRIVATE
    BlueprintEditorCore
    Qt6::Test
)

add_test(NAME blueprint_validator COMMAND tst_blueprint_validator)

set_tests_properties(blueprint_validator PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

set(blueprint_scene_moc "${CMAKE_CURRENT_BINARY_DIR}/tst_blueprint_scene.moc")

add_custom_command(
    OUTPUT "${blueprint_scene_moc}"
    COMMAND Qt6::moc -o tst_blueprint_scene.moc "${CMAKE_CURRENT_SOURCE_DIR}/tst_blueprint_scene.cpp"
    DEPENDS tst_blueprint_scene.cpp
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    VERBATIM
)

qt_add_executable(tst_blueprint_scene
    tst_blueprint_scene.cpp
    "${blueprint_scene_moc}"
)

set_target_properties(tst_blueprint_scene PROPERTIES
    AUTOMOC OFF
)

target_include_directories(tst_blueprint_scene PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(tst_blueprint_scene PRIVATE
    BlueprintEditorCore
    Qt6::Test
)

add_test(NAME blueprint_scene COMMAND tst_blueprint_scene)

set_tests_properties(blueprint_scene PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

set(ir_compiler_moc "${CMAKE_CURRENT_BINARY_DIR}/tst_ir_compiler.moc")

add_custom_command(
    OUTPUT "${ir_compiler_moc}"
    COMMAND Qt6::moc -o tst_ir_compiler.moc "${CMAKE_CURRENT_SOURCE_DIR}/tst_ir_compiler.cpp"
    DEPENDS tst_ir_compiler.cpp
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    VERBATIM
)

qt_add_executable(tst_ir_compiler
    tst_ir_compiler.cpp
    "${ir_compiler_moc}"
)

set_target_properties(tst_ir_compiler PROPERTIES
    AUTOMOC OFF
)

target_include_directories(tst_ir_compiler PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(tst_ir_compiler PRIVATE
    BlueprintEditorCore
    Qt6::Test
)

add_test(NAME ir_compiler COMMAND tst_ir_compiler)

set_tests_properties(ir_compiler PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

set(prompt_compiler_moc "${CMAKE_CURRENT_BINARY_DIR}/tst_prompt_compiler.moc")

add_custom_command(
    OUTPUT "${prompt_compiler_moc}"
    COMMAND Qt6::moc -o tst_prompt_compiler.moc "${CMAKE_CURRENT_SOURCE_DIR}/tst_prompt_compiler.cpp"
    DEPENDS tst_prompt_compiler.cpp
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    VERBATIM
)

qt_add_executable(tst_prompt_compiler
    tst_prompt_compiler.cpp
    "${prompt_compiler_moc}"
)

set_target_properties(tst_prompt_compiler PROPERTIES
    AUTOMOC OFF
)

target_include_directories(tst_prompt_compiler PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(tst_prompt_compiler PRIVATE
    BlueprintEditorCore
    Qt6::Test
)

add_test(NAME prompt_compiler COMMAND tst_prompt_compiler)

set_tests_properties(prompt_compiler PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

set(generation_service_moc "${CMAKE_CURRENT_BINARY_DIR}/tst_generation_service.moc")

add_custom_command(
    OUTPUT "${generation_service_moc}"
    COMMAND Qt6::moc -o tst_generation_service.moc "${CMAKE_CURRENT_SOURCE_DIR}/tst_generation_service.cpp"
    DEPENDS tst_generation_service.cpp
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    VERBATIM
)

qt_add_executable(tst_generation_service
    tst_generation_service.cpp
    "${generation_service_moc}"
)

set_target_properties(tst_generation_service PROPERTIES
    AUTOMOC OFF
)

target_include_directories(tst_generation_service PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(tst_generation_service PRIVATE
    BlueprintEditorCore
    Qt6::Network
    Qt6::Test
)

add_test(NAME generation_service COMMAND tst_generation_service)

set_tests_properties(generation_service PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)
