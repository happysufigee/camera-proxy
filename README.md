# Camera Proxy for RTX Remix

> **⚠️ Experimental Branch** - Shader-driven Remix compatibility for arbitrary DirectX 9 engines

A Direct3D9 proxy DLL that enables RTX Remix support for games using programmable shaders by extracting and forwarding transform matrices at draw time.

---

## Table of Contents

- [Overview](#overview)
- [Why does this exist?](#why-does-this-exist)
- [Solution](#solution)
- [Key Features](#key-features)
- [How It Works](#how-it-works)
- [Quick Start](#quick-start)
- [Configuration](#configuration)
- [Game Profiles](#game-profiles)
- [Building](#building)
- [UI Overlay](#ui-overlay)
- [Advanced Features](#advanced-features)
- [Credits](#credits)

---

## Overview

This proxy wraps `IDirect3D9` and `IDirect3DDevice9` to observe vertex shader constant uploads, cache transform state, and forward fixed-function transforms to RTX Remix at draw time. It's designed for games that use programmable shaders and never call the fixed-function `SetTransform()` API.

**Branch Focus**: Shader-driven matrix extraction with deterministic classification, custom lighting integration, and experimental compositing controls.

---

## Why does this exist?

Many modern DX9 games use programmable vertex shaders and never call fixed-function `SetTransform()`, but RTX Remix depends on these transform states for scene reconstruction. 

**Naive approaches fail** because forwarding transforms during `SetVertexShaderConstantF()` is often out-of-sync with the actual draw call that consumes those constants.

---

## Solution

This proxy solves the synchronization issue by:

1. **Tracking** matrix state from shader constant uploads
2. **Caching** World, View, and Projection matrices
3. **Emitting** `SetTransform()` calls **per draw** (`DrawPrimitive`, `DrawIndexedPrimitive`, etc.)

This ensures transforms are synchronized with the exact draw calls that use them.

---

## Key Features

### 🎯 Core Functionality
- **Deterministic matrix extraction** from shader constants
- **Draw-time emission** of fixed-function transforms
- **Structural matrix classification** (World/View/Projection detection)
- **Transpose probing** for different matrix layouts
- **Game-specific profiles** for known engines

### 🎮 Game Profiles
- **Metal Gear Rising: Revengeance** - Fixed register mapping with view inverse
- **Devil May Cry 4** - Strict MVP layout
- **Barnyard (2006)** - Hybrid SetTransform interception

### 🔧 Advanced Capabilities
- Remix light forwarding with shader metadata
- Custom light authoring/import/export
- Optional raster/Remix blend compositing
- Combined MVP fallback decomposition
- Custom projection matrix generation
- Memory scanner for runtime matrix discovery

### 🖥️ User Interface
- Real-time ImGui overlay (F10 to toggle)
- Scalable UI (configurable percentage)
- Live matrix visualization
- Shader constant editor
- Integrated logging system
- Single-key hotkeys for input-challenged games

---

## How It Works

### 1. Matrix State Caching

The proxy maintains three cached matrices:
- **World** - Object-to-world transformation
- **View** - Camera transformation  
- **Projection** - Perspective/orthographic projection

State updates when constant uploads match structural rules, but forwarding is deferred until draw time.

### 2. Deterministic Structural Classification

Constant upload windows (4x4 and 4x3) are analyzed using strict criteria:

| Matrix Type | Detection Criteria |
|-------------|-------------------|
| **Projection** | Strict perspective structure + FOV validation (`MinFOV`/`MaxFOV`) |
| **View** | Strict orthonormal affine camera-style matrix |
| **World** | Affine matrix that is not a strict View |
| **Combined MVP** | Perspective-like but not strict Projection (fallback) |

No probabilistic ranking or heuristic MVP decomposition in the primary path.

### 3. Register Override System

Control which shader constants are interpreted as matrices:

- `ViewMatrixRegister` / `ProjMatrixRegister` / `WorldMatrixRegister`
  - `>= 0` - Only accept this specific base register
  - `-1` - Auto-detect from all observed constant ranges

### 4. Draw-Time Emission

When `EmitFixedFunctionTransforms=1`, cached matrices are forwarded via `SetTransform()` immediately before each draw call:

```
DrawPrimitive() called
  ↓
Proxy emits SetTransform(D3DTS_WORLD)
Proxy emits SetTransform(D3DTS_VIEW)  
Proxy emits SetTransform(D3DTS_PROJECTION)
  ↓
Forward to actual DrawPrimitive()
```

If a matrix is unknown, identity is used as a safe fallback.

---

## Quick Start

### Installation

1. **Install RTX Remix** runtime files in your game directory
2. **Rename** Remix's runtime DLL to `d3d9_remix.dll`  
   *(or configure `RemixDllName` in the INI)*
3. **Copy** this proxy's `d3d9.dll` into the game directory
4. **Copy** `camera_proxy.ini` into the same directory
5. **Launch** the game and press **F10** to open the overlay

### First-Time Configuration

Edit `camera_proxy.ini`:

```ini
[Main]
UseRemixRuntime = 1
RemixDllName = d3d9_remix.dll
EmitFixedFunctionTransforms = 1
EnableLogging = 1

[MatrixDetection]
AutoDetectMatrices = 1
ProbeTransposedLayouts = 1
```

---

## Configuration

### Essential Settings

| Setting | Description | Default |
|---------|-------------|---------|
| `UseRemixRuntime` | Enable RTX Remix integration | `1` |
| `RemixDllName` | Remix DLL filename | `d3d9_remix.dll` |
| `EmitFixedFunctionTransforms` | Forward matrices at draw time | `1` |
| `AutoDetectMatrices` | Enable structural classification | `1` |
| `EnableLogging` | Write debug logs | `0` |

### Matrix Detection

| Setting | Description | Default |
|---------|-------------|---------|
| `ViewMatrixRegister` | Force view matrix register (`-1` = auto) | `-1` |
| `ProjMatrixRegister` | Force projection register (`-1` = auto) | `-1` |
| `WorldMatrixRegister` | Force world register (`-1` = auto) | `-1` |
| `ProbeTransposedLayouts` | Check transposed matrices | `1` |
| `ProbeInverseView` | Derive view from inverse | `0` |

### Hotkeys

| Hotkey | Action | Default |
|--------|--------|---------|
| `HotkeyToggleMenuVK` | Toggle overlay | `F10` |
| `HotkeyTogglePauseVK` | Pause/unpause matrix emission | `F9` |
| `HotkeyEmitMatricesVK` | Force emit matrices once | `F8` |
| `HotkeyResetMatrixOverridesVK` | Reset register overrides | `F7` |

### UI Settings

```ini
[UI]
ImGuiScalePercent = 100  # 50-200% scaling
DisableGameInputWhileMenuOpen = 1
```

---

## Game Profiles

Enable game-specific extraction via `GameProfile` in the INI.

### Metal Gear Rising: Revengeance

```ini
GameProfile = MetalGearRising
```

**Fixed Register Layout:**
- `c4-c7` → Projection
- `c12-c15` → View Inverse (auto-inverted to derive View)
- `c16-c19` → World
- `c8-c11` → ViewProjection (tracking only)
- `c20-c23` → WorldView (tracking only)

**Fallback:** If expected uploads don't match or inverse fails, falls back to structural detection.

**Special Options:**
- `MGRRUseAutoProjectionWhenC4Invalid=1` - Auto-generate projection from ViewProjection if c4-c7 fails validation

---

### Devil May Cry 4

```ini
GameProfile = DevilMayCry4  # or DMC4
```

**Strict Fixed Mapping:**
- `c0-c3` → Combined MVP / World
- `c4-c7` → View
- `c8-c11` → Projection

Intentionally prefers fixed mapping over structural heuristics for compatibility.

---

### Barnyard (2006)

```ini
GameProfile = Barnyard  # or Barnyard2006
```

**Hybrid Approach:**
- Intercepts game `SetTransform(VIEW/PROJECTION)` calls
- Captures and re-forwards them at draw time from proxy
- Detects WORLD from vertex shader constants

**Options:**
- `BarnyardForceWorldFromC0=1` - Treat c0-c3 as WORLD
- `BarnyardUseGameSetTransformsForViewProjection=1` - Enable VIEW/PROJECTION interception

---

## Building

### Option A: Visual Studio Developer Command Prompt

```bat
build.bat
```

### Option B: Auto-detect VS Toolchain

```bat
do_build.bat
```

**Output:** 32-bit `d3d9.dll` in the build directory.

---

## UI Overlay

Press **F10** to toggle the overlay (configurable).

### Panels

#### 📷 Camera
- Live World/View/Projection/MVP matrix display
- Source metadata (register, frame, confidence)
- Matrix validity indicators

#### 🔢 Constants  
- Captured shader register values
- Interactive constant editor
- Register-to-matrix assignment tools

#### 🔍 Memory Scanner
- Background memory scan for matrix patterns
- Quick-assign discovered matrices
- Configurable scan parameters

#### 📋 Logs
- Real-time log stream
- Filterable by severity
- Scrollable history

---

## Advanced Features

### Combined MVP Fallback

Decompose combined model-view-projection matrices when full W/V/P isn't available:

```ini
[CombinedMVP]
EnableCombinedMVP = 0  # Disabled by default
CombinedMVPRequireWorld = 1
CombinedMVPAssumeIdentityWorld = 0
CombinedMVPForceDecomposition = 0
CombinedMVPLogDecomposition = 0
```

### Custom Projection Generation

Generate projection matrices when detection fails:

```ini
[ExperimentalProjection]
ExperimentalCustomProjectionEnabled = 0
ExperimentalCustomProjectionMode = 2  # 1=manual 4x4, 2=auto from FOV
ExperimentalCustomProjectionAutoFovDeg = 75.0
ExperimentalCustomProjectionAutoNearZ = 0.1
ExperimentalCustomProjectionAutoFarZ = 1000.0
ExperimentalCustomProjectionAutoAspectFallback = 1.777778
ExperimentalCustomProjectionAutoHandedness = 1  # 1=LH, 2=RH
```

### SetTransform Compatibility

For games that still use some fixed-function calls:

```ini
[Compatibility]
SetTransformBypassProxyWhenGameProvides = 1  # Bypass when game calls SetTransform
SetTransformRoundTripCompatibilityMode = 1   # Round-trip through GetTransform
```

### Raster/Remix Compositing

Experimental blend controls:

```ini
[RasterBlend]
# See camera_proxy.ini for full options
```

---

## Credits

- **mencelot** - Original DMC4 camera proxy basis
- **cobalticarus92** - Camera-proxy project development

---

## Support

For issues, feature requests, or questions:
- GitHub Issues: [camera-proxy/issues](https://github.com/RemixProjGroup/camera-proxy/issues)
- Branch: `shaders`

---

**Last Updated:** February 2026
