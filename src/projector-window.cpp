/*
Multidestination Studio
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

#include "projector-window.hpp"
#include "blend-render.hpp"
#include "destination.hpp"
#include "render-helpers.hpp"
#include "text.hpp"
#include "qt-display.hpp"

#include <obs-module.h>

#include <QKeyEvent>
#include <QVBoxLayout>

ProjectorWindow::ProjectorWindow(Destination *destination, QScreen *screen, int index)
	: QWidget(nullptr, Qt::Window),
	  dest(destination),
	  target(screen),
	  outputIndex(index)
{
	setAttribute(Qt::WA_DeleteOnClose, true);
	QString title = QString("%1 — %2").arg(Tr("MDS.Projector")).arg(QString::fromStdString(dest->Name()));
	if (outputIndex >= 0) {
		blendRenderer = std::make_unique<BlendRenderer>();
		const BlendConfig config = dest->Blend();
		if (size_t(outputIndex) < config.outputs.size())
			title += " — " + QString::fromStdString(config.outputs[size_t(outputIndex)].name);
	}
	setWindowTitle(title);
	if (target) {
		const qreal ratio = target->devicePixelRatio();
		nativeW = uint32_t(target->geometry().width() * ratio);
		nativeH = uint32_t(target->geometry().height() * ratio);
	}

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);

	display = new OBSQTDisplay(this);
	display->SetDisplayBackgroundColor(Qt::black);
	layout->addWidget(display);

	connect(display, &OBSQTDisplay::DisplayCreated, this,
		[this](OBSQTDisplay *w) { obs_display_add_draw_callback(w->GetDisplay(), DrawCb, this); });

	/* Pas de curseur sur un écran de sortie. */
	if (target)
		setCursor(Qt::BlankCursor);
}

ProjectorWindow::~ProjectorWindow()
{
	if (display && display->GetDisplay())
		obs_display_remove_draw_callback(display->GetDisplay(), DrawCb, this);
}

void ProjectorWindow::Start()
{
	if (target) {
		setGeometry(target->geometry());
		setWindowFlag(Qt::FramelessWindowHint, true);
		showFullScreen();
	} else {
		resize(960, 540);
		show();
	}
	setFocusPolicy(Qt::StrongFocus);
	activateWindow();
	setFocus();
}

void ProjectorWindow::DrawCb(void *data, uint32_t cx, uint32_t cy)
{
	auto *window = static_cast<ProjectorWindow *>(data);
	Destination *d = window->dest;
	if (!d || !d->Attached())
		return;
	if (window->blendRenderer) {
		window->blendRenderer->Draw(d, size_t(window->outputIndex), cx, cy, window->nativeW, window->nativeH);
		return;
	}
	DrawFitted(d->Width(), d->Height(), cx, cy, [d](float) { d->RenderProgram(); });
}

void ProjectorWindow::keyPressEvent(QKeyEvent *event)
{
	if (event->key() == Qt::Key_Escape) {
		close();
		return;
	}
	QWidget::keyPressEvent(event);
}

void ProjectorWindow::mouseDoubleClickEvent(QMouseEvent *event)
{
	/* Double-clic : bascule fenêtre <-> plein écran (utile pour régler). */
	if (isFullScreen()) {
		showNormal();
		unsetCursor();
	} else if (target) {
		setGeometry(target->geometry());
		showFullScreen();
		setCursor(Qt::BlankCursor);
	}
	QWidget::mouseDoubleClickEvent(event);
}
