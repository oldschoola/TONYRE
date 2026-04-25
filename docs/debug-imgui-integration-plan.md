# Dear ImGui Integration Plan — TONYRE

Phase 1 discovery output. Read-only map of touch points. No edits applied.

## Render backend

- **API**: OpenGL 3.3 Core via SDL2 (no D3D)
- **Context init**: `Wn32/Code/Plat/Gfx/nx/nx_init.cpp:52` (SDL_Init), `:74` (SDL_CreateWindow), `:78` (SDL_GL_CreateContext)
- **Window handle**: `NxWn32::EngineGlobals.window` (SDL_Window*), `NxWn32::EngineGlobals.context` (SDL_GLContext)
- **SwapBuffers call site**: `Wn32/Code/Plat/Gfx/p_nx.cpp:138` — `SDL_GL_SwapWindow(EngineGlobals.window)` inside `CEngine::s_plat_post_render()`
- **ImGui backend to use**: `imgui_impl_sdl2.cpp` + `imgui_impl_opengl3.cpp` (official)

## Frame loop

Outer driver: `Code/Gel/MainLoop/Mainloop.cpp:348-359`

```
Nx::CEngine::sRenderWorld()   // world draw into FBO  (Code/Gfx/nx.cpp:186)
  → s_plat_render_world()     // Wn32/Code/Plat/Gfx/p_nx.cpp:153
Nx::CEngine::sPostRender()    // blur/backbuffer blit + SDL_GL_SwapWindow  (Code/Gfx/nx.cpp:173)
  → s_plat_post_render()      // Wn32/Code/Plat/Gfx/p_nx.cpp:88
```

- **Pre-frame event pump**: `CEngine::s_plat_pre_render()` at `Wn32/Code/Plat/Gfx/p_nx.cpp:55-80` — `SDL_PollEvent` loop. ImGui SDL handler hooks here.
- **ImGui BeginFrame**: insert at end of `s_plat_pre_render` (after SDL event pump), so events are already consumed.
- **ImGui Render**: insert in `s_plat_post_render` immediately before `SDL_GL_SwapWindow` at line 138. Backbuffer is bound to FBO 0 there and content already blitted — overlay draws on top.

## Input routing

- Single SDL event loop at `p_nx.cpp:59`. Forward every event to `ImGui_ImplSDL2_ProcessEvent(&event)` before the existing switch.
- Gate: when `ImGui::GetIO().WantCaptureKeyboard` or `WantCaptureMouse`, skip the game's own input consumption.
- Game input backend: `Wn32/Code/Plat/Sys/SIO/p_sioman.cpp` (gamepad/SDL_INIT_JOYSTICK). ImGui doesn't need to block this — only keyboard/mouse when overlay open.
- F1 toggle: cheap check on `SDL_KEYDOWN event.key.keysym.sym == SDLK_F1` in `s_plat_pre_render`, flips `Debug::ImGuiLayer::s_visible`.

## Systems to bind

### Level panel
- **ChangeLevel script fn**: `Code/Sk/Scripting/ftables.cpp:463` — `{"ChangeLevel", Mdl::ScriptChangeLevel}`
- **Strategy**: Build dropdown from hardcoded level CRC list (NY, NJ, Moscow, Hawaii, Vancouver, Slam City, School, Manhattan, Suburbia, Airport, etc.). Button → `Script::RunScript("ChangeLevel", params_with_level_name)`.
- **Teleport**: use skater object directly — `Mdl::Skate::Instance()->GetLocalSkater()` → `CompositeObject::SetPos(Mth::Vector)`. Skater handle from `Code/Sk/Modules/Skate/skate.h:179`.
- **Toggle world geom visibility**: `Nx::CEngine::sHideLevelGeom` / equivalent flag on `CScene`. Dig further in Phase 2.

### NPCs panel
- **Ped class**: `Code/Sk/Objects/ped.h` — `Obj::CPed`
- **Iteration**: every `CGeneralManager` subclass exposes `GetRefObjectList()` → `Lst::Head<CObject>` (see `Code/Gel/objman.h:165`). `CCompositeObjectManager::Instance()` at `Code/Gel/Object/compositeobjectmanager.h:33` holds all composite objects; filter by type or component presence.
- **Spawn**: `CCompositeObjectManager::CreateCompositeObjectFromStructure(pStructure)` — reuse ped definitions from `ped.q`.
- **AI state**: `CPedLogicComponent` (`Code/Gel/Components/PedLogicComponent.h:69`) — getter/setter for behaviour state.
- **Kill**: `CGeneralManager::KillObject(obj)` at `objman.h:154`.

### Quests / Goals panel
- **Manager**: `Game::CGoalManager` at `Code/Sk/Modules/Skate/GoalManager.h:92`; accessor `Game::GetGoalManager()` used widely e.g. `Code/Sk/Scripting/cfuncs.cpp:8162`.
- **Iteration**: `CGoalManager::GetGoalByIndex(int)` + `GetNumGoals()` (lines 105, 121).
- **Operations available**: `AddGoal`, `ActivateGoal`, `DeactivateGoal`, `WinGoal`, `LoseGoal`, `RemoveGoal`, `QuickStartGoal`, `RestartLastGoal`, `EditGoal` (all on GoalManager.h:102-141).
- **Create new goal at runtime**: `AddGoal(goalId, Script::CStruct*)` — build CStruct in code, or exec Qb via `Script::RunScript`.

