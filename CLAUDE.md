# Project

OpenGL path tracer built as a graphics-programming homework. The C++ host code is a thin GLFW/GLAD harness that draws a single fullscreen quad and lets a fragment shader do all the tracing. Targets OpenGL 3.3 Core, C++11.

The reference scene is the classic "Ray Tracing in One Weekend" five-sphere setup (ground + diffuse / dielectric / dielectric-bubble / metal) rendered against a vertical sky gradient.

# Layout

- [src/main.cpp](src/main.cpp) — GLFW window, input, uniform setup, render loop, optional accumulation FBO ping-pong, FreeImage screenshot save.
- [shaders/shader_ray_tracing.vs](shaders/shader_ray_tracing.vs) — passthrough vertex shader for the fullscreen quad.
- [shaders/shader_ray_tracing.fs](shaders/shader_ray_tracing.fs) — the path tracer itself: sphere intersection, Lambertian / metal / dielectric scatter, Schlick Fresnel, gamma-corrected display, running-mean accumulation.
- [src/shader.h](src/shader.h), [src/camera.h](src/camera.h), [src/opengl_utils.h](src/opengl_utils.h) — LearnOpenGL-style helpers (shader program wrapper, FPS camera, VAO/VBO/EBO builder).
- [src/mesh.h](src/mesh.h), [src/model.h](src/model.h), [src/texture.h](src/texture.h), [src/texture_cube.h](src/texture_cube.h), [src/math_utils.h](src/math_utils.h) — carried over from the course template. Not used by the current ray-tracing scene (mesh/model/cubemap code is included but inert; skybox and OBJ resources are present but commented out in [src/main.cpp](src/main.cpp)).
- [glad.c](glad.c) — GLAD loader, compiled into the executable.
- [includes/](includes/), [lib/](lib/), [dlls/](dlls/) — vendored third-party headers, Windows import libs, and runtime DLLs (GLFW, GLM, GLAD, assimp, freetype, FreeImage, SOIL, stb_image, irrKlang, learnopengl).
- [resources/](resources/) — `icosphere.obj`, `monkey.obj`, and a `skybox/` cubemap. Currently unreferenced by the active code path.
- [CMakeLists.txt](CMakeLists.txt) — CMake build script; outputs `build/main(.exe)` and copies the required DLLs next to it on Windows.

# Build & run

Windows (the configured target — see `if(WIN32)` in [CMakeLists.txt](CMakeLists.txt)):

```
cmake -S . -B build
cmake --build build --config Debug
build/Debug/main.exe       # or build/main.exe depending on generator
```

A UNIX branch in the CMake file uses pkg-config and system packages (`glfw3`, `glm`, `assimp`, `freeimage`, `freetype`) but is not the primary build path.

