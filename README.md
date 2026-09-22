# VspEngine

A small experimental game engine: a **3D** Vulkan 1.3 bindless renderer, a
Unity-style managed object model and a **programmable render pipeline written in
C#**, hosted by a native C++20 application that embeds the .NET CoreCLR runtime
through the CoreCLR Hosting API (nethost + hostfxr). Shaders live in **.vsf**
files - one file that carries its properties, its settings, its variants and its
HLSL - and belong to the game Assembly, which compiles them to SPIR-V with
**HLSLCC**. The runtime never compiles HLSL: it only loads the compiled modules.

Scene objects carry **local** and **world** coordinates, cameras project
orthographically, perspectively or through a real lens, and everything is drawn in
3D with a depth buffer - **there is no separate 2D path**: 2D content is 3D
content on a plane, seen through the same camera.

The managed side is split into two assemblies: **VspEngine.dll** (the engine's
managed runtime: script base types, the object model, the input/time facades, the
interop bridge and the render-pipeline framework) and **Assembly.dll** (the game
Assembly: the user scripts, the render pipeline and the shaders). The engine ships
**no pipeline and no shader of its own** - rendering, and with it every shader
load, starts when the game says so.

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
| `Transform`           | local position / rotation / scale, the parent link and children, and the world matrix derived from them |
| `Component`           | owner game object, component kind, enable state, render state           |
| `Camera`              | projection mode, clip planes, the lens of a physical camera, and the view / projection / view-projection matrices that follow from its transform |
| `Scene` (singleton)   | the object tables, slot recycling and handle validation                 |

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
| `IGameModule`                 | what a game assembly implements to take over at startup          |

**The engine installs nothing.** Until a game installs a pipeline, a frame is
opened and closed with no draw command, which leaves the window cleared - and no
shader file is ever read. A game takes over from its `IGameModule`:

```csharp
public sealed class Game : IGameModule
{
    public void OnGameLoad() => RenderPipelineManager.ActivePipeline = new TriangleRenderPipeline();
    public void OnGameUnload() => RenderPipelineManager.ActivePipeline = null;
}
```

The engine calls `OnGameLoad` once, right after it loaded the game assembly.
`Assembly/Rendering/TriangleRenderPipeline.cs` is the worked example: it is
ordinary game code that loads its shader (`Shader.Load`), builds one graphics
pipeline per shader variant and draws the demo triangle.

### 4. Shaders are .vsf files compiled by HLSLCC; the engine only loads the SPIR-V

`Engine/Source/Programs/HLSLCC` is the engine's HLSL cross compiler: a command line
tool (`HLSLCC.exe`) plus the static library it is built from. **The runtime holds no
HLSL compiler.** VspCore neither includes nor links HLSLCC - it reads the files the
compiler wrote:

```
Assembly/Shaders/Triangle2D.vsf        the shader the GAME ships
        |  HLSLCC (during the Assembly's build)
        v
<run directory>/Shaders/               everything the ENGINE loads
        Triangle2D.vsfo                ONE container: every module + its reflection
        Triangle2D.vert.spv ...        the modules on their own (debugger food)
        Triangle2D.shader.json         the manifest, for tools
        Triangle2D.reflection.json     the reflection, for tools
```

The shader is game content: `Assembly/Shaders` holds it, and `Assembly.csproj`
runs `Engine/Tools/Build/CompileShaders.ps1` after its build to compile every
`.vsf` into the run directory's `Shaders` folder. `Graphics/ShaderLibrary` then
loads a container into a native shader **when the game asks for one by name**; the
engine loads no shader at startup.

**The `.vsfo` container is what the engine reads.** One file holds every SPIR-V
module of the shader - all passes, all kept variants, both stages - together with
the reflection of each of them. Its layout is an **index table** followed by a
**data segment** (`Engine/Source/Shared/VsfoFormat.h` is the single definition
that the writer in HLSLCC and the reader in VspCore both compile):

