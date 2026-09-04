set(smoke_moc "${CMAKE_CURRENT_BINARY_DIR}/tst_smoke.moc")

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
