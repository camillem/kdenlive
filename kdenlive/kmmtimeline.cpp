/***************************************************************************
                          kmmtimeline.cpp  -  description
                             -------------------
    begin                : Fri Feb 15 2002
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

#include <cmath>
#include <iostream>
#include <stdlib.h>
#include <iostream>

#include <klocale.h>
#include <qscrollbar.h>
#include <qscrollview.h>
#include <qhbox.h>

#include <kdebug.h>

#include "doctrackclipiterator.h"
#include "kdenlivedoc.h"
#include "kmmrulerpanel.h"
#include "kmmtimeline.h"
#include "kmmtracksoundpanel.h"
#include "kmmtrackvideopanel.h"
#include "kmmtrackkeyframepanel.h"
#include "kmmtimelinetrackview.h"
#include "krulertimemodel.h"
#include "kscalableruler.h"
#include "kmoveclipscommand.h"
#include "kselectclipcommand.h"
#include "kaddclipcommand.h"
#include "kaddrefclipcommand.h"
#include "kresizecommand.h"
#include "clipdrag.h"

uint KMMTimeLine::snapTolerance=10;

namespace {
	uint g_scrollTimerDelay = 50;
	uint g_scrollThreshold = 50;
}

KMMTimeLine::KMMTimeLine(KdenliveApp *app, QWidget *scrollToolWidget, KdenliveDoc *document, QWidget *parent, const char *name ) :
				QVBox(parent, name),
				m_document(document),
				m_selection(),
				m_snapToGrid(document),
				m_scrollTimer(this, "scroll timer"),
				m_scrollingRight(true)
{
	m_app = app;
	m_rulerBox = new QHBox(this, "ruler box");
	m_trackScroll = new QScrollView(this, "track view", WPaintClever);
	m_scrollBox = new QHBox(this, "scroll box");

	m_rulerToolWidget = new KMMRulerPanel(m_rulerBox, "Ruler Panel");
	m_ruler = new KScalableRuler(new KRulerTimeModel(), m_rulerBox, name);
	m_ruler->addSlider(KRuler::TopMark, 0);
	m_ruler->setAutoClickSlider(0);

	m_scrollToolWidget = scrollToolWidget;
	if(!m_scrollToolWidget) m_scrollToolWidget = new QLabel(i18n("Scroll"), 0, "Scroll");
	m_scrollToolWidget->reparent(m_scrollBox, QPoint(0,0));
	m_scrollBar = new QScrollBar(-100, 5000, 50, 500, 0, QScrollBar::Horizontal, m_scrollBox, "horizontal ScrollBar");

	m_trackViewArea = new KMMTimeLineTrackView(*this, m_app, m_trackScroll, "track view area");

	m_trackScroll->enableClipper(TRUE);
	m_trackScroll->setVScrollBarMode(QScrollView::AlwaysOn);
	m_trackScroll->setHScrollBarMode(QScrollView::AlwaysOff);
	m_trackScroll->setDragAutoScroll(true);

	m_rulerToolWidget->setMinimumWidth(200);
	m_rulerToolWidget->setMaximumWidth(200);

	m_scrollToolWidget->setMinimumWidth(200);
	m_scrollToolWidget->setMaximumWidth(200);

	m_ruler->setValueScale(1.0);
	calculateProjectSize();

	connect(m_scrollBar, SIGNAL(valueChanged(int)), m_ruler, SLOT(setStartPixel(int)));
	connect(m_scrollBar, SIGNAL(valueChanged(int)), m_ruler, SLOT(repaint()));
	connect(m_document, SIGNAL(trackListChanged()), this, SLOT(syncWithDocument()));
	connect(m_document, SIGNAL(documentChanged()), this, SLOT(calculateProjectSize()));
	connect(m_ruler, SIGNAL(scaleChanged(double)), this, SLOT(calculateProjectSize()));
	connect(m_ruler, SIGNAL(sliderValueChanged(int, int)), m_trackViewArea, SLOT(invalidateBackBuffer()));
	connect(m_ruler, SIGNAL(sliderValueChanged(int, int)), m_ruler, SLOT(repaint()));
	connect(m_ruler, SIGNAL(sliderValueChanged(int, int)), this, SLOT(slotSliderMoved(int, int)));
	connect(m_document, SIGNAL(clipChanged(DocClipRef* )), this, SLOT(invalidateClipBuffer(DocClipRef *)));

	connect(m_ruler, SIGNAL(requestScrollLeft()), this, SLOT(slotScrollLeft()));
	connect(m_ruler, SIGNAL(requestScrollRight()), this, SLOT(slotScrollRight()));
	connect(&m_scrollTimer, SIGNAL(timeout()), this, SLOT(slotTimerScroll()));

	connect(m_rulerToolWidget, SIGNAL(timeScaleChanged(double)), this, SLOT(setTimeScale(double)));

	setAcceptDrops(true);

	syncWithDocument();

	m_startedClipMove = false;
	m_masterClip = 0;
	m_moveClipsCommand = 0;
	m_deleteClipsCommand = 0;
	m_addingClips = false;

	m_trackList.setAutoDelete(true);
}

KMMTimeLine::~KMMTimeLine()
{
}

void KMMTimeLine::appendTrack(KMMTrackPanel *track)
{
	insertTrack(m_trackList.count(), track);
}

/** Inserts a track at the position specified by index */
void KMMTimeLine::insertTrack(int index, KMMTrackPanel *track)
{
	track->reparent(m_trackScroll->viewport(), 0, QPoint(0, 0), TRUE);
	m_trackScroll->addChild(track);

	m_trackList.insert(index, track);

	connect(m_scrollBar, SIGNAL(valueChanged(int)), this, SLOT(drawTrackViewBackBuffer()));
	connect(track->docTrack(), SIGNAL(clipLayoutChanged()), this, SLOT(drawTrackViewBackBuffer()));
	connect(track->docTrack(), SIGNAL(clipSelectionChanged()), this, SLOT(drawTrackViewBackBuffer()));

	connect(track, SIGNAL(signalClipCropStartChanged(DocClipRef *)), this, SIGNAL(signalClipCropStartChanged(DocClipRef *)));
	connect(track, SIGNAL(signalClipCropEndChanged(DocClipRef *)), this, SIGNAL(signalClipCropEndChanged(DocClipRef *)));

	connect(track, SIGNAL(lookingAtClip(DocClipRef *, const GenTime &)), this, SIGNAL(lookingAtClip(DocClipRef *, const GenTime &)));

	resizeTracks();
}