```
+----------------+  Vsfo::Header        where the sections are, how many modules exist
|    Header      |
+----------------+
|  Index table   |  Vsfo::IndexEntry[]  WHICH STAGE each module is, HOW BIG it is,
|                |                      WHERE it sits and WHAT its reflection
|                |                      summarises to (inputs, outputs, resources,
|                |                      push-constant size)
+----------------+
|  Data segment  |  the SPIR-V modules, each followed by its reflection record
|                |  (variables, resources with set/binding, push-constant members)
+----------------+
|   Metadata     |  UTF-8 JSON: the shader's name, render queue, properties,
|                |  keyword groups and its variants' keyword states
+----------------+
```

A reader therefore finds a stage without parsing anything: the index entry names
the stage, its entry point, its pass, its variant, the byte range of its module and
the reflection summary the engine checks its descriptor set against
(`Graphics/ShaderBindings.h`). A container that disagrees with the engine's table,
or that points outside its own bytes, is refused while loading, by name, instead of
failing inside the driver. The loose `.spv` files stay next to it for a graphics
debugger, and `Compiler.cpp`/`ShaderLibrary.cpp` never need them.

**The HLSL builtin library** ships with the compiler (`Engine/Shaders/Builtin`,
staged next to `HLSLCC.exe`) and is reached from a shader with an angle-bracket
include:

```hlsl
#include <Vsp/Builtin.hlsl>      // everything
#include <Vsp/Lighting.hlsl>     // or just what a shader needs
```

| Header | What it offers |
| ------ | -------------- |
| `Vsp/Common.hlsl`    | constants, `VspSafeNormalize`, remap/smoothstep, sRGB <-> linear, ACES tonemap, Fresnel |
| `Vsp/Transform.hlsl` | Euler/TRS matrices, point/direction/normal transforms, view direction |
| `Vsp/Texture.hlsl`   | the bindless array and sampler, sampling by slot (with LOD, gradients, texel fetch), packed-map readers |
| `Vsp/Normal.hlsl`    | normal-map unpacking, tangent frames from derivatives, world-space normals, normal blending |
| `Vsp/Lighting.hlsl`  | `VspLight`/`VspSurface`, Lambert, Blinn-Phong and metallic/roughness GGX, attenuation, light loops |

The library declares the engine's bindless resources by their canonical names
(`BindlessTextures`, `BindlessSampler`) and **no bindings**: like any other
shader source, it is numbered by the engine rule. `--builtin-directory` points the
lookup somewhere else.

**One file holds everything.** A shader is a single `.vsf` file, and its `Pass`
blocks live **inside** the `Shader` block - a `Shader` block may hold several of
them:

```hlsl
Properties
{
    _ColorMode ("Color Mode", Float) = 3
    _Tint ("Tint", Color) = (1, 1, 1, 1)
}

Shader "Vsp/Triangle2D"
{
    Queue = "Geometry"
    Variant _TINT_ENABLED              // strippable keyword group

    Pass                               // one or more Pass blocks, inside Shader
    {
        #pragma vertex PassVertex       // the entry points live in the file
        #pragma fragment PassFragment
        #pragma multi_variant_local _FLAT_COLOR

        #include "Triangle2DCommon.hlsl" // Pass blocks may include HLSL

        PassVertexOutput PassVertex(PassVertexInput input) { ... }
        float4 PassFragment(PassFragmentInput input) : SV_Target0 { ... }
    }

    Pass "Second"                      // every Pass is compiled on its own
    {
        #pragma vertex PassVertex
        #pragma fragment PassFragment
        ...
    }
}
```

- **`Properties`** (top level) declares the values the editor shows; a material
  starts from their defaults.
- **`Shader`** is the shader itself: its settings (the render queue and the
  keyword groups) and its `Pass` blocks.
- **`Pass`** holds the shader code: HLSL, with `#include` and with the entry
  points named by `#pragma vertex` / `#pragma fragment`. A `Pass` written
  outside the `Shader` block is reported as a mistake.

