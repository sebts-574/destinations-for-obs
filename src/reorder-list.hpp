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

#pragma once

#include <QKeyEvent>
#include <QListWidget>
#include <QMouseEvent>

#include <functional>

/* Liste des scènes / sources :
 * - cliquer-glisser depuis un élément non sélectionné sélectionne plusieurs
 *   éléments ;
 * - cliquer-glisser depuis un élément déjà sélectionné les déplace (ordre) ;
 * - Suppr / Retour arrière appelle onDelete (avec confirmation). */
class ReorderList : public QListWidget {
public:
	explicit ReorderList(QWidget *parent) : QListWidget(parent)
	{
		setSelectionMode(QAbstractItemView::ExtendedSelection);
		setDragDropMode(QAbstractItemView::InternalMove);
		setDefaultDropAction(Qt::MoveAction);
	}

	std::function<void()> onDelete;

protected:
	void mousePressEvent(QMouseEvent *event) override
	{
		QListWidgetItem *item = itemAt(event->position().toPoint());
		const bool modifiers = event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier);
		setDragEnabled(item && item->isSelected() && !modifiers);
		QListWidget::mousePressEvent(event);
	}

	void mouseReleaseEvent(QMouseEvent *event) override
	{
		QListWidget::mouseReleaseEvent(event);
		setDragEnabled(true);
	}

	void keyPressEvent(QKeyEvent *event) override
	{
		if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && onDelete) {
			onDelete();
			event->accept();
			return;
		}
		QListWidget::keyPressEvent(event);
	}
};
