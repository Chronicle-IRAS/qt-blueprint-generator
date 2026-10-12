# 蓝图 Schema v2 设计（Draft）

状态：Draft，2026-10-12。P0.1 已增加唯一权威结构定义 [JSON Schema](../../schemas/blueprint-v2.schema.json)（Draft 2020-12）、[离线 fixtures](../../tests/fixtures/blueprint-v2/README.md) 与独立 TS/Ajv/Vitest 工具；运行见 [schemas/README](../../schemas/README.md)。尚无应用 v2 读取器，协议未冻结。
本页 JSON 示例符合本页描述的提案，不是当前程序可加载的文件，也不是已冻结协议。
总览见[插件平台架构](plugin-platform.md)，节点定义与目标编译边界见[Plugin API v1](plugin-api-v1.md)。

## 与现有版本的边界

当前 [BlueprintDocument](../../src/blueprint/blueprint_document.h) 的 `CurrentSchemaVersion` 为 1，NodeType 固定为 Start、End、UiPage、LogicModule、Decision、ExternalCode。
[Serializer](../../src/blueprint/blueprint_serializer.cpp) 只接受版本 1；现有节点 `type`、工程 `target` 与端口 name/type 字段不改名、不接受本文新模型。
[ProjectStore](../../src/blueprint/blueprint_project_store.cpp) 已将 `blueprint.json` 语义与 `layout.json` 坐标分开，[IrCompiler](../../src/generation/ir_compiler.cpp) 只编译语义。
这些事实与未来 `schemaVersion: 2` 是两个层次，不能用 generation manifest v2 推断蓝图已升级。

v2 继续把布局独立保存；位置、缩放、选中项不进入语义 IR 或生成摘要。
未来布局格式和原子保存流程需另行验证，不借用 schemaVersion 2 静默改写现有 layout Schema 1。
旧读取器应拒绝未知版本，未来读取器也不能把未知新版本降级保存为已知版本。

Draft 编译流程为 `v2 文档 → 可信平台 CoreGraphIR → 按 unitId 投影 UnitCoreIR → TargetAdapter.lower → TargetIR`。
CoreGraphIR 规范化稳定节点/端口、显式边、Unit、Channel、pluginLocks 与驱动引用等目标无关语义，不含布局、猜测的目标执行顺序或 AI 实现类名。
其 `coreIrVersion: 1.0-draft` 与 `coreIrSha256` 是独立 Draft 契约，不改当前 IrCompiler 输出，不等同于蓝图 Schema 或目标 targetIrVersion。
UnitCoreIR 的 `unitIrSha256` 绑定指定 Unit 的规范投影：本 Unit 节点/内部边、相关锁/驱动引用和相邻 Channel 边界，不包含其他 Unit 的节点实现。
投影由可信平台建立，目标适配器只 lower 该投影到版本化 TargetIR，不能修改全局图/Unit 边界；类型接口及摘要约定见[Plugin API](plugin-api-v1.md)。

## 顶层和版本字段

| 字段 | 提案类型 | 约束 |
| --- | --- | --- |
| `schemaVersion` | integer | 本 Draft 为 2；未知值明确拒绝 |
| `projectId` / `projectName` | string | 身份与显示名分开；ID 不随改名变化 |
| `pluginLocks` | PluginLock[] | 生成使用精确包/版本/哈希，不能临时取最新版 |
| `units` | BuildUnit[] | ID 唯一；每个节点引用一个存在的 Unit |
| `nodes` | Node[] | 唯一节点 ID、带命名空间的 typeId、定义版本与端口快照 |
| `edges` | Edge[] | 只允许同一 Unit 内、显式端口到端口 |
| `channels` | Channel[] | 显式跨 Unit/Target 契约；无通信实现承诺 |
| `driverBindings` | DriverBinding[] | Snapshot/Reference 提案；不改现有 import manifest |
| `extensions` | object，可选 | 带命名空间的扩展原始数据；宿主不能据此授予权限 |

