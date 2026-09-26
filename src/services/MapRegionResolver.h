#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

#include <algorithm>

namespace MapRegionResolver {

inline QString resolve(const QJsonObject &address, const QJsonObject &manifest)
{
    const QString country = address.value(QStringLiteral("country_code")).toString();
    if (country.isEmpty())
        return {};

    QStringList subdivisions;
    for (auto it = address.begin(); it != address.end(); ++it) {
        if (it.key().startsWith(QLatin1String("ISO3166-2-lvl")))
            subdivisions.append(it.value().toString());
    }
    QString city = address.value(QStringLiteral("city")).toString();
    if (city.isEmpty()) city = address.value(QStringLiteral("town")).toString();
    if (city.isEmpty()) city = address.value(QStringLiteral("municipality")).toString();

    QString selected;
    int best = -1;
    bool ambiguous = false;
    for (auto it = manifest.begin(); it != manifest.end(); ++it) {
        const auto region = it.value().toObject();
        if (region.value(QStringLiteral("country")).toString().compare(country, Qt::CaseInsensitive) != 0
            || region.value(QStringLiteral("map")).toObject().isEmpty()
            || region.value(QStringLiteral("valhalla")).toObject().isEmpty())
            continue;
        const auto location = region.value(QStringLiteral("location")).toObject();
        int rank = location.value(QStringLiteral("scope")).toString() == QLatin1String("country") ? 0 : -1;
        for (const auto &value : location.value(QStringLiteral("subdivision_codes")).toArray()) {
            if (subdivisions.contains(value.toString(), Qt::CaseInsensitive))
                rank = std::max(rank, 1);
        }
        for (const auto &value : location.value(QStringLiteral("cities")).toArray()) {
            if (city.compare(value.toString(), Qt::CaseInsensitive) == 0)
                rank = 2;
        }
        if (rank > best) {
            best = rank;
            selected = it.key();
            ambiguous = false;
        } else if (rank >= 0 && rank == best) {
            ambiguous = true;
        }
    }
    return ambiguous ? QString{} : selected;
}

} // namespace MapRegionResolver
