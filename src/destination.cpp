/*
Destinations for OBS
Copyright (C) 2026 Sébastien Tombre

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "destination.hpp"

#include <obs-frontend-api.h>
#include <plugin-support.h>

/* Le canvas sert à la vidéo uniquement : pas de MIX_AUDIO, pour éviter
 * qu'une source présente à la fois dans OBS et dans la destination soit
 * mixée deux fois. L'audio reste géré par le mixeur principal d'OBS. */
static constexpr uint32_t DESTINATION_CANVAS_FLAGS = ACTIVATE | SCENE_REF;

Destination::Destination(std::string name_) : name(std::move(name_)) {}

Destination::~Destination()
{
	Detach();
}

/* ------------------------------------------------------------------ */
/* Canvas                                                               */
/* ------------------------------------------------------------------ */

bool Destination::Attach(uint32_t w, uint32_t h)
{
	if (w > 0 && h > 0) {
		width = w;
		height = h;
	}

	if (!canvas) {
		obs_canvas_t *found = nullptr;

		struct obs_frontend_canvas_list list = {};
		obs_frontend_get_canvases(&list);
		for (size_t i = 0; i < list.canvases.num; i++) {
			obs_canvas_t *c = list.canvases.array[i];
			const char *cname = obs_canvas_get_name(c);
			if (cname && name == cname && !obs_canvas_removed(c)) {
				found = obs_canvas_get_ref(c);
				break;
			}
		}
		obs_frontend_canvas_list_free(&list);

		if (!found)
			found = obs_frontend_add_canvas(name.c_str(), nullptr, DESTINATION_CANVAS_FLAGS);

		if (!found) {
			obs_log(LOG_ERROR, "[%s] impossible de créer le canvas", name.c_str());
			return false;
		}

		std::lock_guard<std::mutex> lock(mutex);
		canvas = found;
	}

	StartVideo();

	if (!transition)
		CreateTransition(transitionType);

	/* Remettre le Program sauvegardé à l'antenne, sans transition. */
	OBSSource program;
	{
		std::lock_guard<std::mutex> lock(mutex);
		program = OBSGetStrongRef(programScene);
	}
	if (program && transition)
		obs_transition_set(transition, program);

	obs_log(LOG_INFO, "[%s] destination prête (%ux%u)", name.c_str(), width, height);
	return true;
}

void Destination::StartVideo()
{
	if (!canvas)
		return;

	struct obs_video_info ovi = {};
	if (!obs_get_video_info(&ovi))
		return;

	struct obs_video_info current = {};
	if (obs_canvas_has_video(canvas) && obs_canvas_get_video_info(canvas, &current) &&
	    current.base_width == width && current.base_height == height && current.fps_num == ovi.fps_num &&
	    current.fps_den == ovi.fps_den)
		return;

	/* Même cadence et même format couleur que le canvas principal,
	 * résolution propre à la destination. */
	ovi.base_width = width;
	ovi.base_height = height;
	ovi.output_width = width;
	ovi.output_height = height;

	if (!obs_canvas_reset_video(canvas, &ovi))
		obs_log(LOG_WARNING, "[%s] la vidéo du canvas n'a pas pu démarrer", name.c_str());

	if (transition)
		obs_transition_set_size(transition, width, height);
}

void Destination::Detach()
{
	OBSSource oldPreview;
	obs_canvas_t *oldCanvas = nullptr;
	{
		std::lock_guard<std::mutex> lock(mutex);
		oldPreview = OBSGetStrongRef(previewScene);
		previewScene = nullptr;
		oldCanvas = canvas;
		canvas = nullptr;
	}

	if (oldPreview)
		obs_source_dec_showing(oldPreview);

	if (oldCanvas) {
		obs_canvas_set_channel(oldCanvas, 0, nullptr);
		obs_canvas_release(oldCanvas);
	}

	transition = nullptr;
}

/* ------------------------------------------------------------------ */
/* Scènes                                                               */
/* ------------------------------------------------------------------ */

std::vector<std::string> Destination::SceneNames() const
{
	std::vector<std::string> names;
	if (!canvas)
		return names;

	obs_canvas_enum_scenes(
		canvas,
		[](void *param, obs_source_t *source) {
			auto *list = static_cast<std::vector<std::string> *>(param);
			const char *n = obs_source_get_name(source);
			if (n)
				list->emplace_back(n);
			return true;
		},
		&names);
	return names;
}

