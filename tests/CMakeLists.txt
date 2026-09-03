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
