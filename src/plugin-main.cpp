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

#include "destination-dock.hpp"
#include "destination.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QMainWindow>

#include <memory>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

/* V0.1 : une seule destination. La V0.2 passera à une liste. */
static const char *DOCK_ID = "DestinationsDock1";
static const char *SAVE_KEY = "destinations";
static const char *DEFAULT_NAME = "Destination 1";

static std::unique_ptr<Destination> destination;
static DestinationDock *dock = nullptr;
static OBSData pendingLoad; /* données lues dans la collection, appliquées après chargement */

/* ------------------------------------------------------------------ */

static void ApplyLoadedState()
{
	if (!destination)
		return;

	if (pendingLoad) {
		destination->Load(pendingLoad);
		pendingLoad = nullptr;
	} else if (!destination->Attached()) {
		/* Première utilisation dans cette collection. */
		destination->Attach(1920, 1080);
	}

	if (destination->Attached() && destination->SceneNames().empty()) {
		std::string first = std::string(obs_module_text("Destinations.Scene")) + " 1";
		destination->CreateScene(first);
		destination->SetPreviewScene(first);
	}

	if (dock)
		dock->RefreshAll();
}

static void SaveLoad(obs_data_t *save_data, bool saving, void *)
{
	if (!destination)
		return;

	if (saving) {
		OBSDataAutoRelease obj = obs_data_create();
		destination->Save(obj);
		OBSDataArrayAutoRelease array = obs_data_array_create();
		obs_data_array_push_back(array, obj);
		obs_data_set_array(save_data, SAVE_KEY, array);
		return;
	}

	/* Chargement : on garde les données et on les applique quand OBS
	 * a fini de charger les canvases de la collection. */
	OBSDataArrayAutoRelease array = obs_data_get_array(save_data, SAVE_KEY);
	pendingLoad = nullptr;
	if (array && obs_data_array_count(array) > 0) {
		OBSDataAutoRelease obj = obs_data_array_item(array, 0);
		pendingLoad = obj.Get();
	}
}

static void FrontendEvent(enum obs_frontend_event event, void *)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
		ApplyLoadedState();
		break;

	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP:
		if (dock) {
			dock->CloseProjectors();
			dock->ClearUi();
		}
		if (destination)
			destination->Detach();
		break;

	case OBS_FRONTEND_EVENT_EXIT:
	case OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN:
		if (dock)
			dock->Shutdown();
		if (destination)
			destination->Detach();
		break;

	default:
		break;
	}
}

/* ------------------------------------------------------------------ */

bool obs_module_load(void)
{
	obs_log(LOG_INFO, "plugin chargé (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_post_load(void)
{
	destination = std::make_unique<Destination>(DEFAULT_NAME);

	auto *mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	dock = new DestinationDock(destination.get(), mainWindow);
	if (!obs_frontend_add_dock_by_id(DOCK_ID, DEFAULT_NAME, dock)) {
		obs_log(LOG_ERROR, "impossible d'ajouter le dock");
		delete dock;
		dock = nullptr;
	}

	obs_frontend_add_event_callback(FrontendEvent, nullptr);
	obs_frontend_add_save_callback(SaveLoad, nullptr);
}

void obs_module_unload(void)
{
	obs_frontend_remove_save_callback(SaveLoad, nullptr);
	obs_frontend_remove_event_callback(FrontendEvent, nullptr);

	/* Le dock appartient à la fenêtre principale d'OBS, qui le détruit. */
	dock = nullptr;
	pendingLoad = nullptr;
	destination.reset();

	obs_log(LOG_INFO, "plugin déchargé");
}
