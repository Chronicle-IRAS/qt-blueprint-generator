<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="zh_CN" sourcelanguage="en">
<context>
    <name>CandidateReviewDialog</name>
    <message><source>Conflict: this file contains manual or newer changes. Accepting requires overwrite confirmation.</source><translation>冲突：此文件包含手工修改或较新的更改，接受前必须确认覆盖。</translation></message>
    <message><source>Review generated candidates</source><translation>审核生成的候选文件</translation></message>
    <message><source>Current file</source><translation>当前文件</translation></message>
    <message><source>Candidate / edited draft</source><translation>候选文件 / 编辑草稿</translation></message>
    <message><source>Accept original</source><translation>接受原候选</translation></message>
    <message><source>Accept edited draft</source><translation>接受编辑后的草稿</translation></message>
    <message><source>Reject file</source><translation>拒绝此文件</translation></message>
    <message><source>Cancel remaining</source><translation>取消剩余候选</translation></message>
    <message><source>Refresh preview</source><translation>刷新预览</translation></message>
    <message><source> — Accepted</source><translation> — 已接受</translation></message>
    <message><source> — Rejected</source><translation> — 已拒绝</translation></message>
    <message><source> — Pending</source><translation> — 待审核</translation></message>
    <message><source>Cannot preview candidate: %1</source><translation>无法预览候选文件：%1</translation></message>
    <message><source>Load the current files again? Review the updated comparison before accepting.</source><translation>重新加载当前文件吗？接受前请检查更新后的对照内容。</translation></message>
    <message><source>Cannot refresh preview: %1</source><translation>无法刷新预览：%1</translation></message>
    <message><source>Preview refreshed. Review the files before accepting.</source><translation>预览已刷新。接受前请重新检查文件。</translation></message>
    <message><source>Project consistency check failed: %1</source><translation>工程一致性检查失败：%1</translation></message>
    <message><source>Confirm overwrite</source><translation>确认覆盖</translation></message>
    <message><source>This file contains manual or newer changes. Overwrite it with the reviewed candidate?</source><translation>此文件包含手工修改或较新的更改。是否用审核后的候选内容覆盖？</translation></message>
    <message><source>Cannot accept candidate: %1. Refresh the preview if files have changed.</source><translation>无法接受候选文件：%1。文件如有变化，请刷新预览。</translation></message>
    <message><source>File accepted.</source><translation>文件已接受。</translation></message>
    <message><source>Cannot reject candidate: %1</source><translation>无法拒绝候选文件：%1</translation></message>
    <message><source>File rejected.</source><translation>文件已拒绝。</translation></message>
    <message><source>Cannot cancel remaining candidates: %1</source><translation>无法取消剩余候选文件：%1</translation></message>
    <message><source>The blueprint or workspace changed. Acceptance is disabled.</source><translation>蓝图或工作目录已变更，不能再接受这些候选文件。</translation></message>
    <message><source>Close candidate review</source><translation>关闭候选审核</translation></message>
    <message><source>Reject all remaining candidates and close? Accepted files will be retained.</source><translation>拒绝所有剩余候选并关闭吗？已经接受的文件会保留。</translation></message>
</context>
<context>
    <name>ValidationDiagnosticsDialog</name>
    <message><source>Blueprint validation failed</source><translation>蓝图校验失败</translation></message>
    <message><source>Fix these blueprint problems before generating:</source><translation>生成代码前请先修正以下蓝图问题：</translation></message>
    <message><source>Close</source><translation>关闭</translation></message>
    <message><source>node: %1</source><translation>节点：%1</translation></message>
    <message><source>edge: %1</source><translation>连线：%1</translation></message>