void KMMTimeLine::resizeEvent(QResizeEvent *event)
{
	resizeTracks();
}

void KMMTimeLine::resizeTracks()
{
	int height = 0;
	int widgetHeight;

	QWidget *panel = m_trackList.first();

	while(panel != 0) {
	  widgetHeight = panel->height();

		m_trackScroll->moveChild(panel, 0, height);
		panel->resize(200, widgetHeight);

		height+=widgetHeight;

		panel = m_trackList.next();
	}

	m_trackScroll->moveChild(m_trackViewArea, 200, 0);
	m_trackViewArea->resize(m_trackScroll->visibleWidth()-200 , height);

  int viewWidth = m_trackScroll->visibleWidth()-200;
  if(viewWidth<1) viewWidth=1;

	QPixmap pixmap(viewWidth , height);

	m_trackScroll->resizeContents(m_trackScroll->visibleWidth(), height);
}

/** At least one track within the project have been added or removed.
	*
	* The timeline needs to be updated to show these changes. */
void KMMTimeLine::syncWithDocument()
{
	unsigned int index = 0;

	m_trackList.clear();

	DocTrackBase *track = m_document->firstTrack();
	while(track != 0) {
		if(track->clipType() == "Video") {
			insertTrack(index, new KMMTrackVideoPanel(this, m_document, ((DocTrackVideo *)track)));
			++index;
			insertTrack(index, new KMMTrackKeyFramePanel(this, m_document, track));
			++index;
		} else if(track->clipType() == "Sound") {
			insertTrack(index, new KMMTrackSoundPanel(this, m_document, ((DocTrackSound *)track)));
			++index;
			insertTrack(index, new KMMTrackKeyFramePanel(this, m_document, track));
			++index;
		} else {
			kdWarning() << "Sync failed" << endl;
		}
		track = m_document->nextTrack();
	}

	resizeTracks();
	calculateProjectSize();
}

/** No descriptions */
void KMMTimeLine::polish()
{
	resizeTracks();
}


