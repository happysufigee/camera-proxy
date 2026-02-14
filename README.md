# camera-proxy

Direct3D9 proxy DLL for RTX Remix camera/transform reconstruction.

This project wraps `IDirect3D9` and `IDirect3DDevice9`, inspects vertex shader constant uploads, reconstructs camera transforms, and forwards fixed-function `WORLD` / `VIEW` / `PROJECTION` state before draw calls so Remix can consume consistent matrices in programmable DX9 games.

## What the current main branch does

- Caches matrix candidates from `SetVertexShaderConstantF` uploads.
- Classifies matrices deterministically (World/View/Projection) using structural checks.
- Supports register overrides (`ViewMatrixRegister`, `ProjMatrixRegister`, `WorldMatrixRegister`) when a game uses known fixed slots.
- Emits `SetTransform(D3DTS_WORLD/VIEW/PROJECTION)` at draw time (`DrawPrimitive`, `DrawIndexedPrimitive`, `DrawPrimitiveUP`, `DrawIndexedPrimitiveUP`).
- Falls back to identity for missing matrix slots to keep fixed-function state valid.

## Detection and reconstruction paths

### 1) Deterministic structural detection

When `AutoDetectMatrices=1`, uploaded 4x4 / 4x3 ranges are analyzed and classified:

- **Projection**: perspective-style structure + FOV validation (`MinFOV`, `MaxFOV`).
- **View**: strict camera-style affine/orthonormal checks.
- **World**: affine matrix that is not classified as strict view.

Optional probes:

- `ProbeTransposedLayouts=1` checks one transposed candidate pass.
- `ProbeInverseView=1` allows inverse-view style view recovery checks.

### 2) Optional combined-MVP decomposition fallback

If full W/V/P is not resolved, combined MVP decomposition can be enabled:

- `EnableCombinedMVP`
- `CombinedMVPRequireWorld`
- `CombinedMVPAssumeIdentityWorld`
- `CombinedMVPForceDecomposition`
- `CombinedMVPLogDecomposition`

### 3) Optional experimental custom projection fallback

For difficult titles, an experimental projection fallback can be enabled:

- Manual 4x4 projection matrix mode.
- Auto-generated projection mode (FOV / near / far / aspect / handedness controls).
- Optional override flags for detected projection and combined-MVP projection.

Relevant keys are prefixed with `ExperimentalCustomProjection*` in `camera_proxy.ini`.

## Game profiles

`GameProfile` can switch from general structural detection to game-aware register mapping:

- `MetalGearRising`
  - Projection `c4-c7`
  - ViewProjection `c8-c11` (proxy derives View)
  - World `c16-c19`
  - Tracks optional WorldView `c20-c23`
- `DevilMayCry4` (or `DMC4`)
  - Combined MVP `c0-c3`
  - World `c0-c3`
  - View `c4-c7`
  - Projection `c8-c11`

If no profile is set, the proxy uses structural detection + optional register overrides.

## Overlay and runtime controls

Built-in ImGui overlay includes:

- Camera matrix display/state source info.
- Shader constant inspection and editing tools.
- Optional memory scanner panel.
- In-overlay logs.

Runtime controls:

- `ImGuiScalePercent` for global overlay scaling.
- Hotkeys (default):
  - F10 menu toggle (`HotkeyToggleMenuVK`)
  - F9 pause rendering (`HotkeyTogglePauseVK`)
  - F8 emit cached matrices (`HotkeyEmitMatricesVK`)
  - F7 reset matrix register overrides (`HotkeyResetMatrixOverridesVK`)

## Setup

1. Install RTX Remix runtime files in your game directory.
2. Rename Remix runtime DLL to `d3d9_remix.dll` (or set `RemixDllName`).
3. Copy this project's built `d3d9.dll` into the game directory.
4. Copy `camera_proxy.ini` into the same directory.
5. Launch the game and toggle the overlay with F10 (default).

## Build

### Option A (Visual Studio Developer Command Prompt)

```bat
build.bat
```

### Option B (generic shell script that locates VS toolchain)

```bat
do_build.bat
```

Build output is 32-bit `d3d9.dll`.

## Key config options

See `camera_proxy.ini` for complete comments and defaults. Commonly used keys:

- Runtime/output: `UseRemixRuntime`, `RemixDllName`, `EmitFixedFunctionTransforms`
- Detection: `AutoDetectMatrices`, `ProbeTransposedLayouts`, `ProbeInverseView`
- Register control: `ViewMatrixRegister`, `ProjMatrixRegister`, `WorldMatrixRegister`
- Profiles: `GameProfile`
- Fallbacks: `EnableCombinedMVP`, `ExperimentalCustomProjection*`
- UI/input: `ImGuiScalePercent`, `Hotkey*`
- Diagnostics: `EnableLogging`, `LogAllConstants`

## Credits

- mencelot (original DMC4 camera proxy basis)
- cobalticarus92 (camera-proxy project)
