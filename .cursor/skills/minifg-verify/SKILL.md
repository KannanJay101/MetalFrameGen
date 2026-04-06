---
name: minifg-verify
description: Verifies MiniFG builds, audits whether GPU frame interpolation is actually implemented vs passthrough, and checks SwiftUI capture UI flows. Use when the user asks if the app works, for release readiness, frame generation status, UI operational checks, regression verification, or "test everything".
---

# MiniFG verification

## When this applies

Use for: “does it work”, “is frame gen done”, “is the UI 100% operational”, pre-push checks, or after edits to `MetalEngine`, `Interpolator`, `StreamManager`, or `MiniFGApp`.

## What you must do

### 1. Build (required)

From repo root:

```bash
xcodebuild -project "MiniFG_Mac/MiniFG/MiniFG/MiniFG.xcodeproj" -scheme MiniFG -configuration Debug -destination 'platform=macOS' build
```

- **Pass:** Exit code 0; note `** BUILD SUCCEEDED **`.
- **Fail:** Paste the **first compiler error block** (file + message), then stop and fix or report.

### 2. Frame generation — code audit (required)

Read and cite behavior (do not assume README text):

| Question | Where to look |
|----------|----------------|
| Are frames uploaded from capture? | `MetalEngine::submitFrame`, `StreamManager` → `EngineBridge.submitFrame` |
| What is drawn to the overlay? | `MetalEngine::renderFrame` — which texture / encoder path |
| Is `Interpolator::interpolate` called from `renderFrame`? | If **no**, interpolation is **not** in the hot path |
| MetalFX / synthetic intermediate? | `Interpolator.mm` + any `MTLFX*` usage |

**Verdict labels (pick one):**

- **FG-not-wired:** Only capture + present (e.g. blit or single-texture shader); no prev/curr blend or API that creates an intermediate frame.
- **FG-partial:** Interpolator invoked but trivial (e.g. copy only) or TODO.
- **FG-implemented:** Clear two-frame (or MetalFX) path producing displayed intermediate; cite the call chain.

### 3. UI / state machine (required — code-level)

In `MiniFG/MiniFG/App/MiniFGApp.swift` (paths under `MiniFG_Mac/.../MiniFG/`):

- **Idle:** `SetupView` — text field, Start disabled when empty, window list, refresh, permission hint when empty.
- **Run:** `startCapture` → `WindowTracker`, `OverlayWindowController`, `StreamManager.start`; `isRunning`, error handling on `Task`.
- **Running:** `RunningView` — stats (`captureFPS`, `capturedFrames`, `renderedFrames`, resolution), Stop button → `stop()` invalidates timer and tears down stream/overlay.
- **Threading:** UI mutations on `@MainActor`; timer callbacks hop to main actor for `updateStats`.

Mark each bullet **OK / gap / needs-manual** with one line of evidence (symbol or line behavior).

### 4. Manual / interactive (when user needs “100% UI”)

Automation cannot grant **Screen Recording** or confirm pixel-perfect overlay. If the user wants full operational proof:

1. Build & run `MiniFG.app` from Xcode or DerivedData path.
2. System Settings → Privacy & Security → Screen Recording → enable MiniFG.
3. Open a target app (e.g. Safari); in MiniFG, refresh windows, start capture from list or by name.
4. Confirm: overlay appears, stats increment (`Frames Captured`, `Frames Rendered`), Stop returns to setup without crash.

State clearly: **“Interactive verification: PASS/FAIL/NOT RUN (reason).”**

## Report template

Return a short markdown report:

```markdown
## MiniFG verification

### Build
- Result: SUCCEEDED | FAILED
- (if failed) First error: ...

### Frame generation
- Verdict: FG-not-wired | FG-partial | FG-implemented
- Evidence: (file:function / path — one sentence)

### UI (code review)
- Setup / run / stop / stats: OK | gaps: ...

### Manual
- NOT RUN | PASS | FAIL — (notes)
```

## Anti-patterns

- Do not claim “MetalFX frame gen works” without finding it **called** on the present path.
- Do not skip `xcodebuild` when claiming the project compiles.
- Do not conflate “frames rendered” counter increment with “interpolated frames” unless the code path proves interpolation.
