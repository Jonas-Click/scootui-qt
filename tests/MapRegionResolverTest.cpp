#include <QtTest>

#include "services/MapRegionResolver.h"

class MapRegionResolverTest : public QObject
{
    Q_OBJECT

private slots:
    void matchesSubdivisionAndCountry();
    void matchesCityBeforeCountry();
    void refusesAmbiguousOrIncompleteRegions();
};

static QJsonObject region(const QString &country, const QJsonObject &location,
                          bool complete = true)
{
    return {
        {QStringLiteral("country"), country},
        {QStringLiteral("location"), location},
        {QStringLiteral("map"), QJsonObject{{QStringLiteral("url"), QStringLiteral("map")}}},
        {QStringLiteral("valhalla"), complete
             ? QJsonObject{{QStringLiteral("url"), QStringLiteral("route")}}
             : QJsonObject{}},
    };
}

void MapRegionResolverTest::matchesSubdivisionAndCountry()
{
    const QJsonObject manifest{
        {QStringLiteral("zurich"), region(QStringLiteral("CH"),
            {{QStringLiteral("subdivision_codes"), QJsonArray{QStringLiteral("CH-ZH")}}})},
        {QStringLiteral("alsace"), region(QStringLiteral("FR"),
            {{QStringLiteral("subdivision_codes"), QJsonArray{QStringLiteral("FR-67"), QStringLiteral("FR-68")}}})},
    };
    QCOMPARE(MapRegionResolver::resolve({
        {QStringLiteral("country_code"), QStringLiteral("ch")},
        {QStringLiteral("ISO3166-2-lvl4"), QStringLiteral("CH-ZH")},
        {QStringLiteral("city"), QStringLiteral("Zürich")},
    }, manifest), QStringLiteral("zurich"));
    QCOMPARE(MapRegionResolver::resolve({
        {QStringLiteral("country_code"), QStringLiteral("fr")},
        {QStringLiteral("ISO3166-2-lvl4"), QStringLiteral("FR-GES")},
        {QStringLiteral("ISO3166-2-lvl6"), QStringLiteral("FR-67")},
    }, manifest), QStringLiteral("alsace"));
    QVERIFY(MapRegionResolver::resolve({
        {QStringLiteral("country_code"), QStringLiteral("de")},
        {QStringLiteral("ISO3166-2-lvl4"), QStringLiteral("CH-ZH")},
    }, manifest).isEmpty());
}

void MapRegionResolverTest::matchesCityBeforeCountry()
{
    const QJsonObject manifest{
        {QStringLiteral("austria"), region(QStringLiteral("AT"),
            {{QStringLiteral("scope"), QStringLiteral("country")}})},
        {QStringLiteral("graz"), region(QStringLiteral("AT"),
            {{QStringLiteral("cities"), QJsonArray{QStringLiteral("Graz")}}})},
    };
    QCOMPARE(MapRegionResolver::resolve({
        {QStringLiteral("country_code"), QStringLiteral("at")},
        {QStringLiteral("city"), QStringLiteral("Graz")},
    }, manifest), QStringLiteral("graz"));
    QCOMPARE(MapRegionResolver::resolve({
        {QStringLiteral("country_code"), QStringLiteral("at")},
        {QStringLiteral("city"), QStringLiteral("Linz")},
    }, manifest), QStringLiteral("austria"));
}

void MapRegionResolverTest::refusesAmbiguousOrIncompleteRegions()
{
    const QJsonObject location{{QStringLiteral("subdivision_codes"),
                                QJsonArray{QStringLiteral("DE-BB")}}};
    const QJsonObject manifest{
        {QStringLiteral("brandenburg"), region(QStringLiteral("DE"), location)},
        {QStringLiteral("berlin_brandenburg"), region(QStringLiteral("DE"), location)},
    };
    const QJsonObject address{
        {QStringLiteral("country_code"), QStringLiteral("de")},
        {QStringLiteral("ISO3166-2-lvl4"), QStringLiteral("DE-BB")},
    };
    QVERIFY(MapRegionResolver::resolve(address, manifest).isEmpty());
    QCOMPARE(MapRegionResolver::resolve(address, {
        {QStringLiteral("berlin_brandenburg"), region(QStringLiteral("DE"), location, false)},
    }), QString{});
}

QTEST_GUILESS_MAIN(MapRegionResolverTest)
#include "MapRegionResolverTest.moc"
