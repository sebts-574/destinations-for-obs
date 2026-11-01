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

#include <QPointer>
#include <QWidget>

#include <vector>

class Destination;
class ProjectorWindow;
class TileGrid;
class TileWidget;
class QCheckBox;
class QLabel;
class QListWidget;
class QToolButton;

/* Vue Studio : Preview (édition) et Program des destinations couvertes par
 * la scène sélectionnée, empilées verticalement, avec les listes de scènes,
 * de sources et de destinations. */
class StudioWindow : public QWidget {
	Q_OBJECT

public:
	explicit StudioWindow(QWidget *parent);
	~StudioWindow() override;

	void Toggle();
	void Open();
	void Close();
	void Shutdown();

	/* Demande les destinations d'une scène créée dans OBS. */
	void AskCoverage(obs_source_t *scene);

protected:
	void closeEvent(QCloseEvent *event) override;

private slots:
	void RebuildGrids() { UpdateGrids(false); }
	void UpdateGrids(bool force);
	void RefreshScenes();
	void RefreshSources();
	void RefreshDestinations();
	void RefreshTally();
	void OnReset();

private:
	void BuildUi();
	void ConnectTile(TileWidget *tile);
	void SetActiveDestination(Destination *dest);
	TileWidget *ActivePreviewTile() const;
	void UpdateShowing(const std::vector<OBSSource> &next);

	void AddScene();
	void EditScene(obs_source_t *scene);
	void RemoveScene(obs_source_t *scene);
	OBSSource SelectedScene() const;

	void FillAddSourceMenu();
	void RemoveSelectedSource();
	void MoveSelectedSource(enum obs_order_movement movement);
	void OpenSelectedSourceProperties(bool filters);

	void AddDestination();
	void EditDestination(Destination *dest);
	void RemoveDestination(Destination *dest);
	Destination *SelectedDestination() const;
	void FillProjectorMenu();
	void OpenProjector(Destination *dest, int screenIndex);
	void CloseProjectors(Destination *dest);

	TileGrid *pvwGrid = nullptr;
	TileGrid *pgmGrid = nullptr;
	QListWidget *sceneList = nullptr;
	QListWidget *sourceList = nullptr;
	QListWidget *destList = nullptr;
	QLabel *sourcesTitle = nullptr;
	QToolButton *addSourceButton = nullptr;
	QToolButton *projectorButton = nullptr;
	QCheckBox *followObs = nullptr;

	Destination *activeDest = nullptr;
	std::vector<OBSSource> showing;
	std::vector<std::pair<Destination *, QPointer<ProjectorWindow>>> projectors;
	bool refreshingSources = false;
};
