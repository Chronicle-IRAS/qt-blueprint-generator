# 蓝图式 Qt 代码生成器最小实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 构建一个单人本地使用的桌面工具，让开发者通过节点和连线描述一个小型 Qt 应用，由系统结合蓝图语义与自然语言描述，按模块生成、编辑、验证并导出可编译的 Qt 6 Widgets/C++17/CMake 项目。

**Architecture:** 编辑器以蓝图 JSON 作为唯一主数据源，将节点、连线和文字说明编译为稳定的中间表示（IR），再按模块构造提示词并调用可替换的 AI 客户端。基础工程、构建文件和模块接口由确定性代码生成器产生，AI 只生成模块实现候选；用户修改和重新生成通过候选版本、差异预览及人工确认协调，不尝试把任意代码完整反向还原成蓝图。

**Tech Stack:** Qt 6 Widgets、C++17、CMake、QGraphicsView/QGraphicsScene、Qt Network、Qt Test、QJsonDocument、QProcess、Git。

---

## 1. 产品定位

最小版本是一个“蓝图驱动的 Qt 项目骨架与模块代码生成器”，不是完整的 UE Blueprint，也不是通用低代码平台。

目标用户是一名独立开发者。用户在本地完成以下闭环：

1. 新建蓝图项目。
2. 添加开始、结束、界面、逻辑、判断和外部代码节点。
3. 用有向连线表达执行顺序和依赖关系。
4. 为每个节点填写名称、职责、输入、输出和约束。
5. 运行结构校验并修复错误。
6. 将蓝图编译为结构化 IR。
7. 按模块生成 Qt/C++ 代码候选。
8. 在内置代码编辑器中查看或修改文件。
9. 对重新生成的候选代码进行差异确认。
10. 构建验证并导出完整 CMake 项目目录。

### 1.1 最小版本明确不做

- 不支持多人实时协同编辑。
- 不支持任意语言和任意框架，只生成 Qt 6 Widgets/C++17/CMake 项目。
- 不把自由形状和任意文字直接当成可执行语义。
- 不把任意外部代码自动恢复成完整蓝图。
- 不自动合并 AI 代码与用户代码冲突。
- 不自动执行生成的应用；构建必须由用户显式触发。
- 不训练或微调模型。
- 不实现插件市场、云项目、账号、权限和计费。
- 不在首版支持循环、并行节点、嵌套子图和 SCXML 全量语义。

## 2. 最小交互流程

```text
创建项目
   ↓
绘制蓝图并填写节点说明
   ↓
结构校验
   ↓
编译为规范化 IR
   ↓
生成基础工程与模块接口
   ↓
逐模块调用 AI 生成实现候选
   ↓
差异预览与人工接受
   ↓
CMake 构建验证
   ↓
导出项目目录
```

最小演示蓝图：

```text
开始 → 登录页面 → 用户验证 → 判断验证结果
                                  ├─ 成功 → 主页面 → 结束
                                  └─ 失败 → 错误提示 → 登录页面
```

首版为了保持有向无环图约束，不直接保存“错误提示 → 登录页面”的回边；演示时将“重试”建模为错误页面中的用户动作说明。循环控制作为后续扩展。

## 3. 蓝图语义

### 3.1 节点类型

| 类型 | 最小语义 | 代码生成规则 |
|---|---|---|
| `Start` | 唯一流程入口 | 不单独生成文件 |
| `End` | 流程出口 | 不单独生成文件 |
| `UiPage` | 一个 QWidget 页面或对话框 | 生成 `.h/.cpp` QWidget 子类 |
| `LogicModule` | 一个独立业务服务 | 生成 `.h/.cpp` QObject 子类 |
| `Decision` | 根据一个布尔结果选择分支 | 生成接口声明，由上游模块提供判断结果 |
| `ExternalCode` | 用户已有代码的只读黑盒 | 复制源文件并记录人工填写的接口契约 |

所有节点都允许自定义以下文字字段：

- `name`：稳定、简短的模块名称。
- `description`：模块职责和行为。
- `inputs`：输入名称、类型和含义。
- `outputs`：输出名称、类型和含义。
- `constraints`：不可违反的实现要求。
- `acceptanceCriteria`：生成代码需要满足的验收条件。

节点文字是补充语义，节点类型才决定生成规则。

### 3.2 连线语义