</context>
<context>
    <name>GenerationController</name>
    <message><source>Select a UI page, logic module, or decision node.</source><translation>请选择一个界面页面、逻辑模块或条件分支节点。</translation></message>
    <message><source>AI provider settings are invalid.</source><translation>AI 服务设置无效。</translation></message>
    <message><source>Select an existing absolute workspace directory.</source><translation>请选择已存在的工作目录，并使用绝对路径。</translation></message>
    <message><source>Fix blueprint validation errors before generating.</source><translation>请先修正蓝图校验错误，再生成代码。</translation></message>
    <message><source>The workspace scaffold could not be initialized or does not match the blueprint.</source><translation>无法初始化工程骨架，或工作目录与当前蓝图不匹配。</translation></message>
    <message><source>Could not read the workspace contracts.</source><translation>无法读取工作目录中的接口契约。</translation></message>
    <message><source>The workspace blueprint contract is invalid.</source><translation>工作目录中的蓝图契约无效。</translation></message>
    <message><source>Could not prepare the node generation prompt.</source><translation>无法准备节点生成提示词。</translation></message>
    <message><source>AI client is unavailable.</source><translation>AI 客户端不可用。</translation></message>
    <message><source>The workspace scaffold changed during generation.</source><translation>生成期间工程骨架发生了变化。</translation></message>
    <message><source>The generated candidate could not be safely saved.</source><translation>无法安全保存生成的候选文件。</translation></message>
    <message><source>AI generation failed. Check provider settings and try again.</source><translation>AI 生成失败，请检查服务设置后重试。</translation></message>
    <message><source>AI provider credentials are unavailable.</source><translation>AI 服务凭据不可用，请检查环境变量中的密钥。</translation></message>
    <message><source>AI generation failed due to a network error.</source><translation>网络错误导致 AI 生成失败。</translation></message>
    <message><source>AI generation timed out. Try again.</source><translation>AI 生成超时，请重试。</translation></message>
    <message><source>AI provider authentication failed.</source><translation>AI 服务身份验证失败。</translation></message>
    <message><source>AI provider payment or quota is required.</source><translation>AI 服务需要付费或补充额度。</translation></message>
    <message><source>AI provider endpoint was not found.</source><translation>未找到 AI 服务端点。</translation></message>
    <message><source>The configured AI model was not found.</source><translation>未找到配置的 AI 模型。</translation></message>
    <message><source>AI provider rate limit reached. Try again later.</source><translation>AI 服务已达到速率限制，请稍后重试。</translation></message>
    <message><source>AI returned an invalid or unsafe generation response.</source><translation>AI 返回的生成结果无效，或未通过安全校验。</translation></message>
    <message><source>AI provider is unavailable. Try again later.</source><translation>AI 服务暂时不可用，请稍后重试。</translation></message>
</context>
<context>
    <name>AiSettingsDialog</name>
    <message><source>AI Settings</source><translation>AI 设置</translation></message>
    <message><source>OpenAI Compatible</source><translation>OpenAI 兼容接口</translation></message>
    <message><source>Environment variable BLUEPRINT_AI_API_KEY</source><translation>环境变量 BLUEPRINT_AI_API_KEY</translation></message>
    <message><source>Available</source><translation>可用</translation></message>
    <message><source>Not available</source><translation>不可用</translation></message>
    <message><source>Test Connection</source><translation>测试连接</translation></message>
    <message><source>Not tested</source><translation>未测试</translation></message>
    <message><source>Testing</source><translation>正在测试</translation></message>
    <message><source>Success</source><translation>成功</translation></message>
    <message><source>Invalid configuration</source><translation>配置无效</translation></message>
    <message><source>Settings could not be saved</source><translation>设置无法保存</translation></message>
    <message><source>API key is not available</source><translation>API 密钥不可用</translation></message>
    <message><source>Network error</source><translation>网络错误</translation></message>
    <message><source>Request timed out</source><translation>请求超时</translation></message>
    <message><source>Authentication failed</source><translation>身份验证失败</translation></message>
    <message><source>Payment required</source><translation>需要付费</translation></message>
    <message><source>Endpoint not found</source><translation>未找到端点</translation></message>
    <message><source>Model not found</source><translation>未找到模型</translation></message>
    <message><source>Rate limit reached</source><translation>已达到速率限制</translation></message>
    <message><source>Invalid response</source><translation>响应无效</translation></message>
    <message><source>Provider unavailable</source><translation>服务提供方不可用</translation></message>
    <message><source>Connection failed</source><translation>连接失败</translation></message>
    <message><source>Save</source><translation>保存</translation></message>
    <message><source>Cancel</source><translation>取消</translation></message>
    <message><source>Provider</source><translation>服务提供方</translation></message>
    <message><source>Chat Completions Endpoint</source><translation>聊天补全端点</translation></message>
    <message><source>Model</source><translation>模型</translation></message>
    <message><source>API Key Source</source><translation>API 密钥来源</translation></message>
    <message><source>API Key Status</source><translation>API 密钥状态</translation></message>
    <message><source>Connection Status</source><translation>连接状态</translation></message>