void KMMTimeLine::dragEnterEvent ( QDragEnterEvent *event )
{
	if(m_startedClipMove) {
		m_document->activeSceneListGeneration(false);
		event->accept(true);
	} else 	if(ClipDrag::canDecode(event)) {
		m_document->activeSceneListGeneration(false);
		m_selection = ClipDrag::decode(m_document->clipManager(), event);

		if(!m_selection.isEmpty()) {
    		if(m_selection.masterClip()==0) m_selection.setMasterClip(m_selection.first());

			m_masterClip = m_selection.masterClip();
			m_clipOffset = GenTime();

			if(m_selection.isEmpty()) {
				event->accept(false);
			} else {
				setupSnapToGrid();
				event->accept(true);
			}
		} else {
			kdError() << "ERROR! ERROR! ERROR! ClipDrag:decode decoded a null clip!!!" << endl;
		}
	} else {
		event->accept(false);
	}

	m_startedClipMove = false;
}

void KMMTimeLine::dragMoveEvent ( QDragMoveEvent *event )
{
	QPoint pos = m_trackViewArea->mapFrom(this, event->pos());

	GenTime mouseTime = timeUnderMouse((double)pos.x()) - m_clipOffset;
	mouseTime = m_snapToGrid.getSnappedTime(mouseTime);
	mouseTime = mouseTime + m_clipOffset;

  	int trackUnder = trackUnderPoint(pos);

	if(m_selection.isEmpty()) {
		moveSelectedClips(trackUnder, mouseTime - m_clipOffset);
	} else {
		if(canAddClipsToTracks(m_selection, trackUnder, mouseTime)) {
			addClipsToTracks(m_selection, trackUnder, mouseTime , true);
			setupSnapToGrid();
			m_selection.clear();
		}
	}
	calculateProjectSize();
	m_trackViewArea->repaint();

	if(pos.x() < g_scrollThreshold) {
		if(!m_scrollTimer.isActive()) {
			m_scrollTimer.start(g_scrollTimerDelay, false);
		}
		m_scrollingRight = false;
	} else if(m_trackViewArea->width() - pos.x() < g_scrollThreshold) {
		if(!m_scrollTimer.isActive()) {
			m_scrollTimer.start(g_scrollTimerDelay, false);
		}
		m_scrollingRight = true;
	} else {
		m_scrollTimer.stop();
	}
}

void KMMTimeLine::dragLeaveEvent ( QDragLeaveEvent *event )
{
	if(!m_selection.isEmpty()) {
		m_selection.setAutoDelete(true);
		m_selection.clear();
		m_selection.setAutoDelete(false);
	}

	if(m_addingClips) {
		m_addingClips = false;

	  	QPtrListIterator<KMMTrackPanel> itt(m_trackList);
		while(itt.current() != 0) {
	  		itt.current()->docTrack()->deleteClips(true);
	  		++itt;
	  	}

		m_document->activeSceneListGeneration(true);
	}

	if(m_moveClipsCommand) {
		// In a drag Leave Event, any clips in the selection are removed from the timeline.
		delete m_moveClipsCommand;
		m_moveClipsCommand = 0;
		m_document->activeSceneListGeneration(true);
	}

	if(m_deleteClipsCommand) {
		m_app->addCommand(m_deleteClipsCommand, false);
	   	m_deleteClipsCommand = 0;

	  	QPtrListIterator<KMMTrackPanel> itt(m_trackList);
		while(itt.current() != 0) {
	  		itt.current()->docTrack()->deleteClips(true);
	  		++itt;
	  	}
	}

	calculateProjectSize();
	drawTrackViewBackBuffer();

	m_scrollTimer.stop();
}

void KMMTimeLine::dropEvent ( QDropEvent *event )
{
	if(!m_selection.isEmpty()) {
		m_selection.setAutoDelete(true);
		m_selection.clear();
		m_selection.setAutoDelete(false);
	}

	if(m_addingClips) {
		m_app->addCommand(createAddClipsCommand(true), false);
		m_addingClips = false;
		m_document->activeSceneListGeneration(true);
	}

	if(m_deleteClipsCommand) {
		delete m_deleteClipsCommand;
		m_deleteClipsCommand = 0;
	}

	if(m_moveClipsCommand) {
		m_moveClipsCommand->setEndLocation(m_masterClip);
		m_app->addCommand(m_moveClipsCommand, false);
		m_moveClipsCommand = 0;	// KdenliveApp is now managing this command, we do not need to delete it.
		m_document->activeSceneListGeneration(true);
	}

	m_scrollTimer.stop();
}

