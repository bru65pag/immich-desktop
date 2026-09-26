#include "ui/widgets/MediaTile.h"

#include "ui/IconUtils.h"
#include "ui/widgets/VideoHoverPreview.h"

#include <QContextMenuEvent>
#include <QEnterEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>

namespace Aurora {

MediaTile::MediaTile(const ImmichAsset &asset, QWidget *parent)
    : QWidget(parent)
    , m_asset(asset)
    , m_resolvedAspectRatio(asset.aspectRatio > 0.01 ? asset.aspectRatio : 1.0)
{
    setObjectName(QStringLiteral("mediaTile"));
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMouseTracking(true);
    setToolTip(asset.fileName);
}

const ImmichAsset &MediaTile::asset() const
{
    return m_asset;
}

qreal MediaTile::aspectRatio() const
{
    return m_resolvedAspectRatio;
}

bool MediaTile::hasThumbnail() const
{
    return !m_thumbnail.isNull();
}

bool MediaTile::hasThumbnailError() const
{
    return m_hasError;
}

void MediaTile::setHoverPreview(VideoHoverPreview *preview)
{
    m_hoverPreview = preview;
}

void MediaTile::endHoverPreview()
{
    if (!m_hoverPreviewActive)
        return;
    m_hoverPreviewActive = false;
    update();
}

void MediaTile::setThumbnail(const QPixmap &thumbnail)
{
    m_thumbnail = thumbnail;
    if (thumbnail.height() > 0)
        m_resolvedAspectRatio = qreal(thumbnail.width()) / qreal(thumbnail.height());
    m_hasError = false;
    m_error.clear();
    update();
}

void MediaTile::setThumbnailError(const QString &message)
{
    m_thumbnail = QPixmap();
    m_hasError = true;
    m_error = message;
    update();
}

void MediaTile::clearThumbnail()
{
    if (m_thumbnail.isNull())
        return;
    m_thumbnail = QPixmap();
    update();
}

void MediaTile::setTileSize(const QSize &size)
{
    setFixedSize(size);
    if (m_hoverPreview)
        m_hoverPreview->updateTileGeometry(this);
}

QString MediaTile::formatDuration(const QString &raw)
{
    QString duration = raw.section(u'.', 0, 0);
    if (duration.startsWith(QStringLiteral("00:")))
        duration.remove(0, 3);
    while (duration.startsWith(QStringLiteral("0")) && duration.size() > 4 &&
           duration.at(1) != u':')
        duration.remove(0, 1);
    return duration;
}

void MediaTile::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.fillRect(rect(), QColor(20, 20, 20));

    if (!m_thumbnail.isNull()) {
        const QPixmap scaled = m_thumbnail.scaled(
            size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        const int x = (scaled.width() - width()) / 2;
        const int y = (scaled.height() - height()) / 2;
        painter.drawPixmap(0, 0, scaled, x, y, width(), height());
    } else {
        painter.setPen(QColor(150, 150, 150));
        painter.drawText(rect().adjusted(8, 8, -8, -8),
                         Qt::AlignCenter | Qt::TextWordWrap,
                         m_hasError ? m_error : tr("…"));
    }

    if (m_uploadPending) {
        painter.fillRect(rect(), QColor(0, 0, 0, 110));
        const QPixmap uploadIcon =
            renderSvgIcon(QStringLiteral(":/icons/upload.svg"), Qt::white, QSize(20, 20));
        if (!uploadIcon.isNull()) {
            painter.drawPixmap((width() - uploadIcon.width()) / 2,
                               height() / 2 - uploadIcon.height() - 4, uploadIcon);
        }
        painter.setPen(Qt::white);
        painter.drawText(QRect(4, height() / 2 + 2, width() - 8, height() / 2 - 8),
                         Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, tr("Queued"));
        return;
    }

    constexpr int kPinBadgeDiameter = 22;
    constexpr int kPinBadgeMargin = 8;
    QRect pinBadgeRect;
    if (m_pinned) {
        pinBadgeRect = QRect(width() - kPinBadgeMargin - kPinBadgeDiameter, kPinBadgeMargin,
                             kPinBadgeDiameter, kPinBadgeDiameter);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 140));
        painter.drawEllipse(pinBadgeRect);

