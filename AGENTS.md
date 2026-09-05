# 注意事项
- 每次改动完成后，都必须创建一个对应的git commit，以便后续追踪回滚
- 每次改动后，都必须编写或更新相关测试，并在交付给用户前，确保所有测试和验证全部通过
- 每个 Task 完成并创建提交后，必须将当前开发分支同步到对应的远端仓库；不得强制推送或改写远端历史，同步受阻时必须记录 Issue 并在进度文档中说明
- `doc/multilanguage-development-design.md` 是 MVP 完成后的后续规划；当前 Task 1 至 Task 10 仍以 `doc/mvp-implementation-plan.md` 为唯一实施基线
- 每个 Task 完成后的提交都需要同步更新 README
- 从 Task 6 开始，每个 Task 必须从最新远端 `main` 新建独立开发分支；开始依赖前序 Task 的工作前，先确认前序成果已合入 `main`。