PluginLock 字段为 `pluginId`、`package`、`version`、`sha256`、`pluginApiVersion`。
`sha256` 是包字节摘要，以 64 个小写十六进制字符表示（256 bit），示例哈希仅是占位值；包来源与信任另行校验。
`version` pin 插件包版本，不代表节点定义、API 或目标 IR 版本。
Node 的 `nodeDefinitionVersion` 单独标记定义语义；API 使用 Draft `1.0-draft`，蓝图使用整数 2。
workspaceManifestVersion 与这些版本独立，当前 generation manifest 的 `version: 1/2` 仍按旧契约解析。

机器 Schema 要求上述顶层字段（仅 extensions 可选）；所有数组可空，ID/版本非空，projectName/Port.name 允许空显示名。typeId/targetId 检查 `pluginId/localType` 命名空间并允许类型的 `@version` 后缀，不枚举 Qt/GPIO 业务节点或当前九种 Qt 类型。数组为空或结构通过不代表可生成。
所有记录允许未知额外字段，已知字段仍须符合 required/type 等约束。properties/settings/configuration/interfaceContract/wireSchema.schema 是任意 Json（含 null、标量、数组），Core 通常使用 `{}`；插件与未来 wire checker 才负责各自 payload 的含义和子 Schema 合法性。

## Unit、节点和端口

| 记录 | 关键字段 | 约束 |
| --- | --- | --- |
| BuildUnit | `id/targetId/pluginId/settings` | pluginId 是目标提供者，id 是本工程身份；一个插件可对应多 Unit |
| Node | `id/unitId/typeId/nodeDefinitionVersion/properties/ports` | typeId 如 `org.blueprint.qt/logic-module`；properties 由定义的 propertiesSchema 校验 |
| Port | `id/name/direction/kind/typeId` | 稳定 id、`in/out` 方向、`control/data/event` 种类、显式类型 |
| Edge | `id/kind/source/target` | source/target 为 `nodeId/portId`，kind 必须匹配两端 |

`targetId` 如 `org.blueprint.qt/widgets`，不能由 pluginId、UI 语言或包名隐式推导。
节点插件可以不同于 Unit 目标提供者，但必须存在该目标对该节点的显式编译支持。
`ports` 是已持久化实例快照；getPorts(properties, previousPorts, context) 接收该快照与宿主的 PortResolutionContext，返回待复验提案，不自行分配实例 ID。
动态属性条目持久化宿主管理的 itemId/portId（由对应 propertiesSchema 声明）；宿主只在显式新增时一次分配并提供 context.itemPortIds 的 itemId→portId 映射，不靠显示名/数组位置对账。
普通属性 edit、重排、rename 与同版本 reload 复用原 ID。connected port 删除或类型/方向/kind 变化默认拒绝；仅经用户明确批准受影响 Edge/Channel 的删除或重连计划、全图复验后，才原子提交 properties/ports/edges/channels；失败/取消保留原状态，不静默丢边或改绑。
定义升级需显式版本迁移及旧 itemId/portId 到新定义的无歧义映射，尽量保留 ID；缺映射/歧义/复验失败保留旧 node/ports/edges/channels。reload 不自动升级，也不按名称猜配。
control 也有类型，如宿主定义的 `org.blueprint.core/control-flow@1`；data 示例用 `org.blueprint.qt/QString@1`。
event 使用注册的明确事件类型，不能用空类型、任意 JSON 或默认 QVariant 代替契约。
端口 ID 在节点内唯一；连线引用 ID，不引用显示名或数组索引。
输出到输入、同种类、显式兼容类型、基数与目标语义全部通过才允许普通边；不存在隐式转换。

## 完整蓝图示例（未来 Draft 模型）

下面使用 Qt 目标的示意节点定义：start/end、logic-module 与 ui-page；Logic 输出 message，页面接收同名类型。
它有三条 control 边和一条 data 边，属于同一个 Unit。示意定义不是已安装的 Qt 插件。

