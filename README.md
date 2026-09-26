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
    public void OnGameLoad() => RenderPipelineManager.ActivePipeline = new LitCubeRenderPipeline();
    public void OnGameUnload() => RenderPipelineManager.ActivePipeline = null;
}
```

The engine calls `OnGameLoad` once, right after it loaded the game assembly.
`Assembly/Rendering/LitCubeRenderPipeline.cs` is the worked example: it is
ordinary game code that loads its shader (`Shader.Load`), builds one graphics
pipeline per shader variant, assembles the frame as a **render graph** (see
"8. A frame is a render graph") and draws the demo's lit cube plus its flat
interface.

### 4. Shaders are .vsf files compiled by HLSLCC; the engine only loads the SPIR-V

`Engine/Source/Programs/HLSLCC` is the engine's HLSL cross compiler: a command line
tool (`HLSLCC.exe`) plus the static library it is built from. **The runtime holds no
HLSL compiler.** VspCore neither includes nor links HLSLCC - it reads the files the
compiler wrote:

```
Assembly/Shaders/LitCube.vsf           the shaders the GAME ships
Assembly/Shaders/UiQuad.vsf
        |  HLSLCC (during the Assembly's build)
        v
<run directory>/Shaders/               everything the ENGINE loads
        LitCube.vsfo                   ONE container per shader: every module + its reflection
        LitCube.vert.spv ...           the modules on their own (debugger food)
        LitCube.shader.json            the manifest, for tools
        LitCube.reflection.json        the reflection, for tools
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
    _BaseColor ("Base Color", Color) = (1, 1, 1, 1)
    _Ambient ("Ambient", Float) = 0.35
}

