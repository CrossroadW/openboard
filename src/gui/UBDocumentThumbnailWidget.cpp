/*
 * Copyright (C) 2015-2022 Département de l'Instruction Publique (DIP-SEM)
 *
 * Copyright (C) 2013 Open Education Foundation
 *
 * Copyright (C) 2010-2013 Groupement d'Intérêt Public pour
 * l'Education Numérique en Afrique (GIP ENA)
 *
 * This file is part of OpenBoard.
 *
 * OpenBoard is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License,
 * with a specific linking exception for the OpenSSL project's
 * "OpenSSL" library (or with modified versions of it that use the
 * same license as the "OpenSSL" library).
 *
 * OpenBoard is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with OpenBoard. If not, see <http://www.gnu.org/licenses/>.
 */




#include "UBDocumentThumbnailWidget.h"

#include "core/UBApplication.h"
#include "core/UBMimeData.h"
#include "core/UBSettings.h"

#include "board/UBBoardController.h"

#include "document/UBDocumentController.h"

#include "core/memcheck.h"


UBDocumentThumbnailWidget::UBDocumentThumbnailWidget(QWidget* parent)
    : UBDocumentThumbnailsView(parent)
    , mDropCaretRectItem(0)
    , mClosestDropItem(0)
    , mDragEnabled(true)
    , mScrollMagnitude(0)
{
    bCanDrag = false;
    mScrollTimer = new QTimer(this);
    connect(mScrollTimer, SIGNAL(timeout()), this, SLOT(autoScroll()));
}


UBDocumentThumbnailWidget::~UBDocumentThumbnailWidget()
{
    // NOOP
}

void UBDocumentThumbnailWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (!dragEnabled())
    {
        event->ignore();
        return;
    }

    if (!(event->buttons() & Qt::LeftButton))
        return;

    if ((event->pos() - mMousePressPos).manhattanLength() < QApplication::startDragDistance())
        return;

    QList<QGraphicsItem*> graphicsItems = items(mMousePressPos);

    UBSceneThumbnailPixmap* sceneItem = 0;

    while (!graphicsItems.isEmpty() && !sceneItem)
        sceneItem = dynamic_cast<UBSceneThumbnailPixmap*>(graphicsItems.takeFirst());

    if (sceneItem)
    {
        QDrag *drag = new QDrag(this);
        QList<UBMimeDataItem> mimeDataItems;
        foreach (QGraphicsItem *item, selectedItems())
            mimeDataItems.append(UBMimeDataItem(sceneItem->documentProxy(), mGraphicItems.indexOf(item)));

        UBMimeData *mime = new UBMimeData(mimeDataItems);
        drag->setMimeData(mime);

        drag->setPixmap(sceneItem->pixmap().scaledToWidth(100));
        drag->setHotSpot(QPoint(drag->pixmap().width()/2, drag->pixmap().height() / 2));

        drag->exec(Qt::MoveAction);
    }

    UBDocumentThumbnailsView::mouseMoveEvent(event);
}

void UBDocumentThumbnailWidget::dragEnterEvent(QDragEnterEvent *event)
{
    if (!event->mimeData()->hasFormat(UBApplication::mimeTypeUniboardPage))
    {
        event->setDropAction(Qt::IgnoreAction);
        event->ignore();
        return;
    }

    UBDocumentThumbnailsView::dragEnterEvent(event);
}

void UBDocumentThumbnailWidget::dragLeaveEvent(QDragLeaveEvent *event)
{
    Q_UNUSED(event);
    if (mScrollTimer->isActive())
    {
        mScrollMagnitude = 0;
        mScrollTimer->stop();
    }
    deleteDropCaret();
    UBDocumentThumbnailsView::dragLeaveEvent(event);
}

void UBDocumentThumbnailWidget::autoScroll()
{
    this->verticalScrollBar()->setValue(this->verticalScrollBar()->value() + mScrollMagnitude);
}

