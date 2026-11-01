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
	/* Nouvelle destination, ajoutée à toutes les scènes existantes (on la
	 * retire ensuite des scènes où elle n'a rien à faire). */
	Destination *Add(const std::string &name, uint32_t width, uint32_t height);
	void Remove(Destination *dest);
	void NotifyDestinationsChanged() { emit destinationsChanged(); }

	/* --- Scènes communes (= scènes OBS) --- */
	static std::vector<OBSSource> MainScenes();
	OBSSource CreateScene(const std::string &name, const std::set<Destination *> &coverage);
	void SetCoverage(obs_source_t *mainScene, const std::set<Destination *> &coverage);
	void RenameScene(obs_source_t *mainScene, const std::string &name);
	void RemoveScene(obs_source_t *mainScene);
	void Sync();

	/* --- Preview / Program globaux --- */
	void SetPreviewScene(obs_source_t *mainScene);
	OBSSource PreviewScene() const { return OBSGetStrongRef(previewScene); }
	OBSSource ProgramScene() const { return OBSGetStrongRef(programScene); }
	/* Transition : chaque destination couverte et non verrouillée passe à
	 * sa scène liée avec la transition d'OBS. Les destinations que la
	 * scène ne couvre pas gardent leur Program. Le Preview reçoit ensuite
	 * l'ancien Program, comme dans OBS. */
	void Take(bool cut);
	void OnObsProgramChanged();

	/* --- Cycle de vie de la collection de scènes --- */
	void Save(obs_data_t *data) const;
	void SetPendingLoad(obs_data_t *data) { pendingLoad = data; }
	void ApplyLoad();
	void Reset();

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

private:
	Manager();

	std::vector<std::unique_ptr<Destination>> destinations;
	OBSWeakSource previewScene;
	OBSWeakSource programScene;
	OBSData pendingLoad;
	bool ready = false;
	bool taking = false;
	bool creatingScene = false;
};