/** This method maps a local coordinate value to the corresponding
value that should be represented at that position. By using this, there is no need to calculate scale factors yourself. Takes the x
coordinate, and returns the value associated with it. */
double KMMTimeLine::mapLocalToValue(const double coordinate) const
{
	return m_ruler->mapLocalToValue(coordinate);
}

/** Takes the value that we wish to find the coordinate for, and returns the x coordinate. In cases where a single value covers multiple
pixels, the left-most pixel is returned. */
double KMMTimeLine::mapValueToLocal(const double value) const
{
	return m_ruler->mapValueToLocal(value);
}

KCommand *KMMTimeLine::selectNone()
{
	QPtrListIterator<KMMTrackPanel> itt(m_trackList);

	KMacroCommand *command = new KMacroCommand(i18n("Selection"));

	while(itt.current()!=0) {
		QPtrListIterator<DocClipRef> clipItt(itt.current()->docTrack()->firstClip(true));
		while(clipItt.current()!=0) {
			Command::KSelectClipCommand *clipComm = new Command::KSelectClipCommand(m_document, clipItt.current(), false);
			command->addCommand(clipComm);
			++clipItt;
		}
		++itt;
	}

	return command;
}

void KMMTimeLine::drawTrackViewBackBuffer()
{
	m_trackViewArea->invalidateBackBuffer();
}

/** Returns m_trackList

Warning - this method is a bit of a hack, not good OO practice, and should be removed at some point. */
QPtrList<KMMTrackPanel> &KMMTimeLine::trackList()
{
	return m_trackList;
}

bool KMMTimeLine::moveSelectedClips(int newTrack, GenTime start)
{
	int trackOffset = m_document->trackIndex(m_document->findTrack(m_masterClip));
	GenTime startOffset;

	if( (!m_masterClip) || (trackOffset==-1)) {
		kdError() << "Trying to move selected clips, master clip is not set." << endl;
		return false;
	} else {
		startOffset = m_masterClip->trackStart();
	}

	trackOffset = newTrack - trackOffset;
	startOffset = start - startOffset;

	m_document->moveSelectedClips(startOffset, trackOffset);

	drawTrackViewBackBuffer();
	return true;
}

void KMMTimeLine::scrollViewLeft()
{
	m_scrollBar->subtractLine();
}

void KMMTimeLine::scrollViewRight()
{
	m_scrollBar->addLine();
}

void KMMTimeLine::toggleSelectClipAt(DocTrackBase &track, GenTime value)
{
	DocClipRef *clip = track.getClipAt(value);
	if(clip) {
		Command::KSelectClipCommand *command = new Command::KSelectClipCommand(m_document, clip, !track.clipSelected(clip));
		m_app->addCommand(command, true);
	}
}

void KMMTimeLine::selectClipAt(DocTrackBase &track, GenTime value)
{
	DocClipRef *clip = track.getClipAt(value);
	if(clip) {
		Command::KSelectClipCommand *command = new Command::KSelectClipCommand(m_document, clip, true);
		m_app->addCommand(command, true);
	}
}

void KMMTimeLine::addClipsToTracks(DocClipRefList &clips, int track, GenTime value, bool selected)
{
	if(clips.isEmpty()) return;

	if(selected) {
		addCommand(selectNone(), true);
	}

	DocClipRef *masterClip = clips.masterClip();
	if(!masterClip) masterClip = clips.first();

	GenTime startOffset = value - masterClip->trackStart();

	int trackOffset = masterClip->trackNum();
	if(trackOffset == -1) trackOffset = 0;
  trackOffset = track - trackOffset;

	QPtrListIterator<DocClipRef> itt(clips);
	int moveToTrack;

	while(itt.current() != 0) {
		moveToTrack = itt.current()->trackNum();

		if(moveToTrack==-1) {
			moveToTrack = track;
		} else {
			moveToTrack += trackOffset;
		}

		itt.current()->moveTrackStart(itt.current()->trackStart() + startOffset);

		if((moveToTrack >=0) && (moveToTrack < m_document->numTracks())) {
	    m_document->track(moveToTrack)->addClip(itt.current(), selected);
	  }

		++itt;
	}

	m_addingClips = true;
}