bool Destination::CreateScene(const std::string &sceneName)
{
	if (!canvas || sceneName.empty())
		return false;

	OBSSourceAutoRelease existing = obs_canvas_get_source_by_name(canvas, sceneName.c_str());
	if (existing)
		return false;

	obs_scene_t *scene = obs_canvas_scene_create(canvas, sceneName.c_str());
	if (!scene)
		return false;
	/* Le canvas (SCENE_REF) garde sa propre référence. */
	obs_scene_release(scene);
	return true;
}

void Destination::RemoveScene(const std::string &sceneName)
{
	OBSSourceAutoRelease source = GetSceneSource(sceneName);
	if (!source)
		return;

	bool wasPreview = false;
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (obs_weak_source_references_source(previewScene, source)) {
			wasPreview = true;
			previewScene = nullptr;
		}
		if (obs_weak_source_references_source(programScene, source))
			programScene = nullptr;
	}
	if (wasPreview)
		obs_source_dec_showing(source);

	if (transition) {
		OBSSourceAutoRelease active = obs_transition_get_active_source(transition);
		if (active == source.Get())
			obs_transition_set(transition, nullptr);
	}

	obs_source_remove(source);
}

OBSSourceAutoRelease Destination::GetSceneSource(const std::string &sceneName) const
{
	if (!canvas || sceneName.empty())
		return nullptr;
	return obs_canvas_get_source_by_name(canvas, sceneName.c_str());
}

bool Destination::AddSourceToScene(const std::string &sceneName, obs_source_t *source)
{
	if (!canvas || !source)
		return false;

	OBSSceneAutoRelease scene = obs_canvas_get_scene_by_name(canvas, sceneName.c_str());
	if (!scene)
		return false;

	/* Éviter d'imbriquer une scène dans elle-même. */
	if (obs_scene_get_source(scene) == source)
		return false;

	obs_sceneitem_t *item = obs_scene_add(scene, source);
	if (!item)
		return false;

	/* V0.1 : pas d'édition interactive, la source est ajustée au cadre. */
	struct vec2 bounds;
	vec2_set(&bounds, float(width), float(height));
	obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_SCALE_INNER);
	obs_sceneitem_set_bounds_alignment(item, OBS_ALIGN_CENTER);
	obs_sceneitem_set_bounds(item, &bounds);
	return true;
}

/* ------------------------------------------------------------------ */
/* Preview / Program                                                    */
/* ------------------------------------------------------------------ */

void Destination::SetPreviewScene(const std::string &sceneName)
{
	OBSSourceAutoRelease next = GetSceneSource(sceneName);

	OBSSource previous;
	{
		std::lock_guard<std::mutex> lock(mutex);
		previous = OBSGetStrongRef(previewScene);
		if (previous == next.Get())
			return;
		previewScene = next ? OBSGetWeakRef(next.Get()) : OBSWeakSource();
	}

	/* La scène en Preview doit être « showing » pour que ses sources
	 * (médias, navigateurs…) se rendent même si elle n'est pas à l'antenne. */
	if (next)
		obs_source_inc_showing(next);
	if (previous)
		obs_source_dec_showing(previous);
}

std::string Destination::PreviewSceneName() const
{
	std::lock_guard<std::mutex> lock(mutex);
	OBSSource s = OBSGetStrongRef(previewScene);
	const char *n = s ? obs_source_get_name(s) : nullptr;
	return n ? n : "";
}

std::string Destination::ProgramSceneName() const
{
	std::lock_guard<std::mutex> lock(mutex);
	OBSSource s = OBSGetStrongRef(programScene);
	const char *n = s ? obs_source_get_name(s) : nullptr;
	return n ? n : "";
}

/* ------------------------------------------------------------------ */
/* Transitions                                                          */
/* ------------------------------------------------------------------ */

std::vector<std::pair<std::string, std::string>> Destination::TransitionTypes()
{
	std::vector<std::pair<std::string, std::string>> types;
	const char *id = nullptr;
	size_t idx = 0;
	while (obs_enum_transition_types(idx++, &id)) {
		/* Les transitions qui ont besoin de réglages (stinger, luma wipe…)
		 * arriveront en V0.2 avec une fenêtre de propriétés. */
		const char *display = obs_source_get_display_name(id);
		types.emplace_back(id, display ? display : id);
	}
	return types;
}

