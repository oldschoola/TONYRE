#include <Sys/File/filesys.h>
#include <cmath>

#include <Gfx/camera.h>
#include <Gfx/gfxman.h>
#include <Gfx/nx.h>
#include <Gfx/NxTexMan.h>
#include <Gfx/NxViewMan.h>
#include <Gfx/NxQuickAnim.h>
#include <Gfx/nxparticlemgr.h>
#include <Gfx/NxMiscFX.h>
#include <Gfx/debuggfx.h>
#include <Gfx/FrameDiag.h>
#include <Core/math.h>
#include <Sk/Engine/SuperSector.h>
#include <Gel/Scripting/script.h>
#include <Gel/mainloop.h>

#include "p_NxMesh.h"
#include "p_NxGeom.h"
#include "p_NxSprite.h"
#include "p_NxModel.h"
#include "p_nxtexture.h"
#include "p_nxscene.h"
#include "p_nxnewparticlemgr.h"
#include "p_nxweather.h"

#include "nx/nx_init.h"
#include "nx/scene.h"
#include "nx/render.h"
#include "nx/occlude.h"

#if defined(DEBUG_IMGUI)
#include "Plugins/ImGui/ImGuiLayer.h"
#include "Plugins/ImGui/Panels/PerformancePanel.h"
#endif

#include <stdlib.h>

namespace Nx
{

// GPU pre-render queue limiter. We insert a fence after each SwapWindow and
// wait on it at the top of the next frame so the driver never queues more
// than one frame of GL work ahead of the GPU. Without this the NVIDIA/AMD
// driver may buffer 2-3 frames, adding 33-50 ms of input lag at 60 fps.
static GLsync g_prev_frame_fence = nullptr;

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/
	void CEngine::s_plat_start_engine(void)
	{
		// Initialize engine
		NxWn32::InitialiseEngine();

		mp_particle_manager = new CXboxNewParticleManager;
		mp_weather = new CXboxWeather;
	}



	/******************************************************************/
	/*                                                                */
	/*                                                                */
	/******************************************************************/
	void CEngine::s_plat_pre_render(void)
	{
		// Handle SDL events
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
#if defined(DEBUG_IMGUI)
			// SDL is our WndProc equivalent — forward every event so ImGui
			// can track keyboard, mouse, focus, resize, and clipboard state.
			Debug::ImGuiLayer::ProcessEvent(&event);
#endif

			switch (event.type)
			{
				case SDL_QUIT:
					// Mlp::Manager *mlp_manager = Mlp::Manager::Instance();
					// mlp_manager->QuitLoop();
					_Exit(0);
					break;
				case SDL_WINDOWEVENT:
					switch (event.window.event)
					{
						case SDL_WINDOWEVENT_RESIZED:
							// Set viewport
							glViewport(0, 0, event.window.data1, event.window.data2);
							NxWn32::set_dimensions(event.window.data1, event.window.data2);
							break;
					}
					break;
#if defined(DEBUG_IMGUI)
				case SDL_KEYDOWN:
					if (event.key.repeat == 0)
					{
						if (event.key.keysym.sym == SDLK_F1)
							Debug::ImGuiLayer::ToggleVisible();
						else if (event.key.keysym.sym == SDLK_F2)
							Debug::ImGuiLayer::ToggleLevelEditor();
						else if (event.key.keysym.sym == SDLK_F3)
							Debug::ImGuiLayer::ToggleObjectEditor();
					}
					break;
#endif
			}
		}

		// Block here until the previous frame's GPU work has completed.
		// Caps driver pre-render queue at 1 frame for low input lag. Disabling the
		// limiter via the perf panel hands queue depth control back to the driver,
		// trading input lag for higher throughput.
#if defined(DEBUG_IMGUI)
		const bool limit_queue = Debug::PerformancePanel::IsPrerenderQueueLimited();
#else
		const bool limit_queue = true;
#endif
		if (g_prev_frame_fence != nullptr)
		{
			if (limit_queue)
			{
#if defined(DEBUG_IMGUI)
				const uint64_t t_wait_start = SDL_GetPerformanceCounter();
#endif
				glClientWaitSync(g_prev_frame_fence, GL_SYNC_FLUSH_COMMANDS_BIT, 50000000ULL);
#if defined(DEBUG_IMGUI)
				Debug::PerformancePanel::RecordGpuWaitForLastFrame(
					SDL_GetPerformanceCounter() - t_wait_start);
#endif
			}
			glDeleteSync(g_prev_frame_fence);
			g_prev_frame_fence = nullptr;
		}

#if defined(DEBUG_IMGUI)
		Debug::PerformancePanel::BeginFrame();
		Debug::ImGuiLayer::BeginFrame();
#endif
	}



	/******************************************************************/
	/*                                                                */
	/*                                                                */
	/******************************************************************/
	void CEngine::s_plat_post_render(void)
	{
		NxWn32::sShader *shader = NxWn32::DirectShader();
		glUseProgram(shader->program);

		// If blurring, render blur buffer to backbuffer
		if (NxWn32::EngineGlobals.screen_blur > 0.0f)
		{
			if (NxWn32::EngineGlobals.screen_blur_duration > 1)
			{
				// Bind backbuffer FBO
				NxWn32::EngineGlobals.backbuffer->BindFBO();
				glClear(GL_DEPTH_BUFFER_BIT);

				// Get alpha to blur at
				glUniform4f(glGetUniformLocation(shader->program, "u_col"), 1.0f, 1.0f, 1.0f, NxWn32::EngineGlobals.screen_blur);

				// Bind blur texture
				glActiveTexture(GL_TEXTURE0);
				NxWn32::EngineGlobals.blurbuffer->BindColorTexture();

				// Draw blur quad
				NxWn32::EngineGlobals.fullscreen_quad->Bind();
				glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, nullptr);
			}

			// Tick blur
			NxWn32::EngineGlobals.screen_blur_duration++;
		}
		else
		{
			// Reset blur
			NxWn32::EngineGlobals.screen_blur_duration = 0;
		}

		// Bind screen FBO
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glClear(GL_DEPTH_BUFFER_BIT);
		
		// Bind backbuffer texture
		glActiveTexture(GL_TEXTURE0);
		NxWn32::EngineGlobals.backbuffer->BindColorTexture();

		// Draw backbuffer quad
		glUniform4f(glGetUniformLocation(shader->program, "u_col"), 1.0f, 1.0f, 1.0f, 1.0f);

		NxWn32::EngineGlobals.fullscreen_quad->Bind();
		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, nullptr);

