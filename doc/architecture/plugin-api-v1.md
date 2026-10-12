# Plugin API v1 设计（Draft）

状态：Draft，2026-10-12。本文是语言无关的边界提案，API、manifest、RPC 与类型约定均未冻结。
示例不是现有可安装插件或当前程序可加载文件；TypeScript 记法仅用于说明类型，不表示已选定 TypeScript/Cordis 宿主。
共享字段与完整蓝图示例见[蓝图 Schema v2](blueprint-schema-v2.md)，信任与阶段范围见[平台架构](plugin-platform.md)。

## 插件身份与能力声明

插件定义节点、propertiesSchema、稳定端口、连线语义、目标 IR 与代码生成；宿主负责通用文档、图约束和工程授权。
节点 `typeId` 使用 `pluginId/localType`，如 `org.blueprint.qt/logic-module`；目标 `targetId` 如 `org.blueprint.qt/widgets`。
节点 `unitId` 引用独立 Build Unit，不是 pluginId；多个 Unit 可使用同一插件，Unit 内也可使用目标明确支持的其他插件节点。
pluginLocks 精确绑定 `pluginId/package/version/sha256/pluginApiVersion`，实际包哈希与声明必须一致。

| 版本字段 | 管理对象 | 不能替代的检查 |
| --- | --- | --- |
| `manifestVersion` | capability manifest 结构 | 不能代替 Plugin API 兼容握手 |
| `version` | 插件包发布版本 | 不能代替包哈希或节点定义版本 |
| `pluginApiVersion` | 宿主/插件调用语义 | 本提案为 `1.0-draft`，不是稳定 API |
| `nodeDefinitionVersion` | 单个节点的属性/端口语义 | 不因包版本相同就认定可互换 |
| `schemaVersion` | 蓝图文档结构 | Draft 2 与当前 Schema 1 分别解析 |
| `coreIrVersion` | 可信平台的目标无关 CoreGraphIR/UnitCoreIR | Draft `1.0-draft`，不改现行 IrCompiler 输出 |
| `targetIrVersion` | 目标 IR 契约 | 目标适配器必须明确接受 |
| `workspaceManifestVersion` | 未来独立多 Unit 产物索引 | 不替代旧 generation manifest 的 version 1/2 |

以下 capability manifest 是 Draft。它声明所需扩展接口，不授予文件、网络或构建权限。

```json
{
  "manifestVersion": "1.0-draft",
  "pluginId": "org.blueprint.qt",
  "package": "blueprint-qt-target",
  "version": "0.1.0-draft.1",
  "pluginApiVersion": "1.0-draft",
  "supportedBlueprintSchemas": [2],
  "nodeTypes": ["org.blueprint.qt/start", "org.blueprint.qt/end",
    "org.blueprint.qt/logic-module", "org.blueprint.qt/ui-page"],
  "targets": ["org.blueprint.qt/widgets"],
  "capabilities": ["nodes.define", "types.define", "connections.validate",
    "target.lower", "candidate.propose", "scaffold.propose", "build.plan"],
  "dependencies": []
}
```

初期仅加载明确可信、锁定并验证的插件；未知来源或不可信插件在隔离验证前拒绝。
capabilities 是宿主可审核的声明，不是 OS sandbox；在 Sidecar 中运行也不能自动限制插件直接访问文件/网络。
可信进程内插件与宿主同权，接口收窄只能减少误用，不能把任意第三方代码变成安全代码。
后续接受不可信插件必须另做实际隔离和越权验证，本次不承诺具备该能力。

## NodeDefinition 示例

下面是完整蓝图示例中 logic-module 的示意定义：name/description 可编辑，输出固定为 message:QString。
它只展示一版最小定义，不代表完整 Qt 插件已经实现，也不限制未来定义通过 getPorts(properties) 提供动态端口。
propertiesSchema 以 JSON Schema 风格表达；采用哪个正式 dialect 与校验器仍需契约验证。

```json
{
  "typeId": "org.blueprint.qt/logic-module",
  "nodeDefinitionVersion": "1.0.0-draft.1",
  "title": "Logic Module",
  "supportedTargets": ["org.blueprint.qt/widgets"],
  "propertiesSchema": {
    "type": "object", "required": ["name", "description"],
    "properties": {"name": {"type": "string"}, "description": {"type": "string"}},
    "additionalProperties": false
  },
  "ports": [
    {"id": "flow-in", "name": "Flow", "direction": "in", "kind": "control",
      "typeId": "org.blueprint.core/control-flow@1"},
    {"id": "flow-out", "name": "Flow", "direction": "out", "kind": "control",
      "typeId": "org.blueprint.core/control-flow@1"},
    {"id": "message-out", "name": "message", "direction": "out", "kind": "data",
      "typeId": "org.blueprint.qt/QString@1"}
  ]
}
```