/** Returns the integer value of the track underneath the mouse cursor.
The track number is that in the track list of the document, which is
sorted incrementally from top to bottom. i.e. track 0 is topmost, track 1 is next down, etc. */
int KMMTimeLine::trackUnderPoint(const QPoint &pos)
{
  uint y = pos.y();
	KMMTrackPanel *panel = m_trackViewArea->panelAt(y);
  if(panel) {
    DocTrackBase *docTrack = panel->docTrack();
    if(docTrack) {
      return m_document->trackIndex(docTrack);
    }
  }

	return -1;
}

void KMMTimeLine::initiateDrag(DocClipRef *clipUnderMouse, GenTime mouseTime)
{
	m_masterClip = clipUnderMouse;
	m_clipOffset = mouseTime - clipUnderMouse->trackStart();

	m_moveClipsCommand = new Command::KMoveClipsCommand(this, m_document, m_masterClip);
	m_deleteClipsCommand = createAddClipsCommand(false);
	setupSnapToGrid();

	m_startedClipMove = true;

	DocClipRefList selection = listSelected();

	selection.setMasterClip(m_masterClip);
	ClipDrag *clip = new ClipDrag(selection, this, "Timeline Drag");

	clip->dragCopy();
}

/** Sets a new time scale for the timeline. This in turn calls the correct kruler funtion and updates
the display. The scale is the size of one frame.*/
void KMMTimeLine::setTimeScale(double scale)
{
	int localValue = (int)mapValueToLocal(m_ruler->getSliderValue(0));

	m_ruler->setValueScale(scale);

	m_scrollBar->setValue((int)(scale*m_ruler->getSliderValue(0)) - localValue);

	drawTrackViewBackBuffer();
}

/** Returns true if the specified clip exists and is selected, false otherwise. If a track is
specified, we look at that track first, but fall back to a full search of tracks if the clip is
 not there. */
bool KMMTimeLine::clipSelected(DocClipRef *clip, DocTrackBase *track)
{
	if(track) {
		if(track->clipExists(clip)) {
			return track->clipSelected(clip);
		}
	}

	QPtrListIterator<KMMTrackPanel> itt(m_trackList);
	while(itt.current()) {
		if(itt.current()->docTrack()->clipExists(clip)) {
			return itt.current()->docTrack()->clipSelected(clip);
		}

		++itt;
	}

	return false;
}

bool KMMTimeLine::canAddClipsToTracks(DocClipRefList &clips, int track, GenTime clipOffset)
{
	QPtrListIterator<DocClipRef> itt(clips);
	int numTracks = m_trackList.count();
	int trackOffset;
	GenTime startOffset;

	if(clips.masterClip()) {
		trackOffset = clips.masterClip()->trackNum();
		startOffset = clipOffset - clips.masterClip()->trackStart();
	} else {
		trackOffset = clips.first()->trackNum();
		startOffset = clipOffset - clips.first()->trackStart();
	}

	if(trackOffset==-1) trackOffset = 0;
	trackOffset = track - trackOffset;

	while(itt.current()) {
		itt.current()->moveTrackStart(itt.current()->trackStart() + startOffset);
		++itt;
	}
	itt.toFirst();

	while(itt.current()) {
    if(!itt.current()->durationKnown()) {
        kdWarning() << "Clip Duration not known, cannot add clips" << endl;
        return false;
    }
		int curTrack = itt.current()->trackNum();
		if(curTrack==-1) curTrack = 0;
		curTrack += trackOffset;

		if((curTrack < 0) || (curTrack >= m_document->numTracks())) {
			return false;
		}

		if(!m_document->track(curTrack)->canAddClip(itt.current())) {
			return false;
		}

		++itt;
	}

	return true;
}

/** Constructs a list of all clips that are currently selected. It does nothing else i.e.
it does not remove the clips from the timeline. */
DocClipRefList KMMTimeLine::listSelected()
{
	DocClipRefList list;

 	QPtrListIterator<KMMTrackPanel> itt(m_trackList);

	while(itt.current()) {
		QPtrListIterator<DocClipRef> clipItt(itt.current()->docTrack()->firstClip(true));

		while(clipItt.current()) {
			list.inSort(clipItt.current());
			++clipItt;
		}
		++itt;
	}

	return list;
}

