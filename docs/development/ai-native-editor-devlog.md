# PocketEngine AI 原生编辑器 Devlog

状态：Implementation  
开始日期：2026-07-21  
关联规格：[AI 原生编辑器实现规格](ai-native-editor-spec.md)

## 维护规则

- 本文是持续更新的工程日志，不替代实现规格。
- 新日志追加在“开发记录”顶部，日期使用 `YYYY-MM-DD`。
- 每条记录包含目标、完成内容、验证、问题和下一步。
- 影响架构或兼容性的决定记录到“决策记录”。
- 尚未验证的想法标记为“假设”，不要写成已完成事实。
- 阶段状态只使用：`Not started`、`In progress`、`Blocked`、`Done`。

## 当前状态

| 项目 | 状态 |
|---|---|
| 总体 | In progress |
| Phase 0：协议与测试骨架 | Done |
| Phase 1：只读 AI 会话 | In progress |
| Phase 2：ChangeSet 场景编辑 | Not started |
| Phase 3：游戏观察与验证 | Not started |
| Phase 4：Lua 和项目文件事务 | Not started |

当前下一步：完成 Codex/Claude 真实云端手动验收；代码实现完成后进入 Phase 2 ChangeSet。

## 成功标准

项目完成首个重要里程碑时，用户应当可以：

1. 在 PocketEngine 中打开 AI Assistant。
2. 选择 Codex 或 Claude Code。
3. 用自然语言描述一个场景修改。
4. 查看结构化修改预览。
5. 一次确认后应用全部修改。
6. 在 Hierarchy、Inspector 和 Runtime Preview 中看到结果。
7. 一次 Undo 撤销整次 AI 修改。

## 路线图

### Phase 0：协议与测试骨架

- [x] 建立 `tools/pocket-agent-host`。
- [x] 定义 Editor Bridge protocol v1。
- [x] C++ sidecar 进程启动与关闭。
- [x] 双向 JSONL reader/writer。
- [x] 线程安全 Editor 事件队列。
- [x] 定义统一 `AgentEvent`。
- [x] Fake Agent adapter。
- [x] 在编辑器中显示 Fake Agent 流式事件。
- [x] 建立 protocol fixture tests。

### Phase 1：只读 AI 会话

- [x] 新增 `AIAssistantPanel` 基础界面。
- [x] Agent provider 选择和探测 UI。
- [x] Codex CLI adapter。
- [x] Claude Code adapter。
- [x] PocketEngine MCP Server。
- [x] MCP bearer token 和 loopback 限制。
- [x] `get_editor_state`。
- [x] `get_current_scene`。
- [x] `inspect_actor`。
- [x] `list_component_types`。
- [x] `search_assets`。
- [x] `pocketengine-scene-authoring` Skill 初稿。

### Phase 2：ChangeSet 场景编辑

- [ ] 定义 `AIChangeSet` 和状态机。
- [ ] `begin_change_set`。
- [ ] `stage_scene_mutations`。
- [ ] 文档副本验证。
- [ ] `preview_change_set`。
- [ ] ChangeSet 确认 modal。
- [ ] 原子提交。
- [ ] Runtime Mirror 增量同步和 full reload fallback。
- [ ] 全编辑器 Undo/Redo 或 AI 专用事务 Undo。
- [ ] Codex/Claude 跨 Agent 验收。

### Phase 3：游戏观察与验证

- [ ] 统一 `DiagnosticManager`。
- [ ] Lua 错误迁移到结构化 diagnostics。
- [ ] Runtime snapshot。
- [ ] 单帧/多帧推进。
- [ ] Runtime event trace。
- [ ] Viewport 截图工具。
- [ ] SceneView 截图工具。
- [ ] 自动运行验证工作流。

### Phase 4：Lua 和项目文件事务

