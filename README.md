# MiniFG — debugging guide

A macOS prototype that captures a window via **ScreenCaptureKit**, synthesises intermediate frames on the **GPU**, and presents them through a transparent overlay positioned over the original window. The goal is frame-rate-doubling-style "frame generation" (think DLSS FG / FSR FG / AFMF) entirely outside the target app.

> **Honest preface — read this first.**
> Screen-capture frame generation has unavoidable downsides vs the unmodified game. If gameplay feels *worse* with MiniFG running, that's expected and explained in [Why "smoother" is hard](#why-smoother-is-hard) below. The fix is usually to understand which trade-off is biting, not to keep tuning shaders.

---

## Build & run

```sh
# Build the macOS app (Debug)
xcodebuild \
  -project MiniFG_Mac/MiniFG/MiniFG/MiniFG.xcodeproj \
  -scheme MiniFG \
  -configuration Debug \
  -destination 'platform=macOS' \
  build

# Launch it (path depends on your DerivedData hash)
open ~/Library/Developer/Xcode/DerivedData/MiniFG-*/Build/Products/Debug/MiniFG.app
```

First launch will prompt for **Screen Recording** in System Settings → Privacy & Security. Grant it, restart MiniFG. The setup window lists capturable windows; pick one and capture starts.

Press **Escape** while the overlay is up to stop capture. The overlay is click‑through, so input goes to the underlying app/game.

---

## What the stats panel actually means

| Field | What it counts | What "wrong" looks like |
|---|---|---|
| **Capture FPS** | Frames per second arriving from `SCStream` (`StreamManager` callback). | Drops to ~0 = capture stalled (target window static or in a Space the stream lost). |
| **Output FPS** | Render-loop ticks per second from `CVDisplayLink`. | Should be at or near display refresh (60 Hz here). Lower = engine falling behind / drawable acquisition stalling. |
| **Frames Captured** | Lifetime count from `StreamManager`. | Stuck = capture broken. |
| **Frames Rendered** | Lifetime count from `MetalEngine::renderFrame`. | Stuck = display link not firing or `nextDrawable` returning nil. |
| **Interpolated** | Lifetime count of *synthesised* frames (flow **or** crossfade). | If it stays 0 with motion, the gate isn't opening — see [The interpolation gate](#the-interpolation-gate). |
| **Flow Frames** | Subset of Interpolated where Vision optical-flow synthesis was used. | If 0 but Interpolated is climbing, every synthesised frame is using crossfade fallback (Vision late / disabled / not enough work). |
| **Status line** | Plain-language summary. | "Crossfade fallback" with motion = ghosting. "Optical-flow synthesis" = motion-aware path active. |

Sample healthy state with 60 Hz display + 60 FPS source motion + optical flow on:
- Capture FPS ≈ 60, Output FPS ≈ 60
- Frames Rendered ≈ Output FPS × seconds running
- Interpolated ≈ ~half of Rendered (every other tick is synthesised, alternating with captured passthroughs)
- Flow Frames ≤ Interpolated (depends on Vision throughput)

---

## Render path in one paragraph