### Lighting panel
- **Manager**: `Nx::CLightManager` — static accessors at `Code/Gfx/NxLightMan.h:111-149`
- **Sun dir**: `sSetLightDirection(0, Mth::Vector)` line 121
- **Ambient**: `sSetLightAmbientColor(Image::RGBA)` line 118; `sSetAmbientLightModulationFactor(float)` line 127
- **Fog**: `Nx::CFog::sSetFogNearDistance/sSetFogExponent/sSetFogRGBA` at `Code/Gfx/NxMiscFX.h:49-52`
- **Per-light multiplier**: `sSetDiffuseLightModulationFactor(idx, factor)` line 129
- **Time-of-day slider**: synthesize by driving sun direction on a circle + blending ambient warm→cold. No existing TOD system to hook into; purely ImGui-side.

### Misc panel
- **Frame time**: `Tmr::FrameLength()` already used at `Code/Gfx/nx.cpp:188` — plot via `ImGui::PlotLines`.
- **Memory stats**: `Mem::Manager::sHandle()` from `Code/Core/Defines.h` region. Poll per-heap usage.
- **Qscript console**: text input → `Script::RunScript(name, params)` from `Code/Gel/Scripting/script.h:413`. For raw Qb eval use `Script::SpawnScript(pScriptName, ...)` line 429. Parse single-line commands into name+params CStruct.
- **Render debug flags**: wireframe via `glPolygonMode(GL_FRONT_AND_BACK, GL_LINE)` toggle injected in `p_nx.cpp` render path. Collision overlay exists at `Mdl::Rail_DebugRender()` line 355 of Mainloop — mirror pattern.

## ImGui vendoring

- **Location**: `Code/Plugins/ImGui/` (new directory)
- **Files to copy** (from ocornut/imgui master, no submodule):
  - `imgui.{cpp,h}`, `imgui_draw.cpp`, `imgui_tables.cpp`, `imgui_widgets.cpp`, `imgui_internal.h`, `imconfig.h`, `imstb_*.h`
  - `backends/imgui_impl_sdl2.{cpp,h}`, `backends/imgui_impl_opengl3.{cpp,h}`
  - `backends/imgui_impl_opengl3_loader.h`

- **CMake**: append to `Wn32/CMakeLists.txt` `add_library(TonyRE.Wn32 STATIC ...)` source list (same pattern as existing files, starts `Wn32/CMakeLists.txt:3`). Include dirs already have SDL2 + OpenGL.
- **Layer entry**: `Code/Plugins/ImGui/ImGuiLayer.{cpp,h}` with `Init/BeginFrame/Render/Shutdown`.
- **Panels dir**: `Code/Plugins/ImGui/Panels/` with `IDebugPanel` interface + one file per panel (Level.cpp, NPCs.cpp, Goals.cpp, Lighting.cpp, Misc.cpp).
- **Build gate**: `#ifdef DEBUG_IMGUI` around entry-point calls in `p_nx.cpp`. Add `-DDEBUG_IMGUI` to Debug/Release, remove for shipping.

## Integration touch list

| File | Line | Change |
|------|------|--------|
| `Wn32/CMakeLists.txt` | 3+ | Add ImGui sources + Plugins/ImGui/* |
| `Wn32/Code/Plat/Gfx/p_nx.cpp` | 55-80 | Forward SDL events to ImGui, add F1 toggle |
| `Wn32/Code/Plat/Gfx/p_nx.cpp` | 80 | `ImGuiLayer::BeginFrame()` at end of `s_plat_pre_render` |
| `Wn32/Code/Plat/Gfx/p_nx.cpp` | 137 | `ImGuiLayer::Render()` before `SDL_GL_SwapWindow` |
| `Wn32/Code/Plat/Gfx/nx/nx_init.cpp` | 78+ | `ImGuiLayer::Init(window, context)` after context create |
| `Code/Plugins/ImGui/*` | NEW | Full subsystem |

## Per-panel strategy (one line each)

- **Level** — hardcoded level CRC list → `Script::RunScript("ChangeLevel", params)`; teleport via `GetLocalSkater()->SetPos`.
- **NPCs** — iterate `CCompositeObjectManager` object list, filter by `CPed` type; spawn via `CreateCompositeObjectFromStructure`.
- **Goals** — `CGoalManager::GetGoalByIndex` loop; buttons call `ActivateGoal/WinGoal/RemoveGoal`.
- **Lighting** — `CLightManager` static setters, `CFog` static setters; synthesize TOD by driving sun dir.
- **Misc** — `Tmr::FrameLength` graph, `Script::RunScript` from text input, `glPolygonMode` wireframe toggle.

## Open questions for Phase 2

1. Does `CCompositeObjectManager` instance exist as singleton or per-scene? Check `DeclareSingletonClass` usage.
2. Qb console needs Qb compiler or only RunScript-by-name? RunScript-by-name is the safe MVP.
3. Is there a ped prefab CRC table for spawn dropdown, or must user type CRC?
4. Shipping-build gating — is there already a `__SHIPPING__` or `__FINAL__` define to mirror?

**STOP.** Awaiting "continue" for Phase 2.