- [ ] 文件 patch 数据结构。
- [ ] 文件变更预览。
- [ ] 文件冲突检测。
- [ ] Lua 语法验证。
- [ ] Lua component reload 验证。
- [ ] 场景与文件跨资源原子事务。
- [ ] `pocketengine-lua-component` Skill。

## 决策记录

### ADR-001：编辑器是唯一状态所有者

日期：2026-07-21  
状态：Accepted

决定：MCP Server 不直接解析和修改 `.scene` 文件。所有查询与写入经 Editor Bridge 到 `SceneDocument`、`EditorSceneSession` 和 `Engine`。

原因：

- 保持 Inspector、Hierarchy、Runtime Mirror 与 AI 结果一致。
- 复用当前 scene format 和 mutation 规则。
- 避免外部文件写入绕过 dirty、save、play sandbox 和 UID 管理。

### ADR-002：Agent 使用 Adapter 接入

日期：2026-07-21  
状态：Accepted

决定：Codex、Claude Code 和未来 Agent 实现统一 Adapter，原始供应商事件只存在于 sidecar 内。

原因：

- 防止 UI 和 Editor Core 依赖供应商协议。
- 可以独立处理版本探测、认证、取消和 session resume。
- 允许 Fake Agent 驱动离线集成测试。

### ADR-003：MVP 使用外部 CLI Agent

日期：2026-07-21  
状态：Accepted

决定：MVP 使用用户本机安装并登录的 Codex CLI 与 Claude Code CLI。暂不直接在编辑器内管理模型 API Key。

原因：

- 复用现有 Agent loop、登录和会话能力。
- 缩短首个端到端版本路径。
- 避免第一阶段同时实现供应商 API、密钥存储和模型选择器。

### ADR-004：MCP 是能力协议，Skill 是工作流

日期：2026-07-21  
状态：Accepted

决定：查询、修改、运行和截图能力通过 MCP Tool 提供；PocketEngine 的正确操作顺序通过 Skill 描述。

原因：Skill 无法代替 schema 校验、权限、事务和执行结果；MCP Tool 也不适合承载完整的领域工作流指导。

### ADR-005：所有 AI 写入属于 ChangeSet

日期：2026-07-21  
状态：Accepted

决定：Agent 不获得立即写入式场景工具。它只能暂存 mutation，随后 validate、preview 并请求 commit。

原因：

- 为用户提供明确控制权。
- 支持一次性 Undo。
- 支持跨场景 mutation 和未来文件 patch 的统一事务。
- 便于记录、测试和恢复失败操作。

### ADR-006：MVP sidecar 使用 TypeScript

日期：2026-07-21  
状态：Provisional

决定：开发阶段使用 TypeScript/Node.js 20 实现 `pocket-agent-host`。发布打包方式后续评估。

原因：

- MCP、HTTP、JSONL 和子进程库成熟。
- Codex/Claude 事件适配迭代速度快。
- sidecar 不进入游戏 Runtime，不影响最终游戏包依赖。

重新评估条件：

- Node 发布体积或跨平台安装成为明显负担。
- 官方稳定的跨平台 Agent SDK 更适合单文件分发。
- C++ MCP 实现达到相同开发和维护成本。

## 风险登记

| ID | 风险 | 影响 | 当前缓解措施 |
|---|---|---|---|
| R-001 | Codex/Claude CLI JSON 协议变化 | Adapter 失效 | capability probe、fixture、供应商隔离 |
| R-002 | Agent 绕过 MCP 直接写文件 | 破坏 Undo 和一致性 | 默认只读、禁用 Edit/Write/Bash |
| R-003 | MCP 请求阻塞编辑器主线程 | UI 卡顿 | 后台 IO、主线程任务队列、超时 |
| R-004 | ChangeSet 部分提交 | 场景损坏 | 文档副本验证、原子 apply/rollback |
| R-005 | 大场景上下文过大 | 成本和延迟增加 | 摘要、分页、按 UID 查询、truncated 标志 |
| R-006 | Agent 云端数据边界不清晰 | 用户隐私风险 | 首次提示、provider 状态、最小上下文 |
| R-007 | sidecar 发布依赖 Node | 安装体验变差 | MVP 后评估单文件打包或替代实现 |
| R-008 | Runtime 非确定性导致 AI 误判 | 验证不稳定 | 固定步数、结构化快照、事件 trace |

