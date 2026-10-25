#pragma once

#include <QHostAddress>
#include <QObject>
#include <QString>

#include <functional>

// Browse through the host DNS-SD daemon (Bonjour or Avahi), obtaining its
// cached, per-interface view of the LAN rather than opening another mDNS socket.
class BonjourDiscovery final : public QObject {
public:
    struct Service {
        QString name;
        QHostAddress address;
        quint16 port = 0;
    };

    using ServiceHandler = std::function<void(const Service &)>;

    explicit BonjourDiscovery(QObject *parent = nullptr);
    ~BonjourDiscovery() override;

    bool startAirPlayBrowse(ServiceHandler handler, int timeoutMs = 7000);
    QString errorString() const;

private:
    class Impl;
    Impl *impl;
};
