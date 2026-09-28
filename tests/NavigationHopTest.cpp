#include <QtTest>
#include <QDateTime>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>

#include "routing/ValhallaClient.h"
#include "services/AddressDatabaseService.h"
#include "services/NavigationService.h"
#include "repositories/InMemoryMdbRepository.h"
#include "stores/GpsStore.h"
#include "stores/NavigationStore.h"
#include "stores/VehicleStore.h"
#include "stores/SettingsStore.h"
#include "stores/SpeedLimitStore.h"

const QString AddressDatabaseService::MbtilesPath =
    QStringLiteral("/nonexistent/navigation-hop-test.mbtiles");

class NavigationHopTest : public QObject
{
    Q_OBJECT
private slots:
    void twoImmediateAppendsRetainBothStops();
    void futureGeometryStartsAfterNextStop();
    void clearWhilePlanLoadingCannotEraseExternalRoute();
    void concurrentExternalAppendRetainsBothStops();
    void staleProgressDoesNotAdvanceReplacement();
    void queuedArrivalCannotReachReplacement();
    void reachedStopAdvancesAndFinalArrivalRetainsOwner();
    void moveJumpAndRemovePreserveTarget();
    void insertStopPlacesBeforeActiveStop();
    void rebootRestoresOwnerSnapshot();
    void rebootCompletedPlanDoesNotRearmArrival();
    void clearDoesNotRestoreLegacySettings();
    void appendAfterFinalArrivalReopensPrompt();
    void unavailableOwnerReportsErrorWithoutBlocking();
    void unavailableOwnerRestoreStaysSilent();
    void keepStopIsDurableAndClearedOnDismount();
    void intermediatePlanResumesAfterHopOn();
    void planCreatedInHopOnStartsWhenParked();
    void pausedPlanResumesOnUnlockToParked();
    void restoredPlanStartsWhileParked();
    void reachedStopWaitsUntilReadyToDrive();
    void singleDestinationRouteSurvivesHopOn();
    void missingRoutingTilesReportsSpecificError();
    void recoveryWaitsForFirstHealthyProbe();
    void recoveryTimesOutWithoutHealthyProbe();
    void rejectedRecoveryLeavesCalculatingState();

private:
    struct Fixture {
        InMemoryMdbRepository repo;
        GpsStore gps{&repo};
        SpeedLimitStore speed{&repo};
        NavigationStore navStore{&repo};
        VehicleStore vehicle{&repo};
        SettingsStore settings{&repo};
        NavigationService nav{&gps, &navStore, &vehicle, &settings, &speed, &repo};
        Fixture() {
            gps.start(); navStore.start(); vehicle.start(); settings.start(); speed.start();
            repo.set(QStringLiteral("vehicle"), QStringLiteral("state"),
                     QStringLiteral("ready-to-drive"));
        }
        ~Fixture() {
            if (auto *client = nav.findChild<ValhallaClient *>()) client->cancelPending();
        }
    };
    static void external(InMemoryMdbRepository &repo, const QString &method,
                         const QJsonObject &payload) {
        const QJsonObject envelope{{QStringLiteral("reply_channel"), QStringLiteral("test:reply")},
                                   {QStringLiteral("method"), method},
                                   {QStringLiteral("payload"), payload}};
        repo.push(QStringLiteral("settings:route-plan"),
                  QString::fromUtf8(QJsonDocument(envelope).toJson(QJsonDocument::Compact)));
    }
    static QJsonObject stop(double lat, double lon, const QString &label) {
        return {{QStringLiteral("lat"), lat}, {QStringLiteral("lon"), lon},
                {QStringLiteral("label"), label}};
    }
    static QJsonObject snapshot(InMemoryMdbRepository &repo) {
        return QJsonDocument::fromJson(repo.get(QStringLiteral("navigation"),
                                                 QStringLiteral("plan")).toUtf8()).object();
    }
    static void gps(Fixture &f, double lat, double lon) {
        f.repo.publish(QStringLiteral("gps:tpv"), QStringLiteral(
            "{\"latitude\":\"%1\",\"longitude\":\"%2\",\"course\":\"90\","
            "\"speed\":\"20\",\"eph\":\"4.5\",\"state\":\"fix-established\","
            "\"timestamp\":\"%3\"}")
            .arg(lat, 0, 'f', 7).arg(lon, 0, 'f', 7)
            .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
    }
};

void NavigationHopTest::recoveryWaitsForFirstHealthyProbe()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QTcpSocket *healthRequest = nullptr;
    int routeRequests = 0;
    connect(&server, &QTcpServer::newConnection, &server, [&]() {
        while (server.hasPendingConnections()) {
            auto *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, &server, [&, socket]() {
                const QByteArray request = socket->readAll();
                if (request.startsWith("GET /status"))
                    healthRequest = socket;
                else if (request.startsWith("POST /route"))
                    ++routeRequests;
            });
        }
    });

    ValhallaClient client;
    client.setEndpoint(QStringLiteral("http://127.0.0.1:%1/").arg(server.serverPort()));
    QSignalSpy rejected(&client, &ValhallaClient::requestRejected);
    QSignalSpy dispatched(&client, &ValhallaClient::requestDispatched);
    client.requestRoute(LatLng{52.5, 13.4}, LatLng{52.6, 13.5},
                        ValhallaClient::Reason::Recovery);
    QTRY_VERIFY_WITH_TIMEOUT(healthRequest != nullptr, 3000);
    QTest::qWait(ValhallaClient::DebounceIntervalMs + 100);
    QCOMPARE(rejected.size(), 0);
    QCOMPARE(dispatched.size(), 0);
    QCOMPARE(routeRequests, 0);

    healthRequest->write("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n{}");
    healthRequest->flush();
    QTRY_COMPARE_WITH_TIMEOUT(dispatched.size(), 1, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(routeRequests, 1, 3000);
    QCOMPARE(rejected.size(), 0);
    client.cancelPending();
}

