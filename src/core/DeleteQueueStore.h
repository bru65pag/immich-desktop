#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace Aurora {

struct PendingDeletion {
    QString assetId;
    bool permanent = false;
};

class DeleteQueueStore final {
public:
    DeleteQueueStore();

    QList<PendingDeletion> load() const;
    void save(const QList<PendingDeletion> &items) const;

    QString filePath() const;

private:
    QString m_filePath;
};

} // namespace Aurora