#if defined(DEBUG_IMGUI)
		// Overlay draws on backbuffer (FBO 0 already bound above) right
		// before Present so game geometry is fully composited underneath.
		Debug::PerformancePanel::RecordCheckpoint("render");
		Debug::ImGuiLayer::Render();
		Debug::PerformancePanel::RecordCheckpoint("imgui");
#endif

#if defined(DEBUG_IMGUI)
		// Honour any pending VSync change from the perf panel before swapping.
		// Edge-triggered: ConsumePendingSwapInterval returns true exactly once.
		if (Debug::PerformancePanel::ConsumePendingSwapInterval())
			NxWn32::ApplySwapInterval(Debug::PerformancePanel::GetSwapInterval());
#endif

		// Swap window
		SDL_GL_SwapWindow(NxWn32::EngineGlobals.window);

#if defined(DEBUG_IMGUI)
		Debug::PerformancePanel::RecordSwapIssued();
#endif

		// Mark the end of this frame's GL commands. Next frame's pre_render
		// blocks on this fence so we never queue more than one frame ahead.
		// Skipped when the queue limiter is off so the driver can buffer freely.
#if defined(DEBUG_IMGUI)
		if (Debug::PerformancePanel::IsPrerenderQueueLimited())
#endif
		{
			g_prev_frame_fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
		}

#if defined(DEBUG_IMGUI)
		Debug::PerformancePanel::RecordCheckpoint("swap");
		Debug::PerformancePanel::EndFrame();
#endif

		// Increment frame counter
		NxWn32::EngineGlobals.frame_count++;

		// Wait for next frame (60 fps when capped, returns immediately when uncapped)
#if defined(DEBUG_IMGUI)
		const uint64_t t_pace_start = SDL_GetPerformanceCounter();
#endif
		NxWn32::WaitForNextFrame();
#if defined(DEBUG_IMGUI)
		Debug::PerformancePanel::RecordPacingWait(
			SDL_GetPerformanceCounter() - t_pace_start);
