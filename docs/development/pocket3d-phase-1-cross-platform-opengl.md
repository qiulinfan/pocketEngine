# Pocket3D Phase 1：Cross-platform OpenGL 3D Rendering

状态：Accepted  
版本：2.0  
目标平台：Windows x64、macOS、Linux  
参考实现：SDL2 + OpenGL 4.1 Core + GLSL 410

测试合同见：[Pocket3D Phase 1 Test Spec](pocket3d-phase-1-test-spec.md)。长期语言与多后端路线见：[Pocket3D Development Plan](pocket3d-development-plan.md)。

## 1. 产品目标

Phase 1 的唯一产品目标是：在现有 PocketEngine Runtime 与 Editor 中交付一套相同的 OpenGL 3D 路径，使同一个项目、场景和 Shader 在 Windows、macOS、Linux 上得到一致结果。

完成后的最小闭环必须允许用户：

1. 创建或打开 `rendering_mode = "3d"` 项目；
2. 创建带 `Transform3D`、`Camera3D`、`MeshRenderer` 的 Actor；
3. 在 Scene View 中观察、选择并编辑 Cube；
4. 保存场景并进入 Play；
5. Runtime View 使用游戏 Camera 渲染相同层级；
6. 在三端 resize、暂停、停止和退出，没有持续增长的 OpenGL 资源或未处理 GL error。

Phase 1 不是未来 RHI 的最终形态。它首先建立一个真实、可移植、可测试的 3D 行为基线，随后 Windows 与 macOS 才分别发展 Direct3D 12 与 Metal 后端。

## 2. 范围

### 2.1 In scope

- Windows、macOS、Linux 的现有 CMake 构建入口；
- SDL2 OpenGL window/context 创建、交换、resize 和 HiDPI drawable size；
- OpenGL 4.1 Core function loading、debug diagnostics 和显式状态设置；
- color/depth framebuffer、VAO、VBO、32-bit IBO、UBO 和 indexed draw；
- GLSL 410 vertex/fragment Shader；
- 单线程 render extraction 与提交；
- `Transform3D`、`Camera3D`、`MeshRenderer` 原生内建组件；
- 内建 Cube Mesh、Unlit Color Material 和缺失资源占位；
- Runtime View 与 Scene View 两个独立 framebuffer；
- Dear ImGui SDL2 platform backend + OpenGL3 renderer backend；
- Scene View orbit、pan、dolly、picking 和 TRS gizmo；
- 场景格式、Inspector、LuaBridge 与 EditorBridge 的 3D 属性往返；
- 一个可跟踪的 `Projects/Pocket3D` 验收项目；
- portable 单测、Linux software readback 和三端真实窗口验收。

### 2.2 Out of scope

- Direct3D 11、Direct3D 12、Metal、Vulkan 或 WebGPU；
- 通用 RHI、render graph、渲染线程或 job system；
- Python runtime 或 Lua 替换；
- PBR、阴影、透明排序、后处理、动画、skin、terrain；
- 3D Rigidbody、Collider 或其他 3D physics；
- glTF/FBX production importer；
- 在 Phase 1 同时维护多个 3D graphics backend。

## 3. 固定技术决策

### 3.1 OpenGL baseline

三端统一请求：

- OpenGL 4.1 Core Profile；
- GLSL `#version 410 core`；
- forward-compatible context；
- double buffer；
- 24-bit depth，8-bit stencil；
- Debug 构建在平台允许时请求 debug context；
- vertical sync 由明确配置控制，不依赖驱动默认值。

Context 创建后必须查询实际 GL/GLSL version、profile、vendor 和 renderer。低于 4.1 Core 时启动失败并返回 `opengl.unsupported_context`，不得静默切换 compatibility profile。

macOS 的 OpenGL 已被 Apple 弃用，因此这里是有边界的启动方案：macOS OpenGL 实机测试是 Phase 1 release gate；Metal 后端成为后续路线的退出条件。上层场景和 extraction 不得保存 OpenGL 对象，以避免将这个临时平台实现固化成数据模型。