/** Calculates the size of the project, and sets up the timeline to accomodate it. */
void KMMTimeLine::calculateProjectSize()
{
	double rulerScale = m_ruler->valueScale();
	GenTime length = m_document->projectDuration();

	int frames = length.frames(m_document->framesPerSecond());

	m_scrollBar->setRange(0, (frames * rulerScale) + m_scrollBar->width());
	m_ruler->setRange(0, frames);

	emit projectLengthChanged(frames);
}

GenTime KMMTimeLine::seekPosition()
{
	return GenTime(m_ruler->getSliderValue(0), m_document->framesPerSecond());
}

KMacroCommand *KMMTimeLine::createAddClipsCommand(bool addingClips)
{
	KMacroCommand *macroCommand = new KMacroCommand( addingClips ? i18n("Add Clips") : i18n("Delete Clips") );

	for(int count=0; count<m_document->numTracks(); count++) {
		DocTrackBase *track = m_document->track(count);

		QPtrListIterator<DocClipRef> itt = track->firstClip(true);

		while(itt.current()) {
			Command::KAddRefClipCommand *command = new Command::KAddRefClipCommand(m_document->clipManager(), &m_document->projectClip(), itt.current(), addingClips);
			macroCommand->addCommand(command);
			++itt;
		}
	}

	return macroCommand;
}

void KMMTimeLine::addCommand(KCommand *command, bool execute)
{
	m_app->addCommand(command, execute);
}

KdenliveApp::TimelineEditMode KMMTimeLine::editMode()
{
	return m_app->timelineEditMode();
}

KCommand *KMMTimeLine::razorAllClipsAt(GenTime time)
{
	return 0;
}

KCommand *KMMTimeLine::razorClipAt(DocTrackBase &track, GenTime &time)
{
	KMacroCommand *command = 0;

	DocClipRef *clip = track.getClipAt(time);
	if(clip) {
		// disallow the creation of clips with 0 length.
		if((clip->trackStart() == time) || (clip->trackEnd() == time)) return 0;

		command = new KMacroCommand(i18n("Razor clip"));

		command->addCommand(selectNone());

		DocClipRef *clone = clip->clone(m_document->clipManager());

		if(clone) {
			clone->setTrackStart(time);
			clone->setCropStartTime(clip->cropStartTime() + (time - clip->trackStart()));

			command->addCommand(resizeClip(*clip, true, time));
			command->addCommand(new Command::KAddRefClipCommand(m_document->clipManager(), &m_document->projectClip(), clone, true));

			delete clone;
		} else {
			kdError() << "razorClipAt - could not clone clip!!!" << endl;
		}
	}

	return command;
}

KCommand * KMMTimeLine::resizeClip(DocClipRef &clip, bool resizeEnd, GenTime &time)
{
	Command::KResizeCommand *command = new Command::KResizeCommand(m_document, clip);

	if(resizeEnd) {
		command->setEndTrackEnd(time);
	} else {
		command->setEndTrackStart(time);
	}

	return command;
}

/** Selects all of the clips on the timeline which occur later than the
specified time. If include is true, then clips which overlap the
specified time will be selected, otherwise only those clips which
are later on the tiemline (i.e. trackStart() > time) will be selected. */
KCommand * KMMTimeLine::selectLaterClips(GenTime time, bool include)
{
	KMacroCommand *command = new KMacroCommand(i18n("Selection"));

	QPtrListIterator<KMMTrackPanel> itt(m_trackList);

	bool select;

	while(itt.current()!=0) {
		DocTrackClipIterator clipItt(*(itt.current()->docTrack()));
		while(clipItt.current()!=0) {
			if(include) {
				select = clipItt.current()->trackEnd() > time;
			} else {
				select = clipItt.current()->trackStart() > time;
			}
			Command::KSelectClipCommand *clipComm = new Command::KSelectClipCommand(m_document, clipItt.current(), select);
			command->addCommand(clipComm);
			++clipItt;
		}
		++itt;
	}

	return command;
}

/** A ruler slider has moved - do something! */
void KMMTimeLine::slotSliderMoved(int slider, int value)
{
  if(slider == 0) {
    emit seekPositionChanged(GenTime(value, m_document->framesPerSecond()));
  }
}