void NavigationHopTest::recoveryTimesOutWithoutHealthyProbe()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    ValhallaClient client;
    client.setEndpoint(QStringLiteral("http://127.0.0.1:%1/").arg(server.serverPort()));
    QSignalSpy rejected(&client, &ValhallaClient::requestRejected);
    QSignalSpy dispatched(&client, &ValhallaClient::requestDispatched);
    client.requestRoute(LatLng{52.5, 13.4}, LatLng{52.6, 13.5},
                        ValhallaClient::Reason::Recovery);
    QTRY_COMPARE_WITH_TIMEOUT(rejected.size(), 1,
                              ValhallaClient::UserRequestTimeoutMs + 3000);
    QCOMPARE(rejected.first().at(0).value<ValhallaClient::Reason>(),
             ValhallaClient::Reason::Recovery);
    QCOMPARE(rejected.first().at(1).value<ValhallaClient::RejectionCause>(),
             ValhallaClient::RejectionCause::Unhealthy);
    QCOMPARE(dispatched.size(), 0);
}

void NavigationHopTest::rejectedRecoveryLeavesCalculatingState()
{
    Fixture f;
    auto *client = f.nav.findChild<ValhallaClient *>();
    QVERIFY(client);
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    client->setEndpoint(QStringLiteral("http://127.0.0.1:%1/").arg(server.serverPort()));
    gps(f, 52.5, 13.4);
    f.nav.setRoutePlan(QVariantList{QVariantMap{{QStringLiteral("lat"), 52.6},
        {QStringLiteral("lon"), 13.5}}});
    QTRY_COMPARE_WITH_TIMEOUT(f.nav.status(), static_cast<int>(NavigationStatus::Calculating), 3000);
    client->requestRejected(ValhallaClient::Reason::Recovery,
                            ValhallaClient::RejectionCause::Unhealthy);
    QCOMPARE(f.nav.status(), static_cast<int>(NavigationStatus::Error));
    QCOMPARE(f.nav.errorMessage(), QStringLiteral("Cannot reach routing server"));
    client->cancelPending();
    QSignalSpy samples(&f.gps, &GpsStore::sampleChanged);
    gps(f, 52.5001, 13.4);
    QTRY_VERIFY_WITH_TIMEOUT(!samples.isEmpty(), 3000);
    QTRY_COMPARE_WITH_TIMEOUT(f.nav.status(), static_cast<int>(NavigationStatus::Calculating), 3000);
}

void NavigationHopTest::intermediatePlanResumesAfterHopOn()
{
    Fixture f;
    gps(f, 52.50, 13.40);
    f.nav.setRoutePlan(QVariantList{
        QVariantMap{{QStringLiteral("lat"), 52.51}, {QStringLiteral("lon"), 13.41}},
        QVariantMap{{QStringLiteral("lat"), 52.52}, {QStringLiteral("lon"), 13.42}}});
    QTRY_COMPARE_WITH_TIMEOUT(f.nav.planState(), int(RoutePlanState::Navigating), 3000);
    Route route;
    route.waypoints = {{52.50, 13.40}, {52.51, 13.41}};
    route.distance = 100;
    route.duration = 60;
    RouteInstruction arrival;
    arrival.type = ManeuverType::Arrive;
    arrival.originalShapeIndex = 1;
    route.instructions = {arrival};
    f.nav.setRoute(route);
    QCOMPARE(f.nav.status(), int(NavigationStatus::Navigating));

    f.repo.set(QStringLiteral("vehicle"), QStringLiteral("state"), QStringLiteral("parked"));
    QTRY_COMPARE_WITH_TIMEOUT(f.nav.planState(), int(RoutePlanState::Paused), 3000);
    f.repo.set(QStringLiteral("vehicle"), QStringLiteral("state"), QStringLiteral("hop-on"));
    QTRY_VERIFY_WITH_TIMEOUT(f.vehicle.hopOnActive(), 3000);
    QCOMPARE(f.nav.planState(), int(RoutePlanState::Paused));
    QSignalSpy routeChanges(&f.nav, &NavigationService::routeChanged);
    f.repo.set(QStringLiteral("vehicle"), QStringLiteral("state"), QStringLiteral("parked"));
    QTRY_COMPARE_WITH_TIMEOUT(f.nav.planState(), int(RoutePlanState::Navigating), 3000);
    QCOMPARE(f.nav.status(), int(NavigationStatus::Calculating));
    QVERIFY(!f.nav.hasRoute());
    QCOMPARE(routeChanges.size(), 1);
    f.repo.set(QStringLiteral("vehicle"), QStringLiteral("state"), QStringLiteral("ready-to-drive"));
    QTRY_VERIFY_WITH_TIMEOUT(f.vehicle.isReadyToDrive(), 3000);
    QCOMPARE(f.nav.planState(), int(RoutePlanState::Navigating));
    QCOMPARE(routeChanges.size(), 1);
    QCOMPARE(f.nav.currentStep(), 0);
}