Shader "Assembly/LitCube"
{
    Queue = "Geometry"
    Variant _TINT_ENABLED              // strippable keyword group

    Pass                               // one or more Pass blocks, inside Shader
    {
        #pragma vertex PassVertex       // the entry points live in the file
        #pragma fragment PassFragment
        #pragma multi_variant_local _FLAT_COLOR

        #include "LitCubeCommon.hlsl" // Pass blocks may include HLSL

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
HLSLCC.exe Assembly/Shaders/LitCube.vsf --output-directory <run>\Shaders [--used-variant _UNLIT]
  -> out/LitCube.vsfo                                     the container the ENGINE loads
  -> out/LitCube.vert.spv / LitCube.frag.spv              the default variant
  -> out/LitCube.<keywords>.vert.spv / .frag.spv          the other kept variants
  -> out/<name>.<pass>.vert.spv / .frag.spv               one module pair per Pass
  -> out/LitCube.shader.json                              the manifest, for tools
  -> out/LitCube.reflection.json                          full reflection of the default variant
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
| C#     | `VspEngine.Shader`  | reference handle: `Shader.Load("LitCube")` reads the compiled shader |
| C#     | `VspEngine.Material` | reference handle: `SetFloat`, `SetVector`, `SetKeywordEnabled`, `ResolveVariantIndex` |

`Assembly/Rendering/LitCubeRenderPipeline.cs` builds **one graphics pipeline per
shader variant** and picks between them from each drawable's material, so a variant
is only paid for when something actually selects it. The engine names no shader
property: `_BaseColor` and `_Ambient` live in `Assembly/Rendering/LitCubeMaterial.cs`,
next to the shader that declares them.

### 8. A frame is a render graph

A pipeline that draws more than one thing stops being a list of calls and becomes a
graph of passes. `VspEngine.Rendering.RenderGraph` is that graph:

```csharp
frameGraph.Reset();
RenderGraphTextureHandle fontAtlas = frameGraph.CreateTexture("UiFontAtlas", 1024, 1024, slot);

frameGraph.AddPass("Cube").WriteBackBuffer().SetExecute(DrawCubePass).Done();
frameGraph.AddPass("Interface").WriteBackBuffer()
          .ReadTexture(fontAtlas).SetExecute(DrawInterfacePass).Done();
frameGraph.AddPass("DiagnosticsOverlay").WriteBackBuffer()
          .SetEnabled(false).SetExecute(DrawOverlayPass).Done();   // culled

if (frameGraph.Compile())
{
    frameGraph.Execute(context);      // the surviving passes, in dependency order
}
```

`Compile()` culls every pass that cannot reach the back buffer, orders what is left
with a stable topological sort (so independent passes keep the order they were added
in), refuses a graph with no back-buffer pass and reports a cycle by name instead of
hanging. It also tracks each resource's first and last use - its lifetime - and the
peak number of live resources. Logging in a graph that is compiled once per frame
would bury the log, so nothing is written on the success path: a pipeline that wants
the details asks for `GetDebugSummary()`.

### 9. The interface, and the text in it

`VspEngine.UI` is a small flat-style widget framework: `Canvas` is the root and
owns the theme, the fonts and the render-target size; `Image` is a filled rectangle
(or a textured one) with an optional hairline border; `Text` is a run of text;
`Button` is the state machine - rest, hover, press, release - with a click that
fires only when the release lands inside it. `UiRenderer` owns what the widgets do
not: the shader, one pipeline, one dynamic vertex and index buffer for the whole
frame, and the 1x1 white texture solid fills sample.

Text does not come from a bitmap font baked into the engine. `Core/Text` rasterizes
a real face with **FreeType** into a 1024x1024 coverage atlas (white RGB, coverage in
alpha) on demand, and lays a UTF-8 run out into one glyph quad per code point, with
kerning from the face's `kern` table. A shader samples the atlas' alpha and
multiplies it by the vertex colour, which is why a solid panel, a glyph and an icon
are all the same draw.

Every string a player reads goes through **`Core/String/I18N`**, whose text is an
**ICU `UnicodeString`** and whose locales are **ICU `Locale`** objects:

- a `UnicodeString` is a length-counted UTF-16 string that already knows the Unicode
  rules the engine would otherwise have to write itself - code point access that never
  splits a surrogate pair, substring, search and comparison in code point order, and
  UTF-8 conversion that always produces well-formed text;
- an `ic::Locale` parses `"zh-CN"`, `"zh_Hans_CN"` and `"zh-CN-u-nu-latn"` itself,
  and `Locale::getDefault()` is what tells the engine what the machine is set to;
- a translation crosses the C ABI as **UTF-16 code units** - the encoding the native
  string already holds and the one a .NET `string` is made of - so the managed side
  gets the text with no conversion in between. The engine's own `VspString` stays the
  UTF-8 type of the file system, the log line and the C ABI; `I18N::FromUtf8` and
  `I18N::ToUtf8` are the only two places the encodings meet.

A key (`demo.title`) is resolved in the current locale, then in the fallback locale,
and finally resolves to the key itself - so a half-translated interface shows something
a developer can act on instead of a blank label. Catalogs are JSON files, one per
locale, staged next to the executable. `Text` takes a `TranslationKey` or a
`Literal`, and the widgets never see a hardcoded string.

### 10. Errors, and how the engine reports them

The runtime is built with `/EHs-c-` and `_HAS_EXCEPTIONS=0`, so **nothing throws**
and every failure travels through a return value. There are exactly two kinds, and
each has one prescribed reaction (`Core/Diagnostics/ErrorHandling.h`):

| Kind | Reaction |
| ---- | -------- |
| **Non-fatal** - the engine can keep running | log the reason, then return the EMPTY value (`nullptr`, 0, `false`, an invalid handle, an empty `VspString`); a void function just returns. `VSP_RETURN_EMPTY` / `VSP_RETURN_VOID` |
| **Fatal** - the engine cannot continue | log the reason, then call `ProcessFailedExit`, which writes the fatal entry with the collected log history, flushes every backend, shows the crash prompt when prompts are enabled and terminates with a non-zero exit code |

`ProcessFailedExit` is declared `[[noreturn]]`, so every call site reads as "the
engine stops here", and `Log` itself no longer terminates: writing a fatal entry
collects the crash report, killing the process is the exit path's job.

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
- **A render graph in C#** (`VspEngine.Rendering.RenderGraph`): passes declare what
  they read and write, the graph culls what cannot reach the back buffer, orders the
  rest by dependency with a stable topological sort, refuses a graph with no
  back-buffer pass or with a cycle, and reports resource lifetimes and the peak
  number of live resources.
- **Native vector and matrix maths** (`VspCore/Math`): `Vector2`, `Vector3`,
  `Vector4`, `Matrix4x4` (column-major, the engine's Euler convention and Vulkan
  clip space) and `Quaternion`, exported to managed code as `VspMath_*` and wrapped
  by `VspEngine.NativeMath` - so the engine has one implementation of its own
  conventions instead of two that can drift apart.
- **A UI framework** (`VspEngine.UI`): `Canvas`, `Image`, `Text` and `Button` in a
  flat, minimal style - solid fills, one accent colour, hairline borders, no
  gradients and no shadows - drawn through the same camera and the same render pass
  as the 3D scene.
- **Real text, localised** (`Core/Text` + `Core/String/I18N`): FreeType rasterizes
  the face into a coverage atlas on demand, text is laid out one glyph quad per code
  point, and every string is a key resolved against a JSON catalog per locale with a
  fallback locale and the key itself as the last resort. The localisation service holds
  its text as **ICU `UnicodeString`** and its locales as **ICU `Locale`**, and hands a
  translation to managed code as UTF-16 code units - the encoding both sides already
  use.
- **The engine's error rule** (`Core/Diagnostics/ErrorHandling.h`): a non-fatal
  failure logs and returns the empty value, a fatal one logs and calls
  `ProcessFailedExit`, which reports the collected log history and terminates with a
  non-zero exit code. `Log` itself never terminates the process.
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

`Assembly/CubeController.cs` (the game Assembly loaded by the engine) drives the
demo: a **lit, per-face coloured cube** seen through a **third-person camera that
follows it**, plus a flat interface drawn on top of the scene.

The camera opens 45 degrees above the horizon, behind the cube, and always looks AT
it: the pointer turns the ORBIT, so the cube stays centred whatever the view does.
The light shines straight down (90 degrees to the horizon), so the top face is
fully lit while the four sides receive only the ambient fill - which is what makes
the shape read as a solid.

| Input | Action                                                          |
| ----- | --------------------------------------------------------------- |
| W / A / S / D | move the cube over the ground, relative to the camera    |
| Mouse         | orbit the camera around the cube (it always looks at it) |
| Mouse wheel   | move the camera closer to / further from the cube        |
| T             | flip the spin: clockwise about Y, or counter-clockwise   |
| R             | send the cube back to the origin                         |

The cube spins about its own Y axis from the first frame on; T only decides WHICH
WAY. Every static string the interface shows is a localisation KEY
(`demo.title`, `demo.hint`, ...) resolved by the engine's native
`Core/String/I18N` service from `Assembly/Locales/<locale>.json`, so the same
build reads correctly in English and in Chinese.

The script owns no render state: it writes world positions and rotations into the
native scene, and the game's render pipeline reads them while it builds the frame.

## Building

Requirements: Visual Studio 2026 (v145 toolset), .NET SDK 10 and a Vulkan SDK
(the project looks for `Engine/Source/Thirdparty/Vulkan` first, then the
`VULKAN_SDK` environment variable).

The runtime links two **static** third-party libraries, and each ships BOTH CRT
flavours because the linker refuses to mix a release-built static library into a
`/MDd` image (`LNK2038`):

| Library | Release | Debug | Source |
| ------- | ------- | ----- | ------ |
| FreeType 2.14.3 | `Engine/Binaries/freetype/freetype.lib` | `freetyped.lib` | `Engine/Source/Thirdparty/FreeType2` |
| ICU 77.1 (common + i18n, data stubbed) | `Engine/Binaries/ICU/icu.lib` | `icud.lib` | `Engine/Source/Thirdparty/ICU/icu4c-77_1` |

ICU's debug library is built from the recipe the repository already carries, with
the CMake that ships with Visual Studio:

```
cmake -S Engine/Source/Thirdparty/ICU/icu4c-77_1/BuildToStaticLibrary ^
      -B Engine/Intermediate/ICU/build -G "Visual Studio 18 2026" -A x64 ^
      -DCMAKE_CXX_STANDARD=17 ^
      -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=Engine/Intermediate/ICU/lib
cmake --build Engine/Intermediate/ICU/build --config Debug --parallel
copy Engine\Intermediate\ICU\lib\Debug\icu.lib Engine\Binaries\ICU\icud.lib
```

It needs no ICU data files: the runtime uses `UnicodeString` and `Locale`, and
neither reads locale data, which is why the recipe builds ICU with
`UCONFIG_NO_FILE_IO` and `stubdata.cpp`.

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
   the game's two shaders - `Assembly/Shaders/LitCube.vsf` (the lit cube) and
   `Assembly/Shaders/UiQuad.vsf` (the flat interface) - and checks that

   - the `Properties` block, the `Shader` block, the `Pass` block nested inside
     it, the entry-point pragmas and the `#include` of the Pass block are all
     read out of the file,
   - one SPIR-V binary per stage of the default variant is written, each starting
     with the SPIR-V magic number and carrying the entry point of its stage,
   - `spirv-val` accepts both modules (when the SDK ships it),
   - the reflection document describes both stages, the vertex attributes, the
     resources and the push-constant block, and **every resource sits on the
     engine's own binding**: a uniform buffer on `k_nCameraUniformBuffer`, a
     sampled image on `k_nBindlessTextures`, a sampler on `k_nBindlessSampler`.
     That check is what fails when a shader that declares FEWER resources than the
     engine provides is numbered as if the missing kinds were still there,
   - **the `.vsfo` container** the engine loads is well formed: the magic number,
     the version, the section table inside the file, one index entry per module of
     every variant, the stage/entry-point/pass/variant each entry names, the
     modules byte-identical to the loose `.spv` files, and the reflection the
     entries summarise and point at,
   - the manifest names the shader, its queue, its properties, its keyword groups
     and the module file of every stage - and every module it names exists,
   - the strip rules hold: without `--used-variant` a strippable group ships only
     its default state, reporting a keyword swaps which state ships, and reporting
     every state keeps every state,
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
   `--fixed-delta-time=16.6667 --frames=460` together with the scheduled key
   presses, mouse moves and captures below, **fails on any `[ERROR]`, `[WARNING]`
   or `[FATAL]` entry in the engine log**, and checks the captured frames with
   `AnalyzeShots.ps1`:

   - **the cube is on screen in every capture** - the camera follows it, so it can
     never leave the frame however far it moves,
   - it is **colourful**: several face colours appear over the run (the per-face
     colours are what the spin makes visible),
   - the **light comes from straight above**: the upper part of the cube is far
     brighter than the lower part,
   - the **interface is drawn on top** of the scene, in the corner its panel is
     pinned to.

   What a still frame cannot show is read out of the demo's once-a-second state
   lines instead, because a cube a follow camera keeps centred looks the same
   wherever it is:

   - **W / D / A moved it** the way the camera's frame says they should, and **R**
     put it back at the origin,
   - **T flipped the spin** to counter-clockwise and the second T flipped it back,
     while the cube kept spinning the whole time,
   - the camera stayed **45 degrees above the cube at a fixed distance** - the
     follow camera never lost it - and a **300-pixel mouse move turned the yaw by
     exactly the 45 degrees the sensitivity asks for**, without touching the pitch,
   - the **localisation catalog was read** (`Locales/<locale>.json`, staged next
     to the executable by the Assembly's build), and both shaders and the render
     graph reported themselves ready.

   It then patches the binding of the camera block **inside the container** and
   runs the engine once more: the load must be refused, by name, with
   "the shader 'LitCube' reads 'CameraUniformBuffer' ... at set 0 binding 5, but
   the engine provides it at set 0 binding 0". That closes the loop from the bytes
   on disk to the error message.

The shader half can be run on its own:

```
powershell -NoProfile -ExecutionPolicy Bypass -File Engine\Tools\Acceptance\VerifyShaderCompilation.ps1
```

The equivalent command line:

```
Launch.exe --silent --frames=460 --fixed-delta-time=16.6667 ^
  --key 0x54:0 --key 0x57:1500 --key 0x44:1500 --key 0x41:1500 ^
  --key 0x54:0 --key 0x52:0 ^
  --mouse 400:300:6300 --mouse 700:300:6400 ^
  --capture 40:shot0_initial.bmp --capture 60:shot1_after_T.bmp ^
  --capture 150:shot2_after_W.bmp --capture 235:shot3_after_D.bmp ^
  --capture 350:shot4_after_A.bmp --capture 430:shot5_after_reset.bmp
```

## Layout

- `Engine/Source/Runtime/VspCore`    - the native engine DLL. `Classes/` owns the object model, `Graphics/` the wrapped graphics API and the backends, `Scripting/` the CoreCLR host and every export managed code P/Invokes. All Windows-only code is encapsulated in its `Common/` folder behind `#if VSP_PLATFORM_WINDOWS`.
- `Engine/Source/Runtime/VspEngine`  - the engine's managed runtime assembly (VspEngine.dll): the object model reference handles, the Input/Time facades, the interop bridge, the RHI wrappers, the render-pipeline framework with the render graph (`Rendering/`), the UI framework (`UI/`), the localisation facade (`I18N.cs`) and the native-maths facade (`NativeMath.cs`).
- `Assembly`                          - the game Assembly (Assembly.dll): the user script (the demo `CubeController`), the render pipeline the game installs (`Rendering/`, including its render graph and the third-person camera rig), the flat interface it builds (`UI/`), the translation catalogs it ships (`Locales/*.json`) and the shaders it ships (`Assembly/Shaders/*.vsf`), which HLSLCC compiles during its build.
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
- `Engine/Source/Runtime/VspCore/Math` - the engine's own vector and matrix maths (`Vector2/3/4`, `Matrix4x4`, `Quaternion`), exported to managed code as `VspMath_*`.
- `Engine/Source/Runtime/VspCore/Core/Text` - the native text service (`Font`, `TextSystem`): FreeType rasterization into a coverage atlas, laid out into glyph quads.
- `Engine/Source/Runtime/VspCore/Core/String/I18N` - the localisation service: locales, JSON catalogs, fallback and lookup.
- `Engine/Source/Runtime/VspCore/Core/Diagnostics/ErrorHandling.h` - the engine's error rule and `ProcessFailedExit`.
- `Engine/Intermediate/Binaries/<config>/Locales` - the game's translation catalogs, staged next to the executable.
- `Engine/Intermediate/Binaries/<config>/Shaders` - the compiled shaders the engine loads: the `.vsfo` container per shader, next to the loose SPIR-V modules and the JSON documents the tools read.
- `Engine/Intermediate/Binaries/<config>/Builtin` - the HLSL builtin library staged next to HLSLCC.exe.