</context>
<context>
    <name>BlueprintScene</name>
    <message>
        <location filename="../src/editor/blueprint_scene.cpp" line="92"/>
        <source>Add node</source>
        <translation>添加节点</translation>
    </message>
    <message>
        <location filename="../src/editor/blueprint_scene.cpp" line="117"/>
        <source>Delete node</source>
        <translation>删除节点</translation>
    </message>
    <message>
        <location filename="../src/editor/blueprint_scene.cpp" line="171"/>
        <source>Delete edge</source>
        <translation>删除连线</translation>
    </message>
    <message>
        <location filename="../src/editor/blueprint_scene.cpp" line="134"/>
        <location filename="../src/editor/blueprint_scene.cpp" line="490"/>
        <source>Move node</source>
        <translation>移动节点</translation>
    </message>
    <message>
        <location filename="../src/editor/blueprint_scene.cpp" line="153"/>
        <source>Connect nodes</source>
        <translation>连接节点</translation>
    </message>
    <message>
        <location filename="../src/editor/blueprint_scene.cpp" line="243"/>
        <source>Edit node</source>
        <translation>编辑节点</translation>
    </message>
</context>
<context>
    <name>MainWindow</name>
    <message><source>Generate selected node</source><translation>生成选中节点</translation></message>
    <message><source>Cancel generation</source><translation>取消生成</translation></message>
    <message><source>Ready to generate</source><translation>可以开始生成</translation></message>
    <message><source>Generating</source><translation>正在生成</translation></message>
    <message><source>Success — review candidates</source><translation>生成成功 — 请审核候选文件</translation></message>
    <message><source>Failed: %1</source><translation>失败：%1</translation></message>
    <message><source>Cancelled</source><translation>已取消</translation></message>
    <message><source>%1: %2</source><translation>%1：%2</translation></message>
    <message><source>Select All</source><translation>全选</translation></message>
    <message><source>Fit View</source><translation>适应视图</translation></message>
    <message><source>Reset View</source><translation>重置视图</translation></message>
    <message>
        <location filename="../src/app/main_window.cpp" line="180"/>
        <location filename="../src/app/main_window.cpp" line="490"/>
        <source>Blueprint Editor</source>
        <translation>蓝图编辑器</translation>
    </message>
    <message>
        <location filename="../src/app/main_window.cpp" line="190"/>
        <location filename="../src/app/main_window.cpp" line="491"/>
        <source>Blueprint</source>
        <translation>蓝图</translation>
    </message>
    <message>
        <location filename="../src/app/main_window.cpp" line="194"/>
        <location filename="../src/app/main_window.cpp" line="492"/>
        <source>Add node</source>
        <translation>添加节点</translation>
    </message>
    <message>
        <location filename="../src/app/main_window.cpp" line="215"/>
        <location filename="../src/app/main_window.cpp" line="496"/>
        <source>Edge label (optional)</source>
        <translation>连线标签（可选）</translation>
    </message>
    <message>
        <location filename="../src/app/main_window.cpp" line="216"/>
        <location filename="../src/app/main_window.cpp" line="497"/>
        <source>Label for the next source-to-target connection</source>
        <translation>下一条源到目标连线的标签</translation>
    </message>
    <message>
        <location filename="../src/app/main_window.cpp" line="219"/>
        <location filename="../src/app/main_window.cpp" line="498"/>
        <source>Delete</source>
        <translation>删除</translation>
    </message>
    <message>
        <location filename="../src/app/main_window.cpp" line="220"/>
        <location filename="../src/app/main_window.cpp" line="499"/>
        <source>Connect: choose source then target</source>
        <translation>连接：依次选择来源和目标</translation>
    </message>
    <message>
        <location filename="../src/app/main_window.cpp" line="222"/>
        <location filename="../src/app/main_window.cpp" line="500"/>
        <source>Cancel connection</source>
        <translation>取消连接</translation>
    </message>
    <message>
        <location filename="../src/app/main_window.cpp" line="228"/>
        <location filename="../src/app/main_window.cpp" line="238"/>
        <location filename="../src/app/main_window.cpp" line="501"/>
        <location filename="../src/app/main_window.cpp" line="504"/>
        <source>Undo</source>
        <translation>撤销</translation>
    </message>
    <message>
        <location filename="../src/app/main_window.cpp" line="229"/>
        <location filename="../src/app/main_window.cpp" line="239"/>
        <location filename="../src/app/main_window.cpp" line="502"/>
        <location filename="../src/app/main_window.cpp" line="505"/>
        <source>Redo</source>
        <translation>重做</translation>
    </message>
    <message>
        <location filename="../src/app/main_window.cpp" line="236"/>
        <location filename="../src/app/main_window.cpp" line="503"/>
        <source>Edit</source>
        <translation>编辑</translation>
    </message>
    <message>
        <location filename="../src/app/main_window.cpp" line="244"/>
        <location filename="../src/app/main_window.cpp" line="507"/>
        <source>Language</source>
        <translation>语言</translation>
    </message>
    <message>
        <location filename="../src/app/main_window.cpp" line="248"/>
        <location filename="../src/app/main_window.cpp" line="508"/>
        <source>English</source>
        <translation>英语</translation>
    </message>
    <message>
        <location filename="../src/app/main_window.cpp" line="252"/>
        <location filename="../src/app/main_window.cpp" line="509"/>
        <source>Chinese</source>
        <translation>中文</translation>
    </message>
    <message>
        <location filename="../src/app/main_window.cpp" line="257"/>
        <location filename="../src/app/main_window.cpp" line="511"/>
        <location filename="../src/app/main_window.cpp" line="512"/>
        <source>Properties</source>
        <translation>属性</translation>
    </message>
    <message>
        <location filename="../src/app/main_window.cpp" line="112"/>
        <source>Edit node</source>
        <translation>编辑节点</translation>
    </message>
    <message>
        <location filename="../src/app/main_window.cpp" line="128"/>
        <source>Save</source>
        <translation>保存</translation>
    </message>
    <message>
        <location filename="../src/app/main_window.cpp" line="131"/>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <location filename="../src/app/main_window.cpp" line="241"/>
        <location filename="../src/app/main_window.cpp" line="506"/>
        <source>View</source>
        <translation>视图</translation>
    </message>
    <message>
        <source>AI</source>
        <translation>AI</translation>
    </message>
    <message>
        <source>AI Settings...</source>
        <translation>AI 设置...</translation>
    </message>
    <message>
        <location filename="../src/app/main_window.cpp" line="267"/>
        <location filename="../src/app/main_window.cpp" line="514"/>
        <source>Apply</source>
        <translation>应用</translation>
    </message>
    <message>
        <location filename="../src/app/main_window.cpp" line="273"/>
        <location filename="../src/app/main_window.cpp" line="516"/>
        <location filename="../src/app/main_window.cpp" line="517"/>
        <source>Build and export</source>
        <translation>构建与导出</translation>
    </message>
    <message>
        <location filename="../src/app/main_window.cpp" line="280"/>
        <location filename="../src/app/main_window.cpp" line="519"/>
        <source>Workspace root containing generated-project</source>
        <translation>包含 generated-project 的工作区根目录</translation>
    </message>
    <message>
        <location filename="../src/app/main_window.cpp" line="289"/>
        <location filename="../src/app/main_window.cpp" line="521"/>
        <source>For example: -G Ninja -DCMAKE_PREFIX_PATH=C:/Qt/6.x/mingw_64</source>
        <translation>示例：-G Ninja -DCMAKE_PREFIX_PATH=C:/Qt/6.x/mingw_64</translation>
    </message>
    <message>
        <location filename="../src/app/main_window.cpp" line="290"/>
        <location filename="../src/app/main_window.cpp" line="522"/>
        <source>Workspace root</source>
        <translation>工作区根目录</translation>
    </message>
    <message>
        <location filename="../src/app/main_window.cpp" line="291"/>
        <location filename="../src/app/main_window.cpp" line="523"/>
        <source>Build directory</source>
        <translation>构建目录</translation>
    </message>
    <message>
        <location filename="../src/app/main_window.cpp" line="292"/>
        <location filename="../src/app/main_window.cpp" line="524"/>
        <source>Empty export directory</source>
        <translation>空导出目录</translation>
    </message>
    <message>
        <location filename="../src/app/main_window.cpp" line="293"/>
        <location filename="../src/app/main_window.cpp" line="525"/>
        <source>CMake executable</source>
        <translation>CMake 可执行文件</translation>
    </message>
    <message>
        <location filename="../src/app/main_window.cpp" line="294"/>
        <location filename="../src/app/main_window.cpp" line="526"/>
        <source>Configure arguments</source>
        <translation>配置参数</translation>
    </message>
    <message>
        <location filename="../src/app/main_window.cpp" line="231"/>
        <location filename="../src/app/main_window.cpp" line="297"/>
        <location filename="../src/app/main_window.cpp" line="527"/>
        <location filename="../src/app/main_window.cpp" line="529"/>
        <source>Build</source>
        <translation>构建</translation>
    </message>
    <message>
        <location filename="../src/app/main_window.cpp" line="233"/>
        <location filename="../src/app/main_window.cpp" line="299"/>
        <location filename="../src/app/main_window.cpp" line="528"/>
        <location filename="../src/app/main_window.cpp" line="530"/>
        <source>Export</source>
        <translation>导出</translation>
    </message>
    <message>
        <location filename="../src/app/main_window.cpp" line="308"/>
        <location filename="../src/app/main_window.cpp" line="531"/>
        <source>Configure, build, and export output appears here.</source>
        <translation>配置、构建和导出的输出显示在这里。</translation>
    </message>
    <message>
        <location filename="../src/app/main_window.cpp" line="321"/>
        <location filename="../src/app/main_window.cpp" line="518"/>
        <source>Reset Layout</source>
        <translation>重置布局</translation>
    </message>
    <message>
        <location filename="../src/app/main_window.cpp" line="329"/>
        <source>Choose source node, then target node</source>
        <translation>选择来源节点，然后选择目标节点</translation>
    </message>
    <message>
        <location filename="../src/app/main_window.cpp" line="369"/>
        <source>[%1] exit code %2