```json
{
  "schemaVersion": 2,
  "projectId": "plugin-demo",
  "projectName": "Plugin Demo",
  "pluginLocks": [{
    "pluginId": "org.blueprint.qt", "package": "blueprint-qt-target",
    "version": "0.1.0-draft.1", "pluginApiVersion": "1.0-draft",
    "sha256": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
  }],
  "units": [{
    "id": "desktop-main", "targetId": "org.blueprint.qt/widgets",
    "pluginId": "org.blueprint.qt", "settings": {}
  }],
  "nodes": [
    {
      "id": "start", "unitId": "desktop-main", "typeId": "org.blueprint.qt/start",
      "nodeDefinitionVersion": "1.0.0-draft.1", "properties": {"name": "Start"},
      "ports": [{"id": "flow-out", "name": "Flow", "direction": "out",
        "kind": "control", "typeId": "org.blueprint.core/control-flow@1"}]
    },
    {
      "id": "logic", "unitId": "desktop-main", "typeId": "org.blueprint.qt/logic-module",
      "nodeDefinitionVersion": "1.0.0-draft.1",
      "properties": {"name": "Compute", "description": "produce message"},
      "ports": [
        {"id": "flow-in", "name": "Flow", "direction": "in",
          "kind": "control", "typeId": "org.blueprint.core/control-flow@1"},
        {"id": "flow-out", "name": "Flow", "direction": "out",
          "kind": "control", "typeId": "org.blueprint.core/control-flow@1"},
        {"id": "message-out", "name": "message", "direction": "out",
          "kind": "data", "typeId": "org.blueprint.qt/QString@1"}
      ]
    },
    {
      "id": "page", "unitId": "desktop-main", "typeId": "org.blueprint.qt/ui-page",
      "nodeDefinitionVersion": "1.0.0-draft.1",
      "properties": {"name": "Result", "description": "display message"},
      "ports": [
        {"id": "flow-in", "name": "Flow", "direction": "in",
          "kind": "control", "typeId": "org.blueprint.core/control-flow@1"},
        {"id": "flow-out", "name": "Flow", "direction": "out",
          "kind": "control", "typeId": "org.blueprint.core/control-flow@1"},
        {"id": "message-in", "name": "message", "direction": "in",
          "kind": "data", "typeId": "org.blueprint.qt/QString@1"}
      ]
    },
    {
      "id": "end", "unitId": "desktop-main", "typeId": "org.blueprint.qt/end",
      "nodeDefinitionVersion": "1.0.0-draft.1", "properties": {"name": "End"},
      "ports": [{"id": "flow-in", "name": "Flow", "direction": "in",
        "kind": "control", "typeId": "org.blueprint.core/control-flow@1"}]
    }
  ],
  "edges": [
    {"id": "flow-1", "kind": "control", "source": {"nodeId": "start", "portId": "flow-out"},
      "target": {"nodeId": "logic", "portId": "flow-in"}},
    {"id": "flow-2", "kind": "control", "source": {"nodeId": "logic", "portId": "flow-out"},
      "target": {"nodeId": "page", "portId": "flow-in"}},
    {"id": "flow-3", "kind": "control", "source": {"nodeId": "page", "portId": "flow-out"},
      "target": {"nodeId": "end", "portId": "flow-in"}},
    {"id": "data-1", "kind": "data", "source": {"nodeId": "logic", "portId": "message-out"},
      "target": {"nodeId": "page", "portId": "message-in"}}
  ],
  "channels": [], "driverBindings": [], "extensions": {}
}
```

## Channel 与驱动提案

普通边的两个节点 unitId 必须相等，即使两个 Unit 都使用 `org.blueprint.qt/widgets` 也不能跨接。
跨 Unit/Target 通过 Channel 的 `source/target` 明确指定 `unitId/nodeId/portId`。
Channel 还要求 `wireSchema`、`bridge`；bridge 必需字段为 `pluginId/version/configuration`，可选 `endpointDriverBindingIds` 按 `source/target` 分组为 binding ID 数组。
Bridge 包须在 pluginLocks 中锁定，bridge.version 绑定该 Bridge 实现的适配器版本，端点目标类型与 wireSchema 的编码/解码映射须显式支持。
wireSchema 是传输数据契约，不是 Qt/C++ 内存类型；Bridge 缺失、映射不兼容或必需端点依赖缺失则禁止生成。
无导入驱动的 Qt-Qt IPC/WebSocket 等 Bridge 可省略 endpointDriverBindingIds、用空对象或每侧空数组；锁定 Bridge 版本的契约逐侧声明必需驱动及允许接口，不能因可选字段就放过必需依赖缺失。
提供的 binding ID 逐侧核对存在/去重、所属 Unit 等于该侧 endpoint.unitId、接口兼容与哈希有效；不能把另一 Unit 的驱动错绑到此侧，不强制两端共用一个全局 driver ID。

