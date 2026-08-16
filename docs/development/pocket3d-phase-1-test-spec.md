# Pocket3D Phase 1 Test Spec

状态：Accepted  
版本：2.0  
确定日期：2026-08-16  
适用阶段：[Pocket3D Phase 1：Cross-platform OpenGL 3D](pocket3d-phase-1-cross-platform-opengl.md)  
机器可读目录：[phase1-test-catalog.json](../../tests/pocket3d/fixtures/phase1-test-catalog.json)

## 1. 目的与权威

本文把 Phase 1 的 OpenGL 架构决策转换为稳定、可执行、可追踪的测试契约。主规格定义产品行为；本文定义验证语义和环境；JSON catalog 镜像 case metadata；C++ runner 实现 oracle。

任何有意行为变更必须同时更新主规格、本文、catalog 和对应测试。不得通过放宽容差、修改 golden 或把失败改成 skip 获得绿色结果。

## 2. 状态

Catalog 的 `execution` 只有三种状态：

| 状态 | 含义 |
|---|---|
| `active` | 已注册到 CTest，要求环境中的构建必须通过 |
| `planned` | 尚未交付；不得用永远成功的占位测试代替 |
| `manual` | 必须保存人工操作 receipt 和图窗证据 |

当前切片已经交付跨平台共享代码、Linux/WSLg OpenGL 实证、组件与提取层。Windows、macOS、3D editor authoring 和 typed property 仍是 `planned`；因此当前实现不是整个 Phase 1 Complete。

## 3. 环境

| Environment | 用途 | 最低要求 |
|---|---|---|
| `portable` | config、数学、组件、SceneFormat、extraction | 任意受支持 C++17 平台 |
| `linux-software` | 可重复 OpenGL context、draw、readback | Linux + SDL2 + Mesa OpenGL 4.1 Core；llvmpipe 可用 |
| `windows-opengl` | Windows host、硬件截图、生命周期 | Windows x64 + OpenGL 4.1 driver |
| `macos-opengl` | macOS 兼容验证 | macOS + 系统 OpenGL 4.1 Core |
| `editor-linux` | 当前 editor 自动化与人工流程 | Linux 可交互桌面 |
| `editor-windows` | Windows editor 人工流程 | Windows 可交互桌面 |
| `editor-macos` | macOS editor 人工流程 | macOS 可交互桌面 |

指定图形环境缺失时，runner 可以返回 CTest skip code 77，但 milestone 退出前必须在对应环境得到一次真实通过。Windows/macOS 失败不得静默降级到 SDL Renderer 或另一个 API。

## 4. 当前 runners

| CTest runner | Active case IDs | 验证内容 |
|---|---|---|
| `pocket3d_phase1_spec` | `P1-SPEC-001` | 文档、catalog、固定值和 case registry |
| `pocket3d_phase1_math_spec` | `P1-MATH-001`–`P1-MATH-005` | 坐标、TRS、ZO depth、ray、winding 独立 oracle |
| `pocket3d_rendering_mode_contract` | `P1-CFG-001`–`P1-CFG-003` | legacy 2D 默认、严格模式解析、3D OpenGL 选择 |
| `pocket3d_component_contract` | `P1-TRS-001`、`P1-TRS-002`、`P1-TRS-004`、`P1-CAM-001`、`P1-CAM-002`、`P1-EXT-001`、`P1-DRAW-002` | Transform3D、Camera3D、MeshRenderer、层级和 pointer-free extraction |
| `pocket3d_opengl_smoke` | `P1-HOST-001`、`P1-DRAW-001`、`P1-SHADER-001`、`P1-GFX-001` | SDL OpenGL 4.1 context、GLSL 410、indexed Cube、depth 和 readback |

## 5. Case registry

### P1.0：规格、数学与模式

| Case | Execution | Oracle 摘要 |
|---|---|---|
| `P1-SPEC-001` | active / portable | Markdown、catalog、固定架构值和 case IDs 一致 |
| `P1-MATH-001` | active / portable | 右手基和正 Y quaternion 旋转正确 |
| `P1-MATH-002` | active / portable | `parent * T*R*S` 得到固定 child world point |
| `P1-MATH-003` | active / portable | RH_ZO near/far 映射 0/1 |
| `P1-MATH-004` | active / portable | 默认 Camera 中心 ray 指向 `-Z` |
| `P1-MATH-005` | active / portable | 默认正面三角形投影为 CCW |
| `P1-CFG-001` | active / portable | 缺少 `rendering_mode` 保持 2D |
| `P1-CFG-002` | active / portable | 只接受 `2d`/`3d` |
| `P1-CFG-003` | active / portable | `3d` 选择统一 OpenGL host |
| `P1-CFG-004` | planned / editor | 活动 graphics mode 不热切换 |

### P1.1：OpenGL host

| Case | Execution | Oracle 摘要 |
|---|---|---|
| `P1-HOST-001` | active / linux-software | 4.1+ Core context 与 private loader 初始化成功 |
| `P1-HOST-002` | planned | Runtime/Scene/ImGui/Swap 共用唯一 context |
| `P1-HOST-003` | planned | 初始化 fault injection 后资源全清且诊断稳定 |
| `P1-HOST-004` | planned | resize/minimize/restore 同步 drawable viewport |
| `P1-HOST-005` | planned | 重复 initialize/shutdown 无泄漏或 double delete |
| `P1-HOST-006` | planned | debug callback 无 HIGH severity 错误 |

