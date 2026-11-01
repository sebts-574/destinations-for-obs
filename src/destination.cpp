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

#include <cstring>

/* Le canvas sert à la vidéo uniquement : pas de MIX_AUDIO, pour éviter
 * qu'une source présente dans plusieurs destinations soit mixée plusieurs
 * fois. L'audio reste géré par le mixeur principal d'OBS. */
static constexpr uint32_t DESTINATION_CANVAS_FLAGS = ACTIVATE | SCENE_REF;

static std::string SourceUuid(obs_source_t *source)
{
	const char *uuid = source ? obs_source_get_uuid(source) : nullptr;
	return uuid ? uuid : "";
}

Destination *Destination::CreateMain()
{
	auto *dest = new Destination();
	dest->isMain = true;
	dest->name = "Défaut (OBS)";
	return dest;
}

Destination::Destination(std::string name_) : name(std::move(name_)) {}

Destination::~Destination()
{
	Detach();
}

std::string Destination::Id() const
{
	if (isMain)
		return MAIN_ID;
	std::lock_guard<std::mutex> lock(mutex);
	const char *uuid = canvas ? obs_canvas_get_uuid(canvas) : nullptr;
	return uuid ? uuid : "";
}

void Destination::SetName(const std::string &newName)
{
	if (isMain || newName.empty())
		return;
	name = newName;
	if (canvas)
		obs_canvas_set_name(canvas, name.c_str());
	/* Le nom réel peut avoir été dédoublonné par OBS. */
	if (canvas && obs_canvas_get_name(canvas))
		name = obs_canvas_get_name(canvas);
}

/* ------------------------------------------------------------------ */
/* Canvas                                                               */
/* ------------------------------------------------------------------ */

bool Destination::Attach(uint32_t w, uint32_t h, const std::string &uuidHint)
{
	if (isMain)
		return true;

	if (w > 0 && h > 0) {
		width = w;
		height = h;
	}

	if (!canvas) {
		obs_canvas_t *found = nullptr;

		struct obs_frontend_canvas_list list = {};
		obs_frontend_get_canvases(&list);
		/* D'abord par UUID (stable), puis par nom. */
		for (int pass = 0; pass < 2 && !found; pass++) {
			for (size_t i = 0; i < list.canvases.num; i++) {
				obs_canvas_t *c = list.canvases.array[i];
				if (obs_canvas_removed(c))
					continue;
				const char *key = pass == 0 ? obs_canvas_get_uuid(c) : obs_canvas_get_name(c);
				const std::string &wanted = pass == 0 ? uuidHint : name;
				if (key && !wanted.empty() && wanted == key) {
					found = obs_canvas_get_ref(c);
					break;
				}
			}
		}
		obs_frontend_canvas_list_free(&list);

		if (!found)
			found = obs_frontend_add_canvas(name.c_str(), nullptr, DESTINATION_CANVAS_FLAGS);

		if (!found) {
			obs_log(LOG_ERROR, "[%s] impossible de créer le canvas", name.c_str());
			return false;
		}

		{
			std::lock_guard<std::mutex> lock(mutex);
			canvas = found;
		}
		if (obs_canvas_get_name(found))
			name = obs_canvas_get_name(found);
	}

	StartVideo();

	if (!transition)
		CreateTransition(transitionType);

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
	obs_canvas_t *old = nullptr;
	{
		std::lock_guard<std::mutex> lock(mutex);
		old = canvas;
		canvas = nullptr;
		programScene = nullptr;
	}
	if (old) {
		obs_canvas_set_channel(old, 0, nullptr);
		obs_canvas_release(old);
	}
	transition = nullptr;
}

void Destination::DestroyCanvas()
{
	if (isMain)
		return;
	obs_canvas_t *c = nullptr;
	{
		std::lock_guard<std::mutex> lock(mutex);
		c = canvas ? obs_canvas_get_ref(canvas) : nullptr;
	}
	Detach();
	if (c) {
		obs_frontend_remove_canvas(c);
		obs_canvas_release(c);
	}
}

bool Destination::Attached() const
{
	if (isMain)
		return true;
	std::lock_guard<std::mutex> lock(mutex);
	return canvas != nullptr;
}

uint32_t Destination::Width() const
{
	if (isMain) {
		struct obs_video_info ovi = {};
		return obs_get_video_info(&ovi) ? ovi.base_width : 1920;
	}
	return width;
}

uint32_t Destination::Height() const
{
	if (isMain) {
		struct obs_video_info ovi = {};
		return obs_get_video_info(&ovi) ? ovi.base_height : 1080;
	}
	return height;
}

/* ------------------------------------------------------------------ */
/* Scènes liées                                                         */
/* ------------------------------------------------------------------ */

