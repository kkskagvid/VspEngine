# VspEngine

A small experimental game engine: a Vulkan 2D renderer and a Unity-style C# scripting
system, hosted by a native C++20 application that embeds the .NET CoreCLR runtime
through the CoreCLR Hosting API (nethost + hostfxr).

## Features

- **Vulkan renderer with automatic feature-path negotiation**
  - Device reports **Vulkan < 1.2**  -> startup error: "Unsupported device" (no exceptions used anywhere).
  - Device reports **Vulkan 1.3** + descriptor-indexing features -> **bindless path**
    (4096-slot sampled-image array indexed with `nonuniformEXT`).
  - Otherwise (**Vulkan 1.2**, or 1.3 without bindless features) -> classic-descriptor **fallback path**.
  - Both paths render the same demo triangle; only one is built per device.
- **Complete keyboard + mouse input** (`Core/Input/InputManager`): held/pressed/released
  edge state, mouse position/delta/wheel, typed characters. Window messages flow
  Win32 -> events -> InputManager -> scripts.
- **C# scripting via CoreCLR Hosting API** (`Scripting/ScriptEngine`):
  - `nethost.dll` -> `get_hostfxr_path` -> `hostfxr_initialize_for_runtime_config` ->
    `load_assembly_and_get_function_pointer`.
  - The C++ host calls into managed `VspEngine.NativeBridge` (`[UnmanagedCallersOnly]` entry points)
    to create and drive script instances by **non-negative integer InstanceID** (0 = invalid).
  - Managed code calls back into the native runtime through `DllImport("VspRuntime")`
    (`Input`, `Time`, `Transform`, `Renderer`), so interop is fully bidirectional.
- **Unity-style scripting model** (`VspPlayer`): `GameObject` / `Component` / `ScriptBehaviour`
  with `OnInit / OnStart / OnUpdate / OnDestroy`, `Input.GetKey(...)`, `Time.DeltaTime`,
  `Transform.Position`.
- C++20, MSVC, **no C++ exceptions** (`/EHs-c-`, `_HAS_EXCEPTIONS=0`). Function names are
  PascalCase; class fields use Hungarian notation (`m_pDevice`, `m_nFrameIndex`, `m_fDeltaTime`, ...).

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

Requirements: Visual Studio 2026 (v145 toolset), .NET SDK 10, the Vulkan SDK
(`VULKAN_SDK` environment variable). Shaders are compiled to SPIR-V and embedded
automatically by `Graphics/Vulkan/Shaders/CompileShaders.ps1` (PreBuildEvent).

```
msbuild VspEngine.slnx /restore /p:Configuration=Debug /p:Platform=x64
```

All binaries land in `Engine/Intermediate/Binaries/<Config>_x64/`; the shipped .NET
runtime lives in `Engine/Binaries/dotnet/runtime10.0.10` and is located automatically
relative to the executable.

## Running

```
Engine\Intermediate\Binaries\Debug_x64\Launch.exe
```

Useful flags (see `LaunchLoop.cpp`):

| Flag                     | Meaning                                              |
| ------------------------ | ---------------------------------------------------- |
| `--frames N`             | exit after N frames (smoke tests)                    |
| `--silent`               | errors go to the log instead of a message box        |
| `--key VK:MS`            | post synthetic WM_KEYDOWN/KEYUP to the engine window |
| `--capture N:path.bmp`   | save the framebuffer after frame N (BMP)             |

Environment switches: `VSP_NO_VALIDATION=1` disables the validation layer,
`VSP_FORCE_FALLBACK=1` forces the Vulkan 1.2 fallback path.

Example acceptance run (see `Engine/Tools/Acceptance/AnalyzeShots.ps1` for analysis):

```
Launch.exe --silent --frames=445 ^
  --key 0x44:1500 --key 0x57:800 --key 0x41:800 --key 0x53:800 --key 0x52:0 ^
  --key 0x54:0 --key 0x54:0 --key 0x54:0 --key 0x54:0 ^
  --capture 40:shot0.bmp ... --capture 432:shot9.bmp
```

## Layout

- `Engine/Source/VspRuntime` - the engine DLL: windowing, events, input, Vulkan renderer, script host.
- `Engine/Source/VspPlayer`   - the managed game assembly: ScriptBehaviour, Input/Time/Transform facades, demo script.
- `Engine/Source/Launch`      - the host executable: parses the command line, runs `GameEngine`.
- `Engine/Binaries`           - third-party binaries (dotnet runtime, Vulkan import lib, ...).
