#include "bonjour_discovery.h"

#include <QSocketNotifier>
#include <QTimer>

#ifdef BEAMER_HAVE_SYSTEM_DNSSD
#include <dns_sd.h>
#include <netinet/in.h>

class BonjourDiscovery::Impl final : public QObject {
public:
    struct ResolveOperation final : QObject {
        DNSServiceRef ref = nullptr;
        QSocketNotifier *notifier = nullptr;
        Impl *owner = nullptr;
        QString name;
        quint16 port = 0;

        ~ResolveOperation() override {
            if (ref) DNSServiceRefDeallocate(ref);
        }
    };

    explicit Impl(BonjourDiscovery *q) : QObject(q) {}
    ~Impl() override {
        if (browseRef) DNSServiceRefDeallocate(browseRef);
    }

    static void browseReply(DNSServiceRef, DNSServiceFlags flags, uint32_t interfaceIndex,
                            DNSServiceErrorType error, const char *serviceName,
                            const char *regtype, const char *domain, void *context) {
        auto *self = static_cast<Impl *>(context);
        if (error != kDNSServiceErr_NoError) {
            self->lastError = QStringLiteral("Bonjour browse failed (%1)").arg(error);
            return;
        }
        if (!(flags & kDNSServiceFlagsAdd)) return;

        auto *op = new ResolveOperation;
        op->owner = self;
        op->name = QString::fromUtf8(serviceName);
        const auto result = DNSServiceResolve(&op->ref, 0, interfaceIndex, serviceName,
                                               regtype, domain, &Impl::resolveReply, op);
        if (result != kDNSServiceErr_NoError) {
            self->lastError = QStringLiteral("Bonjour resolve failed (%1)").arg(result);
            delete op;
            return;
        }
        self->watch(op);
    }

    static void resolveReply(DNSServiceRef, DNSServiceFlags, uint32_t interfaceIndex,
                             DNSServiceErrorType error, const char *, const char *hostTarget,
                             uint16_t port, uint16_t, const unsigned char *, void *context) {
        auto *op = static_cast<ResolveOperation *>(context);
        if (error != kDNSServiceErr_NoError) {
            op->owner->lastError = QStringLiteral("Bonjour service resolution failed (%1)").arg(error);
            op->deleteLater();
            return;
        }
        op->port = ntohs(port);
        op->notifier->setEnabled(false);
        DNSServiceRefDeallocate(op->ref);
        op->ref = nullptr;
        const auto result = DNSServiceGetAddrInfo(&op->ref, 0, interfaceIndex,
                                                  kDNSServiceProtocol_IPv4, hostTarget,
                                                  &Impl::addressReply, op);
        if (result != kDNSServiceErr_NoError) {
            op->owner->lastError = QStringLiteral("Bonjour address lookup failed (%1)").arg(result);
            op->deleteLater();
            return;
        }
        op->notifier->setSocket(DNSServiceRefSockFD(op->ref));
        op->notifier->setEnabled(true);
    }

    static void addressReply(DNSServiceRef, DNSServiceFlags, uint32_t,
                             DNSServiceErrorType error, const char *,
                             const sockaddr *address, uint32_t, void *context) {
        auto *op = static_cast<ResolveOperation *>(context);
        if (error == kDNSServiceErr_NoError && address && address->sa_family == AF_INET) {
            const auto *ipv4 = reinterpret_cast<const sockaddr_in *>(address);
            const QHostAddress host(ntohl(ipv4->sin_addr.s_addr));
            if (!host.isNull() && op->owner->handler)
                op->owner->handler({op->name, host, op->port});
        } else if (error != kDNSServiceErr_NoError) {
            op->owner->lastError = QStringLiteral("Bonjour address lookup failed (%1)").arg(error);
        }
        op->deleteLater();
    }

    void watch(ResolveOperation *op) {
        op->notifier = new QSocketNotifier(DNSServiceRefSockFD(op->ref), QSocketNotifier::Read, op);
        connect(op->notifier, &QSocketNotifier::activated, op, [op] {
            op->notifier->setEnabled(false);
            const auto error = DNSServiceProcessResult(op->ref);
            if (error != kDNSServiceErr_NoError) {
                op->owner->lastError = QStringLiteral("Bonjour processing failed (%1)").arg(error);
                op->deleteLater();
            } else if (op->ref) {
                op->notifier->setEnabled(true);
            }
        });
    }

    bool start(ServiceHandler newHandler, int timeoutMs) {
        handler = std::move(newHandler);
        const auto result = DNSServiceBrowse(&browseRef, 0, kDNSServiceInterfaceIndexAny,
                                             "_airplay._tcp", "local.",
                                             &Impl::browseReply, this);
        if (result != kDNSServiceErr_NoError) {
            lastError = QStringLiteral("Could not start Bonjour browse (%1)").arg(result);
            return false;
        }
        browseNotifier = new QSocketNotifier(DNSServiceRefSockFD(browseRef), QSocketNotifier::Read, this);
        connect(browseNotifier, &QSocketNotifier::activated, this, [this] {
            browseNotifier->setEnabled(false);
            const auto error = DNSServiceProcessResult(browseRef);
            if (error != kDNSServiceErr_NoError)
                lastError = QStringLiteral("Bonjour processing failed (%1)").arg(error);
            else if (browseRef)
                browseNotifier->setEnabled(true);
        });
        QTimer::singleShot(timeoutMs, this, [this] {
            if (browseNotifier) browseNotifier->setEnabled(false);
            if (browseRef) { DNSServiceRefDeallocate(browseRef); browseRef = nullptr; }
        });
        return true;
    }

    DNSServiceRef browseRef = nullptr;
    QSocketNotifier *browseNotifier = nullptr;
    ServiceHandler handler;
    QString lastError;
};

#else

class BonjourDiscovery::Impl final {
public:
    explicit Impl(BonjourDiscovery *) {}
    bool start(ServiceHandler, int) { return false; }
    QString lastError = QStringLiteral("System DNS-SD discovery is unavailable in this build");
};

#endif

BonjourDiscovery::BonjourDiscovery(QObject *parent) : QObject(parent), impl(new Impl(this)) {}
BonjourDiscovery::~BonjourDiscovery() { delete impl; }

bool BonjourDiscovery::startAirPlayBrowse(ServiceHandler handler, int timeoutMs) {
    return impl->start(std::move(handler), timeoutMs);
}

QString BonjourDiscovery::errorString() const { return impl->lastError; }