首版只有一种有向连线 `FlowEdge`，表示上游完成后进入下游，同时表示下游可以依赖上游公开接口。

Decision 节点必须恰好有两条出边，标签分别为 `true` 和 `false`。其他节点的出边不带条件标签。

### 3.3 最小校验规则

- 节点 ID 全局唯一且不可为空。
- 恰好存在一个 Start 节点。
- 至少存在一个 End 节点。
- Start 没有入边且恰好有一条出边。
- End 没有出边。
- 每个非 End 节点至少有一条出边。
- 除 Start 外，每个节点至少有一条入边。
- 从 Start 出发可以到达全部节点。
- 首版禁止有向环。
- Decision 恰好拥有 `true`、`false` 两条出边。
- `UiPage`、`LogicModule` 和 `ExternalCode` 的名称及说明不能为空。
- ExternalCode 至少关联一个存在的 `.h`、`.hpp`、`.cpp` 或 `.cc` 文件。

## 4. 项目文件和中间表示

编辑器工程目录：

```text
sample.blueprint-project/
├─ blueprint.json
├─ layout.json
├─ external/
├─ candidates/
├─ generated-project/
└─ generation-manifest.json
```

- `blueprint.json`：语义主数据，节点、文字和边。
- `layout.json`：节点坐标、尺寸、颜色及连线路径。
- `external/`：用户明确导入的外部源文件副本。
- `candidates/`：尚未接受的 AI 生成候选。
- `generated-project/`：当前已接受并可导出的 Qt 项目。
- `generation-manifest.json`：生成批次、模型、提示词哈希、文件哈希和节点归属。

### 4.1 蓝图 JSON 示例

```json
{
  "schemaVersion": 1,
  "projectId": "login-demo",
  "projectName": "LoginDemo",
  "target": "qt6-widgets-cpp17-cmake",
  "nodes": [
    {
      "id": "start",
      "type": "Start",
      "name": "开始",
      "description": "应用启动",
      "inputs": [],
      "outputs": [],
      "constraints": [],
      "acceptanceCriteria": []
    },
    {
      "id": "login_page",
      "type": "UiPage",
      "name": "LoginPage",
      "description": "输入用户名和密码并提交登录请求",
      "inputs": [],
      "outputs": [
        { "name": "credentials", "type": "Credentials", "description": "登录凭据" }
      ],
      "constraints": ["密码输入框必须隐藏明文"],
      "acceptanceCriteria": ["空用户名时不得提交"]
    },
    {
      "id": "end",
      "type": "End",
      "name": "结束",
      "description": "流程完成",
      "inputs": [],
      "outputs": [],
      "constraints": [],
      "acceptanceCriteria": []
    }
  ],
  "edges": [
    { "id": "edge_1", "source": "start", "target": "login_page", "label": "" },
    { "id": "edge_2", "source": "login_page", "target": "end", "label": "" }
  ]
}
```

### 4.2 IR 规则

IR 不是第二份可编辑数据，而是从 `blueprint.json` 确定性编译得到的只读结果。IR 必须：

- 按节点 ID 排序，保证同一蓝图产生稳定输出。
- 包含当前模块的直接上游、直接下游和公开接口。
- 排除布局、颜色等与代码无关的信息。
- 包含项目级命名空间、目标平台和编码规范。
- 为每个可生成节点计算独立上下文，避免把完整项目反复发送给模型。

## 5. AI 生成边界

### 5.1 AI 输入

提示词编译器为每个可生成节点构造以下内容：

1. 固定系统约束：Qt 6、C++17、CMake、禁止未声明依赖。
2. 项目摘要：项目名称、命名空间和总体目标。
3. 当前节点 IR：职责、输入、输出、约束和验收标准。
4. 上下游接口：只提供公开契约，不提供无关实现。
5. 输出契约：要求返回严格 JSON，不使用 Markdown 代码围栏。

### 5.2 AI 输出契约

```json
{
  "nodeId": "login_page",
  "summary": "实现登录页面",
  "files": [
    {
      "path": "src/modules/login_page/LoginPage.h",
      "content": "生成的头文件内容"
    },
    {
      "path": "src/modules/login_page/LoginPage.cpp",
      "content": "生成的实现文件内容"
    },
    {
      "path": "tests/tst_login_page.cpp",
      "content": "生成的 Qt Test 内容"
    }
  ]
}
```