void Destination::CreateTransition(const std::string &id)
{
	std::string trName = name + " - transition";
	OBSSourceAutoRelease next = obs_source_create_private(id.c_str(), trName.c_str(), nullptr);
	if (!next) {
		obs_log(LOG_WARNING, "[%s] transition « %s » introuvable, fondu utilisé", name.c_str(), id.c_str());
		next = obs_source_create_private("fade_transition", trName.c_str(), nullptr);
		if (!next)
			return;
	}

	obs_transition_set_size(next, width, height);

	OBSSource previous = transition;
	if (previous) {
		/* Échange sans coupure de l'image à l'antenne. */
		obs_transition_swap_begin(next, previous);
		if (canvas)
			obs_canvas_set_channel(canvas, 0, next);
		obs_transition_swap_end(next, previous);
	} else {
		OBSSource program;
		{
			std::lock_guard<std::mutex> lock(mutex);
			program = OBSGetStrongRef(programScene);
		}
		obs_transition_set(next, program);
		if (canvas)
			obs_canvas_set_channel(canvas, 0, next);
	}

	transition = next.Get();
}

void Destination::SetTransitionType(const std::string &id)
{
	if (id.empty() || id == transitionType)
		return;
	transitionType = id;
	CreateTransition(id);
}

std::string Destination::TransitionType() const
{
	return transitionType;
}

void Destination::SetTransitionDuration(int ms)
{
	durationMs = ms < 0 ? 0 : ms;
}

int Destination::TransitionDuration() const
{
	return durationMs;
}

void Destination::Cut()
{
	OBSSource preview;
	{
		std::lock_guard<std::mutex> lock(mutex);
		preview = OBSGetStrongRef(previewScene);
	}
	if (!preview || !transition)
		return;

	obs_transition_force_stop(transition);
	obs_transition_set(transition, preview);

	std::lock_guard<std::mutex> lock(mutex);
	programScene = OBSGetWeakRef(preview.Get());
}

void Destination::Take()
{
	if (transitionType == "cut_transition" || durationMs == 0) {
		Cut();
		return;
	}

	OBSSource preview;
	{
		std::lock_guard<std::mutex> lock(mutex);
		preview = OBSGetStrongRef(previewScene);
	}
	if (!preview || !transition)
		return;

	if (!obs_transition_start(transition, OBS_TRANSITION_MODE_AUTO, uint32_t(durationMs), preview))
		return; /* une transition est déjà en cours */

	std::lock_guard<std::mutex> lock(mutex);
	programScene = OBSGetWeakRef(preview.Get());
}

/* ------------------------------------------------------------------ */
/* Rendu (thread graphique)                                             */
/* ------------------------------------------------------------------ */

void Destination::RenderPreview()
{
	OBSSource preview;
	{
		std::lock_guard<std::mutex> lock(mutex);
		preview = OBSGetStrongRef(previewScene);
	}
	if (preview)
		obs_source_video_render(preview);
}

void Destination::RenderProgram()
{
	obs_canvas_t *c = nullptr;
	{
		std::lock_guard<std::mutex> lock(mutex);
		c = canvas ? obs_canvas_get_ref(canvas) : nullptr;
	}
	if (!c)
		return;

	/* Texture finale du canvas = exactement ce qui part vers les sorties. */
	obs_render_canvas_texture(c);
	obs_canvas_release(c);
}

/* ------------------------------------------------------------------ */
/* Persistance                                                          */
/* ------------------------------------------------------------------ */

void Destination::Save(obs_data_t *data) const
{
	obs_data_set_string(data, "name", name.c_str());
	obs_data_set_int(data, "width", width);
	obs_data_set_int(data, "height", height);
	obs_data_set_string(data, "transition", transitionType.c_str());
	obs_data_set_int(data, "transition_duration", durationMs);
	obs_data_set_string(data, "preview_scene", PreviewSceneName().c_str());
	obs_data_set_string(data, "program_scene", ProgramSceneName().c_str());
}

void Destination::Load(obs_data_t *data)
{
	obs_data_set_default_int(data, "width", 1920);
	obs_data_set_default_int(data, "height", 1080);
	obs_data_set_default_string(data, "transition", "fade_transition");
	obs_data_set_default_int(data, "transition_duration", 500);

	uint32_t w = uint32_t(obs_data_get_int(data, "width"));
	uint32_t h = uint32_t(obs_data_get_int(data, "height"));
	transitionType = obs_data_get_string(data, "transition");
	durationMs = int(obs_data_get_int(data, "transition_duration"));

	/* Attach avant de résoudre les noms de scènes (il faut le canvas). */
	Attach(w, h);

	std::string programName = obs_data_get_string(data, "program_scene");
	OBSSourceAutoRelease program = GetSceneSource(programName);
	if (program) {
		{
			std::lock_guard<std::mutex> lock(mutex);
			programScene = OBSGetWeakRef(program.Get());
		}
		if (transition)
			obs_transition_set(transition, program);
	}

	SetPreviewScene(obs_data_get_string(data, "preview_scene"));
}
