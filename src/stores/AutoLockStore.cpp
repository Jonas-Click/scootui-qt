#include "AutoLockStore.h"

#include <QDateTime>

AutoLockStore::AutoLockStore(MdbRepository *repo, QObject *parent)
    : SyncableStore(repo, parent)
{
    // The hold countdown is only a few seconds long, so tick faster than the
    // idle auto-standby store: at 1 Hz a spent deadline would linger on screen
    // for almost a full second after the scooter has already locked.
    m_tickTimer.setInterval(250);
    m_tickTimer.setSingleShot(false);
    connect(&m_tickTimer, &QTimer::timeout, this, &AutoLockStore::recomputeRemaining);
}

SyncSettings AutoLockStore::syncSettings() const
{
    return SyncSettings{
        QStringLiteral("vehicle"), 500,
        {
            // Unix timestamp (seconds) when the handlebar hold countdown
            // fires; cleared when no countdown is active. Published by
            // vehicle-service via PublishAutoLockDeadline.
            {QStringLiteral("deadline"), QStringLiteral("auto-lock-deadline"), /*clearable=*/true},
        },
        {}, {}
    };
}

void AutoLockStore::applyFieldUpdate(const QString &variable, const QString &value)
{
    if (variable != QLatin1String("auto-lock-deadline"))
        return;

    const qint64 newDeadline = value.isEmpty() ? 0 : value.toLongLong();
    if (newDeadline != m_deadline) {
        m_deadline = newDeadline;
        emit deadlineChanged();
    }

    recomputeRemaining();

    if (m_deadline > 0) {
        if (!m_tickTimer.isActive())
            m_tickTimer.start();
    } else {
        if (m_tickTimer.isActive())
            m_tickTimer.stop();
    }
}

void AutoLockStore::recomputeRemaining()
{
    int newRemaining = 0;
    if (m_deadline > 0) {
        const qint64 nowSec = QDateTime::currentSecsSinceEpoch();
        const qint64 diff = m_deadline - nowSec;
        newRemaining = diff > 0 ? static_cast<int>(diff) : 0;
    }

    if (newRemaining != m_remainingSeconds) {
        m_remainingSeconds = newRemaining;
        emit remainingSecondsChanged();
    }

    // Stop the tick when we've reached zero — no need to keep firing.
    if (m_remainingSeconds == 0 && m_tickTimer.isActive())
        m_tickTimer.stop();
}
