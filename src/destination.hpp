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

#include <obs.hpp>

#include <mutex>
#include <string>
#include <utility>
#include <vector>

/*
 * Une Destination = un canvas OBS dédié avec :
 *  - ses propres scènes,
 *  - une scène en Preview (rendue uniquement dans l'interface),
 *  - une sortie Program (canal 0 du canvas = une transition privée),
 *  - un type de transition et une durée.
 *
 * Cette classe ne dépend que de libobs + obs-frontend-api (pas de Qt),
 * pour pouvoir être réutilisée quand on passera à N destinations (V0.2).
 */
class Destination {
public:
	explicit Destination(std::string name);
	~Destination();

	Destination(const Destination &) = delete;
	Destination &operator=(const Destination &) = delete;

	/* Trouve le canvas existant (chargé avec la collection de scènes)
	 * ou le crée, puis démarre sa vidéo. */
	bool Attach(uint32_t width, uint32_t height);
	/* Libère les références (appelé au nettoyage de la collection). */
	void Detach();
	bool Attached() const { return canvas != nullptr; }

	const std::string &Name() const { return name; }
	uint32_t Width() const { return width; }
	uint32_t Height() const { return height; }

	/* --- Scènes --- */
	std::vector<std::string> SceneNames() const;
	bool CreateScene(const std::string &sceneName);
	void RemoveScene(const std::string &sceneName);
	OBSSourceAutoRelease GetSceneSource(const std::string &sceneName) const;

	/* Ajoute une source existante (entrée ou scène du canvas principal)
	 * dans une scène de la destination, ajustée au cadre. */
	bool AddSourceToScene(const std::string &sceneName, obs_source_t *source);

	/* --- Preview / Program --- */
	void SetPreviewScene(const std::string &sceneName);
	std::string PreviewSceneName() const;
	std::string ProgramSceneName() const;

	/* --- Transitions --- */
	static std::vector<std::pair<std::string, std::string>> TransitionTypes();
	void SetTransitionType(const std::string &id);
	std::string TransitionType() const;
	void SetTransitionDuration(int ms);
	int TransitionDuration() const;

	void Cut();  /* passage immédiat Preview -> Program */
	void Take(); /* passage avec la transition choisie   */

	/* --- Rendu (appelé depuis le thread graphique) --- */
	void RenderPreview();
	void RenderProgram();

	/* --- Persistance (dans la collection de scènes) --- */
	void Save(obs_data_t *data) const;
	void Load(obs_data_t *data);

private:
	void CreateTransition(const std::string &id);
	void StartVideo();

	std::string name;
	obs_canvas_t *canvas = nullptr;
	uint32_t width = 1920;
	uint32_t height = 1080;

	/* Protège uniquement les références lues par le thread graphique.
	 * Ne jamais appeler libobs en tenant ce verrou. */
	mutable std::mutex mutex;
	OBSWeakSource previewScene;
	OBSWeakSource programScene;
	OBSSource transition;
	std::string transitionType = "fade_transition";
	int durationMs = 500;
};