**Variants.** Four pragmas declare keyword groups, and the difference between them
is what the build does with the variants nobody uses:

| Pragma                        | Keywords | Unused variants          |
| ----------------------------- | -------- | ------------------------ |
| `#pragma variant`             | global   | **stripped** from the build |
| `#pragma variant_local`       | local    | **stripped** from the build |
| `#pragma multi_variant`       | global   | **always compiled and kept** |
| `#pragma multi_variant_local` | local    | **always compiled and kept** |

A strippable group keeps the states the build reports with `--used-variant`
(`_` names the "no keyword" state) and only its default state when none is
reported; a group declared with the `multi_variant` pragmas keeps every state.
The same declarations can be written in the `Shader` block (`Variant`,
`VariantLocal`, `MultiVariant`, `MultiVariantLocal`).

**HLSLCC writes** (the Assembly's build runs exactly this)

```
HLSLCC.exe Assembly/Shaders/Triangle2D.vsf --output-directory <run>\Shaders [--used-variant _TINT_ENABLED]
  -> out/Triangle2D.vsfo                                  the container the ENGINE loads
  -> out/Triangle2D.vert.spv / Triangle2D.frag.spv        the default variant
  -> out/Triangle2D.<keywords>.vert.spv / .frag.spv       the other kept variants
  -> out/<name>.<pass>.vert.spv / .frag.spv               one module pair per Pass
  -> out/Triangle2D.shader.json                           the manifest, for tools
  -> out/Triangle2D.reflection.json                       full reflection of the default variant
```

**The engine loads what HLSLCC wrote.** `Graphics/ShaderLibrary` reads a
container into a native `Classes/Shader`, and `Classes/Material` pairs one of
those shaders with the values a component draws with. That is the engine's whole
involvement with shaders: it has no HLSL front end, no DirectX Shader Compiler
dependency and no runtime compile path.

**The Vulkan HLSL namespace is injected automatically.** HLSLCC prepends the
Vulkan `vk::` namespace (`#include <vk/spirv.h>`, with the include directory that
provides it) plus the engine's own attribute shorthands (`VSP_VK_BINDING`,
`VSP_VK_LOCATION`, `VSP_VK_OFFSET`, ...) in front of the shader's first
`#include`, so a shader file contains the shader and nothing else. The injection
is idempotent and can be switched off per file with
`#define VSP_NO_VULKAN_NAMESPACE` or per run with `--no-vulkan-namespace`.

**The engine decides where a resource lives, the shader only names it.** A shader
never writes `[[vk::binding]]`, `[[vk::descriptor_set]]` or `register(...)`: it
declares the camera block, the bindless texture array and the sampler, and HLSLCC
numbers them to match the engine's descriptor set (`VspCore/Graphics/ShaderBindings.h`):

| Binding (set 0) | Kind | What the engine binds there | Constant |
| --------------- | ---- | --------------------------- | -------- |
| 0 | uniform buffer | the per-frame camera block | `k_nCameraUniformBuffer` |
| 1 | sampled image | the 4096-slot bindless texture array | `k_nBindlessTextures` |
| 2 | sampler | the sampler those images are read with | `k_nBindlessSampler` |

The numbering is by **resource kind and declaration order, across the whole Pass**,
so both stages of a Pass agree on one number for a resource they share, and the
camera block lands on 0 whatever a shader calls it. A container whose reflection
disagrees with the engine's table - an unknown kind, or a binding the engine does
not provide - is rejected while loading, by name, instead of failing inside the
driver.

A Pass that does write its own bindings keeps them. The test is made on the whole
compiled source, so a `[[vk::binding]]` or `register(...)` written in an included
`.hlsl` file counts just as much as one written in the Pass block itself, and
`--keep-explicit-bindings` turns the automatic assignment off for a whole run.

**Reflection comes from the SPIR-V itself.** HLSLCC reads the compiled module back
and reports the entry point, the interface variables with their locations and
types, the descriptor bindings with set/binding/kind, and the push-constant layout
- into the container's index table and reflection records, as
`<name>.reflection.json`, and as a console summary. The runtime keeps what it read
in `Classes/Shader` so a pipeline can size its push-constant range and check itself
against the shader it loaded.

