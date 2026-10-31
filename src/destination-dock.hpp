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

#include <QPointer>
#include <QWidget>

#include <memory>
#include <vector>

class OBSQTDisplay;
class ProjectorWindow;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTimer;
class QToolButton;

/* Rendu d'une destination, centrée et à l'échelle, dans un affichage OBS.
 * Utilisé par le dock (PVW/PGM) et par le projecteur plein écran. */
void DrawDestination(Destination *dest, bool program, uint32_t cx, uint32_t cy);

class DestinationDock : public QWidget {
	Q_OBJECT

public:
	explicit DestinationDock(Destination *destination, QWidget *parent = nullptr);
	~DestinationDock() override;

	/* À appeler après un chargement / changement de collection. */
	void RefreshAll();
	/* À appeler avant le nettoyage de la collection. */
	void ClearUi();
	void CloseProjectors();
	/* À la fermeture d'OBS : retire les callbacks de rendu. */
	void Shutdown();

private slots:
	void OnSceneSelected();
	void OnAddScene();
	void OnRemoveScene();
	void OnRemoveItem();
	void OnTransitionChanged(int index);
	void OnDurationChanged(int value);
	void OnCut();
	void OnTake();
	void OnResolution();
	void UpdateTally();

private:
	void BuildUi();
	void RefreshScenes();
	void RefreshItems();
	void RefreshTransitions();
	void FillAddSourceMenu();
	void FillProjectorMenu();
	void OpenProjector(int screenIndex);

	static void DrawPreviewCb(void *data, uint32_t cx, uint32_t cy);
	static void DrawProgramCb(void *data, uint32_t cx, uint32_t cy);

	Destination *dest;

	QLabel *titleLabel = nullptr;
	OBSQTDisplay *previewDisplay = nullptr;
	OBSQTDisplay *programDisplay = nullptr;
	QLabel *previewLabel = nullptr;
	QLabel *programLabel = nullptr;

	QComboBox *transitionCombo = nullptr;
	QSpinBox *durationSpin = nullptr;
	QPushButton *cutButton = nullptr;
	QPushButton *takeButton = nullptr;

	QListWidget *sceneList = nullptr;
	QListWidget *itemList = nullptr;
	QToolButton *addSourceButton = nullptr;
	QToolButton *projectorButton = nullptr;

	QTimer *tallyTimer = nullptr;
	std::string lastPreview;
	std::string lastProgram;

	std::vector<QPointer<ProjectorWindow>> projectors;
};
