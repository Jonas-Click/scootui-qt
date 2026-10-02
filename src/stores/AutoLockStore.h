#pragma once

#include "SyncableStore.h"

// Handlebar hold-to-lock countdown. Mirrors AutoStandbyStore, but tracks the
// short countdown vehicle-service arms while the rider holds the handlebar at
// full left, which is separate from the long idle auto-standby deadline.
class AutoLockStore : public SyncableStore
{
    Q_OBJECT
    Q_PROPERTY(qint64 deadline READ deadline NOTIFY deadlineChanged)
    Q_PROPERTY(int remainingSeconds READ remainingSeconds NOTIFY remainingSecondsChanged)

public:
    explicit AutoLockStore(MdbRepository *repo, QObject *parent = nullptr);

    qint64 deadline() const { return m_deadline; }
    int remainingSeconds() const { return m_remainingSeconds; }

signals:
    void deadlineChanged();
    void remainingSecondsChanged();

protected:
    SyncSettings syncSettings() const override;
    void applyFieldUpdate(const QString &variable, const QString &value) override;

private:
    void recomputeRemaining();

    qint64 m_deadline = 0;       // Unix seconds; 0 = no countdown active
    int m_remainingSeconds = 0;  // max(0, deadline - now); 0 when inactive
    QTimer m_tickTimer;          // fast tick while a countdown is active
};