void UBDocumentThumbnailWidget::dragMoveEvent(QDragMoveEvent *event)
{
    QRect boundingFrame = frameRect();
    //setting up automatic scrolling
    const int SCROLL_DISTANCE = 16;
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    QPoint eventPos = event->position().toPoint();
#else
    QPoint eventPos = event->pos();
#endif
    int bottomDist = boundingFrame.bottom() - eventPos.y(), topDist = boundingFrame.top() - eventPos.y();
    if(qAbs(bottomDist) <= SCROLL_DISTANCE)
    {
        mScrollMagnitude = (SCROLL_DISTANCE - bottomDist)*4;
        if(verticalScrollBar()->isVisible() && !mScrollTimer->isActive()) mScrollTimer->start(100);
    }
    else if(qAbs(topDist) <= SCROLL_DISTANCE)
    {
        mScrollMagnitude = (- SCROLL_DISTANCE - topDist)*4;
        if(verticalScrollBar()->isVisible() && !mScrollTimer->isActive()) mScrollTimer->start(100);
    }
    else
    {
        mScrollMagnitude = 0;
        mScrollTimer->stop();
    }

    QList<UBThumbnailPixmap*> pixmapItems;
    foreach (QGraphicsItem *item, scene()->items(mapToScene(boundingFrame)))
    {
        UBThumbnailPixmap* sceneItem = dynamic_cast<UBThumbnailPixmap*>(item);
        if (sceneItem)
            pixmapItems.append(sceneItem);
    }

    int minDistance = 0;
    QGraphicsItem *underlyingItem = itemAt(eventPos);
    mClosestDropItem = dynamic_cast<UBThumbnailPixmap*>(underlyingItem);

    if (!mClosestDropItem)
    {
        foreach (UBThumbnailPixmap *item, pixmapItems)
        {
            qreal scale = item->transform().m11();
            QPointF itemCenter(
                        item->pos().x() + item->boundingRect().width() * scale / 2,
                        item->pos().y() + item->boundingRect().height() * scale / 2);

            int distance = (itemCenter.toPoint() - mapToScene(eventPos).toPoint()).manhattanLength();
            if (!mClosestDropItem || distance < minDistance)
            {
                mClosestDropItem = item;
                minDistance = distance;
            }
        }
    }

    if (mClosestDropItem)
    {
        qreal scale = mClosestDropItem->transform().m11();

        QPointF itemCenter(
                    mClosestDropItem->pos().x() + mClosestDropItem->boundingRect().width() * scale / 2,
                    mClosestDropItem->pos().y() + mClosestDropItem->boundingRect().height() * scale / 2);

        mDropIsRight = mapToScene(eventPos).x() > itemCenter.x();

        if (!mDropCaretRectItem && selectedItems().count() < mGraphicItems.count())
        {
            mDropCaretRectItem = new QGraphicsRectItem(0);
            scene()->addItem(mDropCaretRectItem);
            mDropCaretRectItem->setPen(QPen(Qt::darkGray));
            mDropCaretRectItem->setBrush(QBrush(Qt::lightGray));
        }

        QRectF dropCaretRect(
                    mDropIsRight ? mClosestDropItem->pos().x() + mClosestDropItem->boundingRect().width() * scale + spacing() / 2 - 1 : mClosestDropItem->pos().x() - spacing() / 2 - 1,
                    mClosestDropItem->pos().y(),
                    3,
                    mClosestDropItem->boundingRect().height() * scale);

        if (mDropCaretRectItem)
            mDropCaretRectItem->setRect(dropCaretRect);
    }

    event->acceptProposedAction();
}


void UBDocumentThumbnailWidget::dropEvent(QDropEvent *event)
{
    if (mScrollTimer->isActive())
    {
        mScrollMagnitude = 0;
        mScrollTimer->stop();
    }
    deleteDropCaret();

    if (mClosestDropItem)
    {
        int targetIndex = mDropIsRight ? mGraphicItems.indexOf(mClosestDropItem) + 1 : mGraphicItems.indexOf(mClosestDropItem);

        QList<UBMimeDataItem> mimeDataItems;
        if (event->mimeData()->hasFormat(UBApplication::mimeTypeUniboardPage))
        {
            const UBMimeData* mimeData = qobject_cast<const UBMimeData*>(event->mimeData());
            if (mimeData)
                mimeDataItems = mimeData->items();
        }

        if (1 == mimeDataItems.count() &&
                (mimeDataItems.at(0).sceneIndex() == mGraphicItems.indexOf(mClosestDropItem) ||
                 targetIndex == mimeDataItems.at(0).sceneIndex() ||
                 targetIndex == mimeDataItems.at(0).sceneIndex() + 1))
        {
            return;
        }

        int sourceIndexOffset = 0;
        int actualTargetIndex = targetIndex;
        for (int i = mimeDataItems.count() - 1; i >= 0; i--)
        {
            UBMimeDataItem sourceItem = mimeDataItems.at(i);
            int actualSourceIndex = sourceItem.sceneIndex();
            if (sourceItem.sceneIndex() >= targetIndex)
                actualSourceIndex += sourceIndexOffset;

            //event->acceptProposedAction();
            if (sourceItem.sceneIndex() < targetIndex)
            {
                if (actualSourceIndex != actualTargetIndex - 1)
                    emit sceneDropped(sourceItem.documentProxy(), actualSourceIndex, actualTargetIndex - 1);
                actualTargetIndex -= 1;
            }
            else
            {
                if (actualSourceIndex != actualTargetIndex)
                    emit sceneDropped(sourceItem.documentProxy(), actualSourceIndex, actualTargetIndex);
                sourceIndexOffset += 1;
            }
        }
    }
    UBDocumentThumbnailsView::dropEvent(event);
}

