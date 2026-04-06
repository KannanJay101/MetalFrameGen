# MetalFrameGen / MiniFG — AI context

## Role

Treat this repo as a **macOS Metal + ScreenCaptureKit** prototype for **frame generation** (interpolation): capture a target window, process on GPU, present on a transparent overlay.

## Truth in code (keep this current)

When answering “does frame generation work?”, **verify the render path in source**, not marketing text.

- **Capture:** Swift `StreamManager` / `SCStream` → `EngineBridge.submitFrame` → `MetalEngine::submitFrame` (upload to `m_inputTex`).
- **Present:** `OverlayWindow` + `CVDisplayLink` drives `engine.renderFrame()` → `MetalEngine::renderFrame`.
- **Interpolation today:** `MetalEngine::renderFrame` currently draws **`m_inputTex[m_readIdx]`** through the inline **fullscreen triangle shader** (passthrough). It does **not** call `Interpolator::interpolate` in that path. `Interpolator` is a **blit stub** (no `MTLFXFrameInterpolator` wired on public SDK). Until the engine invokes interpolation and produces a **synthesized intermediate** from **prev + curr** (or MetalFX where available), treat **true frame generation as not implemented**—only **capture + display** is.

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

---

## Verification mandate (agents must follow)

When the user asks whether things **work**, are **ready**, or the **UI is operational**, do **not** answer from memory. Produce evidence:

1. **Build:** Run `xcodebuild` for scheme `MiniFG` (macOS). Record **BUILD SUCCEEDED** or paste the **first real error**.
2. **Frame-generation audit:** Read `MetalEngine.cpp` (and `Interpolator.mm`). State explicitly whether interpolation / MetalFX is **invoked** on the present path or **passthrough only**; name the functions involved.
3. **UI audit:** Trace `MiniFGApp.swift` — `AppState` (`refreshWindows`, `startCapture`, `stop`, stats timer), `SetupView`, `RunningView`. Confirm controls exist, disabled states, error surfacing, and `@MainActor` safety for UI updates.
4. **Runtime / manual:** Full overlay + capture needs **Screen Recording** permission and a real target window. If you cannot run the GUI, label UI verification **“manual required”** and list exact steps (launch app, grant permission, pick window, confirm overlay + counters).

Prefer the project skill **minifg-verify** (`.cursor/skills/minifg-verify/SKILL.md`) for the full checklist and report template.