### 5. A transform has two coordinate spaces; a camera turns them into pixels

**Local and world.** A `Transform` stores the LOCAL position, rotation and scale
a script writes, plus the parent link and the children that make up the scene
graph. The WORLD transform is derived from that chain and cached, so a child that
follows its parent pays for one matrix multiply, not for a walk up the graph:

```csharp
Transform child = someObject.Transform;
child.SetParent(parentTransform, keepWorldPosition: true);   // stays where it is
Vector3 local = child.LocalPosition;      // relative to the parent
Vector3 world = child.Position;           // where the scene says it is
Matrix4x4 toWorld = child.LocalToWorldMatrix;
```

`VspEngine/SceneSerializer` records a whole scene to JSON with BOTH spaces -
local is what a file stores, world is what that put in the scene - and reads it
back:

```json
{ "name": "DemoCube", "parentGameObjectHandle": 268435460,
  "localPosition": { "x": 0, "y": 0, "z": 0 },
  "worldPosition": { "x": 0, "y": 0, "z": -3 } }
```

**Cameras.** A `Camera` belongs to a game object, so its transform is where it is
and which way it looks (down its local -Z, Y up). Three projection modes:

| Mode | Described by | Use |
| ---- | ------------ | --- |
| `Orthographic` | `OrthographicSize` (half height in world units) | flat content: a world unit stays the same size however far away it is |
| `Perspective` | `FieldOfView` (vertical, degrees) | the usual 3D camera |
| `Physical` | `FocalLength`, `SensorWidth`/`SensorHeight`, `Aperture`, `FocusDistance` | a real lens: a 50 mm lens on a full-frame sensor is 27 degrees, and the aperture and focus distance travel to the shader |

A pipeline renders from a camera by handing it to the frame:

```csharp
commandBuffer.SetCamera(camera);   // fills the engine's camera uniform buffer
```

The engine writes that buffer from the camera - view-projection, view, projection,
camera-to-world, the camera position, the aperture and the focus distance - so a
shader reads the camera the pipeline chose without managing a buffer of its own.

### 6. Everything is drawn in 3D

The render pass has a **color and a depth attachment** (one depth image per
swapchain image), and a pipeline states whether it tests and writes depth:

```csharp
builder.SetCullMode(CullMode.Back)
       .SetDepthTest(true, true, CompareOperation.LessOrEqual);
```

Meshes are drawn through a vertex buffer and an **index buffer**
(`DrawIndexed`), each draw carrying its own world matrix in the push-constant
block, and the camera block supplies the view-projection. There is no 2D
pipeline, no 2D render pass and no 2D shader convention: the demo's colored
triangle is a mesh in the same scene as the cube, drawn through the same camera,
culled and depth-tested exactly like it.

### 7. Materials drive what is drawn

A renderable component points at a **material**; the material points at a
**shader** and carries that shader's property values and keywords:

| Layer | Type | Role |
| ----- | ---- | ---- |
| Native | `Classes/Shader`   | the compiled modules of every variant, the queue, the properties, the keyword groups |
| Native | `Classes/Material` | one shader plus the values and keywords a draw uses |
| C#     | `VspEngine.Shader`  | reference handle: `Shader.Load("Triangle2D")` reads the compiled shader |
| C#     | `VspEngine.Material` | reference handle: `SetFloat`, `SetVector`, `SetKeywordEnabled`, `ResolveVariantIndex` |

`Assembly/Rendering/TriangleRenderPipeline.cs` builds **one graphics pipeline per
shader variant** and picks between them from each drawable's material, so a variant
is only paid for when something actually selects it. The engine names no shader
property: `_ColorMode` and `_Tint` live in `Assembly/Rendering/TriangleMaterial.cs`,
next to the shader that declares them.

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
  - Images and the sampler are separate descriptors (bindings 1 and 2), which is the
    layout HLSL `Texture2D` + `SamplerState` compiles to.