系统必须拒绝以下响应：

- `nodeId` 与请求节点不一致。
- 文件路径为绝对路径。
- 文件路径包含 `..`。
- 文件写出范围超出候选目录。
- 文件扩展名不在允许列表中。
- JSON 缺少必需字段。
- 单文件或总响应超过配置的大小限制。

### 5.3 AI 客户端

最小版本定义 `IAiClient` 接口，并提供两个实现：

- `FakeAiClient`：测试和离线演示使用，返回固定响应。
- `OpenAiCompatibleClient`：调用用户配置的 OpenAI-compatible HTTPS 接口。

API 密钥只从 `BLUEPRINT_AI_API_KEY` 环境变量读取，不写入工程文件或日志。接口地址和模型名称存入本地设置，但导出项目时不携带。

## 6. 生成代码、人工修改和重新生成

### 6.1 唯一主数据源

蓝图是架构和模块契约的主数据源；代码不是蓝图的完整反向来源。用户修改代码后，系统只记录文件变化，不尝试推断并修改蓝图结构。

### 6.2 确定性生成内容

以下文件由本地模板生成，不交给 AI：

- 根 `CMakeLists.txt`
- `src/main.cpp`
- 公共接口和类型声明
- 模块目录结构
- 测试入口和 CTest 配置
- `README.md`

这样即使 AI 不可用，系统也能导出完整项目骨架。

### 6.3 候选版本

AI 结果首先写入：

```text
candidates/<generation-id>/<node-id>/
```

用户可以逐文件查看当前版本与候选版本的差异，并执行：

- 接受该文件。
- 拒绝该文件。
- 在候选代码上修改后接受。
- 保留当前文件并取消生成。

首版不做三方自动合并。只要当前文件哈希与上次生成记录不同，就视为存在人工修改，默认禁止无提示覆盖。

### 6.4 外部代码

外部代码导入采用黑盒策略：

- 只接受 C/C++ 头文件和源文件。
- 复制到项目 `external/<node-id>/`。
- 用户手动填写该模块的输入、输出和接口说明。
- 系统可展示文件内容，但 AI 默认只接收接口说明。
- 导入文件不参与重新生成。
- 删除或替换外部文件必须再次通过蓝图校验。

## 7. 导出项目结构

```text
LoginDemo/
├─ CMakeLists.txt
├─ README.md
├─ blueprint.json
├─ generation-manifest.json
├─ src/
│  ├─ main.cpp
│  ├─ MainWindow.h
│  ├─ MainWindow.cpp
│  ├─ workflow/
│  │  ├─ WorkflowController.h
│  │  └─ WorkflowController.cpp
│  ├─ modules/
│  │  └─ <node-id>/
│  └─ external/
└─ tests/
   ├─ CMakeLists.txt
   └─ tst_<node-id>.cpp
```

“导出”是复制到用户选择的空目录，不做 ZIP 打包。若目标目录非空，程序必须停止并提示用户选择新目录，避免覆盖现有项目。

## 8. 编辑器自身建议目录

```text
project/
├─ AGENTS.md
├─ CMakeLists.txt
├─ doc/
│  └─ mvp-implementation-plan.md
├─ src/
│  ├─ main.cpp
│  ├─ app/
│  │  ├─ main_window.h
│  │  └─ main_window.cpp
│  ├─ blueprint/
│  │  ├─ blueprint_document.h
│  │  ├─ blueprint_document.cpp
│  │  ├─ blueprint_serializer.h
│  │  ├─ blueprint_serializer.cpp
│  │  ├─ blueprint_validator.h
│  │  └─ blueprint_validator.cpp
│  ├─ editor/
│  │  ├─ blueprint_scene.h
│  │  ├─ blueprint_scene.cpp
│  │  ├─ node_item.h
│  │  ├─ node_item.cpp
│  │  ├─ edge_item.h
│  │  └─ edge_item.cpp
│  ├─ generation/
│  │  ├─ ir_compiler.h
│  │  ├─ ir_compiler.cpp
│  │  ├─ prompt_compiler.h
│  │  ├─ prompt_compiler.cpp
│  │  ├─ project_scaffolder.h
│  │  ├─ project_scaffolder.cpp
│  │  ├─ generation_service.h
│  │  └─ generation_service.cpp
│  ├─ ai/
│  │  ├─ ai_client.h
│  │  ├─ fake_ai_client.h
│  │  ├─ fake_ai_client.cpp
│  │  ├─ openai_compatible_client.h
│  │  └─ openai_compatible_client.cpp
│  └─ workspace/
│     ├─ external_code_importer.h
│     ├─ external_code_importer.cpp
│     ├─ build_service.h
│     ├─ build_service.cpp
│     ├─ project_exporter.h
│     └─ project_exporter.cpp
└─ tests/
   ├─ CMakeLists.txt
   ├─ tst_blueprint_document.cpp
   ├─ tst_blueprint_validator.cpp
   ├─ tst_ir_compiler.cpp
   ├─ tst_prompt_compiler.cpp
   ├─ tst_generation_service.cpp
   ├─ tst_external_code_importer.cpp
   └─ tst_project_exporter.cpp
```

