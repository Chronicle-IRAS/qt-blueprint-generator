#include "generation/project_scaffolder.h"
#include "generation/ir_compiler.h"
#include "generation/runtime_assembler.h"
#include "generation/workspace_io.h"
#include "blueprint/blueprint_serializer.h"
#include <QJsonArray>
#include <QSet>
#include <algorithm>

bool ProjectScaffolder::createRuntime(const BlueprintDocument &document, const QString &workspace, QString *error)
{
    return createInMode(document, workspace, Mode::LinearRuntime, error);
}

bool ProjectScaffolder::create(const BlueprintDocument &document, const QString &workspace, QString *error)
{
    return createInMode(document, workspace, Mode::ModulesOnly, error);
}

bool ProjectScaffolder::createInMode(const BlueprintDocument &document, const QString &workspace, Mode mode, QString *error)
{
    using namespace WorkspaceIo;
    if (error) error->clear();
    QJsonObject ir = IrCompiler::compile(document);
    QJsonObject project = ir.value("project").toObject();
    // IR identifiers are valid C++, but may still name Qt classes or macros.
    // Store the actual scaffold namespace alongside the contracts consumed by callers.
    const QString ns = "Blueprint_" + project.value("namespace").toString();
    project["namespace"] = ns;
    ir["project"] = project;
    const QByteArray canonical = IrCompiler::toCanonicalJson(ir);
    BlueprintDocument sorted = document;
    std::sort(sorted.nodes.begin(), sorted.nodes.end(), [](const BlueprintNode &a, const BlueprintNode &b) { return a.id < b.id; });
    std::sort(sorted.edges.begin(), sorted.edges.end(), [](const BlueprintEdge &a, const BlueprintEdge &b) { return a.id < b.id; });
    const QByteArray sourceBlueprint = BlueprintSerializer::toJson(sorted);
    QMap<QString, QByteArray> files;
    const bool runtime = mode == Mode::LinearRuntime;
    QMap<QString, QByteArray> runtimeFiles;
    if (runtime && !RuntimeAssembler::assemble(document, ns, runtimeFiles, error)) return false;
    files["src/contracts/blueprint.json"] = canonical + '\n';
    files["src/contracts/source-blueprint.json"] = sourceBlueprint;
    files["src/contracts/types.h"] = ("#pragma once\n#include <QVariantMap>\nnamespace " + ns
        + " {\n// User port names and types are metadata in blueprint.json; values use Qt variants.\nusing Inputs = QVariantMap;\nusing Outputs = QVariantMap;\n}\n").toUtf8();
    QJsonArray modules;
    QSet<QString> ids;
    QString smokeIncludes;
    for (const QJsonValue &value : ir.value("modules").toArray()) {
        const QJsonObject module = value.toObject();
        const QString id = module.value("id").toString();
        if (!safeId(id) || ids.contains(id.toCaseFolded())) return fail(error, "Module IDs must be unique portable single path segments");
        ids.insert(id.toCaseFolded()); modules.append(id);
        const QString type = module.value("type").toString();
        const QString name = "Module_" + sha256(id.toUtf8()).left(16);
        const QString base = type == "UiPage" ? "QWidget" : "QObject";
        QString header = "#pragma once\n#include <" + base + ">\n#include \"contracts/types.h\"\nnamespace " + ns + " {\n";
        if (runtime) header.replace("#include \"contracts/types.h\"", "#include <memory>\n#include \"contracts/types.h\"");
        if (type == "Decision") {
            header += "class " + name + " {\npublic:\n    virtual ~" + name + "() = default;\n    virtual bool decide(const Inputs &inputs) const = 0;\n};\n";
        } else {
            header += "class " + name + " : public " + base + " {\npublic:\n    explicit " + name + "(" + base + " *parent = nullptr) : " + base
                + "(parent) {}\n";
            if (!runtime) header += "    virtual Outputs execute(const Inputs &) { return {}; }\n";
            else if (type == "LogicModule") header += "    virtual Outputs execute(const Inputs &) = 0;\n";
            header += "};\n";
        }
        if (runtime) header += "std::unique_ptr<" + name + "> create" + name
            + (type == "UiPage" ? "(const Inputs &inputs, QWidget *parent = nullptr);\n" : "(QObject *parent = nullptr);\n");
        header += "}\n";
        const QString path = "src/modules/" + id + "/contract.h";
        files[path] = header.toUtf8();
        files["src/modules/" + id + "/implementation/placeholder.h"] = "#pragma once\n// Place node implementations beside this protected placeholder.\n";
        files["tests/" + id + "/placeholder.h"] = "#pragma once\n// Each node test .cpp/.cc is a standalone CTest executable.\n";
        smokeIncludes += "#include \"modules/" + id + "/contract.h\"\n";
    }
    files["src/main.cpp"] = "#include <QApplication>\n#include <QWidget>\nint main(int argc, char **argv) { QApplication app(argc, argv); QWidget window; window.resize(800, 600); window.show(); return app.exec(); }\n";
    files["tests/scaffold_smoke.cpp"] = (smokeIncludes + "int main() { return 0; }\n").toUtf8();
    files["CMakeLists.txt"] = R"(cmake_minimum_required(VERSION 3.22)
project(GeneratedBlueprint LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 REQUIRED COMPONENTS Widgets)
set(CMAKE_AUTOMOC ON)
file(GLOB_RECURSE module_sources CONFIGURE_DEPENDS src/modules/*.h src/modules/*.hpp src/modules/*.cpp src/modules/*.cc)
add_library(blueprint_modules STATIC ${module_sources} src/contracts/types.h)
set_target_properties(blueprint_modules PROPERTIES LINKER_LANGUAGE CXX)
target_include_directories(blueprint_modules PUBLIC src)
target_link_libraries(blueprint_modules PUBLIC Qt6::Widgets)
add_executable(generated_app src/main.cpp)
target_link_libraries(generated_app PRIVATE blueprint_modules)
include(CTest)
if(BUILD_TESTING)
  find_package(Qt6 REQUIRED COMPONENTS Test)
  add_executable(scaffold_smoke tests/scaffold_smoke.cpp)
  target_link_libraries(scaffold_smoke PRIVATE blueprint_modules)
  add_test(NAME scaffold_smoke COMMAND scaffold_smoke)
  file(GLOB_RECURSE node_tests CONFIGURE_DEPENDS tests/*/*.cpp tests/*/*.cc)
  foreach(test_source IN LISTS node_tests)
    file(RELATIVE_PATH relative_test "${CMAKE_CURRENT_SOURCE_DIR}" "${test_source}")
    string(SHA256 test_id "${relative_test}")
    add_executable(test_${test_id} "${test_source}")
    target_link_libraries(test_${test_id} PRIVATE blueprint_modules Qt6::Test)
    add_test(NAME test_${test_id} COMMAND test_${test_id})
    set_tests_properties(test_${test_id} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  endforeach()
endif()
)";
    files["README.md"] = "# Generated blueprint skeleton\n\nBuild with CMake and Qt 6 Widgets (Qt Test when BUILD_TESTING is enabled).\n\nContracts and metadata are deterministic. Node names and port types remain metadata; Inputs/Outputs use QVariantMap. Each protected contract exposes an extension interface; the application is a placeholder window and does not execute graph logic. Implementations must include the node contract and derive from its class. Callers must include the contract text when prompting the model.\n\nOnly src/modules/<node-id>/implementation/ and tests/<node-id>/ permit candidate files. Each test .cpp/.cc must provide its own main. Accepted implementation files are compiled on the next CMake configure. Candidate acceptance does not imply that code compiles or satisfies its contract.\n";
    if (runtime) {
        for (auto it = runtimeFiles.begin(); it != runtimeFiles.end(); ++it) files[it.key()] = it.value();
        files["CMakeLists.txt"].replace("add_executable(generated_app src/main.cpp)",
                                      "add_executable(generated_app src/main.cpp src/runtime/workflow_controller.cpp src/runtime/workflow_controller.h)");
        files["README.md"] = "# Generated blueprint runtime\n\nBuild generated_app with CMake and Qt 6 Widgets (Qt Test when BUILD_TESTING is enabled). A full executable build requires every declared factory; configuring, compiling blueprint_modules, or scaffold_smoke alone is insufficient.\n\nThe protected controller executes the Start-to-End linear flow once, binding only declared input keys from the direct predecessor with identical names and types. Logic factories return an implementation of the protected contract with a real execute override. Output keys, validity, and exact Qt metatypes are checked at startup. The optional terminal page factory consumes Inputs; copy any values retained by the page because the Inputs reference is valid only for the factory call. End marks assembly completion; the visible page remains owned across the Qt event loop. Logic-only flows exit after execution. Factories must return non-null unique_ptr values, with no QObject/QWidget parent ownership. The controller clears WA_DeleteOnClose before showing the owned page.\n\nOnly src/modules/<node-id>/implementation/ and tests/<node-id>/ permit candidate files. Define each exact factory in a .cpp/.cc implementation file, using the protected namespace and signature, with default arguments only in the protected declaration. Each test .cpp/.cc must provide its own main. Accepted files compile on the next CMake configure; acceptance does not prove compilation or behavior. Runtime workspaces use manifest version 2; use a new workspace for module-only version 1 projects.\n";
    }
    QJsonObject protectedFiles;
    for (auto it = files.begin(); it != files.end(); ++it) protectedFiles.insert(it.key(), sha256(it.value()));
    const QJsonObject manifest{{"version", runtime ? 2 : 1}, {"blueprintSha256", sha256(sourceBlueprint)}, {"modules", modules},
                               {"protectedFiles", protectedFiles}, {"files", QJsonObject{}}, {"batches", QJsonObject{}}};
    std::optional<QByteArray> existing;
    if (!read(workspace, "generation-manifest.json", existing, error)) return false;
    if (existing) {
        QJsonObject old;
        if (!loadManifest(workspace, old, error)) return false;
        if (old.value("version") != manifest.value("version"))
            return fail(error, "Workspace scaffold mode changed; use a new workspace");
        if (old.value("blueprintSha256") != manifest.value("blueprintSha256") || old.value("protectedFiles") != protectedFiles || old.value("modules") != modules)
            return fail(error, "Blueprint or scaffold version changed; use a new workspace");
        for (auto it = files.begin(); it != files.end(); ++it) {
            std::optional<QByteArray> current;
            if (!read(workspace, "generated-project/" + it.key(), current, error)) return false;
            if (!current || sha256(*current) != sha256(it.value())) return fail(error, "Scaffold file was manually changed or removed: " + it.key());
        }
        return true;
    }
    QMap<QString, QByteArray> writes;
    for (auto it = files.begin(); it != files.end(); ++it) {
        const QString path = "generated-project/" + it.key();
        std::optional<QByteArray> current;
        if (!read(workspace, path, current, error)) return false;
        if (current) return fail(error, "Refusing to overwrite an untracked project file: " + it.key());
        writes[path] = it.value();
    }
    writes["generation-manifest.json"] = json(manifest);
    return transaction(workspace, writes, error);
}