#endif
	}



	/******************************************************************/
	/*                                                                */
	/*                                                                */
	/******************************************************************/
	void CEngine::s_plat_render_world(void)
	{
		// Bind FBO
		if (NxWn32::EngineGlobals.screen_blur > 0.0f && (NxWn32::EngineGlobals.screen_blur_duration > 1))
			NxWn32::EngineGlobals.blurbuffer->BindFBO();
		else
			NxWn32::EngineGlobals.backbuffer->BindFBO();

		// Clear the screen
		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		// Render detailed shadow casters into their projector FBOs before the world
		// is drawn. render_shadow_targets binds/unbinds its own FBO so the backbuffer
		// remains the target afterwards; it also publishes shadow_texture_id +
		// shadow_tex_proj_matrix into EngineGlobals for the mesh draw path to sample.
		NxWn32::render_shadow_targets();

		// Process imposters
		CEngine::sGetImposterManager()->ProcessImposters();

		// Draw viewports
		int num_viewports = CViewportManager::sGetNumActiveViewports();
		for (int v = 0; v < num_viewports; ++v)
		{
			// Get viewport and camera
			CViewport *p_cur_viewport = CViewportManager::sGetActiveViewport(v);
			Gfx::Camera *p_cur_camera = p_cur_viewport->GetCamera();

			if (p_cur_camera == nullptr)
				continue;

			// Set viewport
			GLint left = (GLint)(p_cur_viewport->GetOriginX() * NxWn32::EngineGlobals.backbuffer_width);
			GLint top = (GLint)((1.0f - (p_cur_viewport->GetOriginY() + p_cur_viewport->GetHeight())) * NxWn32::EngineGlobals.backbuffer_height);
			GLsizei width = (GLsizei)(p_cur_viewport->GetWidth() * NxWn32::EngineGlobals.backbuffer_width);
			GLsizei height = (GLsizei)(p_cur_viewport->GetHeight() * NxWn32::EngineGlobals.backbuffer_height);

			if (NxWn32::EngineGlobals.letterbox_active)
			{
				// If letterbox active, adjust viewport to 16:9
				// The original game does it like this for multiple viewports, which I don't think
				// should happen, but I'm doing it like this just in case.
				float aspect_ratio = (float)width / (float)height;
				float intended_aspect_ratio = (p_cur_viewport->GetWidth() * 640.0f) / (p_cur_viewport->GetHeight() * (480.0f * 3.0f / 4.0f));

				if (aspect_ratio > intended_aspect_ratio)
				{
					float new_width = (float)height * intended_aspect_ratio;
					left += (width - (GLsizei)new_width) / 2;
					width = (GLsizei)new_width;
				}
				else
				{
					float new_height = (float)width / intended_aspect_ratio;
					top += (height - (GLsizei)new_height) / 2;
					height = (GLsizei)new_height;
				}
			}

			glViewport(left, top, width, height);

			// There is no bounding box transform for rendering the world.
			NxWn32::set_frustum_bbox_transform(nullptr);

			// Set up the camera..
			float aspect_ratio = p_cur_viewport->GetAspectRatio();

			NxWn32::set_camera(&(p_cur_camera->GetMatrix()), &(p_cur_camera->GetPos()), p_cur_camera->GetAdjustedHFOV(), aspect_ratio);

			// Render the non-sky world scenes.
			for (int i = 0; i < MAX_LOADED_SCENES; i++)
			{
				if (sp_loaded_scenes[i] != nullptr)
				{
					CXboxScene *pXboxScene = static_cast<CXboxScene*>(sp_loaded_scenes[i]);
					if (!pXboxScene->IsSky())
					{
						// Build relevant occlusion poly list, now that the camera is set.
						NxWn32::BuildOccluders(&(p_cur_camera->GetPos()), v);

						// Render scene
						pXboxScene->GetEngineScene()->m_flags |= SCENE_FLAG_RECEIVE_SHADOWS;
						NxWn32::render_scene(pXboxScene->GetEngineScene(), NxWn32::vRENDER_OPAQUE | NxWn32::vRENDER_OCCLUDED | NxWn32::vRENDER_SORT_FRONT_TO_BACK | NxWn32::vRENDER_BILLBOARDS, v);
					}
				}
			}

			// Render imposters
			CEngine::sGetImposterManager()->DrawImposters();

			// Render opaque instances
			NxWn32::render_instances(NxWn32::vRENDER_OPAQUE);

			// Render the sky, followed by all the non-sky semitransparent scene geometry. There is no bounding box transform for rendering the world.
			NxWn32::set_frustum_bbox_transform(nullptr);

			// Set up the sky camera.
			Mth::Vector	centre_pos(0.0f, 0.0f, 0.0f);
			NxWn32::set_camera(&(p_cur_camera->GetMatrix()), &centre_pos, p_cur_camera->GetAdjustedHFOV(), aspect_ratio, true);

			// Render the sky
			for (int i = 0; i < MAX_LOADED_SCENES; i++)
			{
				if (sp_loaded_scenes[i] != nullptr)
				{
					CXboxScene *pXboxScene = static_cast<CXboxScene *>(sp_loaded_scenes[i]);
					if (pXboxScene->IsSky())
					{
						// No anisotropic filtering for the sky.
						NxWn32::render_scene(pXboxScene->GetEngineScene(), NxWn32::vRENDER_OPAQUE | NxWn32::vRENDER_SEMITRANSPARENT, v);
					}
				}
			}

			// Revert to the regular camera
			NxWn32::set_camera(&(p_cur_camera->GetMatrix()), &(p_cur_camera->GetPos()), p_cur_camera->GetAdjustedHFOV(), aspect_ratio);

			// Render all semitransparent instances.
			NxWn32::render_instances(NxWn32::vRENDER_SEMITRANSPARENT | NxWn32::vRENDER_INSTANCE_PRE_WORLD_SEMITRANSPARENT);

			// Render the non - sky semitransparent scene geometry.
			// Setting the depth clip control to clamp here means that semitransparent periphary objects that would usually cull out
			// are now drawn correctly (since they will clamp at 1.0, and the z test is <=).
			for (int i = 0; i < MAX_LOADED_SCENES; i++)
			{
				if (sp_loaded_scenes[i])
				{
					CXboxScene *pXboxScene = static_cast<CXboxScene *>(sp_loaded_scenes[i]);
					if (!pXboxScene->IsSky())
					{
						// Build relevant occlusion poly list, now that the camera is set.
						NxWn32::render_scene(pXboxScene->GetEngineScene(), NxWn32::vRENDER_SEMITRANSPARENT |
							NxWn32::vRENDER_OCCLUDED |
							NxWn32::vRENDER_BILLBOARDS, v);
					}
				}
			}

			// Render all semitransparent instances.
			NxWn32::render_instances(NxWn32::vRENDER_SEMITRANSPARENT | NxWn32::vRENDER_INSTANCE_POST_WORLD_SEMITRANSPARENT);
		}

		#define FRAME_TRACE(tag) do { \
			FILE *_f = FrameDiag::OpenFrameTrace(); \
			if (_f) { fprintf(_f, "%s\n", tag); fclose(_f); } \
		} while(0)

		FRAME_TRACE("post-viewport-loop");

		// Update + render new-style particles once per frame, after all viewports are drawn.
		if (CEngine::sGetParticleManager())
		{
			FRAME_TRACE("pre-particle-update");
			CEngine::sGetParticleManager()->UpdateParticles();
			FRAME_TRACE("post-particle-update");
			CEngine::sGetParticleManager()->RenderParticles();
			FRAME_TRACE("post-particle-render");
		}

		// Reset viewport
		glViewport(0, 0, NxWn32::EngineGlobals.backbuffer_width, NxWn32::EngineGlobals.backbuffer_height);
		FRAME_TRACE("post-viewport-reset");

		// Draw 2D sprites
		NxWn32::SDraw2D::DrawAll();
		FRAME_TRACE("post-sdraw2d-drawall");
		#undef FRAME_TRACE
	}



	/******************************************************************/
	/*                                                                */
	/*                                                                */
	/******************************************************************/
	CScene *CEngine::s_plat_create_scene(const char *p_name, CTexDict *p_tex_dict, bool add_super_sectors)
	{
		(void)p_name;
		(void)p_tex_dict;

		// Create scene class instance
		CXboxScene *p_xbox_scene = new CXboxScene;
		CScene *p_new_scene = p_xbox_scene;
		p_new_scene->SetInSuperSectors(add_super_sectors);
		p_new_scene->SetIsSky(false);

		// Create a new sScene so the engine can track assets for this scene.
		NxWn32::sScene *p_engine_scene = new NxWn32::sScene();
		p_xbox_scene->SetEngineScene(p_engine_scene);

		return p_new_scene;
	}

	#define MemoryRead( dst, size, num, src )	memcpy(( dst ), ( src ), (( num ) * ( size )));	\
											( src ) += (( num ) * ( size ))

	/******************************************************************/
	/*                                                                */
	/*                                                                */
	/******************************************************************/
	CScene *CEngine::s_plat_load_scene_from_memory(void *p_mem, CTexDict *p_tex_dict, bool add_super_sectors, bool is_sky, bool is_dictionary)
	{
		uint8 *p_data = (uint8 *)p_mem;
		CSector *pSector;
		CXboxSector *pXboxSector;

		// Create a new sScene so the engine can track assets for this scene.
		NxWn32::sScene *p_engine_scene = new NxWn32::sScene();

		// Set the dictionary flag.
		p_engine_scene->m_is_dictionary = is_dictionary;

		// Version numbers.
		uint32 mat_version, mesh_version, vert_version;
		MemoryRead(&mat_version, sizeof(uint32), 1, p_data);
		MemoryRead(&mesh_version, sizeof(uint32), 1, p_data);
		MemoryRead(&vert_version, sizeof(uint32), 1, p_data);

		// Import materials (they will now be associated at the engine-level with this scene).
		p_engine_scene->pMaterialTable = NxWn32::LoadMaterialsFromMemory((void**)&p_data, p_tex_dict->GetTexLookup());

		// Read number of sectors.
		int num_sectors;
		MemoryRead(&num_sectors, sizeof(int), 1, p_data);

		// Figure optimum hash table lookup size.
		uint32 optimal_table_size = num_sectors * 2;
		uint32 test = 2;
		uint32 size = 1;
		for (;; test <<= 1, ++size)
		{
			// Check if this iteration of table size is sufficient, or if we have hit the maximum size.
			if ((optimal_table_size <= test) || (size >= 12))
			{
				break;
			}
		}

		// Create scene class instance, using optimum size sector table.
		CScene *new_scene = new CXboxScene(size);
		new_scene->SetInSuperSectors(add_super_sectors);
		new_scene->SetIsSky(is_sky);

		// Get a scene id from the engine.
		CXboxScene *p_new_xbox_scene = static_cast<CXboxScene *>(new_scene);
		p_new_xbox_scene->SetEngineScene(p_engine_scene);

		for (int s = 0; s < num_sectors; ++s)
		{
			// Create a new sector to hold the incoming details.
			pSector = p_new_xbox_scene->CreateSector();
			pXboxSector = static_cast<CXboxSector *>(pSector);

			// Generate a hanging geom for the sector, used for creating level objects etc.
			CXboxGeom *p_xbox_geom = new CXboxGeom();
			p_xbox_geom->SetScene(p_new_xbox_scene);
			pXboxSector->SetGeom(p_xbox_geom);

			// Prepare CXboxGeom for receiving data.
			p_xbox_geom->InitMeshList();

			// Load sector data.
			if (pXboxSector->LoadFromMemory((void **)&p_data))
			{
				new_scene->AddSector(pSector);
			}
		}

		// At this point get the engine scene to figure it's bounding volumes.
		p_engine_scene->FigureBoundingVolumes();

		// Read hierarchy information.
		int num_hierarchy_objects;
		MemoryRead(&num_hierarchy_objects, sizeof(int), 1, p_data);

		if (num_hierarchy_objects > 0)
		{
			p_engine_scene->mp_hierarchyObjects = new CHierarchyObject[num_hierarchy_objects];
			MemoryRead(p_engine_scene->mp_hierarchyObjects, sizeof(CHierarchyObject), num_hierarchy_objects, p_data);
			p_engine_scene->m_numHierarchyObjects = num_hierarchy_objects;
		}

		return new_scene;
	}



	/******************************************************************/
	/*                                                                */
	/*                                                                */
	/******************************************************************/
	CScene *CEngine::s_plat_load_scene(const char *p_name, CTexDict *p_tex_dict, bool add_super_sectors, bool is_sky, bool is_dictionary)
	{
		CSector *pSector;
		CXboxSector *pXboxSector;

		Dbg_Message("loading scene from file %s\n", p_name);

		// Create a new NxWn32::sScene so the engine can track assets for this scene.
		NxWn32::sScene *p_engine_scene = new NxWn32::sScene();

		// Set the dictionary flag.
		p_engine_scene->m_is_dictionary = is_dictionary;

		// Open the scene file.
		void *p_file = File::Open(p_name, "rb");
		if (!p_file)
		{
			Dbg_MsgAssert(p_file, ("Couldn't open scene file %s\n", p_name));
			return nullptr;
		}

		// Version numbers.
		uint32 mat_version, mesh_version, vert_version;
		File::Read(&mat_version, sizeof(uint32), 1, p_file);
		File::Read(&mesh_version, sizeof(uint32), 1, p_file);
		File::Read(&vert_version, sizeof(uint32), 1, p_file);

		// Import materials (they will now be associated at the engine-level with this scene).
		p_engine_scene->pMaterialTable = NxWn32::LoadMaterials(p_file, p_tex_dict->GetTexLookup());

		// Read number of sectors.
		int num_sectors;
		File::Read(&num_sectors, sizeof(int), 1, p_file);

		// Figure optimum hash table lookup size.
		uint32 optimal_table_size = num_sectors * 2;
		uint32 test = 2;
		uint32 size = 1;
		for (;; test <<= 1, ++size)
		{
			// Check if this iteration of table size is sufficient, or if we have hit the maximum size.
			if ((optimal_table_size <= test) || (size >= 12))
			{
				break;
			}
		}

		// Create scene class instance, using optimum size sector table.
		CScene *new_scene = new CXboxScene(size);
		new_scene->SetInSuperSectors(add_super_sectors);
		new_scene->SetIsSky(is_sky);

		// Get a scene id from the engine.
		CXboxScene *p_new_xbox_scene = static_cast<CXboxScene *>(new_scene);
		p_new_xbox_scene->SetEngineScene(p_engine_scene);

		for (int s = 0; s < num_sectors; ++s)
		{
			// Create a new sector to hold the incoming details.
			pSector = p_new_xbox_scene->CreateSector();
			pXboxSector = static_cast<CXboxSector *>(pSector);

			// Generate a hanging geom for the sector, used for creating level objects etc.
			CXboxGeom *p_xbox_geom = new CXboxGeom();
			p_xbox_geom->SetScene(p_new_xbox_scene);
			pXboxSector->SetGeom(p_xbox_geom);

			// Prepare CXboxGeom for receiving data.
			p_xbox_geom->InitMeshList();

			// Load sector data.
			if (pXboxSector->LoadFromFile(p_file))
			{
				new_scene->AddSector(pSector);
			}
		}

		// At this point get the engine scene to figure it's bounding volumes.
		p_engine_scene->FigureBoundingVolumes();

		// Read hierarchy information.
		int num_hierarchy_objects;
		File::Read(&num_hierarchy_objects, sizeof(int), 1, p_file);

		if (num_hierarchy_objects > 0)
		{
			p_engine_scene->mp_hierarchyObjects = new CHierarchyObject[num_hierarchy_objects];
			File::Read(p_engine_scene->mp_hierarchyObjects, sizeof(CHierarchyObject), num_hierarchy_objects, p_file);
			p_engine_scene->m_numHierarchyObjects = num_hierarchy_objects;
		}

		File::Close(p_file);

		return new_scene;
	}



	/******************************************************************/
	/*                                                                */
	/*                                                                */
	/******************************************************************/
	bool CEngine::s_plat_unload_scene(CScene *p_scene)
	{
		Dbg_MsgAssert(p_scene, ("Trying to delete a NULL scene"));

		CXboxScene *p_xbox_scene = (CXboxScene *)p_scene;

		// Ask the engine to remove the associated meshes for each sector in the scene.
		p_xbox_scene->DestroySectorMeshes();

		// Get the engine specific scene data and pass it to the engine to delete.
		NxWn32::DeleteScene(p_xbox_scene->GetEngineScene());
		p_xbox_scene->SetEngineScene(NULL);

		return true;
	}



	/******************************************************************/
	/*                                                                */
	/*                                                                */
	/******************************************************************/
	bool CEngine::s_plat_add_scene(CScene *p_scene, const char *p_filename)
	{
		(void)p_scene;
		(void)p_filename;
		// Function to incrementally add geometry to a scene - should NOT be getting called on Xbox.
		Dbg_Assert(0);
		return false;
	}



	/******************************************************************/
	/*                                                                */
	/*                                                                */
	/******************************************************************/
	/*
	CTexDict* CEngine::s_plat_load_textures( const char* p_name )
	{
		// NxWn32::LoadTextureFile( p_name );
		return nullptr;
	}
	*/



	/******************************************************************/
	/*                                                                */
	/*                                                                */
	/******************************************************************/
	CModel *CEngine::s_plat_init_model(void)
	{
		return new CXboxModel;
	}



	/******************************************************************/
	/*                                                                */
	/*                                                                */
	/******************************************************************/
	bool CEngine::s_plat_uninit_model(CModel *pModel)
	{
		delete pModel;
		return true;
	}



	/******************************************************************/
	/*                                                                */
	/*                                                                */
	/******************************************************************/
	CGeom *CEngine::s_plat_init_geom(void)
	{
		CXboxGeom *pGeom = new CXboxGeom;
		return pGeom;
	}



	/******************************************************************/
	/*                                                                */
	/*                                                                */
	/******************************************************************/
	bool CEngine::s_plat_uninit_geom(CGeom *p_geom)
	{
		delete p_geom;
		return true;
	}



	/******************************************************************/
	/*                                                                */
	/*                                                                */
	/******************************************************************/
	CQuickAnim *CEngine::s_plat_init_quick_anim()
	{
		CQuickAnim *pQuickAnim = new CQuickAnim;
		return pQuickAnim;
	}



	/******************************************************************/
	/*                                                                */
	/*                                                                */
	/******************************************************************/
	void CEngine::s_plat_uninit_quick_anim(CQuickAnim *pQuickAnim)
	{
		delete pQuickAnim;
		return;
	}



	/******************************************************************/
	/*                                                                */
	/*                                                                */
	/******************************************************************/
	CMesh *CEngine::s_plat_load_mesh(const char *pMeshFileName, Nx::CTexDict *pTexDict, uint32 texDictOffset, bool isSkin, bool doShadowVolume)
	{
		(void)texDictOffset;
		(void)isSkin;
		(void)doShadowVolume;

		// Load the scene.
		Nx::CScene *p_scene = Nx::CEngine::s_plat_load_scene(pMeshFileName, pTexDict, false, false, false);

		// Store the checksum of the scene name.
		p_scene->SetID(Script::GenerateCRC(pMeshFileName)); // store the checksum of the scene name

		p_scene->SetTexDict(pTexDict);
		p_scene->PostLoad(pMeshFileName);

		// Disable cutscene scaling
		NxWn32::DisableMeshScaling();

		// Create mesh
		CXboxMesh *pMesh = new CXboxMesh(pMeshFileName);

		Nx::CXboxScene *p_xbox_scene = static_cast<Nx::CXboxScene*>(p_scene);
		pMesh->SetScene(p_xbox_scene);
		pMesh->SetTexDict(pTexDict);

		return pMesh;
	}



	/******************************************************************/
	/*                                                                */
	/*                                                                */
	/******************************************************************/
	CMesh *CEngine::s_plat_load_mesh(uint32 id, uint32 *p_model_data, int model_data_size, uint8 *p_cas_data, Nx::CTexDict *pTexDict, uint32 texDictOffset, bool isSkin, bool doShadowVolume)
	{
		(void)model_data_size;
		(void)texDictOffset;
		(void)isSkin;
		(void)doShadowVolume;

		// Convert the id into a usable string.
		Dbg_Assert(id > 0);
		char id_as_string[16];
		sprintf(id_as_string, "%d\n", id);

		// Load the scene.
		Nx::CScene *p_scene = Nx::CEngine::s_plat_load_scene_from_memory(p_model_data, pTexDict, false, false, false);

		// Store the checksum of the scene name.
		p_scene->SetID(Script::GenerateCRC(id_as_string));

		p_scene->SetTexDict(pTexDict);
		p_scene->PostLoad(id_as_string);

		CXboxMesh *pMesh = new CXboxMesh();

		// Set CAS data for mesh.
		pMesh->SetCASData(p_cas_data);

		// Disable any scaling.
		NxWn32::DisableMeshScaling();

		Nx::CXboxScene *p_xbox_scene = static_cast<Nx::CXboxScene *>(p_scene);
		pMesh->SetScene(p_xbox_scene);
		pMesh->SetTexDict(pTexDict);
		return pMesh;
	}



	/******************************************************************/
	/*                                                                */
	/*                                                                */
	/******************************************************************/
	bool CEngine::s_plat_unload_mesh(CMesh *pMesh)
	{
		delete pMesh;
		return true;
	}



	/******************************************************************/
	/*                                                                */
	/*                                                                */
	/******************************************************************/
	void CEngine::s_plat_set_mesh_scaling_parameters(SMeshScalingParameters *pParams)
	{
		NxWn32::SetMeshScalingParameters(pParams);
	}



	/******************************************************************/
	/*                                                                */
	/*                                                                */
	/******************************************************************/
	CSprite *CEngine::s_plat_create_sprite(CWindow2D *p_window)
	{
		(void)p_window;
		return new CXboxSprite;
	}



	/******************************************************************/
	/*                                                                */
	/*                                                                */
	/******************************************************************/
	bool CEngine::s_plat_destroy_sprite(CSprite *p_sprite)
	{
		delete p_sprite;
		return true;
	}

	/******************************************************************/
	/*                                                                */
	/*                                                                */
	/******************************************************************/

	CTextured3dPoly *CEngine::s_plat_create_textured_3d_poly()
	{
		return nullptr;
	}

	/******************************************************************/
	/*                                                                */
	/*                                                                */
	/******************************************************************/

	bool		CEngine::s_plat_destroy_textured_3d_poly(CTextured3dPoly *p_poly)
	{
		(void)p_poly;
		return true;
	}


	/******************************************************************/
	/*                                                                */
	/*                                                                */
	/******************************************************************/
	Nx::CTexture *CEngine::s_plat_create_render_target_texture(int width, int height, int depth, int z_depth)
	{
		CXboxTexture *p_tex = new CXboxTexture;
		NxWn32::sTexture *p_engine_tex = new NxWn32::sTexture;
		p_tex->SetEngineTexture(p_engine_tex);
		if( !p_engine_tex->SetRenderTarget(width, height, depth, z_depth) )
		{
			FILE *f = FrameDiag::OpenShadow();
			if (f) { fprintf(f, "s_plat_create_render_target_texture: FAILED w=%d h=%d\n", width, height); fclose(f); }
		}
		return p_tex;
	}



	/******************************************************************/
	/*                                                                */
	/*                                                                */
	/******************************************************************/
	void CEngine::s_plat_project_texture_into_scene(Nx::CTexture *p_texture, Nx::CModel *p_model, Nx::CScene *p_scene)
	{
		if( !p_texture || !p_model ) return;

		CXboxTexture *p_xbox_tex = static_cast<CXboxTexture*>(p_texture);
		CXboxModel   *p_xbox_model = static_cast<CXboxModel*>(p_model);
		NxWn32::sTexture *p_engine_tex = p_xbox_tex->GetEngineTexture();
		NxWn32::sScene   *p_engine_scene = nullptr;

		// If scene not explicitly passed, derive from caster model's first geom's instance.
		if( p_scene )
		{
			CXboxScene *p_xbox_scene = static_cast<CXboxScene*>(p_scene);
			p_engine_scene = p_xbox_scene->GetEngineScene();
		}

		NxWn32::create_texture_projection_details(p_engine_tex, p_xbox_model, p_engine_scene);

		// Caster identity at main-pass render is determined via SCENE_FLAG_SELF_SHADOWS
		// (set on caster instance scenes by render_shadow_targets each frame).
		// No need to stash a single instance pointer — skater has many geom instances.

		// Register every loaded world scene as a receiver so the skater's shadow falls onto the world.
		for (int i = 0; i < MAX_LOADED_SCENES; ++i)
		{
			if( sp_loaded_scenes[i] == nullptr ) continue;
			CXboxScene *p_xb = static_cast<CXboxScene*>(sp_loaded_scenes[i]);
			if( p_xb && p_xb->GetEngineScene() )
			{
				p_xb->GetEngineScene()->m_flags |= SCENE_FLAG_RECEIVE_SHADOWS;
			}
		}

		FILE *f = FrameDiag::OpenShadow();
		if (f) { fprintf(f, "s_plat_project_texture_into_scene: tex=%p model=%p\n", (void*)p_engine_tex, (void*)p_xbox_model); fclose(f); }
	}



	/******************************************************************/
	/*                                                                */
	/*                                                                */
	/******************************************************************/
	void CEngine::s_plat_set_projection_texture_camera(Nx::CTexture *p_texture, Gfx::Camera *p_camera)
	{
		if( !p_texture || !p_camera ) return;
		CXboxTexture *p_xbox_tex = static_cast<CXboxTexture*>(p_texture);
		NxWn32::sTexture *p_engine_tex = p_xbox_tex->GetEngineTexture();
		if( !p_engine_tex ) return;

		// Camera position is the "eye" of the projection. Look-at is along -Z of camera matrix, one unit away from eye.
		Mth::Vector cam_pos = p_camera->GetPos();
		Mth::Matrix cam_mat = p_camera->GetMatrix();
		Mth::Vector cam_at  = cam_pos + cam_mat[Z] * 1.0f;

		glm::vec3 glm_pos((float)cam_pos[X], (float)cam_pos[Y], (float)cam_pos[Z]);
		glm::vec3 glm_at ((float)cam_at[X],  (float)cam_at[Y],  (float)cam_at[Z]);

		// Guard NaN/Inf — skip update, keep last good camera matrices.
		auto finite3 = [](const glm::vec3& v) {
			return (v.x == v.x) && (v.y == v.y) && (v.z == v.z)
			    && !std::isinf(v.x) && !std::isinf(v.y) && !std::isinf(v.z);
		};
		if( !finite3(glm_pos) || !finite3(glm_at) )
		{
			static int s_nan = 0;
			if( (s_nan++ % 240) == 0 ) {
				FILE *f = FrameDiag::OpenShadow();
				if (f) { fprintf(f, "PROJCAM NaN skipped tex=%p pos=(%.1f,%.1f,%.1f) at=(%.1f,%.1f,%.1f)\n",
					(void*)p_engine_tex, glm_pos.x, glm_pos.y, glm_pos.z, glm_at.x, glm_at.y, glm_at.z); fclose(f); }
			}
			return;
		}

		{
			static int s_call = 0;
			if( (s_call++ % 120) == 0 ) {
				FILE *f = FrameDiag::OpenShadow();
				if (f) { fprintf(f, "PROJCAM[%d] tex=%p pos=(%.1f,%.1f,%.1f) at=(%.1f,%.1f,%.1f)\n",
					s_call, (void*)p_engine_tex, glm_pos.x, glm_pos.y, glm_pos.z, glm_at.x, glm_at.y, glm_at.z); fclose(f); }
			}
		}

		NxWn32::set_texture_projection_camera(p_engine_tex, glm_pos, glm_at);
	}



	/******************************************************************/
	/*                                                                */
	/*                                                                */
	/******************************************************************/
	void CEngine::s_plat_stop_projection_texture(Nx::CTexture *p_texture)
	{
		if( !p_texture ) return;
		CXboxTexture *p_xbox_tex = static_cast<CXboxTexture*>(p_texture);
		NxWn32::sTexture *p_engine_tex = p_xbox_tex->GetEngineTexture();
		if( p_engine_tex )
		{
			NxWn32::destroy_texture_projection_details(p_engine_tex);
		}
		NxWn32::EngineGlobals.caster_instance = nullptr;
	}



	/******************************************************************/
	/*                                                                */
	/*                                                                */
	/******************************************************************/
	void CEngine::s_plat_add_occlusion_poly(uint32 num_verts, Mth::Vector *p_vert_array, uint32 checksum)
	{
		if (num_verts == 4)
		{
			NxWn32::AddOcclusionPoly(p_vert_array[0], p_vert_array[1], p_vert_array[2], p_vert_array[3], checksum);
		}
	}



	/******************************************************************/
	/*                                                                */
	/*                                                                */
	/******************************************************************/
	void CEngine::s_plat_enable_occlusion_poly(uint32 checksum, bool enable)
	{
		NxWn32::EnableOcclusionPoly(checksum, enable);
	}



	/******************************************************************/
	/*                                                                */
	/*                                                                */
	/******************************************************************/
	void CEngine::s_plat_remove_all_occlusion_polys(void)
	{
		NxWn32::RemoveAllOcclusionPolys();
	}



	/******************************************************************/
	/*                                                                */
	/*                                                                */
	/******************************************************************/
	// returns true if the sphere at "center", with the "radius"
	// is visible to the current camera
	// (note, currently this is the last frame's camera on PS2)
	bool CEngine::s_plat_is_visible(Mth::Vector &center, float radius)
	{
		return NxWn32::IsVisible(center, radius);
	}



	/******************************************************************/
	/*                                                                */
	/*                                                                */
	/******************************************************************/
	void CEngine::s_plat_set_max_multipass_distance(float dist)
	{
		(void)dist;
		// Has no meaning for Xbox.
	}



	/******************************************************************/
	/*                                                                */
	/*                                                                */
	/******************************************************************/
	const char *CEngine::s_plat_get_platform_extension(void)
	{
		// String literals are statically allocated so can be returned safely, (Bjarne, p90)
		return "Xbx";
	}


	/******************************************************************/
	// Wait for any pending asyncronous rendering to finish, so rendering
	// data can be unloaded
	/******************************************************************/
	void CEngine::s_plat_finish_rendering()
	{
		
	}

	/******************************************************************/
	// Set the amount that the previous frame is blended with this frame
	// 0 = none	  	(just see current frame) 	
	// 128 = 50/50
	// 255 = 100% 	(so you only see the previous frame)												  
	/******************************************************************/
	void CEngine::s_plat_set_screen_blur(uint32 amount)
	{
		if (amount == 0)
		{
			NxWn32::EngineGlobals.screen_blur = 0.0f;
			return;
		}

		float alpha = (255.0f - amount) / 255.0f;
		if (alpha < 0.25f)
			alpha = 0.25f;
		NxWn32::EngineGlobals.screen_blur = alpha;
	}



	/******************************************************************/
	/*                                                                */
	/*                                                                */
	/******************************************************************/
	int	CEngine::s_plat_get_num_soundtracks(void)
	{
		return 0;
	}



	/******************************************************************/
	/*                                                                */
	/*                                                                */
	/******************************************************************/
	const char *CEngine::s_plat_get_soundtrack_name(int soundtrack_number)
	{
		(void)soundtrack_number;
		return "";
	}



	/******************************************************************/
	/*                                                                */
	/*                                                                */
	/******************************************************************/
	void CEngine::s_plat_set_letterbox(bool letterbox)
	{
		NxWn32::EngineGlobals.letterbox_active = letterbox;
	}



	/******************************************************************/
	/*                                                                */
	/*                                                                */
	/******************************************************************/
	void CEngine::s_plat_set_color_buffer_clear(bool clear)
	{
		(void)clear;
	}

} // namespace Nx