## 9. 实施任务

每项任务完成后都必须运行相关测试、单独提交，并将当前开发分支同步到对应的远端仓库。同步不得使用强制推送或改写远端历史；若同步受阻，必须记录 Issue 并在实现进度中说明。下面的命令默认在仓库根目录执行。

### Task 1：建立可构建、可测试的 Qt 工程

**Files:**
- Create: `CMakeLists.txt`
- Create: `src/main.cpp`
- Create: `src/app/main_window.h`
- Create: `src/app/main_window.cpp`
- Create: `tests/CMakeLists.txt`
- Create: `tests/tst_smoke.cpp`

- [ ] 创建只包含空主窗口的 Qt 6 Widgets 应用。
- [ ] 创建 `tst_smoke.cpp`，验证 `MainWindow` 可以构造且窗口标题非空。
- [ ] 运行 `cmake -S . -B build -DBUILD_TESTING=ON`，预期配置成功。
- [ ] 运行 `cmake --build build --config Debug`，预期构建成功。
- [ ] 运行 `ctest --test-dir build -C Debug --output-on-failure`，预期全部通过。
- [ ] 提交 `chore: scaffold Qt blueprint editor`。

### Task 2：实现蓝图领域模型与 JSON 往返

**Files:**
- Create: `src/blueprint/blueprint_document.h`
- Create: `src/blueprint/blueprint_document.cpp`
- Create: `src/blueprint/blueprint_serializer.h`
- Create: `src/blueprint/blueprint_serializer.cpp`
- Create: `tests/tst_blueprint_document.cpp`

- [ ] 先编写测试，覆盖六种节点、FlowEdge、自定义文字和 JSON 往返。
- [ ] 运行 `ctest --test-dir build -C Debug -R blueprint_document --output-on-failure`，确认测试因实现缺失而失败。
- [ ] 实现 `NodeType`、`PortSpec`、`BlueprintNode`、`BlueprintEdge` 和 `BlueprintDocument`。
- [ ] 实现 `BlueprintSerializer::toJson()` 与 `BlueprintSerializer::fromJson()`。
- [ ] 再次运行相关测试，预期全部通过。
- [ ] 提交 `feat: add blueprint model and serialization`。

### Task 3：实现蓝图静态验证

**Files:**
- Create: `src/blueprint/blueprint_validator.h`
- Create: `src/blueprint/blueprint_validator.cpp`
- Create: `tests/tst_blueprint_validator.cpp`

- [ ] 先编写测试，覆盖重复 ID、缺少 Start、不可达节点、有向环和 Decision 错误分支。
- [ ] 运行 `ctest --test-dir build -C Debug -R blueprint_validator --output-on-failure`，确认失败。
- [ ] 使用哈希表检查引用和 ID，使用 DFS/BFS 检查可达性，使用三色 DFS 检测有向环。
- [ ] 返回带 `code`、`nodeId`、`edgeId` 和 `message` 的结构化诊断。
- [ ] 运行相关测试，预期全部通过。
- [ ] 提交 `feat: validate blueprint structure`。

### Task 4：实现节点画布

**Files:**
- Create: `src/editor/blueprint_scene.h`
- Create: `src/editor/blueprint_scene.cpp`
- Create: `src/editor/node_item.h`
- Create: `src/editor/node_item.cpp`
- Create: `src/editor/edge_item.h`
- Create: `src/editor/edge_item.cpp`
- Modify: `src/app/main_window.h`
- Modify: `src/app/main_window.cpp`
- Create: `tests/tst_blueprint_scene.cpp`