定义版本变化需显式升级诊断；不能读取旧实例时重新生成随机端口 ID。
动态端口 ID 必须持久化，名称和顺序与身份分开；宿主复验端口快照与定义导出的契约。
data/event/control 都要求显式 typeId；event 必须绑定注册事件类型，不能成为无类型消息通道。
宿主先检查方向、kind、类型、基数、端点存在和 Unit 边界，插件再追加语义限制，不能越过宿主拒绝。

## 最小类型接口（说明性伪代码）

下列名字均为拟议契约类型，不是当前源码接口。Readonly 表达不可修改输入的约定，不是安全隔离措施。

```typescript
type Json = null | boolean | number | string | Json[] | { [key: string]: Json };
type Diagnostic = { code: string; severity: "error" | "warning";
  message: string; nodeId?: string; portId?: string; unitId?: string };
type Port = { id: string; name: string; direction: "in" | "out";
  kind: "control" | "data" | "event"; typeId: string };
type Node = { id: string; unitId: string; typeId: string;
  nodeDefinitionVersion: string; properties: Json; ports: Port[] };
type Endpoint = { nodeId: string; portId: string };
type Edge = { id: string; kind: Port["kind"]; source: Endpoint; target: Endpoint };
type BuildUnit = { id: string; targetId: string; pluginId: string; settings: Json };
type PluginLock = { pluginId: string; package: string; version: string;
  sha256: string; pluginApiVersion: string };
type DriverBindingRef = { id: string; unitId: string; sha256: string };
type Channel = { id: string; source: Endpoint & { unitId: string };
  target: Endpoint & { unitId: string }; wireSchema: { typeId: string; schema: Json };
  bridge: { pluginId: string; version: string; driverBindingId: string; configuration: Json } };
type CoreGraphIR = { coreIrVersion: string; coreIrSha256: string; projectId: string;
  units: BuildUnit[]; nodes: Node[]; edges: Edge[]; channels: Channel[];
  pluginLocks: PluginLock[]; driverBindingRefs: DriverBindingRef[] };
type UnitCoreIR = { coreIrVersion: string; coreIrSha256: string; unitIrSha256: string;
  unit: BuildUnit; nodes: Node[]; edges: Edge[]; channelBoundaries: Channel[];
  pluginLocks: PluginLock[]; driverBindingRefs: DriverBindingRef[] };
interface TrustedCoreCompiler {
  normalize(document: Readonly<Json>): CoreGraphIR;
  project(core: Readonly<CoreGraphIR>, unitId: string): UnitCoreIR;
}
type OperationContext = { requestId: string; projectId: string; documentRevision: string;
  documentSha256: string; unitId: string; pluginLockDigest: string;
  driverHashes: Record<string, string>; protectedHashes: Record<string, string>;
  cancellationId: string };
type TargetIR = { targetId: string; targetIrVersion: string; unitId: string;
  sourceUnitIrSha256: string; body: Json };
type FileProposal = { relativePath: string; content: string };
type CandidatePlan = { unitId: string; nodeId: string; files: FileProposal[] };
type ScaffoldPlan = { unitId: string; files: FileProposal[] };
type BuildPlan = { unitId: string; toolchainId: string; arguments: string[] };
interface NodeDefinition {
  typeId: string; nodeDefinitionVersion: string; propertiesSchema: Json;
  supportedTargets: string[];
  getPorts(properties: Readonly<Json>): Port[];
  validateNode(node: Readonly<Node>): Diagnostic[];
  validateConnection(edge: Readonly<Edge>, graph: Readonly<UnitCoreIR>): Diagnostic[];
}
interface TargetAdapter {
  targetId: string; targetIrVersion: string;
  validateUnit(graph: Readonly<UnitCoreIR>): Diagnostic[];
  lower(graph: Readonly<UnitCoreIR>, context: Readonly<OperationContext>): Promise<TargetIR>;
  proposeScaffold(ir: Readonly<TargetIR>, context: Readonly<OperationContext>): Promise<ScaffoldPlan>;
  generateNode(ir: Readonly<TargetIR>, nodeId: string,
    context: Readonly<OperationContext>): Promise<CandidatePlan>;
  planBuild(ir: Readonly<TargetIR>, context: Readonly<OperationContext>): BuildPlan;
}
```

