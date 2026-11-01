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

#include <obs.h>

/* Géométrie des sources dans une scène, en pixels du canvas, sans rotation.
 * Indépendant de l'alignement OBS de chaque source : la position stockée
 * par OBS est celle du point d'alignement ; on travaille sur la boîte. */
namespace SceneEditor {

struct Box {
	float x = 0.0f;
	float y = 0.0f;
	float w = 0.0f;
	float h = 0.0f;

	bool Contains(float px, float py) const { return px >= x && px <= x + w && py >= y && py <= y + h; }
};

/* Poignées : combinaison de bords. */
enum Edge : int {
	EDGE_NONE = 0,
	EDGE_LEFT = 1 << 0,
	EDGE_RIGHT = 1 << 1,
	EDGE_TOP = 1 << 2,
	EDGE_BOTTOM = 1 << 3,
};

bool GetBox(obs_sceneitem_t *item, Box &box);
/* Redimensionne (échelle ou cadre selon la source) et place la boîte. */
void SetBox(obs_sceneitem_t *item, const Box &box);
bool IsRotated(obs_sceneitem_t *item);
bool UsesBounds(obs_sceneitem_t *item);

/* Taille de la source après recadrage, en pixels source. */
bool CroppedSourceSize(obs_sceneitem_t *item, float &w, float &h);

/* Source visible la plus haute sous le point, -1 si aucune. */
int64_t HitTest(obs_scene_t *scene, float x, float y);

/* Poignée sous le point (tolérance en pixels canvas). */
int HandleAt(const Box &box, float x, float y, float tolerance);

/* Actions de menu. */
void FitToCanvas(obs_sceneitem_t *item, uint32_t cw, uint32_t ch);
void StretchToCanvas(obs_sceneitem_t *item, uint32_t cw, uint32_t ch);
void CenterInCanvas(obs_sceneitem_t *item, uint32_t cw, uint32_t ch);
void ResetTransform(obs_sceneitem_t *item);

/* Dessin de la sélection (thread graphique, coordonnées canvas). */
void DrawSelection(obs_scene_t *scene, int64_t itemId, float screenScale);

} // namespace SceneEditor
