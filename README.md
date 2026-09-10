# VspEngine

A small experimental game engine: a Vulkan 2D renderer and a Unity-style C# scripting
system, hosted by a native C++20 application that embeds the .NET CoreCLR runtime
through the CoreCLR Hosting API (nethost + hostfxr).

The managed side is split into two assemblies: **VspEngine.dll** (the engine's
managed runtime: script base types, input/time/transform facades, the interop
bridge and the managed render flow) and **Assembly.dll** (the game Assembly that
holds the user scripts and is loaded by the engine at startup).

## Features

- **Vulkan 1.3 bindless renderer (no 1.2 fallback)**
  - Device reports **Vulkan < 1.3** or lacks bindless descriptor-indexing features
    (descriptorBindingPartiallyBound / runtimeDescriptorArray /
    shaderSampledImageArrayNonUniformIndexing) -> startup error: "Unsupported device".
  - The **only** implementation is the bindless one: a 4096-slot sampled-image array
    indexed with `nonuniformEXT`. There is no Vulkan 1.2 fallback path.
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
  - The **C++ host generates the non-negative InstanceID** (0 = invalid, 1-based) and
    enumerates the managed script types itself (GetScriptTypeCount / GetScriptTypeName).
  - Every managed `GameObject` holds its own `IntPtr` (`NativePtr`, the object's
    GCHandle); the host passes that pointer back on every lifecycle call, so
    `NativeBridge` keeps **no instances Dictionary**.
  - Managed code calls back into the native core through `DllImport("VspCore")`
    (`Input`, `Time`, `Transform`, `Renderer`), so interop is fully bidirectional.
- **Managed render flow** (`VspEngine.Rendering.RenderFlow`): every frame the native
  host invokes the managed render flow, which builds the frame (BeginFrame -> clear
  color -> triangle draws -> EndFrame) through the native render-command API
  (`Graphics/RenderCore`). The Vulkan renderer consumes the submitted commands when
  it records the frame's command buffer.
- **Split Vulkan module** (`Graphics/Vulkan`): the renderer is decomposed into
  functional units - `VulkanInstance` (instance + surface), `VulkanDevice` (device +
  queues + helpers), `VulkanBuffer`, `VulkanImage`, `VulkanPipeline`,
  `VulkanDescriptors` (bindless), `VulkanSwapChain` and `VulkanRenderer2D`
  (the orchestrator) behind a thin `VulkanContext` RHI facade.
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
- **Unity-style scripting model** (`VspEngine`): `GameObject` / `Component` / `ScriptBehaviour`
  with `OnInit / OnStart / OnUpdate / OnDestroy`, `Input.GetKey(...)`, `Time.DeltaTime`,
  `Transform.Position`.
- C++20, MSVC, **no C++ exceptions** (`/EHs-c-`, `_HAS_EXCEPTIONS=0`). Function names are
  PascalCase; class fields use Hungarian notation (`m_pDevice`, `m_nFrameIndex`, `m_fDeltaTime`, ...).
  The precompiled header (`Common/RuntimePCH.h`) contains **no standard-library includes**:
  every translation unit and header declares the headers it actually uses.

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
vulkan-1.dll (from `%VULKAN_SDK%\Bin`), and ensures the C# runtime is
reachable through the engine's relative lookup
(`<exeDir>\..\..\..\Binaries\dotnet\runtime10.0.10`) - copying the whole
runtime tree there when it is missing. MSBuild is located automatically
(--msbuild override, MSBUILD environment variable, vswhere, known install paths).

Standard layout: every build artifact (binaries, obj/, NuGet restore caches,
generated headers) lives under `Engine/Intermediate` (or `Engine/Binaries` for
third-party libraries) - nothing is ever written into `Engine/Source`. The shipped
.NET runtime lives in `Engine/Binaries/dotnet/runtime10.0.10` and is located
automatically relative to the executable.

## Running

```
Engine\Intermediate\Binaries\Debug_x64\Launch.exe
```

Useful flags (see `LaunchLoop.cpp`):

| Flag                     | Meaning                                              |
| ------------------------ | ---------------------------------------------------- |
| `--frames N`             | exit after N frames (smoke tests)                    |
| `--silent`               | errors go to the log; fatal crash prompt disabled    |
| `--key VK:MS`            | post synthetic WM_KEYDOWN/KEYUP to the engine window |
| `--capture N:path.bmp`   | save the framebuffer after frame N (BMP)             |

Environment switch: `VSP_NO_VALIDATION=1` disables the Vulkan validation layer.

Example acceptance run (see `Engine/Tools/Acceptance/AnalyzeShots.ps1` for analysis):

```
Launch.exe --silent --frames=445 ^
  --key 0x44:1500 --key 0x57:800 --key 0x41:800 --key 0x53:800 --key 0x52:0 ^
  --key 0x54:0 --key 0x54:0 --key 0x54:0 --key 0x54:0 ^
  --capture 40:shot0_initial.bmp --capture 148:shot1_after_D.bmp ^
  --capture 210:shot2_after_W.bmp --capture 276:shot3_after_A.bmp ^
  --capture 342:shot4_after_S.bmp --capture 358:shot5_after_R.bmp ^
  --capture 376:shot6_T_red.bmp --capture 394:shot7_T_blue.bmp ^
  --capture 412:shot8_T_green.bmp --capture 430:shot9_T_multi.bmp
```

## Layout

- `Engine/Source/Runtime/VspCore`    - the native engine DLL: windowing, events, input, the split Vulkan renderer, the script host and the render-command hub. All Windows-only code is encapsulated in its `Common/` folder behind `#if VSP_PLATFORM_WINDOWS`.
- `Engine/Source/Runtime/VspEngine`  - the engine's managed runtime assembly (VspEngine.dll): ScriptBehaviour, the Input/Time/Transform/Renderer facades, the interop bridge and the managed render flow.
- `Assembly`                          - the game Assembly (Assembly.dll) holding the user scripts (the demo TriangleController); loaded by the engine at startup.
- `Engine/Source/Runtime/Launch`     - the host executable: parses the command line, runs `GameEngine`.
- `Engine/Source/Programs/VspBuildTool` - the lightweight build system / stager described above.
- `Engine/Source/Thirdparty`  - imported third-party SDKs (Vulkan, dxc, glm, fmt, ...). Read-only: never modified.
- `Engine/Binaries`           - third-party binaries (dotnet runtime, Vulkan import lib, ...).
- `Engine/Intermediate`       - all build outputs: binaries, obj/, NuGet restore caches, generated shader header.