- **.vsf shader pipeline (HLSLCC, build time)** (`Programs/HLSLCC` +
  `Assembly/Shaders` + `Graphics/ShaderLibrary`):
  - one `.vsf` file per shader with its `Properties` block and its `Shader`
    block, which holds one or more `Pass` blocks,
  - entry points named in the file (`#pragma vertex` / `#pragma fragment`),
  - `#include` inside a Pass block, resolved against the shader's own directory,
  - four variant pragmas with the strip rules described above,
  - automatic injection of the Vulkan HLSL namespace and the engine attribute
    shorthands,
  - SPIR-V compilation through the DirectX Shader Compiler (loaded dynamically from
    the Vulkan SDK, `VSP_DXC_ROOT` or the executable's directory),
  - reflection read straight out of the produced SPIR-V: interface variables,
    descriptor bindings and the push-constant block layout,
  - **one `.vsfo` container per shader**: an index table (which stage, how big,
    what it reflects) plus a data segment (the modules and their reflection
    records), which is the single file the engine loads,
  - an **HLSL builtin library** (`Vsp/Common`, `Vsp/Transform`, `Vsp/Texture`,
    `Vsp/Normal`, `Vsp/Lighting`) that a shader reaches with
    `#include <Vsp/Builtin.hlsl>`; HLSLCC resolves it against its own directory
    (`--builtin-directory` overrides that),
  - the shader BELONGS to the game Assembly, which compiles it while it builds;
    the engine only loads the resulting container.
- **The game owns rendering** (`Assembly/Rendering` + `VspEngine.IGameModule`):
  the engine ships no pipeline and no shader. `Game.OnGameLoad` installs the
  game's pipeline, which loads its shader with `Shader.Load` and draws with it -
  everything a renderer does lives in the game Assembly, one `RenderPipeline`
  class and a couple of helpers next to the scripts.
- **Materials** (`Classes/Material` + `VspEngine.Material`): a component draws with
  a material, the material owns the shader plus its property values and keywords,
  and the pipeline keeps one graphics pipeline per shader variant.
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
3D scene: a **camera**, a **cube** hanging off a rig, and the multicolor triangle -
the demo's 2D content, drawn as a mesh in the same 3D scene.

| Key | Action                                                        |
| --- | ------------------------------------------------------------- |
| W   | move the triangle up (world +Y)                                |
| A   | move it left (world -X)                                        |
| S   | move it down (world -Y)                                        |
| D   | move it right (world +X)                                       |
| Q / E | move it away from / towards the camera (world Z)             |
| R   | reset its position                                             |
| T   | cycle its color: red -> blue -> green -> multicolor            |
| Z / X | send it behind the cube / bring it back (the depth test)     |
| C   | cycle the camera: orthographic -> perspective -> physical       |
| F5 / F9 | record the scene to `DemoScene.json` / read it back        |

The script owns no render state: it writes world positions and material properties
into the native scene, and the game's render pipeline reads them while it builds
the frame. `Z` is the visible proof that the depth buffer works - the triangle is
drawn *after* the cube, so only depth testing can hide it.

## Building

Requirements: Visual Studio 2026 (v145 toolset), .NET SDK 10 and a Vulkan SDK
(the project looks for `Engine/Source/Thirdparty/Vulkan` first, then the
`VULKAN_SDK` environment variable).

Shaders are `.vsf` files and are compiled to SPIR-V by **HLSLCC**, which needs the
DirectX Shader Compiler (`dxcompiler.dll`). It is loaded at runtime from, in
order: the directory of the executable, `%VSP_DXC_ROOT%\bin`, `%DXC_ROOT%\bin`,
`%VULKAN_SDK%\Bin` (the Vulkan SDK ships DXC), and the repository's own
`Engine/Source/Thirdparty/dxc`. No import library is needed.

The game Assembly's build step runs `Engine/Tools/Build/CompileShaders.ps1`, which
compiles every `Assembly/Shaders/*.vsf` into the `Shaders` folder next to the
executable - the folder the engine loads from. The step skips itself while
`HLSLCC.exe` is not built yet, and the acceptance run always recompiles the shaders
itself.

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
visible to the loader; `VSP_DXC_ROOT=<dir>` points HLSLCC at a DirectX Shader
Compiler installation.

### HLSLCC (shader compiler)

```
Engine\Intermediate\Binaries\Debug_x64\HLSLCC.exe <shader.vsf> [options]

  --output-directory <dir>   where the .vsfo/.spv/.json files go (created on demand)
  --builtin-directory <dir>  where the HLSL builtin library (<Vsp/...>) lives
                             (default: <exe dir>\Builtin; the build stages it)
  --used-variant <state>     keyword state the build uses; repeatable.
                             Strippable groups keep only these states, and their
                             default state when none is reported ("_" = none)
  --vertex-entry <name>      entry point for passes without a '#pragma vertex'
  --fragment-entry <name>    entry point for passes without a '#pragma fragment'
  --target-env <env>         SPIR-V target environment (default: vulkan1.3)
  -I <dir>                   extra include directory
  -D <name>[=value]          preprocessor definition
  --no-vulkan-namespace      keep the injected Vulkan HLSL namespace out
  --no-attribute-shorthands  keep the VSP_VK_* shorthands out
  --keep-explicit-bindings   keep the bindings a shader names itself
  --reflection-only          write only the reflection document
  --debug-info               compile unoptimised with debug information
  --quiet                    report failures only
```

## Acceptance test

```
powershell -NoProfile -ExecutionPolicy Bypass -File Engine\Tools\Acceptance\RunAcceptance.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File Engine\Tools\Acceptance\RunAcceptance.ps1 -Configuration Release
```

The script runs both halves of the acceptance test:

1. **Shader compilation** (`VerifyShaderCompilation.ps1`) runs `HLSLCC.exe` over
   the game's `Assembly/Shaders/Triangle2D.vsf` - the one file that defines both
   `PassVertex` and `PassFragment` - and checks that

   - the `Properties` block, the `Shader` block, the `Pass` block nested inside
     it, both entry-point pragmas and the `#include` of the Pass block are all
     read out of the file,
   - one SPIR-V binary per stage of the default variant is written, each starting
     with the SPIR-V magic number and carrying the entry point of its stage,
   - `spirv-val` accepts both modules (when the SDK ships it),
   - the reflection document describes both stages, the four vertex attributes,
     the camera uniform buffer, the bindless sampled-image array, the sampler and
     the 128-byte, eight-member push-constant block (the block carries a draw's
     world matrix, which is why it is the size Vulkan guarantees),
   - **the `.vsfo` container** the engine loads is well formed: the magic number,
     the version, the section table inside the file, one index entry per module of
     every variant, the stage/entry-point/pass/variant each entry names, the
     modules byte-identical to the loose `.spv` files, and the reflection the
     entries summarise and point at (camera block, bindless array, sampler, the
     128-byte push-constant block),
   - the manifest names the shader, its queue, its properties, its keyword groups
     and the module file of every stage - and every module it names exists,
   - the strip rules hold: without `--used-variant` the strippable group ships
     only its default state while `multi_variant_local` ships both, reporting
     `_TINT_ENABLED` swaps which state ships, and reporting both states keeps
     both,
   - a `Shader` block with **two** `Pass` blocks compiles both of them
     (`MultiPassTest.Forward.*.spv` and `MultiPassTest.Tinted.*.spv`), and a
     `Pass` written outside the `Shader` block is rejected with a clear error,
   - no shader source writes a binding itself, while a shader that DOES
     (`ExplicitBindingTest`, with the binding inside an included file) keeps its
     own numbers,
   - a shader that uses the **HLSL builtin library**
     (`BuiltinLibraryTest.vsf`, `#include <Vsp/Builtin.hlsl>`) compiles and its
     bindless resources land on the engine's bindings.

   The compiled output goes into the run directory's `Shaders` folder, so the
   same command compiles the shader the demo then loads.

2. **Engine behaviour** builds the solution, enables the Vulkan validation layer
   when an SDK is present, runs the demo with
   `--fixed-delta-time=16.6667 --frames=445` together with the scheduled key
   presses and captures below, **fails on any `[ERROR]`, `[WARNING]` or
   `[FATAL]` entry in the engine log**, and finally checks the captured frames
   with `AnalyzeShots.ps1`:

   - movement direction (D/W/A/S), reset position (R) and all four color modes (T),
   - **the depth test**: sent behind the cube with `Z`, the triangle has to
     disappear - it is drawn after the cube, so only a depth buffer can hide it -
     and `X` has to bring it back,
   - **the three projections**: the same scene through an orthographic, a 45-degree
     perspective and a 50 mm physical camera produces three different images, with
     the perspective one showing less of the triangle than the orthographic one and
     the 50 mm lens framing tighter still.

   It then reads what the run left behind: the `DemoScene.json` the demo recorded
   (at least four objects, every one with local AND world coordinates, and the cube
   at local z=0 / world z=-3 under its parent - the hierarchy surviving the round
   trip) and the log (all three camera projection modes exercised).

   It then patches the binding of the camera block **inside the container** and
   runs the engine once more: the load must be refused, by name, with
   "the shader 'Triangle2D' reads 'CameraUniformBuffer' ... at set 0 binding 5, but
   the engine provides it at set 0 binding 0". That closes the loop from the bytes
   on disk to the error message.

The shader half can be run on its own:

```
powershell -NoProfile -ExecutionPolicy Bypass -File Engine\Tools\Acceptance\VerifyShaderCompilation.ps1
```

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
- `Assembly`                          - the game Assembly (Assembly.dll): the user scripts (the demo `TriangleController`), the render pipeline the game installs (`Rendering/`) and the shaders it ships (`Assembly/Shaders/*.vsf`), which HLSLCC compiles during its build.
- `Engine/Source/Runtime/Launch`     - the host executable: parses the command line, runs `GameEngine`.
- `Engine/Source/Programs/HLSLCC`  - the HLSL cross compiler `HLSLCC.exe`: the .vsf parser, Vulkan namespace injection, the DXC backend, the engine's binding rules, SPIR-V reflection and the `.vsfo` container writer. Nothing links it into the runtime.
- `Engine/Source/Shared`            - `VsfoFormat.h`, the one definition of the shader-container format that both HLSLCC (writer) and VspCore (reader) compile.
- `Engine/Shaders/Builtin`          - the HLSL builtin library (`Vsp/Common`, `Transform`, `Texture`, `Normal`, `Lighting`, `Builtin`), staged next to HLSLCC.exe and included by shaders as `<Vsp/...>`.
- `Engine/Source/Programs/VspBuildTool` - the lightweight build system / stager described above.
- `Engine/Source/Thirdparty`  - imported third-party SDKs (Vulkan, dxc, glm, fmt, ...). Read-only: never modified.
- `Engine/Binaries`           - third-party binaries (dotnet runtime, Vulkan import lib, ...).
- `Engine/Intermediate`       - all build outputs: binaries, obj/, NuGet restore caches, generated shader header.
- `Engine/Tools/Build`        - the build steps: `CompileShaders.ps1` compiles a game's `.vsf` shaders into the run directory's `Shaders` folder.
- `Engine/Tools/Acceptance`   - the acceptance runner, the shader-compilation verifier and the captured-frame analyzer.
- `Engine/Intermediate/Binaries/<config>/Shaders` - the compiled shaders the engine loads: the `.vsfo` container per shader, next to the loose SPIR-V modules and the JSON documents the tools read.
- `Engine/Intermediate/Binaries/<config>/Builtin` - the HLSL builtin library staged next to HLSLCC.exe.