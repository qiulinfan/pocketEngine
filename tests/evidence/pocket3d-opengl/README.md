# Pocket3D Phase 1 WSLg evidence

日期：2026-08-16  
结果：PASS（Linux/WSLg OpenGL 切片；不代表 Windows、macOS 或 3D editor gate 已通过）

## 环境

- Host：Windows + WSL2
- Distribution：Ubuntu 26.04 LTS
- Kernel：`6.18.33.2-microsoft-standard-WSL2`
- Compiler：GCC `13.4.0`
- CMake：`4.2.3`
- OpenGL：`4.5 (Core Profile) Mesa 26.0.3-1ubuntu1`
- Renderer：`llvmpipe (LLVM 21.1.8, 256 bits)`
- Direct rendering：Yes
- Accelerated：No（本次验证故意使用可重复的软件 GL 环境）

## 自动化

执行：

```bash
cmake --build --preset unix-makefiles-debug -j4
ctest --test-dir build/unix-makefiles-debug --output-on-failure
```

结果：8 个 CTest 中 7 PASS、0 FAIL、1 SKIP。Skip 是仓库原有的 `ai_editor_service_smoke`（本机未安装 sidecar npm dependencies）；全部 Pocket3D tests 均 PASS：

- `pocket3d_phase1_spec`
- `pocket3d_phase1_config`
- `pocket3d_phase1_components`
- `pocket3d_opengl_smoke`
- `pocket3d_phase1_math_spec`

2D 回归启动：

```bash
POCKET_PROJECT_ROOT=Projects/Default POCKET3D_TEST_FRAMES=3 \
./build/unix-makefiles-debug/game
```

结果：exit 0；legacy project 在缺少 `rendering_mode` 时继续走 SDL Renderer 2D path。

## 可见窗口与 backbuffer

3D runtime：

```bash
POCKET_PROJECT_ROOT=Projects/Pocket3D \
POCKET3D_TEST_FRAMES=3 \
POCKET3D_CAPTURE_PATH=tests/evidence/pocket3d-opengl/wsl-backbuffer.ppm \
./build/unix-makefiles-debug/game
```

Runtime 输出：

```text
pocket3d_opengl=4.5 (Core Profile) Mesa 26.0.3-1ubuntu1
pocket3d_capture=tests/evidence/pocket3d-opengl/wsl-backbuffer.ppm
```

证据：

- [`wsl-backbuffer.png`](wsl-backbuffer.png)：`glReadPixels` 得到的 960×540 backbuffer，显示透视、父子 TRS、多个 Cube 和 depth occlusion。
- [`wslg-window.png`](wslg-window.png)：Windows 桌面合成层截取的真实 WSLg 窗口，包含 `Pocket3D OpenGL Phase 1` 标题栏；窗口矩形为 829×510。
- [`wsl-window-run.log`](wsl-window-run.log)：可见窗口进程报告的 OpenGL version。

WSLg 的 X11 `import -window` 会把 OpenGL 合成表面读取为黑色，因此可见窗口证据由 [`capture_windows_window.ps1`](../../pocket3d/capture_windows_window.ps1) 从 Windows 桌面层抓取；渲染内容的确定性证据独立来自 engine backbuffer readback。

SHA-256：

```text
2711a06e32807fbfd2ee26e9866c236616457043b87a582a207a13f52caa7267  wsl-backbuffer.png
7f65525243faa403ef3e7250fd2c89ee51700548dd7bcb044c880aa189f1dc6d  wslg-window.png
```