以下两个独立片段都是未来 Draft Channel 示例，不是上述单 Unit 蓝图的附加项或当前可加载文件，均不表示通信已实现。
无驱动片段假设另一完整蓝图已有 Qt Unit desktop-main/desktop-peer、相应节点/端口及 IPC Bridge 锁，driverBindings 可为空：

```json
{
  "id": "ipc-message-channel",
  "source": {"unitId": "desktop-main", "nodeId": "logic", "portId": "message-out"},
  "target": {"unitId": "desktop-peer", "nodeId": "page", "portId": "message-in"},
  "wireSchema": {"typeId": "org.blueprint.wire/message@1",
    "schema": {"type": "object", "required": ["message"],
      "properties": {"message": {"type": "string"}}, "additionalProperties": false}},
  "bridge": {"pluginId": "org.blueprint.bridge.ipc", "version": "0.1.0-draft.1",
    "configuration": {"codec": "utf8-json"}}
}
```

有驱动片段另假设 device-main/receiver 与 serial Bridge 锁已定义；host-serial 绑定属于 desktop-main，device-uart 绑定属于 device-main，分别满足该 Bridge 两侧的驱动策略：

```json
{
  "id": "message-channel",
  "source": {"unitId": "desktop-main", "nodeId": "logic", "portId": "message-out"},
  "target": {"unitId": "device-main", "nodeId": "receiver", "portId": "message-in"},
  "wireSchema": {
    "typeId": "org.blueprint.wire/message@1",
    "schema": {"type": "object", "required": ["message"],
      "properties": {"message": {"type": "string"}}, "additionalProperties": false}
  },
  "bridge": {
    "pluginId": "org.blueprint.bridge.serial", "version": "0.1.0-draft.1",
    "endpointDriverBindingIds": {"source": ["host-serial"], "target": ["device-uart"]},
    "configuration": {"codec": "utf8-json", "baudRate": 115200}
  }
}
```

DriverBinding 的 P0.1 结构为 `id/unitId/mode/interfaceContract/files`，mode 拼写为 `Snapshot/Reference`，files 是 `{path, sha256}` 记录数组，可空；此数组与现有 importer 的 fileMap 格式不同，不宣称导入清单兼容。Schema 仅检查路径字符串和摘要格式，不访问或验证物理来源。
Reference 的来源定位、controlled root 与可移植导出策略尚未确定，不发明必需 sourceRoot/absolutePath/locator；未知扩展可携带定位数据，但当前工具不读取它。
Snapshot 拟复用当前 [ExternalCodeImporter](../../src/workspace/external_code_importer.h) 的显式选择、复制与复验；现有五字段 import manifest 不加字段。
Reference 是未来方案。当前 Importer 不是插件驱动绑定系统，Runtime Phase 1 不执行 ExternalCode。
AI 只接收 interfaceContract，不接收用户驱动源码；驱动变化使生成/Accept 上下文失效。

## 未知节点、锁与保存

未来 v2 读取器先验证安全的文档容器、ID 和引用；缺失插件/定义时显示 UnknownNode 占位与诊断。
未识别节点的未知字段、properties、ports 及命名空间扩展须无损保留，其中 opaque 插件 payload 的原始字节单独保留。
未改动的 opaque payload 不得因规范化、改名或保存被删除、重写或丢失；布局也保留。
该要求针对 payload 字节与未知字段，不要求重新保存后的整个 JSON 文件字节一致；显式复制保留的原工程不改写。
已知语义的 canonical 摘要与 opaque 原始字节摘要分开处理，不能宣称任意未知数据可语义归一化。
允许只读查看或保留数据的保存，不允许生成；缺失插件、锁哈希不符、定义/API 不兼容均失败关闭。
只有显式选择并诊断的升级/替换才能改变锁或定义版本，不能边打开边自动升级插件。
当前 v1 读取器没有 UnknownNode/opaque 保留能力；遇到 Schema 2 或未知 NodeType 会拒绝。

## LegacyQt 兼容与元数据分离

