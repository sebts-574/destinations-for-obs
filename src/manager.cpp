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

#include "manager.hpp"

#include <obs-frontend-api.h>
#include <plugin-support.h>

Manager &Manager::Get()
{
	static Manager instance;
	return instance;
}

Manager::Manager()
{
	destinations.emplace_back(Destination::CreateMain());
}

std::vector<Destination *> Manager::Destinations() const
{
	std::vector<Destination *> list;
	for (auto &d : destinations)
		list.push_back(d.get());
	return list;
}

Destination *Manager::Add(const std::string &name, uint32_t width, uint32_t height, bool coverAllScenes)
{
	auto dest = std::make_unique<Destination>(name);
	if (!dest->Attach(width, height))
		return nullptr;

	Destination *raw = dest.get();
	destinations.push_back(std::move(dest));

	if (coverAllScenes) {
		creatingScene = true;
		for (auto &scene : MainScenes())
			raw->AddCoverage(scene);
		creatingScene = false;
	}

	emit destinationsChanged();
	emit scenesChanged();
	return raw;
}

void Manager::Remove(Destination *dest)
{
	if (!dest || dest->IsMain())
		return;

	emit aboutToRemove(dest);
	for (auto it = destinations.begin(); it != destinations.end(); ++it) {
		if (it->get() == dest) {
			dest->DestroyCanvas();
			destinations.erase(it);
			break;
		}
	}
	emit destinationsChanged();
}

/* ------------------------------------------------------------------ */
/* Scènes communes                                                      */
/* ------------------------------------------------------------------ */

std::vector<OBSSource> Manager::MainScenes()
{
	std::vector<OBSSource> scenes;
	struct obs_frontend_source_list list = {};
	obs_frontend_get_scenes(&list);
	for (size_t i = 0; i < list.sources.num; i++)
		scenes.emplace_back(list.sources.array[i]);
	obs_frontend_source_list_free(&list);
	return scenes;
}

OBSSource Manager::CreateScene(const std::string &name, const std::set<Destination *> &coverage)
{
	if (name.empty())
		return nullptr;

	OBSSourceAutoRelease existing = obs_get_source_by_name(name.c_str());
	if (existing)
		return nullptr;

	creatingScene = true;
	obs_scene_t *scene = obs_scene_create(name.c_str());
	creatingScene = false;
	if (!scene)
		return nullptr;
	OBSSource source = obs_scene_get_source(scene);
	/* L'interface d'OBS prend sa propre référence en ajoutant la scène à
	 * sa liste (comme obs-websocket). */
	obs_scene_release(scene);

	SetCoverage(source, coverage);
	return source;
}

void Manager::SetCoverage(obs_source_t *mainScene, const std::set<Destination *> &coverage)
{
	if (!mainScene)
		return;
	creatingScene = true;
	for (auto &d : destinations) {
		const bool wanted = coverage.count(d.get()) > 0;
		if (wanted && !d->Covers(mainScene))
			d->AddCoverage(mainScene);
		else if (!wanted && d->Covers(mainScene))
			d->RemoveCoverage(mainScene);
	}
	creatingScene = false;
	emit scenesChanged();
	emit previewChanged();
}

void Manager::RenameScene(obs_source_t *mainScene, const std::string &name)
{
	if (!mainScene || name.empty())
		return;
	OBSSourceAutoRelease existing = obs_get_source_by_name(name.c_str());
	if (existing)
		return;
	obs_source_set_name(mainScene, name.c_str());
	Sync();
}

void Manager::RemoveScene(obs_source_t *mainScene)
{
	if (!mainScene)
		return;
	OBSSource keep = mainScene;
	for (auto &d : destinations) {
		if (!d->IsMain())
			d->RemoveCoverage(keep);
	}
	if (obs_weak_source_references_source(previewScene, keep)) {
		previewScene = nullptr;
		emit previewChanged();
	}
	obs_source_remove(keep);
	emit scenesChanged();
}

void Manager::Sync()
{
	if (!ready)
		return;

	auto scenes = MainScenes();
	for (auto &d : destinations)
		d->SyncWithMainScenes(scenes);

	OBSSource preview = PreviewScene();
	bool previewAlive = false;
	for (auto &s : scenes) {
		if (s.Get() == preview.Get())
			previewAlive = true;
	}
	if (preview && !previewAlive) {
		previewScene = nullptr;
		emit previewChanged();
	}
	if (!preview && !scenes.empty()) {
		previewScene = OBSGetWeakRef(scenes.front().Get());
		emit previewChanged();
	}
	emit scenesChanged();
}

