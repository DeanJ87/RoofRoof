#include "InboundGate.h"

#include <Arduino.h>
#include <AsyncTCP.h>

#include <atomic>

#include "NetState.h"
#include "NetworkReturn.h"
#include "ReplyLedger.h"

namespace SQM
{
    namespace InboundGate
    {
        namespace
        {
            // A request's whole life is well under a second; one open for
            // ReplyLedger::STALE_MS is stuck and stops counting, so a leaked
            // request can never hold the gate shut.
            HeapSafe::ReplyLedger ledger;
            std::atomic<uint8_t> holders[HeapSafe::ReplyLedger::SLOTS] = {};
            std::atomic<uint32_t> refusedCount{0};
            std::atomic<uint32_t> lastArrival{0};

            constexpr char BUSY[] = "HTTP/1.1 503 Service Unavailable\r\n"
                                    "Content-Type: application/json\r\n"
                                    "Retry-After: 2\r\n"
                                    "Connection: close\r\n"
                                    "Content-Length: 41\r\n"
                                    "\r\n"
                                    "{\"error\":\"The device is busy: try again\"}";
            static_assert(sizeof("{\"error\":\"The device is busy: try again\"}") - 1 == 41, "Content-Length");
        } // namespace

        Ticket::Ticket()
            : slot(ledger.open(millis()))
        {
            if (slot != HeapSafe::ReplyLedger::NO_SLOT)
                holders[slot].store(1);
        }

        Ticket::Ticket(const Ticket &other)
            : slot(other.slot)
        {
            if (slot != HeapSafe::ReplyLedger::NO_SLOT)
                holders[slot].fetch_add(1);
        }

        Ticket::~Ticket()
        {
            if (slot != HeapSafe::ReplyLedger::NO_SLOT && holders[slot].fetch_sub(1) == 1)
                ledger.close(slot);
        }

        bool admit(AsyncClient *client, uint32_t largestBlock)
        {
            const uint32_t now = millis();
            lastArrival.store(now | 1); // 0 means none yet
            if (ConnectionBudget::inbound(live(), largestBlock, NetState::phase()) == ConnectionBudget::Inbound::Accept)
                return true;
            ++refusedCount;
            // Copied into one segment and closed after it: the 300 B or so of
            // a PCB until the peer's FIN, instead of a request and its reply.
            client->write(BUSY, sizeof(BUSY) - 1);
            client->close();
            delete client;
            return false;
        }

        size_t live()
        {
            return ledger.sending(millis());
        }

        uint32_t refused()
        {
            return refusedCount.load();
        }

        uint32_t lastArrivalMs()
        {
            return lastArrival.load();
        }
    } // namespace InboundGate
} // namespace SQM