### 3.2 SDL ownership

SDL2 继续负责：

- 窗口与事件；
- 输入、controller、audio 和现有平台集成；
- `SDL_GL_CreateContext`、`SDL_GL_MakeCurrent`、`SDL_GL_SwapWindow`；
- drawable size 查询。

`rendering_mode = "3d"` 时不得创建 `SDL_Renderer`。OpenGL context 是 host window 唯一的图形所有者，OpenGL 绘制 Runtime/Scene framebuffer、ImGui 和最终 backbuffer，并且每帧只有一次 `SDL_GL_SwapWindow`。

`rendering_mode = "2d"` 继续使用现有 SDL Renderer 路径。两种模式只在启动时选择；Phase 1 不支持运行时热切换。

### 3.3 Configuration

`rendering.config` 新增：

```json
{
  "rendering_mode": "3d",
  "vsync": true
}
```

规则：

- 缺少 `rendering_mode` 等价于 `"2d"`；
- 只接受 `"2d"` 与 `"3d"`；
- `"3d"` 在 Windows、macOS、Linux 都选择 OpenGL 4.1 Core；
- 未知值产生 `config.invalid_rendering_mode`；
- Context 创建失败是启动错误，不回退到 2D；
- Editor 打开不同 mode 的项目时重建 graphics host，不在原 context 上热切换。

### 3.4 Threading and frame order

Phase 1 保持当前主线程模型：

```text
poll SDL events
  -> apply pending scene changes
  -> Lua OnStart / OnUpdate / OnLateUpdate
  -> physics and frame mutations
  -> resolve Transform3D hierarchy once
  -> extract Runtime and Scene RenderView3D
  -> execute OpenGL passes
  -> draw ImGui
  -> SDL_GL_SwapWindow
```

OpenGL context 只在主线程 current。不得从 Lua、worker 或资源扫描线程调用 GL。

### 3.5 Ownership boundary

以下类型只能存在于 OpenGL implementation：

- `GLuint`、`GLenum`、`GLsync`；
- VAO/VBO/IBO/UBO/FBO/texture/program object；
- OpenGL function pointers；
- platform GL context implementation detail。

以下层只能看到值类型和逻辑 handle：

- Actor/components；
- Lua runtime；
- SceneFormat/SceneMutation/EditorBridge；
- SceneDocument/Inspector；
- RenderView3D/MeshDrawCommand。

这不是提前建立完整 RHI，而是一个具体的依赖方向：world/extraction 产生可测试命令，OpenGL backend 消费命令。

## 4. 坐标与 clip-space 合同

| 项目 | 约定 |
|---|---|
| 世界坐标系 | 右手系 |
| 世界上方向 | `+Y` |
| 世界右方向 | `+X` |
| Camera 前方向 | `-Z` |
| 长度单位 | 1 unit = 1 meter |
| 正旋转 | 右手定则 |
| 前面绕序 | Counter-clockwise |
| canonical NDC depth | `[0, 1]` |
| OpenGL native NDC depth | `[-1, 1]` |
| C++ 矩阵布局 | column-major |
| 变换顺序 | `clip = projection * view * world * position` |
| UV 原点 | 左上；上传时不隐式翻转 |

Camera、picking、golden math 和未来 backend 使用 canonical `[0,1]` projection。GLSL 顶点出口显式执行：

```glsl
gl_Position.z = gl_Position.z * 2.0 - gl_Position.w;
```

该转换让 OpenGL 的 `[-w,+w]` clip rule 接受 canonical `0..w` depth，同时不改变 scene/camera 数据。禁止依赖 OpenGL 4.5 `glClipControl`，因为它不属于 macOS 4.1 共同基线。

## 5. 3D 场景模型

### 5.1 Typed property values

`Actor::ComponentPropertyValue` 增加：

- `Vec3Value`；
- `QuaternionValue`；
- `ColorValue`；
- `AssetReferenceValue`。

JSON 使用显式 versioned tagged object；不得以无类型 `double[]` 冒充语义类型，也不得保存项目绝对路径。

