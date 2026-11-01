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
#include "studio-window.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QAction>
#include <QMainWindow>
#include <QMetaObject>
#include <QPointer>
#include <QToolBar>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

#define T(key) QString::fromUtf8(obs_module_text(key))

static const char *SAVE_KEY = "destinations_v2";

static QPointer<StudioWindow> studio;
static obs_hotkey_id studioHotkey = OBS_INVALID_HOTKEY_ID;
static obs_hotkey_id takeHotkey = OBS_INVALID_HOTKEY_ID;
static obs_hotkey_id cutHotkey = OBS_INVALID_HOTKEY_ID;

/* ------------------------------------------------------------------ */
/* Sauvegarde dans la collection de scènes                             */
/* ------------------------------------------------------------------ */

static void SaveHotkey(obs_data_t *data, const char *key, obs_hotkey_id id)
{
	if (id == OBS_INVALID_HOTKEY_ID)
		return;
	OBSDataArrayAutoRelease array = obs_hotkey_save(id);
	obs_data_set_array(data, key, array);
}

static void LoadHotkey(obs_data_t *data, const char *key, obs_hotkey_id id)
{
	if (id == OBS_INVALID_HOTKEY_ID)
		return;
	OBSDataArrayAutoRelease array = obs_data_get_array(data, key);
	obs_hotkey_load(id, array);
}

static void SaveLoad(obs_data_t *save_data, bool saving, void *)
{
	if (saving) {
		OBSDataAutoRelease obj = obs_data_create();
		Manager::Get().Save(obj);
		SaveHotkey(obj, "hotkey_studio", studioHotkey);
		SaveHotkey(obj, "hotkey_take", takeHotkey);
		SaveHotkey(obj, "hotkey_cut", cutHotkey);
		obs_data_set_obj(save_data, SAVE_KEY, obj);
		return;
	}

	/* Chargement : appliqué quand OBS a fini de charger les canvases. */
	OBSDataAutoRelease obj = obs_data_get_obj(save_data, SAVE_KEY);
	Manager::Get().SetPendingLoad(obj);
	if (obj) {
		LoadHotkey(obj, "hotkey_studio", studioHotkey);
		LoadHotkey(obj, "hotkey_take", takeHotkey);
		LoadHotkey(obj, "hotkey_cut", cutHotkey);
	}
}

/* ------------------------------------------------------------------ */
/* Événements                                                           */
/* ------------------------------------------------------------------ */

static void FrontendEvent(enum obs_frontend_event event, void *)
{
	Manager &m = Manager::Get();
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
		m.ApplyLoad();
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP:
		m.Reset();
		break;
	case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
		m.Sync();
		break;
	case OBS_FRONTEND_EVENT_SCENE_CHANGED:
		m.OnObsProgramChanged();
		break;
	case OBS_FRONTEND_EVENT_EXIT:
	case OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN:
		if (studio)
			studio->Shutdown();
		m.Reset();
		break;
	default:
		break;
	}
}

/* Une scène créée dans OBS (bouton +, duplication, obs-websocket…) : on
 * demande ses destinations, hors du traitement d'OBS en cours. */
static void SourceCreated(void *, calldata_t *cd)
{
	obs_source_t *source = static_cast<obs_source_t *>(calldata_ptr(cd, "source"));
	if (!source || !obs_source_is_scene(source))
		return;

	Manager &m = Manager::Get();
	if (!m.Ready() || m.CreatingScene())
		return;

	/* Seulement les scènes du canvas principal (pas nos scènes liées). */
	obs_canvas_t *canvas = obs_source_get_canvas(source);
	obs_canvas_t *main = obs_get_main_canvas();
	/* Selon le moment du signal, le canvas peut ne pas être encore
	 * renseigné : une scène sans canvas est une scène principale (nos
	 * scènes liées sont filtrées par CreatingScene()). */
	const bool isMain = !canvas || canvas == main;
	obs_canvas_release(canvas);
	obs_canvas_release(main);
	if (!isMain)
		return;

	OBSWeakSource weak = OBSGetWeakRef(source);
	QMetaObject::invokeMethod(
		studio.data(),
		[weak]() {
			OBSSource scene = OBSGetStrongRef(weak);
			if (scene && studio)
				studio->AskCoverage(scene);
		},
		Qt::QueuedConnection);
}

/* ------------------------------------------------------------------ */
/* Raccourcis                                                           */
/* ------------------------------------------------------------------ */

static void RunOnUi(void (*fn)())
{
	if (!studio)
		return;
	QMetaObject::invokeMethod(studio.data(), [fn]() { fn(); }, Qt::QueuedConnection);
}

static void StudioHotkey(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		RunOnUi([]() {
			if (studio)
				studio->Toggle();
		});
}

static void TakeHotkey(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		RunOnUi([]() { Manager::Get().Take(false); });
}

static void CutHotkey(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		RunOnUi([]() { Manager::Get().Take(true); });
}

/* ------------------------------------------------------------------ */

bool obs_module_load(void)
{
	obs_log(LOG_INFO, "plugin chargé (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_post_load(void)
{
	auto *mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());

	studio = new StudioWindow(mainWindow);

	/* Entrée dans la vue Studio : menu Outils, barre d'outils, raccourci. */
	auto *toolsAction =
		static_cast<QAction *>(obs_frontend_add_tools_menu_qaction(obs_module_text("Destinations.StudioMenu")));
	QObject::connect(toolsAction, &QAction::triggered, []() {
		if (studio)
			studio->Toggle();
	});

	if (mainWindow) {
		auto *toolbar = new QToolBar(T("Destinations.Toolbar"), mainWindow);
		toolbar->setObjectName("destinationsForObsToolbar");
		QAction *studioAction = toolbar->addAction(T("Destinations.StudioButton"));
		studioAction->setToolTip(T("Destinations.StudioMenu"));
		QObject::connect(studioAction, &QAction::triggered, []() {
			if (studio)
				studio->Toggle();
		});
		mainWindow->addToolBar(Qt::TopToolBarArea, toolbar);
	}

	studioHotkey = obs_hotkey_register_frontend(
		"destinations_for_obs.studio", obs_module_text("Destinations.HotkeyStudio"), StudioHotkey, nullptr);
	takeHotkey = obs_hotkey_register_frontend("destinations_for_obs.take",
						  obs_module_text("Destinations.HotkeyTake"), TakeHotkey, nullptr);
	cutHotkey = obs_hotkey_register_frontend("destinations_for_obs.cut", obs_module_text("Destinations.HotkeyCut"),
						 CutHotkey, nullptr);

	signal_handler_connect(obs_get_signal_handler(), "source_create", SourceCreated, nullptr);
	obs_frontend_add_event_callback(FrontendEvent, nullptr);
	obs_frontend_add_save_callback(SaveLoad, nullptr);
}

void obs_module_unload(void)
{
	obs_frontend_remove_save_callback(SaveLoad, nullptr);
	obs_frontend_remove_event_callback(FrontendEvent, nullptr);
	signal_handler_disconnect(obs_get_signal_handler(), "source_create", SourceCreated, nullptr);

	for (obs_hotkey_id id : {studioHotkey, takeHotkey, cutHotkey}) {
		if (id != OBS_INVALID_HOTKEY_ID)
			obs_hotkey_unregister(id);
	}

	/* La fenêtre Studio appartient à la fenêtre principale d'OBS. */
	studio = nullptr;
	obs_log(LOG_INFO, "plugin déchargé");
}