## 开发记录

### 2026-07-27：Phase 1 只读链路实现完成

目标：让 Codex 与 Claude Code 通过统一的 PocketEngine MCP，只读理解编辑器实时场景，而不获得场景或文件写权限。

完成：

- 实现 Codex CLI adapter：`codex exec --json` 事件归一化、thread resume、取消/关闭、`read-only` sandbox 和单次 MCP 配置覆盖。
- 实现 Claude Code adapter：`--print --output-format stream-json` 事件归一化、session resume、取消/关闭、禁用内置工具并只允许五个 PocketEngine MCP 工具。
- `agent.probe` 现在探测安装版本和认证状态；本机探测到 Codex `0.144.6` 已登录、Claude Code `2.1.211` 未登录。
- 使用锁定的 `@modelcontextprotocol/sdk@1.29.0` 实现 Streamable HTTP MCP server。
- MCP 只绑定 `127.0.0.1` 随机端口，启用 Host/Origin 校验和每次 sidecar 生命周期随机 bearer token；token 仅通过环境变量注入 Agent 子进程。
- MCP 请求通过 Editor Bridge 投递到编辑器主线程，从实时 `SceneDocument`、组件注册表和项目根目录执行。
- 实现 `get_editor_state`、`get_current_scene`、`inspect_actor`、`list_component_types`、`search_assets`，全部标记 read-only。
- AI Assistant 增加 provider 选择、版本/认证/MCP 状态、重新探测和通用只读会话入口。
- 创建并验证仓库规范源 `.agents/skills/pocketengine-scene-authoring`；Phase 1 工作流明确禁止直接编辑 `.scene`。
- 新增 Codex/Claude fixture 事件测试、MCP bearer/schema 测试和真实默认场景 C++ 工具测试。

验证：

- 命令：`npm --prefix tools/pocket-agent-host test`
- 结果：8/8 通过，包括 MCP 正反向认证、五工具发现、sidecar JSONL 生命周期和两家事件归一化。
- 命令：`cmake --build build/macos-ninja-debug -j4`
- 结果：编辑器、sidecar 与全部测试目标构建成功。
- 命令：`ctest --test-dir build/macos-ninja-debug --output-on-failure`
- 结果：3/3 通过，包括 C++ sidecar smoke、默认 `ballgame.scene` 的只读工具执行和稳定的 `ACTOR_NOT_FOUND`。
- Skill `quick_validate.py`：通过。
- Codex 临时 MCP TOML 配置使用本机 CLI 解析验证通过，未写入用户全局配置。

问题与假设：

- 本轮没有主动发起会产生模型用量的真实云端 turn。
- 本机 Claude Code 尚未认证，因此“两个真实 Agent 都解释当前场景”的最终人工退出条件仍待用户登录后验收。
- Windows/Linux 尚未运行 Phase 1 实机验证；进程层沿用 Phase 0 的跨平台实现。
- npm audit 报告 2 个关联的 moderate 项：MCP SDK 传递依赖 `@hono/node-server` 的 Windows `serve-static` encoded-backslash path traversal。PocketEngine 未使用 Hono 或 `serve-static`，MCP 只提供固定 `/mcp` 路由，因此当前路径不受影响；上游尚未给 `1.29.0` 提供兼容修复，未降级 SDK 或执行 `audit fix --force`。

下一步：

1. 用户完成 `claude auth login` 后，分别用 Codex/Claude 询问同一默认场景，确认都只调用 PocketEngine MCP。
2. 记录真实 Agent 事件差异，必要时补 fixture。
3. 开始 Phase 2：`AIChangeSet`、验证/预览、确认和统一 Undo。