### 5.2 Transform3D

持久化：local position、归一化 quaternion rotation、local scale。默认 identity。

派生且不序列化：local/world matrix、world position/rotation/scale、dirty/version。

约束：

- 同一 Actor 不得同时存在 `Transform` 与 `Transform3D`；
- 3D parent/child 必须都拥有 `Transform3D`；
- 拒绝 2D/3D 跨空间 parenting；
- hierarchy 使用 `parent_world * T * R * S`；
- quaternion 每次写入后 normalize；
- Inspector 以 Euler degrees 编辑，保存 quaternion；
- reparent 默认保持 world transform；不可逆 parent 或无法无损分解的 shear 必须原子拒绝。

### 5.3 Camera3D

`Camera3D` 必须与同 Actor 的 `Transform3D` 一起使用。属性：

- `projection`：`perspective` / `orthographic`；
- `field_of_view_y_degrees`；
- `orthographic_height`；
- `near_clip` / `far_clip`；
- `is_primary`；
- clear color。

Runtime 按稳定 Actor/component 顺序选择 enabled primary Camera。零 Camera、多 primary、非法 projection/clip 都产生稳定诊断。Scene View 使用 editor-private Camera state。

### 5.4 MeshRenderer

属性：

- `mesh`：逻辑资源 URI；
- `material`：逻辑资源 URI；
- `visible`；
- Phase 1 不包含 `cast_shadow`。

内建资源：`builtin:cube`、`builtin:unlit-color`、missing mesh/material placeholder。Mesh vertex 至少包含 position、normal、UV；index 固定为 32-bit。

## 6. Render extraction

`RenderView3D` 包含：

- view/projection/view-projection；
- drawable viewport size；
- clear values；
- 稳定排序后的 `MeshDrawCommand`；
- Scene View picking object ID。

`MeshDrawCommand` 只包含值、Actor UID、component key、world matrix 和逻辑资源引用。不得包含 Actor pointer、LuaRef、`GLuint` 或 context pointer。

每帧只解析一次 Transform3D hierarchy。Draw 顺序为 Actor scene order、component key；Phase 1 只有 opaque pass。

## 7. OpenGL execution

### 7.1 Required objects

- one SDL OpenGL context per host window；
- one VAO/VBO/IBO for builtin Cube；
- one GLSL program for builtin Unlit；
- per-view framebuffer with RGBA8 color + depth/stencil；
- explicit sampler/texture/program/VAO state；
- resource registry mapping logical handles to GL objects。

每帧 pass：

1. bind target framebuffer；
2. set drawable viewport；
3. clear color/depth/stencil；
4. enable depth test/write，`GL_LESS`；
5. set `GL_CCW` + back-face culling；
6. bind program/VAO/resources；
7. upload view-projection/world/color；
8. `glDrawElements(GL_TRIANGLES, ..., GL_UNSIGNED_INT, ...)`；
9. restore the small explicit state contract needed by ImGui；
10. render ImGui to default framebuffer and swap once。

### 7.2 Resource lifetime

- logical handle 不重用 generation；
- GL object 只在 owning context current 时创建/删除；
- context shutdown 前先释放 view/resources/program/buffers；
- project reload 清空 project resources，不销毁 host context；
- missing resource warning 以稳定 key 去重。

### 7.3 Shader contract

- source language only GLSL 410；
- Shader 文件是项目可跟踪资源，不以内联字符串长期维护；
- build/test 校验 compile/link log；
- attribute locations 与 uniform block bindings 显式固定；
- canonical-to-OpenGL depth conversion 由 shared vertex include/contract 固定；
- 禁止依赖 4.2+、vendor extension 或 compatibility built-in。

## 8. Editor integration

3D editor host 使用 `ImGui_ImplSDL2_InitForOpenGL` 与 ImGui OpenGL3 renderer backend。Runtime/Scene framebuffer color texture 作为 ImGui texture 显示。

Scene View 必须支持：