void NavigationHopTest::planCreatedInHopOnStartsWhenParked()
{
    Fixture f;
    f.repo.set(QStringLiteral("vehicle"), QStringLiteral("state"), QStringLiteral("parked"));
    f.repo.set(QStringLiteral("vehicle"), QStringLiteral("state"), QStringLiteral("hop-on"));
    QTRY_VERIFY_WITH_TIMEOUT(f.vehicle.hopOnActive(), 3000);
    gps(f, 52.50, 13.40);
    f.nav.appendStop(52.51, 13.41);
    QTRY_COMPARE_WITH_TIMEOUT(f.nav.planState(), int(RoutePlanState::Paused), 3000);
    f.repo.set(QStringLiteral("vehicle"), QStringLiteral("state"), QStringLiteral("parked"));
    QTRY_COMPARE_WITH_TIMEOUT(f.nav.planState(), int(RoutePlanState::Navigating), 3000);
    QCOMPARE(f.nav.status(), int(NavigationStatus::Calculating));
    QCOMPARE(f.nav.stopCount(), 1);
}

void NavigationHopTest::pausedPlanResumesOnUnlockToParked()
{
    Fixture f;
    f.repo.set(QStringLiteral("vehicle"), QStringLiteral("state"), QStringLiteral("stand-by"));
    QTRY_VERIFY_WITH_TIMEOUT(f.vehicle.isStandBy(), 3000);
    gps(f, 52.50, 13.40);
    f.nav.appendStop(52.51, 13.41);
    QTRY_COMPARE_WITH_TIMEOUT(f.nav.planState(), int(RoutePlanState::Paused), 3000);
    f.repo.set(QStringLiteral("vehicle"), QStringLiteral("state"), QStringLiteral("parked"));
    QTRY_COMPARE_WITH_TIMEOUT(f.nav.planState(), int(RoutePlanState::Navigating), 3000);
    QCOMPARE(f.nav.status(), int(NavigationStatus::Calculating));
}

void NavigationHopTest::restoredPlanStartsWhileParked()
{
    InMemoryMdbRepository repo;
    external(repo, QStringLiteral("plan.append"),
             {{QStringLiteral("stop"), stop(52.51, 13.41, QStringLiteral("A"))}});
    repo.set(QStringLiteral("vehicle"), QStringLiteral("state"), QStringLiteral("parked"));
    GpsStore gpsStore(&repo); SpeedLimitStore speed(&repo); NavigationStore store(&repo);
    VehicleStore vehicle(&repo); SettingsStore settings(&repo);
    NavigationService nav(&gpsStore, &store, &vehicle, &settings, &speed, &repo);
    gpsStore.start(); store.start(); vehicle.start(); settings.start(); speed.start();
    QTRY_COMPARE_WITH_TIMEOUT(nav.planState(), int(RoutePlanState::Navigating), 3000);
    QCOMPARE(nav.status(), int(NavigationStatus::WaitingForPosition));
    if (auto *client = nav.findChild<ValhallaClient *>()) client->cancelPending();
}

