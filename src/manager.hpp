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

#pragma once

#include "destination.hpp"

#include <QObject>

#include <memory>
#include <set>
#include <string>
#include <vector>

/* Propriétaire des destinations et des opérations sur les scènes communes.
 * Vit sur le thread de l'interface. */
class Manager : public QObject {
	Q_OBJECT

public:
	static Manager &Get();

	/* Destination par défaut en premier. */
	std::vector<Destination *> Destinations() const;
	Destination *Main() const { return destinations.empty() ? nullptr : destinations.front().get(); }
	Destination *Add(const std::string &name, uint32_t width, uint32_t height, bool coverAllScenes);
	void Remove(Destination *dest);
	void NotifyDestinationsChanged() { emit destinationsChanged(); }

	/* --- Scènes communes (= scènes OBS) --- */
	static std::vector<OBSSource> MainScenes();
	/* Crée une scène OBS et ses scènes liées dans les destinations
	 * choisies. Renvoie la scène OBS, ou rien si le nom est pris. */
	OBSSource CreateScene(const std::string &name, const std::set<Destination *> &coverage);
	void SetCoverage(obs_source_t *mainScene, const std::set<Destination *> &coverage);
	void RenameScene(obs_source_t *mainScene, const std::string &name);
	void RemoveScene(obs_source_t *mainScene);
	void Sync();

	/* --- Preview / Program globaux --- */
	void SetPreviewScene(obs_source_t *mainScene);
	OBSSource PreviewScene() const { return OBSGetStrongRef(previewScene); }
	OBSSource ProgramScene() const { return OBSGetStrongRef(programScene); }
	/* TAKE global : chaque destination couverte passe à sa scène liée,
	 * avec sa propre transition. Les autres gardent leur Program. */
	void Take(bool cut);
	/* TAKE d'une seule destination (même si elle est verrouillée). */
	void TakeOne(Destination *dest, bool cut);
	void OnObsProgramChanged();
	/* Quand OBS change de scène (bouton, raccourci, Stream Deck…), les
	 * autres destinations suivent. */
	bool FollowObs() const { return followObs; }
	void SetFollowObs(bool value) { followObs = value; }

	/* --- Cycle de vie de la collection de scènes --- */
	void Save(obs_data_t *data) const;
	void SetPendingLoad(obs_data_t *data) { pendingLoad = data; }
	void ApplyLoad();
	void Reset();

	/* Appelé quand une scène est créée dans OBS (bouton +, duplication,
	 * obs-websocket…) : la vue demande les destinations couvertes. */
	void OnSceneCreated(obs_source_t *scene);
	bool CreatingScene() const { return creatingScene; }
	bool Ready() const { return ready; }

signals:
	void aboutToReset();
	void loaded();
	void destinationsChanged();
	void aboutToRemove(Destination *dest);
	void scenesChanged();
	void previewChanged();
	void programChanged();
	void sceneCreatedInObs(OBSSource scene);

private:
	Manager();

	std::vector<std::unique_ptr<Destination>> destinations;
	OBSWeakSource previewScene;
	OBSWeakSource programScene;
	OBSData pendingLoad;
	bool ready = false;
	bool followObs = true;
	bool taking = false;
	bool creatingScene = false;
};
