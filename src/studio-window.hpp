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

#include <QMainWindow>
#include <QPointer>

#include <vector>

class AudioMixer;
class Destination;
class ProjectorWindow;
class TileGrid;
class TileWidget;
class QAction;
class QComboBox;
class QListWidget;
class QMenu;
class QPushButton;
class QSpinBox;
class QToolButton;

/* Multidestination Studio : alternative complète à la fenêtre d'OBS pour
 * travailler sur plusieurs destinations (Preview / Program par destination,
 * scènes, sources, mixeur, transitions). */
class StudioWindow : public QMainWindow {
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
	void RefreshTransitions();

	QByteArray SaveLayout() const;
	void RestoreLayout(const QByteArray &state);

protected:
	void closeEvent(QCloseEvent *event) override;

private slots:
	void RebuildGrids() { UpdateGrids(false); }
	void UpdateGrids(bool force);
	void RefreshScenes();
	void RefreshSources();
	void RefreshTally();
	void OnReset();

private:
	void BuildCentral();
	void BuildDocks();
	void BuildMenus();
	void ConnectTile(TileWidget *tile);
	void SetActiveDestination(Destination *dest);
	TileWidget *ActivePreviewTile() const;
	void UpdateShowing(const std::vector<OBSSource> &next);

	/* Scènes */
	void AddScene();
	void EditScene(obs_source_t *scene);
	void RemoveScene(obs_source_t *scene);
	OBSSource SelectedScene() const;

	/* Sources de la destination active */
	OBSSceneItem CurrentItem() const;
	void SelectItem(int64_t id);
	void FillAddSourceMenu(QMenu *menu);
	void CreateNewSource(const char *typeId);
	void AddExistingSource(obs_source_t *source);
	void ShowItemMenu(const QPoint &globalPos);

	/* Édition (menus + raccourcis) */
	QAction *MakeEditAction(const QString &text, const QList<QKeySequence> &keys, void (StudioWindow::*fn)());
	void EditCopy();
	void EditPaste();
	void EditRemove();
	void EditCopyTransform();
	void EditPasteTransform();
	void EditResetTransform();
	void EditFit();
	void EditStretch();
	void EditCenter();
	void EditMoveUp();
	void EditMoveDown();
	void EditMoveTop();
	void EditMoveBottom();
	void EditProperties();
	void EditFilters();

	/* Destinations */
	void FillDestinationsMenu();
	void NewDestination();
	void EditDestination(Destination *dest);
	void DeleteDestination(Destination *dest);
	void OpenProjector(Destination *dest, bool fullscreen);
	void CloseProjectors(Destination *dest);
	bool HasProjector(Destination *dest) const;

	void UpdateControls();

	TileGrid *pvwGrid = nullptr;
	TileGrid *pgmGrid = nullptr;
	QWidget *monitors = nullptr;
	QListWidget *sceneList = nullptr;
	QListWidget *sourceList = nullptr;
	QToolButton *addSourceButton = nullptr;
	AudioMixer *mixer = nullptr;
	QComboBox *transitionCombo = nullptr;
	QSpinBox *durationSpin = nullptr;
	QPushButton *streamButton = nullptr;
	QPushButton *recordButton = nullptr;
	QMenu *destinationsMenu = nullptr;
	QMenu *viewMenu = nullptr;
	std::vector<QAction *> editActions;

	Destination *activeDest = nullptr;
	std::vector<OBSSource> showing;
	std::vector<std::pair<Destination *, QPointer<ProjectorWindow>>> projectors;
	bool refreshingSources = false;
	bool refreshingTransitions = false;
	bool layoutRestored = false;

	/* Presse-papiers de l'édition. */
	OBSWeakSource clipboardSource;
	SceneEditor::Transform clipboardTransform;
	bool hasClipboardTransform = false;
	SceneEditor::Transform copiedTransform;
	bool hasCopiedTransform = false;
};