void NavigationHopTest::reachedStopWaitsUntilReadyToDrive()
{
    Fixture f;
    gps(f, 52.50, 13.40);
    f.nav.setRoutePlan(QVariantList{
        QVariantMap{{QStringLiteral("lat"), 52.51}, {QStringLiteral("lon"), 13.41}},
        QVariantMap{{QStringLiteral("lat"), 52.52}, {QStringLiteral("lon"), 13.42}}});
    QTRY_COMPARE_WITH_TIMEOUT(f.nav.planState(), int(RoutePlanState::Navigating), 3000);
    const QJsonObject plan = snapshot(f.repo);
    external(f.repo, QStringLiteral("plan.reached"),
             {{QStringLiteral("expected_plan_id"), plan.value(QStringLiteral("id"))},
              {QStringLiteral("expected_stop_id"), plan.value(QStringLiteral("stops"))
                   .toArray().first().toObject().value(QStringLiteral("id"))}});
    QTRY_COMPARE_WITH_TIMEOUT(f.nav.planState(), int(RoutePlanState::AtStop), 3000);
    f.repo.set(QStringLiteral("vehicle"), QStringLiteral("state"), QStringLiteral("parked"));
    QTRY_COMPARE_WITH_TIMEOUT(f.nav.planState(), int(RoutePlanState::Paused), 3000);
    f.repo.set(QStringLiteral("vehicle"), QStringLiteral("state"), QStringLiteral("hop-on"));
    QTRY_VERIFY_WITH_TIMEOUT(f.vehicle.hopOnActive(), 3000);
    f.repo.set(QStringLiteral("vehicle"), QStringLiteral("state"), QStringLiteral("parked"));
    QTRY_COMPARE_WITH_TIMEOUT(f.vehicle.state(), int(ScootEnums::VehicleState::Parked), 3000);
    QCOMPARE(f.nav.planState(), int(RoutePlanState::Paused));
    QCOMPARE(f.nav.currentStep(), 0);
    f.repo.set(QStringLiteral("vehicle"), QStringLiteral("state"), QStringLiteral("ready-to-drive"));
    QTRY_COMPARE_WITH_TIMEOUT(f.nav.currentStep(), 1, 3000);
}

void NavigationHopTest::singleDestinationRouteSurvivesHopOn()
{
    Fixture f;
    gps(f, 52.50, 13.40);
    f.nav.appendStop(52.51, 13.41);
    QTRY_COMPARE_WITH_TIMEOUT(f.nav.planState(), int(RoutePlanState::Navigating), 3000);
    Route route;
    route.waypoints = {{52.50, 13.40}, {52.51, 13.41}};
    route.distance = 100;
    route.duration = 60;
    RouteInstruction arrival;
    arrival.type = ManeuverType::Arrive;
    arrival.originalShapeIndex = 1;
    route.instructions = {arrival};
    f.nav.setRoute(route);
    f.repo.set(QStringLiteral("vehicle"), QStringLiteral("state"), QStringLiteral("parked"));
    QTRY_VERIFY_WITH_TIMEOUT(f.vehicle.isParked(), 3000);
    f.repo.set(QStringLiteral("vehicle"), QStringLiteral("state"), QStringLiteral("hop-on"));
    QTRY_VERIFY_WITH_TIMEOUT(f.vehicle.hopOnActive(), 3000);
    f.repo.set(QStringLiteral("vehicle"), QStringLiteral("state"), QStringLiteral("parked"));
    QTRY_COMPARE_WITH_TIMEOUT(f.vehicle.state(), int(ScootEnums::VehicleState::Parked), 3000);
    f.repo.set(QStringLiteral("vehicle"), QStringLiteral("state"), QStringLiteral("ready-to-drive"));
    QTRY_VERIFY_WITH_TIMEOUT(f.vehicle.isReadyToDrive(), 3000);
    QVERIFY(f.nav.hasRoute());
    QCOMPARE(f.nav.status(), int(NavigationStatus::Navigating));
    QCOMPARE(f.nav.destLatitude(), 52.51);
}

void NavigationHopTest::missingRoutingTilesReportsSpecificError()
{
    Fixture f;
    bool tilesInstalled = false;
    f.nav.setRoutingTilesAvailable([&tilesInstalled]() { return tilesInstalled; });
    auto *client = f.nav.findChild<ValhallaClient *>();
    QVERIFY(client);

    client->requestRejected(ValhallaClient::Reason::Initial,
                            ValhallaClient::RejectionCause::Unhealthy);
    QCOMPARE(f.nav.errorMessage(),
             QStringLiteral("Navigation requested but no routing tiles installed."));

    QMetaObject::invokeMethod(&f.nav, "onRouteError", Q_ARG(QString, QStringLiteral("Routing failed")));
    QCOMPARE(f.nav.errorMessage(),
             QStringLiteral("Navigation requested but no routing tiles installed."));

    tilesInstalled = true;
    client->requestRejected(ValhallaClient::Reason::Destination,
                            ValhallaClient::RejectionCause::Unhealthy);
    QCOMPARE(f.nav.errorMessage(), QStringLiteral("Cannot reach routing server"));

    tilesInstalled = false;
    client->setEndpoint(QStringLiteral("https://example.org/"));
    client->requestRejected(ValhallaClient::Reason::Initial,
                            ValhallaClient::RejectionCause::Unhealthy);
    QCOMPARE(f.nav.errorMessage(), QStringLiteral("Cannot reach routing server"));
    QMetaObject::invokeMethod(&f.nav, "onRouteError", Q_ARG(QString, QStringLiteral("Remote route error")));
    QCOMPARE(f.nav.errorMessage(), QStringLiteral("Remote route error"));
}