</source>
        <translation>[%1] 退出码 %2
</translation>
    </message>
    <message>
        <location filename="../src/app/main_window.cpp" line="374"/>
        <source>Build finished successfully.
</source>
        <translation>构建成功完成。
</translation>
    </message>
    <message>
        <location filename="../src/app/main_window.cpp" line="375"/>
        <source>Build failed: %1
</source>
        <translation>构建失败：%1
</translation>
    </message>
    <message>
        <location filename="../src/app/main_window.cpp" line="552"/>
        <source>Build request rejected: workspace and build paths must not be empty.
</source>
        <translation>构建请求被拒绝：工作区和构建路径不能为空。
</translation>
    </message>
    <message>
        <location filename="../src/app/main_window.cpp" line="561"/>
        <source>Starting configure for %1
</source>
        <translation>开始为 %1 执行配置
</translation>
    </message>
    <message>
        <location filename="../src/app/main_window.cpp" line="567"/>
        <source>Build request rejected: %1
</source>
        <translation>构建请求被拒绝：%1
</translation>
    </message>
    <message>
        <location filename="../src/app/main_window.cpp" line="577"/>
        <source>Export finished successfully.
</source>
        <translation>导出成功完成。
</translation>
    </message>
    <message>
        <location filename="../src/app/main_window.cpp" line="580"/>
        <source>Export failed: %1