`StreamManager` (Swift) receives `CMSampleBuffer`s from `SCStream`, extracts the `CVPixelBuffer`, and calls `EngineBridge.submitFrame` → `MetalEngineObjC submitFrameWithPixelBuffer:` → `MetalEngine::submitFrame(CVPixelBufferRef, ...)`. The engine CPU‑uploads pixels to a triple‑buffer texture ring (`m_inputTex[m_writeIdx]`), retains the `CVPixelBuffer` for the same slot, assigns a monotonically increasing frame ID, and — if optical flow is enabled and there's a prior buffer with matching dimensions — enqueues a `(prev, curr)` pair to `VisionOpticalFlow` (async, serial utility queue). On every display refresh, `OverlayWindowController`'s `CVDisplayLink` callback calls `MetalEngine::renderFrame`, which rotates the buffer ring (write→read→prev), decides whether to interpolate via [the gate](#the-interpolation-gate), and on each interpolated tick **prefers** optical‑flow synthesis (`estimateConfidence` + `synthesizeFlow` compute kernels) when the worker has a result for the active `(prevID, currID)` pair, otherwise falls back to a `temporalBlend` linear crossfade. The result is blitted to the `CAMetalLayer` drawable along with an FPS overlay, then presented.

---

## The interpolation gate

`MetalEngine::renderFrame` only runs interpolation when **all** of:

1. `m_hasPrevFrame` — at least 2 frames have been submitted.
2. `m_displayInterval > 0 && m_displayInterval < m_estimatedInterval * 0.95` — display rate exceeds capture rate by at least ~5%.
3. The temporal blend factor `t = (now - m_presentStart) / m_estimatedInterval` falls in `(0.01, 0.99)`.

The 0.95 ratio is the gaming-baseline value — tightening it (the previous attempt at 0.75) closed the gate on 60 Hz displays and is what regressed gaming. Don't change this without testing.

If you want interpolation to kick in on a 60 Hz display where the source also runs at 60 FPS, the gate needs the source to be *slightly* slower than the display — capture FPS jitter usually provides enough headroom. If `Interpolated` stays 0 while you have visible motion, capture FPS ≈ display refresh exactly and the gate is staying closed.

---

## Knobs and where they live

| What | File | Detail |
|---|---|---|
| Optical-flow on/off | `MiniFGApp.swift` (`AppState.opticalFlowEnabled`) | Toggle in the Running view; default on. Off = crossfade only. |
| Capture frame rate target | `StreamManager.swift` (`captureFrameRate(for:)`) | At baseline returns 60 for sub-100Hz displays. Halving it would force gate to open but adds latency. |
| Interpolation gate ratio | `MetalEngine.cpp` `renderFrame`, `m_estimatedInterval * 0.95` | See [The interpolation gate](#the-interpolation-gate). |
| Display sync (VSync) | `OverlayWindow.swift` `displaySyncEnabled` | At baseline: **`false`**. Setting `true` caps output to display refresh — fine for measuring latency, kills the "more frames" pitch on high-refresh displays. |
| In-flight frame count | `MetalEngine.hpp` `kMaxFramesInFlight` | 1 = no overlap (lowest latency). Raising it lets the GPU pipeline more frames at the cost of latency. |
| Vision flow revision | `VisionOpticalFlow.mm` `chooseOpticalFlowRevision()` | Picks the latest supported revision. Older revisions are faster but lower quality. |
| Confidence params | `MetalEngine.cpp` `FlowConfidenceParams` | `consistencyBias` / `consistencyScale` control how strict the forward-reverse flow check is. Lower = more pixels trust the flow path; higher = more pixels fall back to crossfade. |

---

## Why "smoother" is hard

These are unavoidable on a screen-capture-based architecture. Knowing them is more useful than tuning shaders:

1. **Interpolation needs the future.** A synthesised frame between captures N and N+1 can only be shown *after* N+1 arrives. That's at least one extra capture interval of input latency. Mouse and controller feel laggier even when "FPS" is the same. DLSS/FSR FG add similar latency inside the game and partially mask it with reflex/anti-lag — we have no equivalent.

2. **The engine is outside the game.** Game render → macOS compositor → ScreenCaptureKit → encode → IPC → MiniFG → upload → engine compute → engine present → compositor blends overlay + window → display. Each arrow is overhead. Native FG runs inside the engine; ours can't.

3. **GPU contention.** On an Apple Silicon GPU the game, ScreenCaptureKit, the system compositor, **and** MiniFG share the same hardware. The game's frame rate often *drops* while MiniFG is running because we're taking GPU cycles for capture + synthesis.

4. **Display refresh is a hard ceiling.** On the M1 13" MBP this is 60 Hz. On a 60 Hz display you literally cannot show more than 60 frames per second, no matter how many the engine generates. "Frame gen" on a matched 60→60 capture/display setup just replaces some captured frames with synthesised approximations — the same total flow at the same rate. To see "more frames", the display must be higher refresh than the source (e.g. 120 Hz ProMotion + 60 FPS source).

5. **Vision optical flow has latency.** Each `(prev, curr)` pair takes ~10–30 ms on an M1. If display refresh is 16 ms (60 Hz), many flow results land *after* their target frame already shipped → that frame falls back to crossfade. `Flow Frames` will be **less** than `Interpolated`, often a lot less.

6. **Crossfade ghosts on motion.** When flow isn't ready, the fallback is `mix(prev, curr, t)` linear blend. Anything moving shows as a double image. Visually worse than no frame gen.

7. **Click-through overlay can mismatch.** If the target window is fullscreen on a Space MiniFG isn't on, or has its own GPU surface MiniFG can't draw above (e.g. some games), the overlay never reaches the screen — you're paying all the cost for none of the visual benefit.

**Practical takeaway:** if your goal is "make this game smoother", screen-capture frame generation at a 60 Hz display is structurally a bad fit. Frame gen helps most when (a) the display refresh genuinely exceeds the source rate (ProMotion 120 Hz capturing a 60 FPS source), and (b) the latency cost is acceptable for the content (e.g. video playback, not competitive shooters). Use this project to understand the techniques, not as a daily driver for gaming.

---

## Symptom → first thing to check

| Symptom | Where to look |
|---|---|
| Black overlay, nothing rendering | Permission granted? `CGPreflightScreenCaptureAccess()` returns true? `Frames Captured` ticking? If yes but no render, check `Frames Rendered` — display link not firing means `OverlayWindow.swift` `startDisplayLink` failed. |
| `Frames Captured` stuck at 1–2 then stops | Source is static and `SCStream` is sending `.idle` frames that the delegate filters out. `StreamManager.swift didOutputSampleBuffer` keeps only `.complete`. Either move the source to keep producing complete frames, or accept `.idle` (was added in commit `6f182f6` — reverted with the gaming-baseline rollback). |
| `Interpolated` stays 0, motion is visible | Gate not opening. Capture FPS ≈ display refresh exactly. See [The interpolation gate](#the-interpolation-gate). |
| `Flow Frames` stays 0 but `Interpolated` climbs | Vision worker isn't producing matching results in time. Could be: optical flow disabled (toggle off?), Vision unavailable on this OS rev, content type Vision can't compute on (HDR / 10-bit, very small windows, etc.), or worker just being too slow → every render frame uses crossfade. |
| Visible ghosting / double image | You're hitting crossfade fallback on motion. Either enable optical flow (`Status` should say "Optical-flow synthesis"), or accept the limitation. |
| Game framerate drops when MiniFG runs | GPU contention. Expected — see [Why "smoother" is hard](#why-smoother-is-hard) #3. Quit MiniFG to compare. |
| Input feels laggy | Frame-gen interpolation latency, see #1. Not fixable at this layer. |
| Build fails after editing | Check `xcodebuild` output for the *first* real error, not the cascading SourceKit ones in your IDE — those are usually header-search-path noise on `Metal/Metal.hpp`. |
| Linker error about Vision symbols | `OTHER_LDFLAGS = -framework Vision` missing from `project.pbxproj`. Should be in both Debug and Release configs. |

---

## Useful git landmarks

```sh
# The known-good baseline where gaming frame gen worked (linear crossfade only).
git checkout gaming-baseline

# Tip of the working branch with optical-flow synthesis layered on top.
git checkout high-end-fg

# Diff baseline vs HEAD to see what optical-flow integration added.
git diff gaming-baseline HEAD

# Try the next revision in isolation (no impact on your branch).
git worktree add ~/MetalFrameGen-test <ref>
xcodebuild -project ~/MetalFrameGen-test/MiniFG_Mac/MiniFG/MiniFG/MiniFG.xcodeproj \
           -scheme MiniFG -configuration Debug -destination 'platform=macOS' build
# When done:
git worktree remove ~/MetalFrameGen-test
```

The `gaming-baseline` annotated tag points at the squash-revert commit that snaps the tree to `fc63031`. If a new change makes things measurably worse, `git revert <bad-sha>` and retest before adding more.

---

## Layout

```
MetalFrameGen/
├── README.md                         # this file
├── claude.md                         # AI-context doc for codegen sessions
├── MiniFG_Mac/MiniFG/MiniFG/
│   ├── MiniFG.xcodeproj/
│   └── MiniFG/
│       ├── App/
│       │   ├── MiniFGApp.swift       # SwiftUI app, AppState, stats panel, OF toggle
│       │   └── OverlayWindow.swift   # Transparent click-through NSWindow + CVDisplayLink
│       ├── Capture/
│       │   ├── StreamManager.swift   # SCStream wrapper, CMSampleBuffer → CVPixelBuffer
│       │   └── WindowTracker.swift   # Window enumeration + frame tracking
│       ├── Compute/
│       │   ├── VisionOpticalFlow.{hpp,mm}  # Async Vision worker
│       │   └── Interpolator.{hpp,mm}       # Stale stub; not on the active path
│       ├── Render/
│       │   ├── MetalEngine.{hpp,cpp}       # Triple-buffer ring, gate, render path
│       │   ├── MetalEngineObjC.{h,mm}      # ObjC++ shim for Swift interop
│       │   └── EngineBridge.swift          # Swift wrapper
│       └── Shaders/
│           └── FlowWarp.metal              # Standalone .metal copy of flow kernels
└── MiniFG_Mac/MiniFG/MiniFG/ThirdParty/metal-cpp/  # metal-cpp headers
```

The `Compute/Interpolator.{hpp,mm}` files are dormant — they aren't called by the render path. Keep or delete at will.
