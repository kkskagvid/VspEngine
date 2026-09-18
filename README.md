# VspEngine

A small experimental game engine: a Vulkan 1.3 bindless renderer, a Unity-style
managed object model and a **programmable render pipeline written in C#**, hosted
by a native C++20 application that embeds the .NET CoreCLR runtime through the
CoreCLR Hosting API (nethost + hostfxr).

The managed side is split into two assemblies: **VspEngine.dll** (the engine's
managed runtime: script base types, the object model, the input/time facades, the
interop bridge, the render-pipeline framework and the default pipeline) and
**Assembly.dll** (the game Assembly that holds the user scripts and is loaded by
the engine at startup).

## Architecture

Three rules shape the whole engine.

### 1. Managed engine classes are reference handles; the data lives natively

`VspEngine.Object` and everything derived from it (`GameObject`, `Component`,
`Transform`, `ScriptBehaviour`) store **nothing but a 32-bit
`NativeHandle`**. Every property getter and setter forwards to the native object
the handle addresses.

The native side of that contract is `Classes/`:

| Native type            | Owns                                                                    |
| ---------------------- | ----------------------------------------------------------------------- |
| `NativeObject`        | the handle, the kind tag and the name every object carries              |
| `GameObject`          | activation state, layer, transform handle, attached component handles   |
| `Transform`           | local position / rotation / scale and a dirty flag                      |
| `Component`           | owner game object, component kind, enable state, render state           |
| `Scene` (singleton)   | the three object tables, slot recycling and handle validation           |

A handle packs the object kind (bits 28..31), a slot generation (bits 20..27) and
the 1-based slot index (bits 0..19). Released slots are recycled with a bumped
generation, so **a stale handle simply stops resolving** instead of silently
addressing whichever object took the slot over. Destroying a game object also
releases its transform and every component attached to it.

Creating a script instance therefore creates real native data: a `GameObject`
with its own `Transform` plus the script `Component`, whose handles the host
hands to the managed instance through the bridge.

### 2. The render flow lives in C#; the native layer only exports a wrapped graphics API

The native layer never builds a frame on its own. It exposes two things:

- **resource creation** — `IGraphics` / `GraphicsSystem` create buffers,
  shader modules, bindless textures and graphics pipelines on request
  (`VspRhi_*` in `Scripting/RhiExports.cpp`);
- **a recorded command list** — `Graphics/RenderCore` collects one frame's
  commands (begin/end render pass, viewport, scissor, bind pipeline, bind vertex
  buffer, push constants, draw) and the backend plays them back while it records
  the swapchain command buffer.

No managed code ever sees a `VkBuffer`, `VkPipeline` or `VkRenderPass`. The
managed side reaches this API through `Rendering/RhiApi.cs` and the thin wrappers
around it (`Shader`, `VertexBuffer`, `Texture2D`, `GraphicsPipeline`,
`CommandBuffer`).

### 3. The render pipeline is programmable from C#

`Rendering/` implements a small Scriptable-Render-Pipeline model:

| Type                          | Role                                                            |
| ----------------------------- | --------------------------------------------------------------- |
| `RenderPipeline`             | abstract base: `Render(ScriptableRenderContext)` + `Dispose()` |
| `RenderPipelineManager`       | gathers the draw list, opens the frame, runs the active pipeline |
| `ScriptableRenderContext`     | the command buffer, the draw list and the target size            |
| `CommandBuffer`               | records the frame through the wrapped graphics API               |
| `Forward2DRenderPipeline`     | the engine's default pipeline (clear + triangles)                |

A game replaces the flow with one line during startup:

```csharp
RenderPipelineManager.ActivePipeline = new MyRenderPipeline();
// or install the default the engine should create when nothing is set:
RenderPipelineManager.SetDefaultPipelineFactory(() => new MyRenderPipeline());
```

The default pipeline is an ordinary `RenderPipeline`, so it doubles as the
worked example of the API.

## Features