注册接口只允许声明自己的节点、类型与目标，不授予任意命令或写盘函数；异步调用必须携带完整 context。
pluginLockDigest 覆盖锁定的完整插件包版本、API 版本和包哈希，不能只用 pluginId 计算。
Draft 数据流为 `v2 文档 → TrustedCoreCompiler.normalize → CoreGraphIR → project(unitId) → UnitCoreIR → TargetAdapter.lower → TargetIR`。
TrustedCoreCompiler 属于可信平台而非插件扩展；CoreGraphIR 保存稳定节点/端口、显式边、Unit/Channel/锁/驱动引用等目标无关语义，不含布局、猜测的目标执行顺序或 AI 类名。
coreIrVersion 暂拟 `1.0-draft`；coreIrSha256 对该版本的规范化语义计算，仅排除 CoreGraphIR 顶层自身 coreIrSha256，不沿用现行 IrCompiler 协议。
UnitCoreIR 只含本 Unit 节点/内部边、相关锁/驱动引用及相邻 Channel 边界，远端只有端点契约；unitIrSha256 对投影语义计算，仅排除 UnitCoreIR 顶层来源 coreIrSha256 与自身 unitIrSha256，不纳入其他 Unit 实现。两种摘要都必须包含相关 pluginLocks[].sha256、driverBindingRefs[].sha256；不能泛化排除其他嵌套同名字段。
平台保留全局图与投影权威，插件不能修改 Unit 边界；lower 只接受该投影，TargetIR 的 sourceUnitIrSha256 必须回指输入，未支持语义明确拒绝。
TargetIR body 由目标契约校验，包含语义、类型映射和确定性工程计划，不含画布布局。
插件返回计划后，由可信服务校验相对路径、资源限制、所有权、上下文与受保护文件，再执行事务。
BuildPlan 不包含任意可执行文件路径、shell 脚本或环境表；服务从受支持工具链配置解析程序和受控参数。
插件若使用 AI，由可信生成服务提供受限请求能力，插件不得通过 RPC 获得 API key 或完整环境。

## 生命周期、依赖与失败

拟议流程为：验证 manifest/锁/信任 → 解析依赖 → 按拓扑激活 → 注册定义 → 提供能力 → 停止并释放。
`activate(registry, context)` 只在依赖版本、哈希和 API 兼容时进行；缺包、版本冲突、循环依赖明确诊断。
`deactivate(reason)` 应取消未完成操作、解绑资源和注册项；插件崩溃同样使其操作与候选上下文失效。
锁完整记录依赖包，不能在打开工程时下载/升级到未锁定版本；包 hash 验证不证明可信来源。
初期不要求热替换；显式替换版本先停止操作，再验证新定义，不直接迁移未保存节点。
不兼容或缺失插件的节点由未来 v2 读取器显示占位并无损保留未知字段与 opaque payload 原始字节，不能生成；当前 v1 读取器不具备此功能。
payload 保留不等于重新保存后的整个 JSON 文件字节一致；显式复制转换不改写原工程。
候选/正式工程证据保留，取消不自动删除已存在文件，也不使旧候选重新可接受。

## 跨语言 RPC 候选