OBSSource Destination::FindLinked(const std::string &mainUuid) const
{
	if (!canvas || mainUuid.empty())
		return nullptr;

	struct Ctx {
		const std::string *uuid;
		OBSSource found;
	} ctx{&mainUuid, nullptr};

	obs_canvas_enum_scenes(
		canvas,
		[](void *param, obs_source_t *source) {
			auto *c = static_cast<Ctx *>(param);
			OBSDataAutoRelease priv = obs_source_get_private_settings(source);
			const char *link = obs_data_get_string(priv, LINK_KEY);
			if (link && *c->uuid == link) {
				c->found = source;
				return false;
			}
			return true;
		},
		&ctx);
	return ctx.found;
}

bool Destination::Covers(obs_source_t *mainScene) const
{
	if (!mainScene)
		return false;
	if (isMain) {
		OBSDataAutoRelease priv = obs_source_get_private_settings(mainScene);
		return !obs_data_get_bool(priv, EXCLUDE_MAIN_KEY);
	}
	return FindLinked(SourceUuid(mainScene)) != nullptr;
}

OBSSource Destination::LinkedScene(obs_source_t *mainScene) const
{
	if (!mainScene)
		return nullptr;
	if (isMain)
		return Covers(mainScene) ? OBSSource(mainScene) : OBSSource();
	return FindLinked(SourceUuid(mainScene));
}

bool Destination::AddCoverage(obs_source_t *mainScene)
{
	if (!mainScene)
		return false;

	if (isMain) {
		OBSDataAutoRelease priv = obs_source_get_private_settings(mainScene);
		obs_data_set_bool(priv, EXCLUDE_MAIN_KEY, false);
		return true;
	}

	if (!canvas)
		return false;
	if (FindLinked(SourceUuid(mainScene)))
		return true;

	const char *sceneName = obs_source_get_name(mainScene);
	obs_scene_t *scene = obs_canvas_scene_create(canvas, sceneName ? sceneName : "Scene");
	if (!scene)
		return false;

	obs_source_t *source = obs_scene_get_source(scene);
	OBSDataAutoRelease priv = obs_source_get_private_settings(source);
	obs_data_set_string(priv, LINK_KEY, SourceUuid(mainScene).c_str());

	/* Le canvas (SCENE_REF) garde sa propre référence. */
	obs_scene_release(scene);
	return true;
}

void Destination::RemoveCoverage(obs_source_t *mainScene)
{
	if (!mainScene)
		return;

	if (isMain) {
		OBSDataAutoRelease priv = obs_source_get_private_settings(mainScene);
		obs_data_set_bool(priv, EXCLUDE_MAIN_KEY, true);
		return;
	}

	OBSSource linked = FindLinked(SourceUuid(mainScene));
	if (!linked)
		return;

	bool wasProgram = false;
	{
		std::lock_guard<std::mutex> lock(mutex);
		wasProgram = obs_weak_source_references_source(programScene, linked);
		if (wasProgram)
			programScene = nullptr;
	}
	if (wasProgram && transition) {
		obs_transition_force_stop(transition);
		obs_transition_set(transition, nullptr);
	}
	obs_source_remove(linked);
}

void Destination::SyncWithMainScenes(const std::vector<OBSSource> &mainScenes)
{
	if (isMain || !canvas)
		return;

	struct Entry {
		OBSSource source;
		std::string link;
	};
	std::vector<Entry> linked;
	obs_canvas_enum_scenes(
		canvas,
		[](void *param, obs_source_t *source) {
			auto *list = static_cast<std::vector<Entry> *>(param);
			OBSDataAutoRelease priv = obs_source_get_private_settings(source);
			const char *link = obs_data_get_string(priv, LINK_KEY);
			if (link && *link)
				list->push_back({OBSSource(source), link});
			return true;
		},
		&linked);

	for (auto &entry : linked) {
		obs_source_t *main = nullptr;
		for (auto &s : mainScenes) {
			if (SourceUuid(s) == entry.link) {
				main = s;
				break;
			}
		}

		if (!main) {
			/* La scène OBS a été supprimée : la scène liée aussi. */
			bool wasProgram = false;
			{
				std::lock_guard<std::mutex> lock(mutex);
				wasProgram = obs_weak_source_references_source(programScene, entry.source);
				if (wasProgram)
					programScene = nullptr;
			}
			if (wasProgram && transition)
				obs_transition_set(transition, nullptr);
			obs_source_remove(entry.source);
			continue;
		}

		const char *mainName = obs_source_get_name(main);
		const char *linkedName = obs_source_get_name(entry.source);
		if (mainName && linkedName && strcmp(mainName, linkedName) != 0)
			obs_source_set_name(entry.source, mainName);
	}
}

/* ------------------------------------------------------------------ */
/* Program                                                              */
/* ------------------------------------------------------------------ */

static OBSSource FindFrontendTransition(const char *id)
{
	OBSSource found;
	struct obs_frontend_source_list list = {};
	obs_frontend_get_transitions(&list);
	for (size_t i = 0; i < list.sources.num; i++) {
		const char *tid = obs_source_get_id(list.sources.array[i]);
		if (tid && strcmp(tid, id) == 0) {
			found = list.sources.array[i];
			break;
		}
	}
	obs_frontend_source_list_free(&list);
	return found;
}