</source>
        <translation>导出失败：%1
</translation>
    </message>
</context>
<context>
    <name>NodePropertiesEditor</name>
    <message>
        <location filename="../src/editor/node_properties_editor.cpp" line="181"/>
        <location filename="../src/editor/node_properties_editor.cpp" line="184"/>
        <source>Name</source>
        <translation>名称</translation>
    </message>
    <message>
        <location filename="../src/editor/node_properties_editor.cpp" line="182"/>
        <location filename="../src/editor/node_properties_editor.cpp" line="184"/>
        <source>Description</source>
        <translation>描述</translation>
    </message>
    <message>
        <location filename="../src/editor/node_properties_editor.cpp" line="184"/>
        <source>Type</source>
        <translation>类型</translation>
    </message>
    <message>
        <location filename="../src/editor/node_properties_editor.cpp" line="187"/>
        <source>Text</source>
        <translation>文本</translation>
    </message>
    <message>
        <location filename="../src/editor/node_properties_editor.cpp" line="191"/>
        <source>Inputs</source>
        <translation>输入</translation>
    </message>
    <message>
        <location filename="../src/editor/node_properties_editor.cpp" line="192"/>
        <source>Outputs</source>
        <translation>输出</translation>
    </message>
    <message>
        <location filename="../src/editor/node_properties_editor.cpp" line="193"/>
        <source>Constraints</source>
        <translation>约束</translation>
    </message>
    <message>
        <location filename="../src/editor/node_properties_editor.cpp" line="194"/>
        <source>Acceptance Criteria</source>
        <translation>验收标准</translation>
    </message>
    <message>
        <location filename="../src/editor/node_properties_editor.cpp" line="195"/>
        <source>Add input</source>
        <translation>添加输入</translation>
    </message>
    <message>
        <location filename="../src/editor/node_properties_editor.cpp" line="196"/>
        <source>Add output</source>
        <translation>添加输出</translation>
    </message>
    <message>
        <location filename="../src/editor/node_properties_editor.cpp" line="197"/>
        <source>Add constraint</source>
        <translation>添加约束</translation>
    </message>
    <message>
        <location filename="../src/editor/node_properties_editor.cpp" line="198"/>
        <source>Add criterion</source>
        <translation>添加验收标准</translation>
    </message>
    <message>
        <location filename="../src/editor/node_properties_editor.cpp" line="184"/>
        <location filename="../src/editor/node_properties_editor.cpp" line="187"/>
        <location filename="../src/editor/node_properties_editor.cpp" line="199"/>
        <location filename="../src/editor/node_properties_editor.cpp" line="200"/>
        <location filename="../src/editor/node_properties_editor.cpp" line="201"/>
        <location filename="../src/editor/node_properties_editor.cpp" line="202"/>
        <location filename="../src/editor/node_properties_editor.cpp" line="212"/>
        <location filename="../src/editor/node_properties_editor.cpp" line="230"/>
        <source>Delete</source>
        <translation>删除</translation>
    </message>