### 2026-07-21：Phase 0 完成

目标：在不连接真实云端 Agent 的情况下，打通 PocketEngine 编辑器到 sidecar、Fake Agent，再返回 ImGui 面板的完整流式事件链路。

完成：

- 建立 TypeScript `pocket-agent-host` 工程和 protocol v1。
- 实现请求、响应、事件三类 JSONL 消息的校验和序列化。
- 实现确定性的 Fake Agent Adapter，支持 session、流式 delta、取消和关闭。
- 实现 macOS/Linux `fork/exec + pipe` 和 Windows `CreateProcess + Pipe` 的 C++ sidecar 进程桥。
- 实现后台 reader thread、线程安全事件队列和主线程事件消费。
- 实现统一 `AgentEvent` 与 `AIEditorService` 状态机。
- 新增 `AI Assistant` 面板，可启动 Fake Agent、发送消息、观察流式回复、停止 turn 和查看协议事件。
- 项目切换和编辑器退出时会有序关闭 Agent 与 sidecar。
- 本地已安装 npm 依赖时，CMake 自动重编译 sidecar；未安装时不阻断引擎构建。
- 新增 TypeScript fixture/integration tests 和 C++ protocol/service smoke tests。

验证：

- 命令：`npm --prefix tools/pocket-agent-host test`
- 结果：5/5 通过，包括真实 sidecar 子进程 JSONL turn。
- 命令：`cmake --build build/macos-ninja-debug -j4`
- 结果：`pocket` 和测试目标构建成功。
- 命令：`ctest --test-dir build/macos-ninja-debug --output-on-failure`
- 结果：2/2 通过，包括 C++ 启动 sidecar 并完成 Fake Agent 流式会话。

问题与假设：

- Windows 代码已编译路径隔离，但本轮尚未在 Windows 主机运行验证。
- sidecar 当前只有 Fake Agent；真实 provider 探测和 MCP 属于 Phase 1。
- `dist/` 和 `node_modules/` 不进入版本控制，开发者首次使用需要执行 `npm install`。

下一步：

1. 实现 `agent.probe` 的 Codex/Claude Code 实际探测。
2. 增加 Provider 选择器和安装/认证状态。
3. 启动只读 PocketEngine MCP Server，先实现 `get_editor_state` 和 `get_current_scene`。

### 2026-07-21：完成初始架构与实现规格

目标：明确 PocketEngine AI 原生编辑器的接入方式，以及 Agent 如何读取和操作游戏。

完成：

- 确定采用 Agent Adapter 支持 Codex 和 Claude Code。
- 确定 PocketEngine MCP 作为供应商无关能力边界。
- 区分 MCP 与 Skill：MCP 提供工具，Skill 提供工作流。
- 确定语义、时间和视觉三层游戏感知模型。
- 确定 AI 写操作使用 ChangeSet、预览、确认和统一 Undo。
- 编写首版实现规格和开发路线图。

验证：

- 当前 PocketEngine 已有 `SceneDocument`、`SceneEditCommand` 和 Runtime Mirror，可以作为 ChangeSet 的内部执行基础。
- Codex 和 Claude Code 均具备非交互 JSON 事件流及 MCP 扩展路径。

问题与假设：

- sidecar 发布打包方式尚未决定。
- 当前仓库没有统一 Editor Undo/Redo，需要在 Phase 2 前决定实现范围。
- Runtime diagnostics 仍有大量 `std::cout` 路径，需要分阶段迁移。

下一步：

1. 创建 `tools/pocket-agent-host` 骨架。
2. 定义并锁定 Editor Bridge protocol v1 fixtures。
3. 实现 Fake Agent，先打通无云依赖的流式消息链路。

## 日志模板

```markdown
### YYYY-MM-DD：标题

目标：

完成：

- ...

验证：

- 命令：`...`
- 结果：...

问题与假设：

- ...

下一步：

1. ...
```