### P1.2：组件、场景与 Camera

| Case | Execution | Oracle 摘要 |
|---|---|---|
| `P1-TRS-001` | active / portable | identity 默认值与 quaternion normalize |
| `P1-TRS-002` | active / portable | hierarchy 与数学 oracle 一致 |
| `P1-TRS-003` | planned | reparent keep-world 与失败原子性 |
| `P1-TRS-004` | active / portable | 拒绝双 Transform 与跨维 parenting |
| `P1-PROP-001` | planned | typed value 无损 JSON round-trip |
| `P1-PROP-002` | planned | Mutation/runtime/bridge 保留 typed value |
| `P1-CAM-001` | active / portable | perspective/orthographic 与参数验证 |
| `P1-CAM-002` | active / portable | primary Camera 选择和诊断稳定 |

### P1.3：提取、绘制、Shader 与资源

| Case | Execution | Oracle 摘要 |
|---|---|---|
| `P1-EXT-001` | active / portable | value-only command，按 scene/component 稳定排序 |
| `P1-EXT-002` | planned | editor Camera 不改变 Runtime RenderView |
| `P1-DRAW-001` | active / linux-software | indexed Cube、depth test/write 和可见 readback |
| `P1-DRAW-002` | active / portable | parent TRS 进入 child world matrix |
| `P1-DRAW-003` | planned | 缺失资源 placeholder 与 warning 去重 |
| `P1-DRAW-004` | planned | resize 同步 viewport/projection/readback |
| `P1-SHADER-001` | active / linux-software | GLSL 410 编译链接及固定 uniform contract |
| `P1-RES-001` | planned | slot+generation 拒绝 stale handle |

CPU extraction 与 OpenGL execution 必须分别测试。最终截图不能替代 command purity、排序和错误诊断测试。

### P1.4：3D editor authoring

| Case | Execution | Oracle 摘要 |
|---|---|---|
| `P1-EDIT-001` | planned | ray/AABB 最近命中与 UID 决胜 |
| `P1-EDIT-002` | planned | gizmo 只写 SceneEditCommand |
| `P1-EDIT-003` | planned | world/local 与 keep-world reparent |
| `P1-EDIT-004` | planned | save/close/reopen 数据和画面一致 |
| `P1-EDIT-005` | planned | Play 修改不污染 authoring document |
| `P1-EDIT-006` | planned | Scene Camera 不产生 scene mutation |
| `P1-EDIT-007` | planned | reorder/duplicate/play 后 UID 选择正确 |

### P1.5：三端、生命周期与兼容

| Case | Execution | Oracle 摘要 |
|---|---|---|
| `P1-GFX-001` | active / linux-software | Mesa fixed-size PPM readback 有效 |
| `P1-GFX-002` | planned / windows-opengl | Windows hierarchy screenshot |
| `P1-GFX-003` | planned / macos-opengl | macOS 4.1 hierarchy screenshot |
| `P1-LIFE-001` | planned | reload/resize/reopen 资源回到稳定上限 |
| `P1-COMPAT-001` | planned | Default ballgame 2D 基线不变 |
| `P1-COMPAT-002` | planned | 2D/3D 切换只保留一个 graphics owner |
| `P1-ACCEPT-001` | manual / three editors | 三端完整创作流程与 receipt |

## 6. 数值、图像与证据

- 矩阵、方向和 world point 默认 absolute tolerance 为 `1e-4`。
- quaternion 比较接受 `q` 与 `-q` 表示同一旋转。
- Linux software golden 固定分辨率、clear color、Camera 和内建 Cube。
- Shader 中只允许在 clip-space 出口执行 `clip.z = 2 * clip.z - clip.w`；CPU 投影保持 `[0,1]`。
- 图窗 evidence 必须同时包含 backbuffer readback 和可见窗口截图；截图记录窗口标题，receipt 记录 Mesa/adapter/driver、分辨率、命令和结果。
- Golden 更新需要 before/after、原因和 case ID。

## 7. WSL 执行

```bash
cmake --preset unix-makefiles-debug
cmake --build --preset unix-makefiles-debug -j4
ctest --test-dir build/unix-makefiles-debug --output-on-failure

POCKET_PROJECT_ROOT=Projects/Pocket3D \
POCKET3D_TEST_FRAMES=3 \
POCKET3D_CAPTURE_PATH=tests/evidence/pocket3d-opengl/wsl-backbuffer.ppm \
./build/unix-makefiles-debug/game
```

`tests/pocket3d/capture_windows_window.ps1` 从 Windows 桌面合成层截取 WSLg 可见窗口；这是因为 X11 `import` 不能可靠读取 WSLg 的 OpenGL 合成表面。

## 8. Milestone gate

一个 milestone 只有在以下条件全部满足时退出：

1. 本 milestone 的 `planned` cases 已转为真实 `active` 测试；
2. 每个要求环境均有通过记录；
3. 当前及之前 milestone 的 active tests 全部通过；
4. 无扩大容差、忽略 GL debug error 或静默 fallback；
5. 文档、catalog、test source case IDs 一致；
6. manual cases 有完整 receipt。

CTest 全绿但 Windows/macOS/editor cases 尚为 planned，不能标记整个 Phase 1 Complete。