可选 Sidecar 使用 QProcess + stdio JSON-RPC 2.0；[规范](https://www.jsonrpc.org/specification)与传输无关，不规定换行 framing。
若采用单行 UTF-8 JSON 或长度前缀，必须作为本应用 framing 契约另行测试；stdout 只放协议，日志写 stderr。
日志脱敏且不包含密钥、用户驱动源码、完整提示词或原始模型响应；RPC 不传 C++ 地址或任意绝对输出路径。
握手明确 API、支持蓝图版本、包身份与会话，示例仍为 Draft：

```json
{"jsonrpc":"2.0","id":1,"method":"plugin.initialize","params":{
  "pluginApiVersion":"1.0-draft","supportedBlueprintSchemas":[2],
  "pluginId":"org.blueprint.qt","version":"0.1.0-draft.1","sessionId":"session-demo"}}
```

方法名不使用规范保留的 `rpc.*`；可选方法为 plugin.initialize、nodes.describe、graph.validate、target.lower、generation.propose、operation.cancel、plugin.shutdown。
需要结果的请求有唯一 id，返回原 id；通知无 id，也不得用于授予 Accept/Build 等操作权限。
初期可明确禁止 batch 请求作为应用子集，不能声称 JSON-RPC 规范本身禁止 batch 或通知。
协议错误、业务诊断与安全脱敏错误分开，消息大小/深度、超时、乱序、重复响应和崩溃必须有界处理。
operation.cancel 确认收到取消不证明外部请求已停止；可信服务作废 context，丢弃迟到结果并阻止 Accept。
Sidecar 未通过一致性、取消和异常测试前不接管已验证 C++/Qt 路径，Cordis 不作为既定依赖。

## 授权与适配器契约

现有 [GenerationController](../../src/app/generation_controller.h) 管理文档/工作区快照与取消，新增 context 维度仍是未来要求。
服务必须在请求开始、结果落盘和 Accept 前核对项目/文档/Unit/插件版本/驱动及受保护文件哈希。
项目切换、Unit 设置/插件/驱动变化或取消使旧结果失败关闭；受保护文件变化不能通过插件声明豁免。
Candidate 写入与 Accept 继续由可信 [GenerationService](../../src/generation/generation_service.h) / [CandidateStore](../../src/generation/candidate_store.cpp) 授权，核对基线和冲突确认。
scaffold 由可信 [ProjectScaffolder](../../src/generation/project_scaffolder.h) 路径管理；[BuildService](../../src/workspace/build_service.h) 执行须经用户明确触发，构建不是代码沙箱。

| 适配器 | 必须满足的契约 | 拒绝条件 |
| --- | --- | --- |
| Qt Target | `org.blueprint.qt/widgets`；优先复用现有 IR/scaffold/runtime | 不能表达的图/端口/目标语义 |
| v2 → LegacyQt | 单 Unit 线性 control 流；data 边与直接前驱同名同类型绑定完全一致 | 分支/标签/event/跨 Unit、重命名、跳跃取值、隐式转换 |
| Embedded Target | 明确芯片/SDK/工具链适配、启动/链接配置和完整目标工程 | 不支持的芯片/SDK、缺驱动/工具链或无法真实交叉编译 |
| 用户驱动 | Snapshot/Reference 的显式绑定、接口与字节哈希；AI 只接收接口 | 哈希变化、越界路径、不兼容接口 |
| Bridge | 稳定端点 unitId/nodeId/portId、wireSchema、锁定适配器版本及驱动/配置 | 缺失任一契约，或把跨 Unit 关系当普通边 |

Qt 适配必须显式映射注册类型到当前九种 Qt 声明类型，不用 QVariant 或类型名字猜转换。
当前 Runtime [Assembler](../../src/generation/runtime_assembler.cpp) 只支持线性 Flow、首模块空输入和精确前驱绑定；控制端口不能伪造旧 Start/End PortSpec。
legacy v1 `create()/start()` 原字节产物、runtime `createRuntime()/startRuntime()` 与 `createModule_HASH` factory 契约均不变。
v1 → v2 只允许显式复制，保留原图/layout/Flow/label/opaque，不猜造 data edge；无法表达时诊断或留在 legacy。
Snapshot 拟复用当前 [ExternalCodeImporter](../../src/workspace/external_code_importer.h)，Reference 未来设计；当前 Runtime 不执行 ExternalCode。
Embedded 毕业验收需真实交叉编译有效工程，不要求烧录/硬件运行；Bridge 本次仅设计，无物理通信实现。

旧 [generation manifest](../../src/generation/workspace_io.cpp) v1/v2 严格保持六字段 `version/blueprintSha256/modules/protectedFiles/files/batches`，Candidate JSON 与五字段 ExternalCode import manifest 不变。
Unit/pluginLocks/产物索引只能进入独立 Draft `unit-artifacts.draft.json` 的 workspaceManifestVersion 契约，不给旧清单添加字段。

## 未来验证条件

- 定义与实例字段/类型一致，动态端口 ID 稳定；包、定义、API、蓝图与目标 IR 版本分别拒绝不兼容值。
- 依赖缺失/冲突/循环、加载失败、停用与崩溃不会丢文档或留下可接受的过期结果。
- 进程内/Sidecar 同 fixture 的语义 IR 与诊断一致；取消、乱序、非法 framing、超时与消息限制可验证。
- 同语义不同布局的 CoreGraphIR/摘要一致；UnitCoreIR 投影隔离；相同投影与锁/配置的 lower 输出确定，不支持语义诊断并拒绝。
- 可信服务拒绝越权路径、受保护文件修改及过期 context，插件 manifest 不能豁免这些规则；不可信插件拒绝加载。
- Qt 严格适配及 legacy 字节/factory 回归通过；Embedded 完整目标工程真实交叉编译，不能用宿主 smoke 替代。
- 跨 Unit 普通边即使同目标仍被拒绝；Channel/Bridge 只在显式类型和版本完整时可规划，通信另行验收。

本次只定义文档边界，不实现宿主、RPC、插件 Registry、适配器、驱动绑定或新测试。