- **Vulkan 1.3 bindless renderer (no 1.2 fallback)**
  - Device reports **Vulkan < 1.3** or lacks bindless descriptor-indexing features
    (descriptorBindingPartiallyBound / runtimeDescriptorArray /
    shaderSampledImageArrayNonUniformIndexing) -> startup error: "Unsupported device".
  - The **only** implementation is the bindless one: a 4096-slot sampled-image array
    indexed with `nonuniformEXT`. Texture slots are filled on demand by
    `CreateTexture`, so no texture is created unless a pipeline asks for one.
  - All Vulkan classes log their own errors through the engine Log module - there are
    no `VspString& outErrorText` out-parameters anywhere in `Graphics/Vulkan`.
- **Complete keyboard + mouse input** (`Core/Input/InputManager`): held/pressed/released
  edge state, mouse position/delta/wheel, typed characters. Window messages flow
  Win32 -> events -> InputManager -> scripts.
- **Output devices** (`Core/Output/OutputDevice.h`): `DebugOutputDevice`,
  `ConsoleOutputDevice`, `FileOutputDevice` plus an `OutputDeviceRegistry` that
  enumerates ("gets") the available devices and looks them up by name.
- **Engine formatter with custom type support** (`Core/String/VspStringFormat.h`):
  `VspFormat::Format(...)` supports `{}`, `{n}`, `{:x}`, `{:#x}`, `{:X}`, `{:f}`
  placeholders. Custom types opt in by specializing the **`VspFormatter<Type>`**
  template struct and implementing its **`Parse`** (interprets the ":spec" text) and
  **`Format`** (appends the formatted value) static methods - see the
  `VspFormatter<Position2D>` specialization in `Scripting/ScriptCore.h` (the
  ":p" / ":P" specifiers).
- **Pluggable logging** (`Core/Logging`): the `Log` facade formats each entry once
  and forwards it to replaceable `LogBackend`s (any `OutputDevice` adapts via
  `OutputDeviceLogBackend`). `Log` also keeps a bounded history of recent entries;
  a **Fatal** entry collects that history into a crash report, shows a crash prompt
  (unless disabled) and aborts the process.
- **Assembly loading** (`Scripting/ScriptEngine`): the engine resolves the interop
  bridge (`VspEngine.NativeBridge`) from **VspEngine.dll** and then loads the game
  **Assembly.dll** - the assembly that stores the user scripts - through the bridge.
  Script type enumeration and instantiation operate on the loaded game Assembly.
- **C# scripting via CoreCLR Hosting API** (`Scripting/ScriptEngine`):
  - `nethost.dll` -> `get_hostfxr_path` -> `hostfxr_initialize_for_runtime_config` ->
    `load_assembly_and_get_function_pointer`.
  - The **C++ host generates the non-negative InstanceID** (0 = invalid, 1-based),
    enumerates the managed script types itself (GetScriptTypeCount / GetScriptTypeName)
    and creates the native scene objects each instance is attached to.
  - Every managed `ScriptBehaviour` holds its own `IntPtr` (`NativePtr`, the object's
    GCHandle); the host passes that pointer back on every lifecycle call, so
    `NativeBridge` keeps **no instances Dictionary**.
  - Managed code calls back into the native core through `DllImport("VspCore")`
    (`Input`, `Time`, the scene object model, the wrapped graphics API), so interop is
    fully bidirectional.
- **Frame clock suitable for automation**: `--fixed-delta-time=MS` replaces the wall
  clock with a fixed step per frame, so frame N always happens at N * step of engine
  time and a scheduled run is reproducible on any machine.
- **Split Vulkan module** (`Graphics/Vulkan`): the renderer is decomposed into
  functional units - `VulkanInstance` (instance + surface), `VulkanDevice` (device +
  queues + helpers), `VulkanBuffer`, `VulkanImage`, `VulkanPipeline`,
  `VulkanDescriptors` (bindless), `VulkanSwapChain` and `VulkanRenderer2D`
  (the backend that owns them and plays the recorded frame back) behind a thin
  `VulkanContext` RHI facade.