/* ------------------------------------------------------------------ */
/* Preview / Program                                                    */
/* ------------------------------------------------------------------ */

void Manager::SetPreviewScene(obs_source_t *mainScene)
{
	if (obs_weak_source_references_source(previewScene, mainScene))
		return;
	previewScene = mainScene ? OBSGetWeakRef(mainScene) : OBSWeakSource();
	emit previewChanged();
}

static void TakeAll(const std::vector<std::unique_ptr<Destination>> &destinations, obs_source_t *mainScene, bool cut,
		    bool includeMain)
{
	for (auto &d : destinations) {
		if ((d->IsMain() && !includeMain) || d->Locked())
			continue;
		OBSSource linked = d->LinkedScene(mainScene);
		if (linked)
			d->Take(linked, cut);
	}
}

void Manager::Take(bool cut)
{
	OBSSource preview = PreviewScene();
	if (!preview)
		return;

	taking = true;
	TakeAll(destinations, preview, cut, true);
	taking = false;

	programScene = OBSGetWeakRef(preview.Get());
	emit programChanged();
}

void Manager::TakeOne(Destination *dest, bool cut)
{
	OBSSource preview = PreviewScene();
	if (!dest || !preview)
		return;
	OBSSource linked = dest->LinkedScene(preview);
	if (!linked)
		return;
	taking = true;
	dest->Take(linked, cut);
	taking = false;
	emit programChanged();
}

void Manager::OnSceneCreated(obs_source_t *scene)
{
	if (!ready || creatingScene || !scene)
		return;
	emit sceneCreatedInObs(OBSSource(scene));
}

void Manager::OnObsProgramChanged()
{
	if (!ready)
		return;

	OBSSourceAutoRelease current = obs_frontend_get_current_scene();
	if (!current)
		return;

	if (!taking && !obs_weak_source_references_source(programScene, current)) {
		/* Changement de scène fait dans OBS, hors de la vue Studio. */
		if (followObs)
			TakeAll(destinations, current, false, false);
		programScene = OBSGetWeakRef(current.Get());
	}
	emit programChanged();
}

/* ------------------------------------------------------------------ */
/* Cycle de vie                                                         */
/* ------------------------------------------------------------------ */

void Manager::Save(obs_data_t *data) const
{
	OBSDataArrayAutoRelease list = obs_data_array_create();
	for (auto &d : destinations) {
		OBSDataAutoRelease item = obs_data_create();
		d->Save(item);
		obs_data_array_push_back(list, item);
	}
	obs_data_set_array(data, "list", list);

	OBSSource preview = PreviewScene();
	const char *uuid = preview ? obs_source_get_uuid(preview) : nullptr;
	obs_data_set_string(data, "preview_scene", uuid ? uuid : "");
	obs_data_set_bool(data, "follow_obs", followObs);
}

void Manager::ApplyLoad()
{
	/* Les destinations supplémentaires de la collection précédente ont
	 * déjà été détachées par Reset(). */
	while (destinations.size() > 1)
		destinations.pop_back();

	OBSData data = pendingLoad;
	pendingLoad = nullptr;

	ready = true;

	if (data) {
		obs_data_set_default_bool(data, "follow_obs", true);
		followObs = obs_data_get_bool(data, "follow_obs");

		OBSDataArrayAutoRelease list = obs_data_get_array(data, "list");
		const size_t count = obs_data_array_count(list);
		for (size_t i = 0; i < count; i++) {
			OBSDataAutoRelease item = obs_data_array_item(list, i);
			if (obs_data_get_bool(item, "main")) {
				Main()->Load(item);
				continue;
			}
			auto dest = std::make_unique<Destination>(obs_data_get_string(item, "name"));
			dest->Load(item);
			if (dest->Attached())
				destinations.push_back(std::move(dest));
		}

		OBSSourceAutoRelease preview = obs_get_source_by_uuid(obs_data_get_string(data, "preview_scene"));
		previewScene = preview ? OBSGetWeakRef(preview.Get()) : OBSWeakSource();
	}

	OBSSourceAutoRelease current = obs_frontend_get_current_scene();
	programScene = current ? OBSGetWeakRef(current.Get()) : OBSWeakSource();

	Sync();
	obs_log(LOG_INFO, "%zu destination(s) chargée(s)", destinations.size());
	emit loaded();
	emit destinationsChanged();
	emit previewChanged();
	emit programChanged();
}

void Manager::Reset()
{
	emit aboutToReset();
	ready = false;
	while (destinations.size() > 1) {
		destinations.back()->Detach();
		destinations.pop_back();
	}
	previewScene = nullptr;
	programScene = nullptr;
}
