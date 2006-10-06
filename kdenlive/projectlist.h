/***************************************************************************
                          projectlist.h  -  description
                             -------------------
    begin                : Sat Feb 16 2002
    copyright            : (C) 2002 by Jason Wood
    email                : jasonwood@blueyonder.co.uk
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef PROJECTLIST_H
#define PROJECTLIST_H

#include <qvaluelist.h>
#include <qpopupmenu.h>
#include <qevent.h>
#include <qheader.h>
#include <qtooltip.h>

#include <kurl.h>
#include <klistviewsearchline.h>

#include <projectlistview.h>
#include "projectview_ui.h"

class DocClipRef;
class KdenliveDoc;
class DocumentBaseNode;

namespace Gui {

    class KdenliveApp;

/**
  * ProjectList is the dialog which contains the project list.
  *@author Jason Wood
  */
//adds tooltips to project list column headers
    class columnToolTip:public QToolTip {
      public:
	columnToolTip(QHeader * header, QToolTipGroup * group = 0);
	virtual ~ columnToolTip();
      private:
	void maybeTip(const QPoint & p);
    };

    class ProjectList:public ProjectList_UI {
      Q_OBJECT public:
	ProjectList(KdenliveApp * app, KdenliveDoc * document,
	    QWidget * parent = 0, const char *name = 0);
	~ProjectList();
	/** Returns the currently selected clip in the project list. */
	DocClipRef *currentSelection();
        
      private:			// Private methods
	/** Holds the document that this projectlist makes use of. */
	 KdenliveDoc * m_document;
	/** Holds a pointer to the application. FIXME: Is this necessary? */
	KdenliveApp *m_app;
	QPopupMenu *contextcreateMenu();
        columnToolTip * colToolTip;
	KListViewSearchLineWidget *lv_search;


      public slots:		// Public slots
	/** No descriptions */
	void rightButtonPressed(QListViewItem * listViewItem,
	    const QPoint & pos, int column);
	/** Get a fresh copy of files and clips from KdenliveDoc and display them. */
	void slot_UpdateList();
	/** The clip specified has changed - update the display. */
	void slot_clipChanged(DocClipRef *);
        void slot_clipChanged();
	void refresh();
	/** The node specified has been deleted - update the display. */
	void slot_nodeDeleted(DocumentBaseNode * node);
	/** updates the list when an item changed */
	void updateListItem();
        void selectClip(DocClipBase *clip);
        
      signals:		// Signals
	/** this signal is called when a number of clips have been dropped onto the project list view. */
	void dragDropOccured(QDropEvent * drop, QListViewItem * parent);
	/** This signal is emitted when an AVFile is selected in the project list. */
	void clipSelected(DocClipRef * file);
        void editItem();
        
	private slots:		// Private slots
	/** Called when the project list changes. */
	    //void projectListSelectionChanged(QListViewItem *item);
         /** an item was double clicked */
         void editRequested( QListViewItem *, const QPoint &, int col);
    };

}				// namespace Gui
#endif