- [ ] 先编写测试，验证新增节点、移动节点、连接节点和删除节点会同步修改 BlueprintDocument。
- [ ] 运行相关测试，确认失败。
- [ ] 使用 QGraphicsScene/QGraphicsView 实现节点、端口、连线、缩放、框选和属性编辑。
- [ ] 使用 QUndoStack 包装新增、删除、移动、连线和文字修改操作。
- [ ] 运行相关测试和全量测试，预期全部通过。
- [ ] 提交 `feat: add interactive blueprint canvas`。

### Task 5：实现 IR 与提示词编译

**Files:**
- Create: `src/generation/ir_compiler.h`
- Create: `src/generation/ir_compiler.cpp`
- Create: `src/generation/prompt_compiler.h`
- Create: `src/generation/prompt_compiler.cpp`
- Create: `tests/tst_ir_compiler.cpp`
- Create: `tests/tst_prompt_compiler.cpp`

- [ ] 先编写测试，证明不同布局产生相同 IR、节点排序稳定、当前模块只包含必要邻接上下文。
- [ ] 先编写测试，证明提示词包含模块契约、Qt版本、禁止依赖和严格JSON输出要求。
- [ ] 运行相关测试，确认失败。
- [ ] 实现稳定排序和规范化 JSON 输出。
- [ ] 实现项目级提示词与模块级提示词模板。
- [ ] 运行相关测试和全量测试，预期全部通过。
- [ ] 提交 `feat: compile blueprints into generation prompts`。

### Task 6：实现 AI 客户端与安全响应解析

**Files:**
- Create: `src/ai/ai_client.h`
- Create: `src/ai/fake_ai_client.h`
- Create: `src/ai/fake_ai_client.cpp`
- Create: `src/ai/openai_compatible_client.h`
- Create: `src/ai/openai_compatible_client.cpp`
- Create: `src/generation/generation_service.h`
- Create: `src/generation/generation_service.cpp`
- Create: `tests/tst_generation_service.cpp`

- [ ] 先编写测试，覆盖正常响应、错误 nodeId、绝对路径、路径穿越、非法扩展名和超大响应。
- [ ] 运行相关测试，确认失败。
- [ ] 定义异步 `IAiClient` 接口，使测试不依赖真实网络。
- [ ] 实现 FakeAiClient 和基于 QNetworkAccessManager 的 OpenAI-compatible 客户端。
- [ ] 实现严格 JSON 校验和候选目录边界检查。
- [ ] 运行相关测试和全量测试，预期全部通过。
- [ ] 提交 `feat: add safe modular AI generation`。

### Task 7：实现确定性工程骨架与候选代码流程

**Files:**
- Create: `src/generation/project_scaffolder.h`
- Create: `src/generation/project_scaffolder.cpp`
- Modify: `src/generation/generation_service.h`
- Modify: `src/generation/generation_service.cpp`
- Create: `tests/tst_project_scaffolder.cpp`

- [ ] 先编写测试，验证生成工程包含 CMakeLists、main.cpp、模块目录、测试目录和 Manifest。
- [ ] 先编写测试，验证存在人工修改时不会静默覆盖当前文件。
- [ ] 运行相关测试，确认失败。
- [ ] 实现确定性模板生成、文件哈希、候选版本和逐文件接受操作。
- [ ] 运行相关测试和全量测试，预期全部通过。
- [ ] 提交 `feat: scaffold projects and manage candidates`。

### Task 8：实现外部代码黑盒导入

**Files:**
- Create: `src/workspace/external_code_importer.h`
- Create: `src/workspace/external_code_importer.cpp`
- Create: `tests/tst_external_code_importer.cpp`

- [ ] 先编写测试，覆盖允许扩展名、拒绝目录穿越、复制文件和保持只读生成策略。
- [ ] 运行相关测试，确认失败。
- [ ] 实现外部文件复制、SHA-256记录和节点接口说明绑定。
- [ ] 确保外部文件永不进入 AI 候选覆盖列表。
- [ ] 运行相关测试和全量测试，预期全部通过。
- [ ] 提交 `feat: import external code as black-box modules`。

### Task 9：实现构建验证与项目导出

