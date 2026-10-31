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

#include <QPointer>
#include <QScreen>
#include <QWidget>

class Destination;
class OBSQTDisplay;

/* Sortie Program d'une destination, en plein écran sur un écran donné
 * (ou en fenêtre si aucun écran). Échap ferme le projecteur. */
class ProjectorWindow : public QWidget {
	Q_OBJECT

public:
	ProjectorWindow(Destination *destination, QScreen *screen);
	~ProjectorWindow() override;

	void Start();
	QScreen *TargetScreen() const { return target; }

protected:
	void keyPressEvent(QKeyEvent *event) override;
	void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
	static void DrawCb(void *data, uint32_t cx, uint32_t cy);

	Destination *dest;
	QPointer<QScreen> target;
	OBSQTDisplay *display = nullptr;
};
