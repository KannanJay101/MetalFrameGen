# MetalFrameGen Agent Guide

Use this file as the first stop for repository orientation. The fuller historical AI context lives in `claude.md`.

## Quick Map

```text
MetalFrameGen/
├── AGENTS.md
├── claude.md
├── STATUS.md
└── MiniFG_Mac/MiniFG/MiniFG/
    ├── MiniFG.xcodeproj/        # Xcode project; keep paths in sync with moves
    ├── MiniFG/                  # App target source root
    │   ├── App/                 # SwiftUI app state, setup/running UI, overlay window
    │   ├── Capture/             # ScreenCaptureKit window tracking and frame ingest
    │   ├── Compute/             # Vision optical flow and interpolation support code
    │   ├── Render/              # Swift/ObjC++ bridge plus C++ Metal renderer
    │   ├── Shaders/             # Metal shader and compute kernels
    │   └── Assets.xcassets/
    ├── MiniFGTests/
    ├── MiniFGUITests/
    └── ThirdParty/metal-cpp/metal-cpp/
```

## High-Value Files

- `MiniFG_Mac/MiniFG/MiniFG/MiniFG/App/MiniFGApp.swift`: app state, controls, stats, capture start/stop.
- `MiniFG_Mac/MiniFG/MiniFG/MiniFG/App/OverlayWindow.swift`: transparent overlay and display-link render loop.
- `MiniFG_Mac/MiniFG/MiniFG/MiniFG/Capture/StreamManager.swift`: `SCStream` setup and captured frame delivery.
- `MiniFG_Mac/MiniFG/MiniFG/MiniFG/Render/EngineBridge.swift`: Swift to ObjC++ bridge.
- `MiniFG_Mac/MiniFG/MiniFG/MiniFG/Render/MetalEngine.cpp`: active renderer and interpolation decision path.
- `MiniFG_Mac/MiniFG/MiniFG/MiniFG/Compute/VisionOpticalFlow.mm`: Vision optical-flow worker.
- `MiniFG_Mac/MiniFG/MiniFG/MiniFG/Shaders/FlowWarp.metal`: optical-flow synthesis kernels.
- `STATUS.md`: dated runtime status log; check before claiming frame generation works.

## Working Rules

- Treat `MiniFG_Mac/MiniFG/MiniFG/MiniFG` as the source root.
- Avoid moving files or folders unless you also update `MiniFG.xcodeproj/project.pbxproj`.
- Ignore generated/editor folders such as `.deriveddata/`, `.codex-home/`, `.vscode/`, and `.DS_Store`.
- Prefer `rg` and `rg --files` for navigation.
- Check `git status --short` before editing; there may be user work in progress.
- Stage only files changed for the current task.

## Common Commands

```sh
xcodebuild -project MiniFG_Mac/MiniFG/MiniFG/MiniFG.xcodeproj -scheme MiniFG -configuration Debug -destination 'platform=macOS' build
rg --files MiniFG_Mac/MiniFG/MiniFG/MiniFG
rg "renderFrame|submitFrame|VisionOpticalFlow" MiniFG_Mac/MiniFG/MiniFG/MiniFG
```
