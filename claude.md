# Master Project Context: Mini-FG (macOS Apple Silicon Swift/C++ Hybrid)

## 1. Project Overview & Role
You are an expert Apple Systems and Graphics Engineer specializing in Metal, `metal-cpp`, ScreenCaptureKit, and High-Performance Computing (HPC). 

The objective is to build a standalone macOS application that provides universal Frame Generation (interpolation) for existing games. This is a 14-day rapid prototype focusing on zero-latency pipelines and hardware-software co-design. 

The application acts as a screen overlay: capturing the target game window with zero latency using `ScreenCaptureKit` (Swift), handing the memory buffer to a native C++ rendering engine, interpolating an intermediate frame utilizing the Apple Neural Engine via `MetalFX` (C++), and presenting the frames at double the original framerate.

## 2. Technical Stack
* **Languages:** Swift 6 (UI / OS Capture) and C++20 (Core Engine / Rendering).
* **Target:** macOS 14+ (Apple Silicon exclusively).
* **Capture Pipeline:** `ScreenCaptureKit` (`SCStream`) in Swift.
* **Compute/Interpolation:** Apple's `metal-cpp` library to interface with Metal and MetalFX natively from C++.
* **Presentation:** `CAMetalLayer` / `CVDisplayLink` in a transparent, borderless `NSWindow` overlay.

## 3. Directory Structure
Adhere strictly to this repository structure when generating code. Place all generated files in their respective directories.

```text
MiniFG_Mac/
├── claude.md                   # AI Assistant context and rules
├── MiniFG.xcodeproj/           # Xcode project configuration
└── MiniFG/                     # Main source code directory
    ├── App/                    
    │   ├── MiniFGApp.swift     # SwiftUI application entry point
    │   └── OverlayWindow.swift # Transparent borderless window setup
    ├── Capture/                
    │   ├── WindowTracker.swift # Finds target application windows
    │   └── StreamManager.swift # Handles SCStream and buffer output
    ├── Render/ (C++ & Bridging)
    │   ├── MetalEngine.hpp     # C++ Metal Engine header
    │   ├── MetalEngine.cpp     # C++ metal-cpp implementation
    │   └── EngineBridge.swift  # Swift-to-C++ interoperability layer
    └── Compute/ (C++)          
        └── Interpolator.cpp    # MetalFX MTLFXFrameInterpolator logic