        const QPixmap offlineIcon = renderSvgIcon(
            QStringLiteral(":/icons/cloud-download.svg"), Qt::white, QSize(14, 14));
        if (!offlineIcon.isNull()) {
            const QPoint iconPos(
                pinBadgeRect.center().x() - offlineIcon.width() / 2,
                pinBadgeRect.center().y() - offlineIcon.height() / 2);
            painter.drawPixmap(iconPos, offlineIcon);
        }
    }

    if (m_asset.isVideo() && !m_hoverPreviewActive) {
        const QString duration = formatDuration(m_asset.duration);
        const QPixmap playIcon =
            renderSvgIcon(QStringLiteral(":/icons/play.svg"), Qt::white, QSize(18, 18));

        int right = width() - 8;
        if (m_pinned)
            right = pinBadgeRect.left() - 8;
        if (!playIcon.isNull()) {
            const int iconX = right - playIcon.width();
            const int iconY = 8;
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0, 0, 0, 120));
            painter.drawEllipse(QRect(iconX - 2, iconY - 2,
                                      playIcon.width() + 4, playIcon.height() + 4));
            painter.drawPixmap(iconX, iconY, playIcon);
            right = iconX - 8;
        }

        if (!duration.isEmpty()) {
            const QFontMetrics metrics(painter.font());
            const int textWidth = metrics.horizontalAdvance(duration);
            const QRect textRect(right - textWidth - 10, 8, textWidth + 10, 20);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0, 0, 0, 140));
            painter.drawRoundedRect(textRect, 4, 4);
            painter.setPen(Qt::white);
            painter.drawText(textRect, Qt::AlignCenter, duration);
        }
    }

    if (m_selected) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(166, 133, 226, 70));
        painter.drawRect(rect());
        painter.setPen(QPen(QColor(166, 133, 226), 3));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(rect().adjusted(1, 1, -1, -1));
    } else if (hasFocus()) {
        painter.setPen(QPen(QColor(166, 133, 226), 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(rect().adjusted(1, 1, -1, -1));
    }

    if (checkboxVisible()) {
        const QRect box = checkboxRect();
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_selected ? QColor(166, 133, 226) : QColor(0, 0, 0, 140));
        painter.drawEllipse(box);
        if (m_selected) {
            QPen checkPen(Qt::white, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            painter.setPen(checkPen);
            const QPointF c = box.center();
            painter.drawLine(QPointF(c.x() - 4.5, c.y() + 0.5), QPointF(c.x() - 1.2, c.y() + 3.8));
            painter.drawLine(QPointF(c.x() - 1.2, c.y() + 3.8), QPointF(c.x() + 4.8, c.y() - 3.5));
        } else {
            painter.setPen(QPen(QColor(255, 255, 255, 200), 1.5));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(box.adjusted(4, 4, -4, -4));
        }
    }
}

void MediaTile::enterEvent(QEnterEvent *event)
{
    emit highlighted(m_asset);
    m_hovered = true;
    if (m_hoverPreview && m_asset.isVideo()) {
        m_hoverPreviewActive = true;
        m_hoverPreview->showForTile(this);
    }
    update();
    QWidget::enterEvent(event);
}

void MediaTile::leaveEvent(QEvent *event)
{
    m_hovered = false;
    if (m_hoverPreview && m_hoverPreviewActive) {
        m_hoverPreviewActive = false;
        m_hoverPreview->hideForTile(this);
    }
    update();
    QWidget::leaveEvent(event);
}

void MediaTile::resizeEvent(QResizeEvent *event)
{
    if (m_hoverPreview)
        m_hoverPreview->updateTileGeometry(this);
    QWidget::resizeEvent(event);
}

void MediaTile::keyPressEvent(QKeyEvent *event)
{
    if (event->matches(QKeySequence::Copy)) {
        emit copyRequested(m_asset);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ||
        event->key() == Qt::Key_Space) {
        emit activated(m_asset);
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void MediaTile::mousePressEvent(QMouseEvent *event)
{
    m_pressedOnCheckbox = false;
    if (event->button() == Qt::LeftButton) {
        setFocus(Qt::MouseFocusReason);
        emit highlighted(m_asset);
        if (checkboxVisible() && checkboxRect().contains(event->position().toPoint())) {
            m_pressedOnCheckbox = true;
            if (event->modifiers() & Qt::ShiftModifier)
                emit rangeSelectRequested(m_asset);
            else
                emit toggleSelectRequested(m_asset);
        }
    }
    QWidget::mousePressEvent(event);
}

void MediaTile::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && !m_pressedOnCheckbox && !m_uploadPending &&
        rect().contains(event->position().toPoint()))
        emit activated(m_asset);
    m_pressedOnCheckbox = false;
    QWidget::mouseReleaseEvent(event);
}

void MediaTile::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    if (m_uploadPending) {
        menu.addAction(tr("Cancel upload"), this,
                      [this] { emit cancelUploadRequested(m_asset); });
        menu.exec(event->globalPos());
        return;
    }
    menu.addAction(tr("Open"), this, [this] { emit activated(m_asset); });
    if (!m_asset.isVideo())
        menu.addAction(tr("Copy"), this, [this] { emit copyRequested(m_asset); });
    menu.addAction(tr("Download"), this, [this] { emit downloadRequested(m_asset); });
    menu.addSeparator();
    if (m_pinned)
        menu.addAction(tr("Remove from this device"), this,
                      [this] { emit unpinRequested(m_asset); });
    else
        menu.addAction(tr("Keep on this device"), this, [this] { emit pinRequested(m_asset); });
    menu.addSeparator();
    menu.addAction(tr("Move to trash"), this, [this] { emit trashRequested(m_asset); });
    menu.addAction(tr("Delete permanently"), this, [this] { emit deleteRequested(m_asset); });
    menu.exec(event->globalPos());
}

void MediaTile::setPinned(bool pinned)
{
    if (m_pinned == pinned)
        return;
    m_pinned = pinned;
    update();
}

bool MediaTile::isPinned() const
{
    return m_pinned;
}

void MediaTile::setSelected(bool selected)
{
    if (m_selected == selected)
        return;
    m_selected = selected;
    update();
}

bool MediaTile::isSelected() const
{
    return m_selected;
}

void MediaTile::setSelectionModeActive(bool active)
{
    if (m_selectionModeActive == active)
        return;
    m_selectionModeActive = active;
    update();
}

QRect MediaTile::checkboxRect() const
{
    constexpr int kDiameter = 22;
    constexpr int kMargin = 8;
    return QRect(kMargin, kMargin, kDiameter, kDiameter);
}

bool MediaTile::checkboxVisible() const
{
    if (m_uploadPending)
        return false;
    return m_selected || m_selectionModeActive || m_hovered;
}

void MediaTile::setUploadPending(bool pending)
{
    if (m_uploadPending == pending)
        return;
    m_uploadPending = pending;
    update();
}

bool MediaTile::isUploadPending() const
{
    return m_uploadPending;
}

} // namespace Aurora