Important: the executable loads shaders via the relative path `../shaders/shader_ray_tracing.{vs,fs}` ([src/main.cpp:119](src/main.cpp#L119)). Run it from a directory one level below the repo root (e.g. from inside `build/`) or it will fail to find them.

# How it renders

1. Build a single fullscreen quad VAO ([src/main.cpp:121-131](src/main.cpp#L121-L131)).
2. Each frame, push camera + scene uniforms (`cameraPosition`, `cameraToWorldRotMatrix`, `fovY`, `W`, `H`, per-sphere `Material` structs) to the program.
3. The fragment shader builds a primary ray per pixel ([shader_ray_tracing.fs:103-113](shaders/shader_ray_tracing.fs#L103-L113)) with sub-pixel jitter, traces it against a hard-coded `Sphere[]` array ([shader_ray_tracing.fs:62-74](shaders/shader_ray_tracing.fs#L62-L74)), and iteratively bounces up to `MAX_DEPTH = 10` times, accumulating albedo on a stack and combining it with the sky color on miss ([shader_ray_tracing.fs:167-223](shaders/shader_ray_tracing.fs#L167-L223)).
4. With `ENABLE_ACCUMULATION = true` ([src/main.cpp:34](src/main.cpp#L34)), the renderer ping-pongs between two `RGBA32F` textures: pass 1 writes `running_mean = old + (new - old) / (n+1)` into the offscreen FBO, pass 2 (`displayOnly = 1`) reads it back, applies gamma 2.2, and writes to the default framebuffer. `frameCountWithoutMove` is reset whenever the view matrix, zoom, or framebuffer size changes.

# Controls

- `W`/`A`/`S`/`D` — translate camera ([src/main.cpp:294-301](src/main.cpp#L294-L301))
- Mouse — look (cursor is captured)
- Scroll — adjust `Camera.Zoom` in 1–45° range, which feeds `fovY`
- `K` — print camera position and yaw to stdout
- `V` — save the current framebuffer as `YYYY_M_D_H_M_S.png` next to the executable via FreeImage ([src/main.cpp:56-70](src/main.cpp#L56-L70), [src/main.cpp:308-318](src/main.cpp#L308-L318))
- `ESC` — quit

# Editing the scene

The scene is fully shader-driven and hot-edits don't require recompiling C++:

- Materials are uniforms set in [src/main.cpp:161-179](src/main.cpp#L161-L179). `material_type`: `0` diffuse, `1` reflective (with `fuzz`), `2` refractive (with `ior` and `fuzz`). The "bubble" inside the dielectric uses `ior = 1/1.5` to model an air pocket inside glass.
- Sphere centers/radii are hard-coded in the `spheres[]` GLSL array in [shaders/shader_ray_tracing.fs:62-74](shaders/shader_ray_tracing.fs#L62-L74). To add geometry, extend that array (and the uniform `Material` list if you want a new material).
- `nsamples = 8` per-frame samples ([shader_ray_tracing.fs:234](shaders/shader_ray_tracing.fs#L234)) and `MAX_DEPTH = 10` bounces ([shader_ray_tracing.fs:8](shaders/shader_ray_tracing.fs#L8)) are the main quality/perf knobs. With accumulation on, low `nsamples` is fine because the running mean converges over stationary frames.

# Notes & gotchas

- The RNG (`rand()` on `vec2`) is a hash, not a real PRNG; seeds are mixed from hit position, bounce index, sample index, and `frameCountWithoutMove`. Banding is visible without accumulation.
- `glEnable(GL_DEPTH_TEST)` is intentionally off — there's nothing to depth-test, only the fullscreen quad.
- A UBO binding (`mesh_vertices_ubo`, binding 0) is wired up in [src/main.cpp:148-149](src/main.cpp#L148-L149) but the current fragment shader does not declare it — it's leftover scaffolding for the (commented-out) mesh path.
- The skybox cubemap is loaded and bound in commented blocks; the active sky is the procedural blue-to-white gradient in `skyColor()` ([shader_ray_tracing.fs:161-165](shaders/shader_ray_tracing.fs#L161-L165)).
- Screenshots use `GL_BGR` reads matched to FreeImage's default channel masks — don't "fix" that to `GL_RGB` without also swapping the masks.

# Final-project roadmap: NEE + MIS

Planned extension: next event estimation with multiple importance sampling, scoped to **emissive spheres as the only light type**, **MIS on diffuse surfaces only** (mirror/glass stay pure-BSDF as delta materials), validated by **visual + equal-time comparison**.

Motivation: pure-BSDF sampling wastes nearly all compute on rays that miss small/distant emitters. NEE deterministically connects to a light every diffuse bounce; MIS keeps the estimator robust on cases where NEE's pdf is poor (large/close lights, glancing geometry).

1. **Baseline.** Tag the current commit, capture reference screenshots at fixed `frameCountWithoutMove` so later changes can be regressed against byte-identical output (the hash RNG is deterministic).
2. **Emission as a material property.** Add `vec3 emission` to `Material` in [shader_ray_tracing.fs](shaders/shader_ray_tracing.fs) and mirror the uniform in [src/main.cpp](src/main.cpp). Make one sphere emissive and confirm BSDF rays that hit it produce visibly noisy but correct illumination.
3. **Throughput × radiance refactor.** Rewrite `castRay` from the current `colorStack` post-multiply to canonical `L += beta * ...; beta *= albedo` form. Verify numerically equivalent output before moving on — this is the cheapest place to catch bias bugs.
4. **Sphere light sampling.** Solid-angle sampling of an emissive sphere from the shading point (PBRT §14.2.2 / RTIOW Book 3 §9), plus an occlusion-only shadow ray. Validate with a "NEE-only" mode that terminates after the direct estimate — should be dramatically cleaner than BSDF-only on shadowed diffuse regions.
5. **Combine NEE with indirect bounces.** Add the NEE estimate at every diffuse hit *and* keep bouncing. **Bias trap:** when a BSDF ray hits an emitter directly from a diffuse surface, *don't* add its emission — NEE already covered it. Exceptions: primary (camera) rays, and BSDF rays leaving delta surfaces (mirror/glass). Track a `lastBounceWasDelta` flag. Test by setting a diffuse surface's albedo to 0 — emission should propagate, indirect should vanish; any indirect contribution = double-count bug.
6. **MIS (power heuristic).** Weight NEE samples by `w_light = p_light² / (p_light² + p_bsdf²)` and emission found by BSDF sampling on diffuse surfaces by the complementary `w_bsdf`. Needs `pdf_light(p, n, wi)` for *arbitrary* `wi` (not just sampled ones) — same sphere-cone formula. Skip MIS on delta materials entirely.
7. **Validation.** Two scenes — small emitter (NEE wins) and large/close emitter (MIS wins over plain NEE). Equal-sample grid at `frameCountWithoutMove ∈ {16, 64, 256, 1024}`, plus equal-wall-clock comparison (NEE adds a shadow ray per diffuse hit; not free). Include one shot where NEE alone is worse than MIS (e.g., big emitter reflected in the metal sphere) to motivate MIS.

Risks to plan for:
- **Phase 5 double-counting** is the highest-likelihood bug and your eyes won't catch it — use the albedo=0 test.
- **Hash RNG correlation** ([shader_ray_tracing.fs:79-86](shaders/shader_ray_tracing.fs#L79-L86)) may produce streaky artifacts once MIS amplifies sample reuse; if so, swap in a PCG hash. Don't preemptively rewrite.
- **Scope creep into mesh lights.** Sphere-only keeps step 4 tractable; the dormant mesh/model code is a trap.
