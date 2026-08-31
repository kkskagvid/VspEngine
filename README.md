# VspEngine

A small experimental game engine: a Vulkan 2D renderer and a Unity-style C# scripting
system, hosted by a native C++20 application that embeds the .NET CoreCLR runtime
through the CoreCLR Hosting API (nethost + hostfxr).

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
- **C# scripting via CoreCLR Hosting API** (`Scripting/ScriptEngine`):
  - `nethost.dll` -> `get_hostfxr_path` -> `hostfxr_initialize_for_runtime_config` ->
    `load_assembly_and_get_function_pointer`.
  - The **C++ host generates the non-negative InstanceID** (0 = invalid, 1-based) and
    enumerates the managed script types itself (GetScriptTypeCount / GetScriptTypeName).
  - Every managed `GameObject` holds its own `IntPtr` (`NativePtr`, the object's
    GCHandle); the host passes that pointer back on every lifecycle call, so
    `NativeBridge` keeps **no instances Dictionary**.
  - Managed code calls back into the native runtime through `DllImport("VspRuntime")`
    (`Input`, `Time`, `Transform`, `Renderer`), so interop is fully bidirectional.
- **Unity-style scripting model** (`VspPlayer`): `GameObject` / `Component` / `ScriptBehaviour`
  with `OnInit / OnStart / OnUpdate / OnDestroy`, `Input.GetKey(...)`, `Time.DeltaTime`,
  `Transform.Position`.
- C++20, MSVC, **no C++ exceptions** (`/EHs-c-`, `_HAS_EXCEPTIONS=0`). Function names are
  PascalCase; class fields use Hungarian notation (`m_pDevice`, `m_nFrameIndex`, `m_fDeltaTime`, ...).
  The precompiled header (`Common/RuntimePCH.h`) contains **no standard-library includes**:
  every translation unit and header declares the headers it actually uses.

## Demo (acceptance test)

`VspPlayer/TriangleController.cs` drives a multicolor triangle:

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
  --capture 40:shot0.bmp ... --capture 432:shot9.bmp
```

## Layout

- `Engine/Source/VspRuntime`  - the engine DLL sources: windowing, events, input, Vulkan renderer, script host.
- `Engine/Source/VspPlayer`   - the managed game assembly: ScriptBehaviour, Input/Time/Transform facades, demo script.
- `Engine/Source/Launch`      - the host executable: parses the command line, runs `GameEngine`.
- `Engine/Source/Thirdparty`  - imported third-party SDKs (Vulkan, dxc, glm, fmt, ...). Read-only: never modified.
- `Engine/Binaries`           - third-party binaries (dotnet runtime, Vulkan import lib, ...).
- `Engine/Intermediate`       - all build outputs: binaries, obj/, NuGet restore caches, generated shader header.