- private orbit camera；
- framebuffer resize；
- object-ID 或 CPU ray picking；
- ImGuizmo world/local TRS；
- quaternion persistence；
- Play/Stop authoring/runtime isolation。

现有 2D editor 继续使用 SDL Renderer backend。两套 renderer backend 由 startup graphics mode 选择，不在同一 ImGui context 同时初始化。

## 9. 目录责任

```text
include/engine/scene/Transform3D.h
include/engine/scene/Camera3D.h
include/engine/rendering/MeshRenderer.h
include/engine/rendering/Render3D.h

src/engine/scene/Transform3D.cpp
src/engine/scene/Camera3D.cpp
src/engine/rendering/MeshRenderer.cpp
src/engine/rendering/Render3DExtraction.cpp
src/engine/rendering/opengl/
  OpenGLHost.cpp
  OpenGLFunctions.cpp
  OpenGLRenderer3D.cpp
  OpenGLResources.cpp

Projects/Pocket3D/
tests/pocket3d/
tests/evidence/pocket3d/
```

目录是责任边界，不要求为空洞名字建立类。公共 3D headers 不得 include OpenGL headers。

## 10. Milestones

### P1.0 Contract and config

- 本文、测试规格、case catalog；
- `rendering_mode`/vsync config；
- OpenGL loader strategy 与 error taxonomy；
- `Projects/Pocket3D` fixture。

Exit：portable contract tests 通过，2D default 不变。

### P1.1 OpenGL host

- SDL 4.1 Core context；
- function loading/version/profile diagnostics；
- clear/swap/resize；
- shutdown fault tests；
- Linux software visible-window evidence。

Exit：WSL/Linux 出现真实 GL window，readback 与截图匹配。

### P1.2 3D data and camera

- typed properties；
- Transform3D hierarchy/reparent；
- Camera3D projection/ray/selection；
- SceneFormat/Lua/Inspector transport。

Exit：portable component/format tests 通过。

### P1.3 Cube rendering

- MeshRenderer extraction；
- builtin Cube/Unlit；
- depth/culling；
- Runtime RenderView；
- missing placeholder。

Exit：Linux llvmpipe deterministic readback 通过。

### P1.4 Editor authoring

- ImGui OpenGL backend；
- Runtime/Scene framebuffer；
- navigation/picking/gizmo；
- Play/Stop isolation。

Exit：editor workflow 自动化与人工证据通过。

### P1.5 Three-platform acceptance

- Windows/MSVC build + visible-window evidence；
- macOS/Xcode build + Retina evidence；
- Linux build + llvmpipe and visible-window evidence；
- 2D compatibility regression；
- project reload/resource lifetime soak。

Exit：同一 commit、同一 fixture、同一 case catalog 在三端形成 receipt。

## 11. Definition of Done

Phase 1 只有同时满足以下条件才完成：

- 三端 3D mode 请求并验证 OpenGL 4.1 Core；
- `Transform3D`、`Camera3D`、`MeshRenderer` 可创建、保存、加载、编辑和运行；
- builtin Cube 在 Runtime/Scene View 正确 depth occlusion；
- Scene Camera 与 Runtime Camera 独立；
- Play/Stop 不污染 authoring scene；
- Debug diagnostics 没有未处理 GL error；
- resize/reload/reopen 后 GL resource count 回到稳定上限；
- 2D 默认项目行为和回归测试不变；
- Linux software golden 与三端 window evidence 已登记；
- 公共 scene/scripting/editor document 层没有 OpenGL 类型。

## 12. Phase 1 之后

OpenGL 实现成为跨平台行为参考，而不是未来所有 backend 的公共接口。下一阶段顺序：

1. 用 portable extraction/conformance tests 固定 backend contract；
2. 将 OpenGL implementation 收敛到该 contract，Linux 保留 OpenGL；
3. Windows 引入 Direct3D 12；
4. macOS 引入 Metal；
5. 三 backend 运行相同 fixture/conformance suite；
6. 再开始 Lua → Python 与更高层渲染能力。
