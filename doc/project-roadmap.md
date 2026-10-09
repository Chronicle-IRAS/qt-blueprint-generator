# 项目方向与后续路线

项目目标是让用户通过蓝图描述软件结构，由 AI 生成可以构建、运行的 Qt 应用原型，再通过行级代码评审逐步修改和验证。工作区服务于生成、评审和返工，不以完整 IDE 为目标。

目前已有蓝图编辑与工程持久化（含节点布局）、ExternalCode GUI 导入与验证、模块源码生成、候选审核、Workspace Editor 安全编辑、构建和导出。应用入口仍是占位窗口，行级评论和 AI 返工闭环尚未实现。

## 后续阶段

1. **蓝图持久化（已完成）**：复用 BlueprintSerializer，GUI 支持新建、打开、保存、另存为和未保存提示；语义与节点布局分别保存。
2. **可运行原型生成**：由确定性生成器负责应用入口和模块装配，AI 负责模块实现。先支持 `Start → LogicModule/UiPage → End`，再逐步支持 Decision，使生成应用具有实际功能。
3. **AI Code Review Workspace**：以源码浏览、行或行范围评论、评审意见汇总及增删改、代码差异为主要交互，保留手动编辑作为辅助。参考 GitHub PR Review，不引入完整 Git UI、LSP 或 IDE 功能。
4. **AI 返工闭环**：代码评论汇总为修改要求，经 AI 生成新 Candidate，人工 Diff 审核与 Accept/Reject 后再 Build 验证，继续评论或 Export。复用 AI Client、Candidate Review、manifest、hash 和文件保护机制；AI 不得未经确认覆盖正式工程。

Issue #32 剩余的 Candidate Accept 与工作区集成，在评审和返工阶段收口。每阶段应单独确认范围、实现并审核，不因本路线提前重构；当前不引入 React、Electron、Monaco 或 Agent 框架。
