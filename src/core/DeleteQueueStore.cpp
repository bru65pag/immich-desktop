#include "core/DeleteQueueStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

namespace Aurora {

DeleteQueueStore::DeleteQueueStore()
    : m_filePath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
                 QStringLiteral("/delete-queue.json"))
{
    QDir().mkpath(QFileInfo(m_filePath).absolutePath());
}

QString DeleteQueueStore::filePath() const
{
    return m_filePath;
}

QList<PendingDeletion> DeleteQueueStore::load() const
{
    QFile file(m_filePath);
    if (!file.open(QIODevice::ReadOnly))
        return {};

    const QJsonArray items = QJsonDocument::fromJson(file.readAll())
                                 .object()
                                 .value(QStringLiteral("items"))
                                 .toArray();
    QList<PendingDeletion> pending;
    pending.reserve(items.size());
    for (const QJsonValue &value : items) {
        const QJsonObject object = value.toObject();
        const QString id = object.value(QStringLiteral("id")).toString();
        if (id.isEmpty())
            continue;
        pending.append({id, object.value(QStringLiteral("permanent")).toBool()});
    }
    return pending;
}

void DeleteQueueStore::save(const QList<PendingDeletion> &items) const
{
    QJsonArray array;
    for (const PendingDeletion &item : items) {
        if (item.assetId.isEmpty())
            continue;
        QJsonObject object;
        object.insert(QStringLiteral("id"), item.assetId);
        object.insert(QStringLiteral("permanent"), item.permanent);
        array.append(object);
    }
    QJsonObject root;
    root.insert(QStringLiteral("items"), array);

    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly))
        return;
    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (file.write(payload) != payload.size())
        file.cancelWriting();
    else
        file.commit();
}

} // namespace Aurora