- **Windows-only code encapsulated in Common** (`Common/PlatformMisc`,
  `Common/PlatformWindow`, `Common/PlatformDllMain`): every platform-specific
  call (window creation, dynamic library loading, environment variables, the
  high-resolution timer, window message injection, the debug output and the crash
  prompt) lives in Common behind `#if VSP_PLATFORM_WINDOWS`; the rest of the
  engine never includes a platform header. Only the executable entry point
  (`wWinMain` + the GPU-selection exports) stays in Launch, because Windows
  requires those in the .exe module.
- **Lightweight build system** (`VspBuildTool`): stages the built executables
  (Launch.exe, VspCore.dll, VspEngine.dll, Assembly.dll) and the necessary
  companion files (runtimeconfig, Vulkan loader, debug symbols) into the run
  directory - following the `Intermediate\Binaries\Debug_x64` layout - and
  makes the C# runtime reachable from it. It can also invoke MSBuild first.
- **Unity-style scripting model** (`VspEngine`): `GameObject` / `Component` /
  `ScriptBehaviour` with `OnInit / OnStart / OnUpdate / OnDestroy`,
  `Input.GetKey(...)`, `Time.DeltaTime`, `Transform.Position`.
- C++20, MSVC, **no C++ exceptions** (`/EHs-c-`, `_HAS_EXCEPTIONS=0`). Function names are
  PascalCase; class fields use Hungarian notation (`m_pDevice`, `m_nFrameIndex`,
  `m_fDeltaTime`, ...). The precompiled header (`Common/RuntimePCH.h`) contains **no
  standard-library includes**: every translation unit and header declares the headers
  it actually uses.

## Demo (acceptance test)

`Assembly/TriangleController.cs` (the game Assembly loaded by the engine) drives a
multicolor triangle:

| Key | Action                                             |
| --- | -------------------------------------------------- |
| W   | move up                                            |
| A   | move left                                          |
| S   | move down                                          |
| D   | move right                                         |
| R   | reset position                                     |
| T   | cycle color: red -> blue -> green -> multicolor    |

The script owns no render state: it writes the position and the color mode into the
native scene, and the active render pipeline reads them while it builds the frame.

## Building

Requirements: Visual Studio 2026 (v145 toolset), .NET SDK 10 and a Vulkan SDK
(the project looks for `Engine/Source/Thirdparty/Vulkan` first, then the
`VULKAN_SDK` environment variable). Shaders are compiled to SPIR-V and embedded
automatically by `Graphics/Vulkan/Shaders/CompileShaders.ps1` (PreBuildEvent) into
`Graphics/Vulkan/Shaders/ShaderBinary.h` (a generated, git-ignored header).

```
msbuild VspEngine.slnx /restore /p:Configuration=Debug /p:Platform=x64
```

### VspBuildTool (lightweight build system / stager)

`Engine\Intermediate\Binaries\Debug_x64\VspBuildTool.exe` builds (optionally)
and stages everything the host needs into the run directory, which follows the
`Intermediate\Binaries\Debug_x64` layout (everything flat next to Launch.exe):

```
VspBuildTool.exe --build                 # msbuild the solution, then stage
VspBuildTool.exe --list                  # print the staging plan (no writes)
VspBuildTool.exe --config Release        # stage the Release binaries
VspBuildTool.exe --output Run\Debug_x64  # stage into a custom run directory
VspBuildTool.exe --clean                 # delete the run directory first
```

The stager copies Launch.exe/.pdb, VspCore.dll/.pdb, VspEngine.dll/.pdb,
Assembly.dll/.pdb, the .deps.json files, Launch.runtimeconfig.json and
vulkan-1.dll (from `%VULKAN_SDK%\Bin`), and makes the shipped C# runtime
reachable. MSBuild is located automatically (--msbuild override, MSBUILD
environment variable, vswhere, known install paths).

