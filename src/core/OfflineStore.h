#pragma once

#include "core/ImmichTypes.h"

#include <QList>
#include <QString>

namespace Aurora {

class OfflineStore final {
public:
    OfflineStore();

    void saveLibrary(const QString &serverUrl, const QList<ImmichAsset> &assets,
                     const QString &query = {});
    void mergeLibrary(const QString &serverUrl, const QList<ImmichAsset> &assets);
    bool loadLibrary(const QString &serverUrl, QList<ImmichAsset> *assets,
                     QString *query = nullptr) const;

    void saveExplore(const QString &serverUrl, const ImmichExploreData &data);
    bool loadExplore(const QString &serverUrl, ImmichExploreData *data) const;

    void setAssetPinned(const QString &serverUrl, const QString &assetId, bool pinned);
    bool isAssetPinned(const QString &serverUrl, const QString &assetId) const;
    QStringList pinnedAssetIds(const QString &serverUrl) const;

    // Removes deleted assets from the cached library snapshot and clears
    // their pinned flag, so they don't resurface when browsing offline and
    // don't leak pinned-cache storage forever.
    void purgeAssets(const QString &serverUrl, const QStringList &assetIds);

    QString directory() const;

private:
    QString libraryPath(const QString &serverUrl) const;
    QString explorePath(const QString &serverUrl) const;
    QString pinnedPath(const QString &serverUrl) const;
    static QString serverKey(const QString &serverUrl);

    QString m_directory;
};

} // namespace Aurora
