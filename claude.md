# MetalFrameGen / MiniFG — AI context

## Role

Treat this repo as a **macOS Metal + ScreenCaptureKit** prototype for **frame generation** (interpolation): capture a target window, process on GPU, present on a transparent overlay.

## Truth in code (keep this current)

When answering “does frame generation work?”, **verify the render path in source**, not marketing text.

- **Capture:** Swift `StreamManager` / `SCStream` → `EngineBridge.submitFrame` → `MetalEngine::submitFrame` (upload to `m_inputTex[m_writeIdx]`, record capture timestamp).
- **Present:** `OverlayWindow` + `CVDisplayLink` drives `engine.renderFrame()` → `MetalEngine::renderFrame`.
- **Interpolation:** `MetalEngine` uses a **triple-buffer ring** (`m_prevIdx`, `m_readIdx`, `m_writeIdx`). On each `renderFrame`, it computes a **temporal blend factor** from capture timestamps and dispatches a **`temporalBlend` Metal compute shader** that writes `mix(prev, curr, factor)` into `m_outputTex`. Intermediate frames (where 0.01 < factor < 0.99) are **synthesized GPU frames** not present in the capture stream — this is **real frame generation**. The fullscreen triangle shader then presents `m_outputTex`. When factor is near 0 or 1, the captured frame is shown directly (no compute dispatch). `Interpolator.mm` remains a **blit stub** (unused in the active render path; kept for future MetalFX integration).
- **Interpolation gate:** Interpolation only runs when `m_displayInterval < m_estimatedInterval * 0.95` (display rate meaningfully exceeds capture rate). At matched rates (e.g. 60 Hz display + 60 Hz capture), each vsync would otherwise show a phase-dependent crossfade with wobbling `factor`, which reads as ghosting/judder. `OverlayWindowController.startDisplayLink` pushes the display's nominal refresh period into the engine via `setDisplayRefreshPeriod`.
- **Exit:** `OverlayWindow` installs global + local `NSEvent` key monitors. Pressing **Escape** calls `onExitHotkey` → `AppState.stop()`, tearing down capture + overlay. The overlay is click-through (`ignoresMouseEvents = true`) so the target window remains interactive.

If you change the render path, update this section in the same commit.

## Layout (actual)

```text
MetalFrameGen/
├── MiniFG_Mac/MiniFG/MiniFG/
│   ├── MiniFG.xcodeproj/
│   └── MiniFG/
│       ├── App/           # MiniFGApp.swift — SwiftUI UI + AppState
│       ├── Capture/       # WindowTracker, StreamManager
│       ├── Render/        # MetalEngine (C++), EngineBridge, MetalEngineObjC
│       └── Compute/       # Interpolator.hpp / Interpolator.mm
└── ThirdParty/metal-cpp/  # Header search path in Xcode
```

## Stack

SwiftUI + ScreenCaptureKit (Swift), metal-cpp / Metal (C++), `CAMetalLayer`, `CVDisplayLink`.

## Rules for codegen

- Match existing naming and folder layout; avoid drive-by refactors.
- ObjC selectors → Swift names: `configureWithLayer:` → `configure(with:)`, `resizeWidth:height:` → `resizeWidth(_:height:)`.
- `NS_PRIVATE_IMPLEMENTATION` / `CA_` / `MTL_` **only** in one `.cpp` (currently `MetalEngine.cpp`).
- **Sync present to display refresh, not to 60 Hz.** Keep `displaySyncEnabled = true` on the `CAMetalLayer` — this caps presentation at *display refresh* (120 Hz on ProMotion, 60 Hz on standard displays), which is exactly the frame-generation target. Disabling it produces screen tearing without increasing useful FPS, since the display cannot physically show more than its refresh rate. CVDisplayLink drives render cadence at display refresh; the in-flight semaphore (`kMaxFramesInFlight = 2`) handles CPU/GPU pipelining.

---

## Verification mandate (agents must follow)

When the user asks whether things **work**, are **ready**, or the **UI is operational**, do **not** answer from memory. Produce evidence:

1. **Build:** Run `xcodebuild` for scheme `MiniFG` (macOS). Record **BUILD SUCCEEDED** or paste the **first real error**.
2. **Frame-generation audit:** Read `MetalEngine.cpp` (and `Interpolator.mm`). State explicitly whether interpolation / MetalFX is **invoked** on the present path or **passthrough only**; name the functions involved.
3. **UI audit:** Trace `MiniFGApp.swift` — `AppState` (`refreshWindows`, `startCapture`, `stop`, stats timer), `SetupView`, `RunningView`. Confirm controls exist, disabled states, error surfacing, and `@MainActor` safety for UI updates.
4. **Runtime / manual:** Full overlay + capture needs **Screen Recording** permission and a real target window. If you cannot run the GUI, label UI verification **“manual required”** and list exact steps (launch app, grant permission, pick window, confirm overlay + counters).

Prefer the project skill **minifg-verify** (`.cursor/skills/minifg-verify/SKILL.md`) for the full checklist and report template.