void NavigationHopTest::twoImmediateAppendsRetainBothStops()
{
    Fixture f;
    f.nav.appendStop(52.51, 13.41, QStringLiteral("A"));
    f.nav.appendStop(52.52, 13.42, QStringLiteral("B"));
    QTRY_COMPARE_WITH_TIMEOUT(f.nav.stopCount(), 2, 3000);
    QCOMPARE(snapshot(f.repo).value(QStringLiteral("stops")).toArray().size(), 2);
    QCOMPARE(f.nav.planStops().first().toMap().value(QStringLiteral("label")).toString(),
             QStringLiteral("A"));
    QVERIFY(f.repo.getAll(QStringLiteral("settings")).isEmpty());
}

void NavigationHopTest::futureGeometryStartsAfterNextStop()
{
    Fixture f;
    f.nav.appendStop(52.51, 13.41, QStringLiteral("A"));
    f.nav.appendStop(52.52, 13.42, QStringLiteral("B"));
    f.nav.appendStop(52.53, 13.43, QStringLiteral("C"));
    QTRY_COMPARE_WITH_TIMEOUT(f.nav.stopCount(), 3, 3000);

    QList<Route> legs(3);
    legs[0].waypoints = {{52.50, 13.40}, {52.51, 13.41}};
    legs[1].waypoints = {{52.51, 13.41}, {52.515, 13.415}, {52.52, 13.42}};
    legs[2].waypoints = {{52.52, 13.42}, {52.53, 13.43}};
    QVERIFY(QMetaObject::invokeMethod(&f.nav, "onPlanPreviewReady", Qt::DirectConnection,
                                      Q_ARG(QList<Route>, legs)));
    QCOMPARE(f.nav.planGeometryWaypoints().first(), legs[0].waypoints.first());
    QCOMPARE(f.nav.futurePlanWaypoints(),
             (QList<LatLng>{{52.51, 13.41}, {52.515, 13.415},
                            {52.52, 13.42}, {52.53, 13.43}}));
}

void NavigationHopTest::clearWhilePlanLoadingCannotEraseExternalRoute()
{
    Fixture f;
    f.nav.appendStop(52.51, 13.41, QStringLiteral("queued"));
    f.nav.clearNavigation();
    QVERIFY(f.nav.errorMessage().contains(QStringLiteral("loading")));
    QTRY_COMPARE_WITH_TIMEOUT(f.nav.stopCount(), 1, 3000);
    QCOMPARE(snapshot(f.repo).value(QStringLiteral("stops")).toArray().size(), 1);
}

void NavigationHopTest::concurrentExternalAppendRetainsBothStops()
{
    Fixture f;
    f.nav.appendStop(52.51, 13.41, QStringLiteral("A"));
    external(f.repo, QStringLiteral("plan.append"),
             {{QStringLiteral("stop"), stop(52.52, 13.42, QStringLiteral("B"))}});
    QTRY_COMPARE_WITH_TIMEOUT(f.nav.stopCount(), 2, 3000);
    QCOMPARE(snapshot(f.repo).value(QStringLiteral("stops")).toArray().size(), 2);
}

void NavigationHopTest::staleProgressDoesNotAdvanceReplacement()
{
    Fixture f;
    gps(f, 52.50, 13.40);
    f.nav.setRoutePlan(QVariantList{QVariantMap{{QStringLiteral("lat"), 52.51},
        {QStringLiteral("lon"), 13.41}}});
    QTRY_COMPARE_WITH_TIMEOUT(f.nav.stopCount(), 1, 3000);
    const QString oldId = snapshot(f.repo).value(QStringLiteral("id")).toString();
    const QString oldStopId = snapshot(f.repo).value(QStringLiteral("stops")).toArray()
        .first().toObject().value(QStringLiteral("id")).toString();
    external(f.repo, QStringLiteral("plan.replace"),
             {{QStringLiteral("stops"), QJsonArray{stop(52.6, 13.5, QStringLiteral("new"))}}});
    external(f.repo, QStringLiteral("plan.reached"),
             {{QStringLiteral("expected_plan_id"), oldId},
              {QStringLiteral("expected_stop_id"), oldStopId}});
    external(f.repo, QStringLiteral("plan.advance"),
             {{QStringLiteral("expected_plan_id"), oldId},
              {QStringLiteral("expected_stop_id"), oldStopId}});
    QTRY_COMPARE_WITH_TIMEOUT(f.nav.destLatitude(), 52.6, 3000);
    QCOMPARE(f.nav.destAddress(), QStringLiteral("new"));
    QCOMPARE(f.nav.currentStep(), 0);
    QCOMPARE(snapshot(f.repo).value(QStringLiteral("stops")).toArray().size(), 1);
}

