# NEXUS Core

NEXUS Core is an experimental native C++ desktop lab combining three technical playgrounds:

- **Particle Lab** — interactive particle simulation and configurable forces.
- **Void Renderer** — procedural wireframe scene and rotating 3D geometry preview.
- **NetScope** — packet-flow dashboard. The MVP uses clearly labelled synthetic traffic; live capture is not implemented.
- **Performance** — frame timing, process working set on Windows, and module counters.

## Stack
C++20, CMake, GLFW 3.4, Dear ImGui 1.91.8, OpenGL.

CMake downloads the pinned dependencies on the first configure, so internet access is required for the first build.

## Build on Windows
Install Visual Studio 2022 with **Desktop development with C++**, Git, and CMake.

```powershell
git clone https://github.com/Kenkanekiqwe/nexuscore.git
cd nexuscore
cmake -S . -B build -A x64
cmake --build build --config Release
.\build\Release\NEXUSCore.exe
```

## Current limitations
This is an MVP foundation, not a production packet sniffer or full 3D engine. NetScope displays synthetic sample rows only and does not capture or transmit traffic. The renderer is currently a procedural wireframe preview drawn in the UI; a dedicated shader-based 3D pipeline and GPU timers are future work.

## Roadmap
1. Stabilize the native shell and module navigation.
2. Add spatial partitioning and repeatable particle benchmarks.
3. Add real 3D geometry, shaders, and GPU timing.
4. Add optional local packet capture via Npcap with explicit interface selection and filters.
5. Add benchmark export and regression tests.
