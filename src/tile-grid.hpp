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

#include "scene-editor.hpp"

#include <obs.hpp>

#include <QWidget>

#include <atomic>
#include <mutex>
#include <vector>

class Destination;
class EditDisplay;
class QLabel;
class QPushButton;

/* Une destination dans la grille Preview ou Program de la vue Studio.
 * En Preview, permet d'éditer les sources à la souris. */
class TileWidget : public QWidget {
	Q_OBJECT

public:
	TileWidget(Destination *destination, bool program, QWidget *parent);
	~TileWidget() override;

	Destination *Dest() const { return dest; }
	bool IsProgram() const { return program; }

	/* Scène liée de cette destination pour le Preview courant (vide si la
	 * scène ne couvre pas la destination). */
	void SetLinkedScene(obs_source_t *scene);
	OBSSource LinkedScene() const;

	void SetActive(bool active);
	void SetTally(bool onAir);
	void SetSelectedItem(int64_t id);
	int64_t SelectedItem() const { return selectedId.load(); }
	void RefreshHeader();

	/* Événements transmis par l'affichage. */
	void MousePress(QMouseEvent *event);
	void MouseMove(QMouseEvent *event);
	void MouseRelease(QMouseEvent *event);
	void MouseDoubleClick(QMouseEvent *event);
	bool KeyPress(QKeyEvent *event);

signals:
	void activated(TileWidget *tile);
	void selectionChanged(TileWidget *tile, qint64 itemId);
	void itemsChanged(TileWidget *tile);
	void lockChanged(TileWidget *tile);
	void contextMenuRequested(TileWidget *tile, const QPoint &globalPos);

private:
	static void DrawCb(void *data, uint32_t cx, uint32_t cy);
	bool ToCanvas(const QPointF &widgetPos, float &x, float &y, float &screenScale) const;
	OBSSceneItem SelectedSceneItem() const;

	Destination *dest;
	bool program;
	bool active = false;
	bool onAir = false;

	QWidget *header = nullptr;
	QLabel *headerLabel = nullptr;
	QPushButton *lockButton = nullptr;
	EditDisplay *display = nullptr;

	mutable std::mutex mutex;
	OBSWeakSource linkedScene;
	std::atomic<int64_t> selectedId{-1};

	/* Glisser-déposer en cours. */
	enum class Drag { None, Move, Resize, Crop };
	Drag drag = Drag::None;
	int dragEdges = 0;
	float startX = 0.0f;
	float startY = 0.0f;
	SceneEditor::Box startBox;
	struct obs_sceneitem_crop startCrop = {};
	float startScaleX = 1.0f;
	float startScaleY = 1.0f;
};

/* Colonne de tuiles (une par destination couverte par la scène), empilées
 * verticalement et mises à l'échelle pour tenir dans la zone. */
class TileGrid : public QWidget {
	Q_OBJECT

public:
	TileGrid(bool program, QWidget *parent);

	void SetDestinations(const std::vector<Destination *> &dests);
	const std::vector<TileWidget *> &Tiles() const { return tiles; }
	TileWidget *TileFor(Destination *dest) const;
	void Clear();

protected:
	void resizeEvent(QResizeEvent *event) override;

private:
	void Relayout();

	bool program;
	std::vector<TileWidget *> tiles;
	QLabel *emptyLabel = nullptr;
};
