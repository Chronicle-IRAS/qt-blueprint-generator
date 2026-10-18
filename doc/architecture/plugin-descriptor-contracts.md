# Plugin Descriptor Contracts — P0.3 Draft

状态：Draft，2026-10-18。[JSON Schema](../../schemas/plugin-descriptor-v1.schema.json) 是机器结构的权威定义，采用 Draft 2020-12，并用本地相对引用复用 Blueprint v2 的 ID、Port、PluginLock 定义。本轮只交付离线声明和验证，不冻结完整 Plugin API。

## 数据与信任

每个 Descriptor 包含 `descriptorVersion: "1.0-draft"`、`manifest`、`nodeDefinitions`、`types`、`targets`、`bridges`。后四项为数组。所有记录允许附加字段，验证不剥离或改写数据；未知字段不增加行为或权限。

调用方必须独立提供可信的五字段 PluginLock：`pluginId/package/version/sha256/pluginApiVersion`，并在调用前保证描述确实来自对应包。装配器比对身份和版本，使用调用方提供的摘要；两份 metadata 相等不能证明来源或字节完整性。本轮不下载包、不建立信任、不复验实际包/描述/驱动字节。manifest 无须自报包摘要，自报的附加 sha256 不会替代可信锁。

capabilities 只是一组字符串声明，不授予文件、命令、网络或构建权限。没有插件激活、动态装载、Registry、Host 或 Cordis。

## Manifest

| 字段 | 声明规则 |
| --- | --- |
| manifestVersion | `1.0-draft`，独立于 descriptorVersion |
| pluginId | 非空，不含 `/`、`@` 或空白 |
| package / version | 非空包身份和精确版本标识 |
| pluginApiVersion | 本轮接受 `1.0-draft` |
| supportedBlueprintSchemas | 整数数组，本轮必须包含 2 |
| nodeTypes / targets | 与实际提供的节点 typeId、targetId 集合一致 |
| capabilities | 不重复的非空字符串，仅描述意向 |
| dependencies | `{pluginId, version}` 数组，同一 provider 不重复 |

版本采用精确字符串匹配，不解释 SemVer 范围、不选择最新版或自动升级。包版本、节点定义版本、Bridge adapter 版本和协议版本相互独立。同一 typeId 可有多个定义版本，但 typeId/version 组合唯一。

输入为封闭描述集合：依赖必须存在、精确版本匹配且没有循环。类型、节点、目标只能在提供者自己的 `pluginId/…` 命名空间声明。消费其他插件的类型、节点定义或 Bridge 目标，必须声明直接依赖；传递依赖不隐式授权消费。

## 节点端口与属性

NodeDefinition 声明 `typeId/nodeDefinitionVersion/title/supportedTargets/propertiesSchema/portMode/ports`。本轮 portMode 仅接受 static。端口复用 `id/name/direction/kind/typeId`，可声明安全非负整数 minConnections/maxConnections，minimum 不得大于 maximum；缺省不增加相应限制。

Descriptor-aware 校验按精确定义版本检查节点快照：静态端口 ID 完整且无额外项，方向、kind、类型一致。显示名可编辑，不按名称匹配，不分配或迁移 ID。基数按 nodeId/portId 展开，再交给 P0.2 统计 Edge 与 Channel 占用。

propertiesSchema 为 boolean 或 object，隔离编译为同步 strict Draft 2020-12 校验器，仅支持自身/本地引用。非法 Schema、未知 dialect、外部未解析引用及 `$async: true` 明确拒绝。不应用默认值、不转换类型、不删除字段。运行期求值异常（例如无终止条件的自引用）转换为 NODE_PROPERTIES_EVALUATION 诊断，不抛出异常；正常的结构递归仍受支持。这不是任意恶意 Schema 的资源隔离机制，描述来源仍须由调用方审查。wireSchema 和驱动 interfaceContract 不在此编译。

动态 getPorts、itemId 对账、连接迁移、升级和保存生命周期仍待后续实现，不能把静态校验描述成动态恢复。

## 类型与 Target/Bridge Policy

TypeDefinition 使用 `typeId/compatibleTo`；兼容目标必须存在。兼容是显式单向直接关系，不自动反向、传递或插入转换。

TargetPolicy 使用 `targetId/supportedNodeDefinitions`，条目为 `{typeId, nodeDefinitionVersion}`。节点的 supportedTargets 与 Target 的节点/版本支持须相互确认，不能由节点单方面声明目标编译支持。所引用的目标与定义均须存在。

节点广告 supportedTargets 只表达兼容意向，不消费 Target，因此不要求反向依赖；Target 消费外部节点定义时则需要依赖其提供者。这避免人为制造双向依赖环。广告目标仍须存在于本次集合中。

BridgePolicy 提供者来自所属 manifest，声明 `version/mappings/requiredDriverCount`。每个映射包含 `sourceTargetId/targetTargetId/sourceTypeId/targetTypeId/wireTypeId`，目标与类型均须存在并满足消费依赖规则；native type 与 wire type 可以不同。

requiredDriverCount 分别声明 source/target 的安全非负整数需求，允许零驱动。实际驱动的引用、歧义、Unit 归属与数量交给 P0.2 检查；不能借用另一端驱动凑数。本轮不校验完整驱动接口、物理哈希或通信行为。

## API 与诊断

[plugin-descriptors.ts](../../tools/schema-contracts/plugin-descriptors.ts) 提供：

```typescript
assembleTrustedContext(descriptors: unknown, trustedLocks: unknown): AssemblyResult
validateBlueprintWithDescriptors(blueprint: unknown, descriptors: unknown, trustedLocks: unknown): Diagnostic[]
```

AssemblyResult 总有 diagnostics，仅全部通过时提供 context，错误时不发布部分上下文。成功 context 为独立快照，描述集合重新排序不改变其内容。输入、锁与蓝图只读。

context 可直接交给原 P0.2 validateBlueprintSemantics；完整声明校验应使用 validateBlueprintWithDescriptors，该入口额外检查静态端口、属性、实例基数及蓝图锁依赖闭包。原 P0.2 API 保持原行为。

Diagnostics 沿用 code/severity/message/path 和相关 ID。结构错误使用 DESCRIPTOR_STRUCTURE、TRUSTED_LOCK_STRUCTURE；身份/信任/版本使用 DESCRIPTOR_*、TRUSTED_LOCK_DUPLICATE、MANIFEST_DECLARATIONS；依赖使用 DEPENDENCY_*；引用、支持、属性 Schema 使用 DESCRIPTOR_REFERENCE、TARGET_SUPPORT_MISMATCH、PORT_LIMITS、PROPERTY_SCHEMA_INVALID；节点实例使用 NODE_PORT_*、NODE_PROPERTIES 和 NODE_PROPERTIES_EVALUATION。源码和测试列出精确错误码。

描述错误位置以 /descriptors 或 /trustedLocks 开头，蓝图错误指向相应节点或锁。相同输入产生稳定诊断；错误输入重排后数组索引可变。消费者依据 code 和位置处理，不解析自然语言 message。

## 离线证据与边界

[示例](../../tests/fixtures/plugin-descriptors/README.md) 包含两个非 Qt 领域插件、两个 Unit、单向兼容、双向支持和 Bridge 驱动需求。测试将命名空间替换为全新领域，无须修改通用代码。

运行现有 npm test/typecheck 即可。原 P0.1/P0.2 测试、Blueprint Schema、C++/Qt、旧 manifest 和生成协议均未改变。语义通过不代表可生成、构建或运行；Host/Registry、Graph IR、GUI、动态端口和工程生成后续单独实施，本轮不进入 P1。