v1 → v2 不做自动保存转换。未来显式复制工具保留原文件、layout、Flow/label 和未知原始字段，给出转换诊断。
v1 没有端口级 data edge，工具不能根据画布位置、节点顺序猜造；无法表达的关系保留 legacy 路径或等待明确建模选择。
v2 → LegacyQt 适配仅接受单个 Qt Unit、当前 Start→Logic→可选末端 UI→End 线性语义及可精确映射的类型。
control endpoint 只按已知定义映射到旧 Flow；v2 控制端口不能写成旧 Start/End 的 PortSpec。
data 边必须恰好对应直接前驱同名、同声明类型输入绑定，不能重命名、跳过前驱、加入转换或猜隐含关系。
Qt 类型注册必须显式映射到当前允许的九种类型；QVariant 也不能作为隐式兜底或转换。
分支、标签、合流、环、event、跨 Unit、ExternalCode 运行时、多/非末端 UI 等不可表示语义均拒绝，不生成“近似”旧图。
当前 legacy `create()/start()` 字节产物、runtime `createRuntime()/startRuntime()` 与固定 factory 不变；工作区不自动互迁。

当前 [generation manifest](../../src/generation/workspace_io.cpp) v1/v2 严格六字段：`version/blueprintSha256/modules/protectedFiles/files/batches`。
蓝图 Schema 2、Unit、pluginLocks 或 driverBindings 不能加入这份旧清单；Candidate JSON 和 ExternalCode import manifest 不改。
未来多 Unit 产物元数据另放 `unit-artifacts.draft.json`，字段拟为 `workspaceManifestVersion/projectId/units`。
其中 units 项拟记录 `unitId/targetId/pluginLockDigest/artifacts`；此 Draft 不定义当前清单升级或当前可读格式。

## 未来契约测试的进入条件

- 中文、端口改名/重排与多次保存保持 ID/边；位置变化不改变语义 IR，未知片段字节不丢失。
- 动态端口显式新增只分配一次 ID；edit/rename/reorder/reload 保持 ID；connected 删除/类型变化及歧义升级拒绝或经明确批准原子迁移，失败保留原状态。
- 同语义不同布局产生相同 CoreGraphIR/摘要；UnitCoreIR 投影隔离，TargetAdapter.lower 确定且拒绝不支持的语义。
- 正反向版本拒绝；Schema 1 固定模型不误读 Schema 2，未识别新版本不会被降级保存。
- 检出重复 ID、悬空端点、错误方向/kind/type、非法 Unit、跨 Unit 普通边和未注册类型。
- 一图多插件与同插件多 Unit 正确区分，缺包/锁摘要变化/定义版本不兼容阻止生成。
- Channel 两端、wireSchema 与 Bridge 锁完整绑定；无驱动 Bridge 允许省略/空依赖，必需端点依赖缺失、错侧 Unit、接口/哈希错误失败关闭；无已验证通信适配器时不声称通信可运行。
- 显式复制保留原图/layout/opaque；LegacyQt 适配对不可表示语义诊断并零生成、零覆盖。
- 新元数据与严格六字段旧清单分开验证，legacy 字节产物及 Runtime v2 固定 factory 回归保持。

P0.1 已实现结构 Schema 与离线测试，校验 required、known-field type、direction/kind/mode enum、命名空间、摘要格式、端点 shape，以及每侧 binding ID 数组的重复项。工具以 unknown 输入直接同步编译机器 Schema，strict/allErrors 开启，不做类型转换、默认值、剥离或输入改写；不另建 TS/Zod 蓝图模型，也不拉取远端 Schema。

上述未来条件仍需要语义层或生命周期实现：对象数组 ID 唯一/引用存在、Unit 归属与同 Unit 普通边、方向/kind/type/基数、注册类型/目标支持、锁与版本兼容、实际字节 hash、wire 映射/逐侧必需驱动、动态 ID 历史与迁移、unknown raw 保存、IR hash 与生成 context。特别是两个相同 Qt target 的不同 Unit 仍须拒绝普通边，锁定 Bridge 要求某侧驱动而字段缺失时仍须失败关闭。

`semantic-only` fixtures 的 schemaPass 只证明结构层未覆盖这些规则，没有语义 checker 或执行承诺；非 mutation 测试仅验证解析后的值和数组保持，不等于 opaque 原字节保存已实现。当前 GUI/C++ Schema 1、旧清单/协议、生产测试均未接入 v2。