void NavigationHopTest::queuedArrivalCannotReachReplacement()
{
    Fixture f;
    gps(f, 52.50, 13.40);
    f.nav.appendStop(52.51, 13.41, QStringLiteral("old"));
    QTRY_COMPARE_WITH_TIMEOUT(f.nav.stopCount(), 1, 3000);
    Route route;
    route.waypoints = {{52.50, 13.40}, {52.51, 13.41}};
    RouteInstruction arrival;
    arrival.type = ManeuverType::Arrive;
    arrival.originalShapeIndex = 1;
    route.instructions = {arrival};
    route.distance = 100;
    route.duration = 60;
    f.nav.setRoute(route);
    gps(f, 52.51, 13.41); // queues a guarded plan.reached
    external(f.repo, QStringLiteral("plan.replace"),
             {{QStringLiteral("stops"), QJsonArray{stop(52.6, 13.5, QStringLiteral("new"))}}});
    QTRY_COMPARE_WITH_TIMEOUT(f.nav.destAddress(), QStringLiteral("new"), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(f.nav.errorMessage().contains(QStringLiteral("stale")), 3000);
    const QJsonObject plan = snapshot(f.repo);
    QVERIFY(!plan.value(QStringLiteral("stops")).toArray().first().toObject()
                 .value(QStringLiteral("reached")).toBool());
    QCOMPARE(f.nav.planState(), int(RoutePlanState::Navigating));
}

void NavigationHopTest::reachedStopAdvancesAndFinalArrivalRetainsOwner()
{
    Fixture f;
    gps(f, 52.50, 13.40);
    f.nav.setRoutePlan(QVariantList{
        QVariantMap{{QStringLiteral("lat"), 52.51}, {QStringLiteral("lon"), 13.41}},
        QVariantMap{{QStringLiteral("lat"), 52.52}, {QStringLiteral("lon"), 13.42}}});
    QTRY_COMPARE_WITH_TIMEOUT(f.nav.stopCount(), 2, 3000);
    // Owner progress from another client triggers the arrival prompt.
    const QJsonObject plan = snapshot(f.repo);
    external(f.repo, QStringLiteral("plan.reached"),
             {{QStringLiteral("expected_plan_id"), plan.value(QStringLiteral("id"))},
              {QStringLiteral("expected_stop_id"), plan.value(QStringLiteral("stops"))
                   .toArray().first().toObject().value(QStringLiteral("id"))}});
    QTRY_COMPARE_WITH_TIMEOUT(f.nav.planState(), int(RoutePlanState::AtStop), 3000);
    f.nav.confirmContinue();
    QTRY_COMPARE_WITH_TIMEOUT(f.nav.currentStep(), 1, 3000);
    QCOMPARE(f.nav.destLatitude(), 52.52);
    const QJsonObject next = snapshot(f.repo);
    external(f.repo, QStringLiteral("plan.reached"),
             {{QStringLiteral("expected_plan_id"), next.value(QStringLiteral("id"))},
              {QStringLiteral("expected_stop_id"), next.value(QStringLiteral("stops"))
                   .toArray().at(1).toObject().value(QStringLiteral("id"))}});
    QTRY_COMPARE_WITH_TIMEOUT(f.nav.planState(), int(RoutePlanState::Complete), 3000);
    QCOMPARE(snapshot(f.repo).value(QStringLiteral("stops")).toArray().size(), 2);
    QCOMPARE(f.nav.status(), int(NavigationStatus::Arrived));
}

void NavigationHopTest::moveJumpAndRemovePreserveTarget()
{
    Fixture f;
    f.nav.setRoutePlan(QVariantList{
        QVariantMap{{QStringLiteral("lat"), 52.51}, {QStringLiteral("lon"), 13.41}},
        QVariantMap{{QStringLiteral("lat"), 52.52}, {QStringLiteral("lon"), 13.42}},
        QVariantMap{{QStringLiteral("lat"), 52.53}, {QStringLiteral("lon"), 13.43}}}, 1);
    QTRY_COMPARE_WITH_TIMEOUT(f.nav.currentStep(), 1, 3000);
    QCOMPARE(f.nav.destLatitude(), 52.52);
    f.nav.moveStop(2, 0);
    QTRY_COMPARE_WITH_TIMEOUT(f.nav.currentStep(), 2, 3000);
    QCOMPARE(f.nav.destLatitude(), 52.52);
    f.nav.jumpToStop(0);
    QTRY_COMPARE_WITH_TIMEOUT(f.nav.currentStep(), 0, 3000);
    QCOMPARE(f.nav.destLatitude(), 52.53);
    f.nav.removeStop(1);
    QTRY_COMPARE_WITH_TIMEOUT(f.nav.stopCount(), 2, 3000);
    QCOMPARE(snapshot(f.repo).value(QStringLiteral("stops")).toArray().size(), 2);
}

void NavigationHopTest::insertStopPlacesBeforeActiveStop()
{
    Fixture f;
    f.nav.setRoutePlan(QVariantList{
        QVariantMap{{QStringLiteral("lat"), 52.51}, {QStringLiteral("lon"), 13.41}},
        QVariantMap{{QStringLiteral("lat"), 52.52}, {QStringLiteral("lon"), 13.42}},
        QVariantMap{{QStringLiteral("lat"), 52.53}, {QStringLiteral("lon"), 13.43}}}, 1);
    QTRY_COMPARE_WITH_TIMEOUT(f.nav.currentStep(), 1, 3000);
    f.nav.insertStop(52.55, 13.45, QStringLiteral("X"));
    QTRY_COMPARE_WITH_TIMEOUT(f.nav.stopCount(), 4, 3000);
    // The inserted stop becomes the next target; guidance still follows the
    // stop the plan was already on, which shifted down one.
    QTRY_COMPARE_WITH_TIMEOUT(f.nav.currentStep(), 2, 3000);
    QCOMPARE(f.nav.destLatitude(), 52.52);
    const QJsonArray stops = snapshot(f.repo).value(QStringLiteral("stops")).toArray();
    QCOMPARE(stops.size(), 4);
    QCOMPARE(stops.at(1).toObject().value(QStringLiteral("label")).toString(), QStringLiteral("X"));
}

void NavigationHopTest::rebootRestoresOwnerSnapshot()
{
    InMemoryMdbRepository repo;
    external(repo, QStringLiteral("plan.replace"),
             {{QStringLiteral("stops"), QJsonArray{stop(52.51, 13.41, QStringLiteral("A")),
                                                    stop(52.52, 13.42, QStringLiteral("B"))}},
              {QStringLiteral("start_step"), 1}});
    GpsStore gps(&repo); SpeedLimitStore speed(&repo); NavigationStore store(&repo);
    VehicleStore vehicle(&repo); SettingsStore settings(&repo);
    NavigationService nav(&gps, &store, &vehicle, &settings, &speed, &repo);
    gps.start(); store.start(); vehicle.start(); settings.start(); speed.start();
    repo.set(QStringLiteral("vehicle"), QStringLiteral("state"), QStringLiteral("ready-to-drive"));
    QTRY_COMPARE_WITH_TIMEOUT(nav.currentStep(), 1, 3000);
    QCOMPARE(nav.stopCount(), 2);
    QCOMPARE(nav.destLatitude(), 52.52);
    if (auto *client = nav.findChild<ValhallaClient *>()) client->cancelPending();
}

void NavigationHopTest::rebootCompletedPlanDoesNotRearmArrival()
{
    InMemoryMdbRepository repo;
    external(repo, QStringLiteral("plan.append"),
             {{QStringLiteral("stop"), stop(52.51, 13.41, QStringLiteral("A"))}});
    const QJsonObject current = snapshot(repo);
    external(repo, QStringLiteral("plan.reached"),
             {{QStringLiteral("expected_plan_id"), current.value(QStringLiteral("id"))},
              {QStringLiteral("expected_stop_id"), current.value(QStringLiteral("stops"))
                   .toArray().first().toObject().value(QStringLiteral("id"))}});
    GpsStore gpsStore(&repo); SpeedLimitStore speed(&repo); NavigationStore store(&repo);
    VehicleStore vehicle(&repo); SettingsStore settings(&repo);
    NavigationService nav(&gpsStore, &store, &vehicle, &settings, &speed, &repo);
    gpsStore.start(); store.start(); vehicle.start(); settings.start(); speed.start();
    QSignalSpy arrived(&nav, &NavigationService::arrived);
    repo.set(QStringLiteral("vehicle"), QStringLiteral("state"),
             QStringLiteral("ready-to-drive"));
    QTRY_COMPARE_WITH_TIMEOUT(nav.planState(), int(RoutePlanState::Complete), 3000);
    QCOMPARE(arrived.count(), 0);
    QVERIFY(!nav.hasRoute());
    QCOMPARE(snapshot(repo).value(QStringLiteral("stops")).toArray().size(), 1);
    nav.appendStop(52.52, 13.42, QStringLiteral("B"));
    QTRY_COMPARE_WITH_TIMEOUT(nav.planState(), int(RoutePlanState::AtStop), 3000);
    QCOMPARE(nav.stopCount(), 2);
}

void NavigationHopTest::appendAfterFinalArrivalReopensPrompt()
{
    Fixture f;
    f.nav.appendStop(52.51, 13.41, QStringLiteral("A"));
    QTRY_COMPARE_WITH_TIMEOUT(f.nav.stopCount(), 1, 3000);
    const QJsonObject plan = snapshot(f.repo);
    external(f.repo, QStringLiteral("plan.reached"),
             {{QStringLiteral("expected_plan_id"), plan.value(QStringLiteral("id"))},
              {QStringLiteral("expected_stop_id"), plan.value(QStringLiteral("stops"))
                   .toArray().first().toObject().value(QStringLiteral("id"))}});
    QTRY_COMPARE_WITH_TIMEOUT(f.nav.planState(), int(RoutePlanState::Complete), 3000);
    f.nav.appendStop(52.52, 13.42, QStringLiteral("B"));
    QTRY_COMPARE_WITH_TIMEOUT(f.nav.planState(), int(RoutePlanState::AtStop), 3000);
    QCOMPARE(f.nav.stopCount(), 2);
    f.nav.confirmContinue();
    QTRY_COMPARE_WITH_TIMEOUT(f.nav.currentStep(), 1, 3000);
}

void NavigationHopTest::keepStopIsDurableAndClearedOnDismount()
{
    Fixture f;
    f.nav.appendStop(52.51, 13.41, QStringLiteral("A"));
    f.nav.appendStop(52.52, 13.42, QStringLiteral("B"));
    QTRY_COMPARE_WITH_TIMEOUT(f.nav.stopCount(), 2, 3000);
    const QJsonObject plan = snapshot(f.repo);
    external(f.repo, QStringLiteral("plan.reached"),
             {{QStringLiteral("expected_plan_id"), plan.value(QStringLiteral("id"))},
              {QStringLiteral("expected_stop_id"), plan.value(QStringLiteral("stops"))
                   .toArray().first().toObject().value(QStringLiteral("id"))}});
    QTRY_COMPARE_WITH_TIMEOUT(f.nav.planState(), int(RoutePlanState::AtStop), 3000);
    f.nav.keepCurrentStop();
    QTRY_VERIFY_WITH_TIMEOUT(snapshot(f.repo).value(QStringLiteral("keep_current_stop")).toBool(), 3000);
    QCOMPARE(f.nav.planState(), int(RoutePlanState::Navigating));
    f.repo.set(QStringLiteral("vehicle"), QStringLiteral("state"), QStringLiteral("stand-by"));
    QTRY_VERIFY_WITH_TIMEOUT(!snapshot(f.repo).value(QStringLiteral("keep_current_stop")).toBool(), 3000);
}

void NavigationHopTest::unavailableOwnerReportsErrorWithoutBlocking()
{
    class UnavailableRepo : public InMemoryMdbRepository {
    public:
        void push(const QString &channel, const QString &command) override {
            if (channel != QLatin1String("settings:route-plan"))
                InMemoryMdbRepository::push(channel, command);
        }
    } repo;
    GpsStore gps(&repo); SpeedLimitStore speed(&repo); NavigationStore store(&repo);
    VehicleStore vehicle(&repo); SettingsStore settings(&repo);
    NavigationService nav(&gps, &store, &vehicle, &settings, &speed, &repo);
    gps.start(); store.start(); vehicle.start(); settings.start(); speed.start();
    QElapsedTimer timer;
    timer.start();
    nav.appendStop(52.51, 13.41);
    QVERIFY(timer.elapsed() < 100);
    QTRY_COMPARE_WITH_TIMEOUT(nav.status(), int(NavigationStatus::Error), 7500);
    QVERIFY(nav.errorMessage().contains(QStringLiteral("timed out")));
    QVERIFY(!nav.hasPlan());
    QVERIFY(repo.get(QStringLiteral("navigation"), QStringLiteral("plan")).isEmpty());
}

// A cold boot can issue the startup plan.get before settings-service has
// registered settings:route-plan. That restore is background work, so it must
// retry quietly; only a rider-initiated request may report a timeout.
void NavigationHopTest::unavailableOwnerRestoreStaysSilent()
{
    class UnavailableRepo : public InMemoryMdbRepository {
    public:
        void push(const QString &channel, const QString &command) override {
            if (channel != QLatin1String("settings:route-plan"))
                InMemoryMdbRepository::push(channel, command);
        }
    } repo;
    GpsStore gps(&repo); SpeedLimitStore speed(&repo); NavigationStore store(&repo);
    VehicleStore vehicle(&repo); SettingsStore settings(&repo);
    NavigationService nav(&gps, &store, &vehicle, &settings, &speed, &repo);
    gps.start(); store.start(); vehicle.start(); settings.start(); speed.start();
    // Long enough for the first 3s RPC timeout and the scheduled retry.
    QTest::qWait(4500);
    QVERIFY(!nav.errorMessage().contains(QStringLiteral("timed out")));
    QVERIFY(nav.status() != int(NavigationStatus::Error));
    QVERIFY(!nav.hasPlan());
}

void NavigationHopTest::clearDoesNotRestoreLegacySettings()
{
    Fixture f;
    f.repo.set(QStringLiteral("settings"), QStringLiteral("dashboard.route-plan.0.latitude"),
               QStringLiteral("52.51"));
    f.nav.appendStop(52.52, 13.42);
    QTRY_COMPARE_WITH_TIMEOUT(f.nav.stopCount(), 1, 3000);
    f.nav.clearNavigation();
    QTRY_VERIFY_WITH_TIMEOUT(!f.nav.hasPlan(), 3000);
    QCOMPARE(snapshot(f.repo).value(QStringLiteral("stops")).toArray().size(), 0);
}

QTEST_MAIN(NavigationHopTest)
#include "NavigationHopTest.moc"