void Destination::Take(obs_source_t *linkedScene, bool cut)
{
	if (!linkedScene)
		return;

	if (isMain) {
		/* La destination par défaut passe par OBS : sa transition, son
		 * Program, son stream et son enregistrement. */
		OBSSourceAutoRelease onAir = obs_frontend_get_current_scene();
		if (onAir.Get() == linkedScene)
			return;
		OBSSourceAutoRelease current = obs_frontend_get_current_transition();
		OBSSource cutTransition = cut ? FindFrontendTransition("cut_transition") : OBSSource();
		if (cut && cutTransition && current && current.Get() != cutTransition.Get()) {
			obs_frontend_set_current_transition(cutTransition);
			obs_frontend_set_current_scene(linkedScene);
			obs_frontend_set_current_transition(current);
		} else {
			obs_frontend_set_current_scene(linkedScene);
		}
		return;
	}

	if (!transition)
		return;

	{
		/* Déjà à l'antenne : rien à faire (évite une double transition
		 * quand OBS et le plugin déclenchent le même changement). */
		std::lock_guard<std::mutex> lock(mutex);
		if (obs_weak_source_references_source(programScene, linkedScene))
			return;
	}

	const bool immediate = cut || transitionType == "cut_transition" || durationMs == 0;
	if (immediate) {
		obs_transition_force_stop(transition);
		obs_transition_set(transition, linkedScene);
	} else if (!obs_transition_start(transition, OBS_TRANSITION_MODE_AUTO, uint32_t(durationMs), linkedScene)) {
		return; /* une transition est déjà en cours */
	}

	std::lock_guard<std::mutex> lock(mutex);
	programScene = OBSGetWeakRef(linkedScene);
}

std::string Destination::ProgramSceneName() const
{
	if (isMain) {
		OBSSourceAutoRelease current = obs_frontend_get_current_scene();
		const char *n = current ? obs_source_get_name(current) : nullptr;
		return n ? n : "";
	}
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
		/* Le stinger a besoin d'un fichier vidéo : réglages plus tard. */
		if (strcmp(id, "obs_stinger_transition") == 0)
			continue;
		const char *display = obs_source_get_display_name(id);
		types.emplace_back(id, display ? display : id);
	}
	return types;
}

void Destination::CreateTransition(const std::string &id)
{
	if (isMain)
		return;

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
	if (isMain || id.empty() || id == transitionType)
		return;
	transitionType = id;
	if (canvas)
		CreateTransition(id);
}

/* ------------------------------------------------------------------ */
/* Rendu                                                                */
/* ------------------------------------------------------------------ */

void Destination::RenderProgram()
{
	if (isMain) {
		obs_render_main_texture();
		return;
	}

	obs_canvas_t *c = nullptr;
	{
		std::lock_guard<std::mutex> lock(mutex);
		c = canvas ? obs_canvas_get_ref(canvas) : nullptr;
	}
	if (!c)
		return;
	obs_render_canvas_texture(c);
	obs_canvas_release(c);
}

/* ------------------------------------------------------------------ */
/* Persistance                                                          */
/* ------------------------------------------------------------------ */

void Destination::Save(obs_data_t *data) const
{
	obs_data_set_string(data, "id", Id().c_str());
	obs_data_set_bool(data, "main", isMain);
	obs_data_set_bool(data, "locked", locked);
	if (isMain)
		return;

	obs_data_set_string(data, "name", name.c_str());
	obs_data_set_int(data, "width", width);
	obs_data_set_int(data, "height", height);
	obs_data_set_string(data, "transition", transitionType.c_str());
	obs_data_set_int(data, "transition_duration", durationMs);

	OBSSource program;
	{
		std::lock_guard<std::mutex> lock(mutex);
		program = OBSGetStrongRef(programScene);
	}
	OBSDataAutoRelease priv = program ? obs_source_get_private_settings(program) : nullptr;
	obs_data_set_string(data, "program_link", priv ? obs_data_get_string(priv, LINK_KEY) : "");
}

void Destination::Load(obs_data_t *data)
{
	locked = obs_data_get_bool(data, "locked");
	if (isMain)
		return;

	obs_data_set_default_int(data, "width", 1920);
	obs_data_set_default_int(data, "height", 1080);
	obs_data_set_default_string(data, "transition", "fade_transition");
	obs_data_set_default_int(data, "transition_duration", 500);

	name = obs_data_get_string(data, "name");
	transitionType = obs_data_get_string(data, "transition");
	durationMs = int(obs_data_get_int(data, "transition_duration"));

	Attach(uint32_t(obs_data_get_int(data, "width")), uint32_t(obs_data_get_int(data, "height")),
	       obs_data_get_string(data, "id"));

	OBSSource program = FindLinked(obs_data_get_string(data, "program_link"));
	if (program && transition) {
		obs_transition_set(transition, program);
		std::lock_guard<std::mutex> lock(mutex);
		programScene = OBSGetWeakRef(program.Get());
	}
}
