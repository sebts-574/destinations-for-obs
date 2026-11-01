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
 * Modèle V0.2
 * -----------
 * - Les scènes OBS (canvas principal) sont la référence : une « scène » du
 *   plugin = une scène OBS.
 * - Chaque destination supplémentaire a son propre canvas. Pour chaque scène
 *   OBS qu'elle couvre, elle possède une scène « liée » dans son canvas, avec
 *   ses propres sources et placements. Le lien est stocké dans les réglages
 *   privés de la scène liée (clé LINK_KEY = UUID de la scène OBS).
 * - La destination par défaut est le canvas principal d'OBS lui-même. Une
 *   scène OBS la couvre sauf si elle porte le drapeau EXCLUDE_MAIN_KEY.
 */
class Destination {
public:
	static constexpr const char *MAIN_ID = "main";
	static constexpr const char *LINK_KEY = "destinations_link";
	static constexpr const char *EXCLUDE_MAIN_KEY = "destinations_exclude_main";

	/* Destination par défaut = canvas principal d'OBS. */
	static Destination *CreateMain();
	/* Destination supplémentaire avec son propre canvas. */
	explicit Destination(std::string name);
	~Destination();

	Destination(const Destination &) = delete;
	Destination &operator=(const Destination &) = delete;

	bool IsMain() const { return isMain; }
	std::string Id() const;
	const std::string &Name() const { return name; }
	void SetName(const std::string &newName);

	/* Trouve le canvas (par UUID puis par nom) ou le crée, puis démarre sa
	 * vidéo. Sans effet sur la destination par défaut. */
	bool Attach(uint32_t width, uint32_t height, const std::string &uuidHint = "");
	void Detach();
	/* Supprime définitivement le canvas et ses scènes. */
	void DestroyCanvas();
	bool Attached() const;

	uint32_t Width() const;
	uint32_t Height() const;

	/* Verrouillée : ignorée par le TAKE global (reste sur son Program). */
	bool Locked() const { return locked; }
	void SetLocked(bool value) { locked = value; }

	/* --- Scènes liées --- */
	bool Covers(obs_source_t *mainScene) const;
	/* Scène à rendre / à prendre pour cette destination (la scène OBS
	 * elle-même pour la destination par défaut). Vide si non couverte. */
	OBSSource LinkedScene(obs_source_t *mainScene) const;
	bool AddCoverage(obs_source_t *mainScene);
	void RemoveCoverage(obs_source_t *mainScene);
	/* Renomme les scènes liées et supprime celles dont la scène OBS a
	 * disparu. */
	void SyncWithMainScenes(const std::vector<OBSSource> &mainScenes);

	/* --- Program --- */
	void Take(obs_source_t *linkedScene, bool cut);
	std::string ProgramSceneName() const;

	/* --- Transitions (destinations supplémentaires ; la destination par
	 * défaut utilise la transition d'OBS) --- */
	static std::vector<std::pair<std::string, std::string>> TransitionTypes();
	void SetTransitionType(const std::string &id);
	std::string TransitionType() const { return transitionType; }
	void SetTransitionDuration(int ms) { durationMs = ms < 0 ? 0 : ms; }
	int TransitionDuration() const { return durationMs; }

	/* --- Rendu (thread graphique) --- */
	void RenderProgram();

	/* --- Persistance --- */
	void Save(obs_data_t *data) const;
	void Load(obs_data_t *data);

private:
	Destination() = default;

	OBSSource FindLinked(const std::string &mainUuid) const;
	void CreateTransition(const std::string &id);
	void StartVideo();

	bool isMain = false;
	std::string name;
	bool locked = false;

	obs_canvas_t *canvas = nullptr;
	uint32_t width = 1920;
	uint32_t height = 1080;

	/* Protège uniquement ce que lit le thread graphique. Ne jamais appeler
	 * libobs en tenant ce verrou. */
	mutable std::mutex mutex;
	OBSWeakSource programScene;
	OBSSource transition;
	std::string transitionType = "fade_transition";
	int durationMs = 500;
};