/** Seek the timeline to the current position. */
void KMMTimeLine::seek(const GenTime &time)
{
    m_ruler->setSliderValue(0, (int)floor(time.frames(m_document->framesPerSecond()) + 0.5));
}

/** Returns the correct "time under mouse", taking into account whether or not snap to frame is on or off, and other relevant effects. */
GenTime KMMTimeLine::timeUnderMouse(double posX)
{
  GenTime value(m_ruler->mapLocalToValue(posX), m_document->framesPerSecond());

  if(m_app->snapToFrameEnabled()) value = GenTime(floor(value.frames(m_document->framesPerSecond()) + 0.5), m_document->framesPerSecond());

  return value;
}

const SnapToGrid &KMMTimeLine::snapToGrid() const
{
	return m_snapToGrid;
}

void KMMTimeLine::setupSnapToGrid()
{
	m_snapToGrid.setSnapToClipEnd(snapToBorders());
	m_snapToGrid.setSnapToClipStart(snapToBorders());
	m_snapToGrid.setSnapToFrame(snapToFrame());
	m_snapToGrid.setSnapToSeekTime(snapToSeekTime());
	m_snapToGrid.setSnapToMarkers(snapToMarkers());
	m_snapToGrid.setIncludeSelectedClips(false);
	m_snapToGrid.clearSeekTimes();
	m_snapToGrid.addSeekTime(GenTime(0.0));
	m_snapToGrid.addSeekTime(seekPosition());
	m_snapToGrid.setCursorTimes(selectedClipTimes());
	m_snapToGrid.setSnapTolerance(GenTime(mapLocalToValue(snapTolerance) - mapLocalToValue(0), m_document->framesPerSecond()));
}

QValueList<GenTime> KMMTimeLine::selectedClipTimes()
{
	QValueList<GenTime> resultList;

	for(uint count=0; count<m_document->numTracks(); ++count) {
		QPtrListIterator<DocClipRef> clipItt(m_document->track(count)->firstClip(true));
		while(clipItt.current()!=0) {
			if(m_masterClip == clipItt.current()) {
				resultList.prepend(clipItt.current()->trackEnd());
				resultList.prepend(clipItt.current()->trackStart());
			} else {
				resultList.append(clipItt.current()->trackStart());
				resultList.append(clipItt.current()->trackEnd());
			}
			if(snapToMarkers()) {
				QValueVector<GenTime> markers = clipItt.current()->snapMarkersOnTrack();
				for(QValueVector<GenTime>::iterator itt=markers.begin(); itt != markers.end(); ++itt) {
					resultList.append(*itt);
				}
			}
			++clipItt;
		}
	}

	return resultList;
}

bool KMMTimeLine::snapToBorders() const
{
	return m_app->snapToBorderEnabled();
}

bool KMMTimeLine::snapToFrame() const
{
	return m_app->snapToFrameEnabled();
}

bool KMMTimeLine::snapToMarkers() const
{
	return m_app->snapToMarkersEnabled();
}

bool KMMTimeLine::snapToSeekTime() const
{
	#warning snapToSeekTime always returns true - needs to be wired up in KdenliveApp to
	#warning a check box.
	return true;
}

GenTime KMMTimeLine::projectLength() const
{
	return GenTime(m_ruler->maxValue(), m_document->framesPerSecond());
}

void KMMTimeLine::slotTimerScroll()
{
	if(m_scrollingRight) {
		m_scrollBar->addLine();
	} else {
		m_scrollBar->subtractLine();
	}
}

void KMMTimeLine::slotScrollLeft()
{
	m_scrollBar->subtractLine();
}

void KMMTimeLine::slotScrollRight()
{
	m_scrollBar->addLine();
}

void KMMTimeLine::invalidateClipBuffer(DocClipRef *clip)
{
	#warning - unoptimised, should only update that part of the back buffer that needs to be updated. Current implementaion
	#warning - wipes the entire buffer.
	m_trackViewArea->invalidateBackBuffer();
}

void KMMTimeLine::fitToWidth()
{
	double duration = projectLength().frames(m_document->framesPerSecond());
	if(duration < 1.0) duration = 1.0;

	double scale = (double)m_ruler->width() / duration;
	m_ruler->setValueScale(scale);
	m_rulerToolWidget->setScale(scale);

	m_trackViewArea->invalidateBackBuffer();
}