</context>
<context>
    <name>QObject</name>
    <message>
        <source>Input: %1</source>
        <translation>输入：%1</translation>
    </message>
    <message>
        <source>Output: %1</source>
        <translation>输出：%1</translation>
    </message>
    <message>
        <location filename="../src/editor/node_item.cpp" line="58"/>
        <source>Type: %1</source>
        <translation>类型：%1</translation>
    </message>
    <message>
        <location filename="../src/ui/node_type_display.cpp" line="9"/>
        <source>Start</source>
        <translation>开始</translation>
    </message>
    <message>
        <location filename="../src/ui/node_type_display.cpp" line="11"/>
        <source>End</source>
        <translation>结束</translation>
    </message>
    <message>
        <location filename="../src/ui/node_type_display.cpp" line="13"/>
        <source>UI Page</source>
        <translation>界面页面</translation>
    </message>
    <message>
        <location filename="../src/ui/node_type_display.cpp" line="15"/>
        <source>Logic Module</source>
        <translation>逻辑模块</translation>
    </message>
    <message>
        <location filename="../src/ui/node_type_display.cpp" line="17"/>
        <source>Decision</source>
        <translation>判断</translation>
    </message>
    <message>
        <location filename="../src/ui/node_type_display.cpp" line="19"/>
        <source>External Code</source>
        <translation>外部代码</translation>
    </message>
    <message>
        <location filename="../src/app/main_window.cpp" line="82"/>
        <source>configure</source>
        <translation>配置</translation>
    </message>
    <message>
        <location filename="../src/app/main_window.cpp" line="82"/>
        <source>build</source>
        <translation>构建</translation>
    </message>
    <message>
        <location filename="../src/editor/node_item.cpp" line="164"/>
        <source>Input</source>
        <translation>输入</translation>
    </message>
    <message>
        <location filename="../src/editor/node_item.cpp" line="166"/>
        <source>Output</source>
        <translation>输出</translation>
    </message>
</context>
</TS>