void UBDocumentThumbnailWidget::deleteDropCaret()
{
    if (mDropCaretRectItem && scene())
    {
        scene()->removeItem(mDropCaretRectItem);
        delete mDropCaretRectItem;
        mDropCaretRectItem = 0;
    }
}

void UBDocumentThumbnailWidget::setGraphicsItems(const QList<QGraphicsItem*>& pGraphicsItems,
                                                 const QList<QUrl>& pItemPaths, const QStringList pLabels,
                                                 const QString& pMimeType)
{
    deleteDropCaret();

    UBDocumentThumbnailsView::setGraphicsItems(pGraphicsItems, pItemPaths, pLabels, pMimeType);
}

void UBDocumentThumbnailWidget::setDragEnabled(bool enabled)
{
    mDragEnabled = enabled;
}

bool UBDocumentThumbnailWidget::dragEnabled() const
{
    return mDragEnabled;
}

void UBDocumentThumbnailWidget::updateThumbnailPixmap(int index, const QPixmap& newThumbnail)
{
    if (index >= 0 && index < mGraphicItems.length())
    {
        UBSceneThumbnailPixmap *thumbnail = dynamic_cast<UBSceneThumbnailPixmap*>(mGraphicItems.at(index));
        if (thumbnail)
        {
            thumbnail->setPixmap(newThumbnail);
        }
    }
}

void UBDocumentThumbnailWidget::removeThumbnail(int sceneIndex)
{
    if (sceneIndex >= 0 && sceneIndex < mGraphicItems.length())
    {
        QGraphicsItem* thumbnailItem = mGraphicItems.at(sceneIndex);
        QGraphicsItem* textItem      = mLabelsItems.at(sceneIndex);
        UBSceneThumbnailPixmap *thumbnail = dynamic_cast<UBSceneThumbnailPixmap*>(thumbnailItem);
        if (thumbnail)
        {
            if (thumbnail->isSelected())
                scene()->removeItem(thumbnail->selectionItem());

            mGraphicItems.removeAt(sceneIndex);
            if (thumbnailItem)
                scene()->removeItem(thumbnailItem);

            mLabelsItems.removeAt(sceneIndex);
            if (textItem)
                scene()->removeItem(textItem);
        }
        refreshScene();
    }
}

void UBDocumentThumbnailWidget::insertThumbnail(int index, QGraphicsPixmapItem* newThumbnail)
{
    if (!mGraphicItems.contains(newThumbnail)) //sometimes, refreshDocumentThumbnailsView is called before
    {
        auto thumbnailTextItem = new UBThumbnailTextItem(index);
        mGraphicItems.insert(index, newThumbnail);
        mLabelsItems.insert(index, thumbnailTextItem);
    }

    refreshScene();
}

void UBDocumentThumbnailWidget::moveThumbnail(int from, int to)
{
    UBSceneThumbnailPixmap *thumbnail = dynamic_cast<UBSceneThumbnailPixmap*>(mGraphicItems.at(from));
    if (thumbnail)
    {
        mGraphicItems.move(from, to);
    }
    refreshScene();
}

void UBDocumentThumbnailWidget::hightlightItem(int index)
{
    if (0 <= index && index < mLabelsItems.length())
    {
        mLabelsItems.at(index)->highlight();
    }
    if (0 <= index && index < mGraphicItems.length())
    {
        UBSceneThumbnailPixmap *thumbnail = dynamic_cast<UBSceneThumbnailPixmap*>(mGraphicItems.at(index));
        if (thumbnail)
            thumbnail->highlight();
    }

    selectItemAt(index);
}

std::shared_ptr<UBDocumentProxy> UBDocumentThumbnailWidget::currentThumbnailsDocument()
{
    if (mGraphicItems.size() > 0)
    {
        UBSceneThumbnailPixmap* sceneThumbnailPixmap = dynamic_cast<UBSceneThumbnailPixmap*>(mGraphicItems.first());

        if (sceneThumbnailPixmap)
        {
            return sceneThumbnailPixmap->documentProxy();
        }
    }

    return nullptr;
}