**Files:**
- Create: `src/workspace/build_service.h`
- Create: `src/workspace/build_service.cpp`
- Create: `src/workspace/project_exporter.h`
- Create: `src/workspace/project_exporter.cpp`
- Create: `tests/tst_project_exporter.cpp`
- Modify: `src/app/main_window.h`
- Modify: `src/app/main_window.cpp`

- [ ] 先编写测试，验证非空目标目录被拒绝，空目录导出后文件哈希保持一致。
- [ ] 运行相关测试，确认失败。
- [ ] 使用 QProcess 显式运行 CMake 配置与构建，并将 stdout、stderr 和退出码显示在构建面板。
- [ ] 实现到空目录的原子化导出；失败时删除未完成的临时副本，不修改目标目录。
- [ ] 运行相关测试和全量测试，预期全部通过。
- [ ] 提交 `feat: build and export generated Qt projects`。

### Task 10：端到端验收

**Files:**
- Create: `tests/tst_end_to_end.cpp`
- Create: `tests/fixtures/login-demo/blueprint.json`
- Create: `tests/fixtures/login-demo/fake-ai-response.json`
- Modify: `README.md`

- [ ] 使用 FakeAiClient 构造登录示例蓝图。
- [ ] 验证“加载 → 校验 → IR → 生成 → 接受 → 导出”的完整流程。
- [ ] 验证导出的项目可以通过 CMake 配置、构建和 Qt Test。
- [ ] 运行 `ctest --test-dir build -C Debug --output-on-failure`，预期所有测试通过。
- [ ] 手动验证节点拖拽、连线、属性修改、候选差异和构建日志。
- [ ] 更新 README，写明安装、环境变量、演示步骤、限制和安全边界。
- [ ] 提交 `test: verify end-to-end blueprint generation`。

## 10. MVP 验收标准

满足以下全部条件才算最小版本完成：

- 能创建六类节点并编辑文字字段。
- 能创建、删除和显示有向连线。
- 蓝图可保存并无损重新加载。
- 能发现最小校验规则中的全部结构错误。
- 相同蓝图在不同布局下产生相同 IR。
- FakeAiClient 能完成离线端到端演示。
- 真实 AI 客户端失败时不会破坏当前工程。
- 每个可生成节点拥有独立候选代码目录。
- 人工修改的文件不会被静默覆盖。
- 外部代码可以作为黑盒节点导入并随项目导出。
- 可以显示 CMake 配置和构建日志。
- 可以将完整项目导出到空目录。
- 编辑器自身全部 Qt Test 和端到端测试通过。
- 导出的示例项目能够配置、编译并通过测试。

## 11. 评估指标

论文实验至少记录：

- 蓝图节点数与加载、保存、校验耗时。
- IR 稳定性：相同语义不同布局的哈希一致率。
- 生成代码首次编译通过率。
- 加入构建错误反馈后再次生成的编译通过率。
- 单模块重新生成时未相关文件的零变更率。
- 路径穿越、非法扩展名和覆盖保护测试通过率。
- 使用蓝图生成与手工搭建同类 Qt 项目所需时间对比。

建议使用 5 个规模递增的示例项目：登录窗口、待办事项、文件搜索、记账工具和多页面设置程序。所有示例使用同一目标技术栈，避免评估被框架差异干扰。

## 12. 后续版本候选

只有 MVP 全部通过后再考虑：

- 有向环和循环流程。
- 子蓝图和复合模块。
- 多种 Qt UI 技术栈。
- 基于 AST/LSP 的外部代码接口发现。
- 三方合并与受保护用户代码区域。
- Git 可视化版本管理。
- 多模型与本地模型。
- SCXML 导入导出。
- 多人异步评审；仍不优先考虑实时协同编辑。

## 13. 自检结论

- 需求覆盖：蓝图绘制、文字自定义、AI上下文构造、模块化生成、单模块修改、外部代码导入和完整项目导出均有对应实现任务。
- 范围控制：首版限定单人、单机、单一 Qt 技术栈、DAG 和黑盒外部代码，避免任意代码双向同步。
- 测试策略：所有核心模块都有独立 Qt Test，AI 使用 FakeAiClient 实现确定性测试，最终包含可构建导出项目的端到端验证。
- 提交策略：每项任务测试通过后创建独立 Git commit，符合仓库 `AGENTS.md` 约束。