Standard layout: every build artifact (binaries, obj/, NuGet restore caches,
generated headers) lives under `Engine/Intermediate` (or `Engine/Binaries` for
third-party libraries) - nothing is ever written into `Engine/Source`. The shipped
.NET runtime lives in `Engine/Binaries/dotnet/runtime/10.0.10`; the engine probes the
known layouts at startup, so it runs both from a staged run directory and straight
out of the repository.

## Running

```
Engine\Intermediate\Binaries\Debug_x64\Launch.exe
```

Useful flags (see `LaunchLoop.cpp`):

| Flag                       | Meaning                                                    |
| -------------------------- | ---------------------------------------------------------- |
| `--frames N`               | exit after N frames (smoke tests)                          |
| `--fixed-delta-time=MS`    | advance the engine clock by a fixed step per frame          |
| `--silent`                 | errors go to the log; fatal crash prompt disabled           |
| `--key VK:MS`              | post synthetic WM_KEYDOWN/KEYUP to the engine window        |
| `--capture N:path.bmp`     | save the framebuffer after frame N (BMP)                    |

Environment switches: `VSP_NO_VALIDATION=1` disables the Vulkan validation layer;
`VK_LAYER_PATH=<sdk>\Bin` makes an SDK that was never registered machine-wide
visible to the loader.

## Acceptance test

```
powershell -NoProfile -ExecutionPolicy Bypass -File Engine\Tools\Acceptance\RunAcceptance.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File Engine\Tools\Acceptance\RunAcceptance.ps1 -Configuration Release
```

The script builds the solution, enables the Vulkan validation layer when an SDK is
present, runs the demo with `--fixed-delta-time=16.6667 --frames=445` together with
the scheduled key presses and captures below, **fails on any `[ERROR]`, `[WARNING]`
or `[FATAL]` entry in the engine log**, and finally checks the captured frames with
`AnalyzeShots.ps1` (movement direction, reset position and all four color modes).

The equivalent command line:

```
Launch.exe --silent --frames=445 --fixed-delta-time=16.6667 ^
  --key 0x44:1500 --key 0x57:800 --key 0x41:800 --key 0x53:800 --key 0x52:0 ^
  --key 0x54:0 --key 0x54:0 --key 0x54:0 --key 0x54:0 ^
  --capture 40:shot0_initial.bmp --capture 148:shot1_after_D.bmp ^
  --capture 210:shot2_after_W.bmp --capture 276:shot3_after_A.bmp ^
  --capture 342:shot4_after_S.bmp --capture 358:shot5_after_R.bmp ^
  --capture 376:shot6_T_red.bmp --capture 394:shot7_T_blue.bmp ^
  --capture 412:shot8_T_green.bmp --capture 430:shot9_T_multi.bmp
```

## Layout

- `Engine/Source/Runtime/VspCore`    - the native engine DLL. `Classes/` owns the object model, `Graphics/` the wrapped graphics API and the backends, `Scripting/` the CoreCLR host and every export managed code P/Invokes. All Windows-only code is encapsulated in its `Common/` folder behind `#if VSP_PLATFORM_WINDOWS`.
- `Engine/Source/Runtime/VspEngine`  - the engine's managed runtime assembly (VspEngine.dll): the object model reference handles, the Input/Time facades, the interop bridge, the RHI wrappers and the render-pipeline framework.
- `Assembly`                          - the game Assembly (Assembly.dll) holding the user scripts (the demo TriangleController); loaded by the engine at startup.
- `Engine/Source/Runtime/Launch`     - the host executable: parses the command line, runs `GameEngine`.
- `Engine/Source/Programs/VspBuildTool` - the lightweight build system / stager described above.
- `Engine/Source/Thirdparty`  - imported third-party SDKs (Vulkan, dxc, glm, fmt, ...). Read-only: never modified.
- `Engine/Binaries`           - third-party binaries (dotnet runtime, Vulkan import lib, ...).
- `Engine/Intermediate`       - all build outputs: binaries, obj/, NuGet restore caches, generated shader header.
- `Engine/Tools/Acceptance`   - the acceptance runner and the captured-frame analyzer.
