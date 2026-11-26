#pragma once

// Spec 048: the inbound HTTP connections the device has open, and the gate
// that turns new ones away before anything is allocated for them.
//
// Every connection the web server's listener takes (Ipv6Network::listen)
// passes here first. One taken is counted while its request object lives (a
// WebSocket until its upgrade); one turned away (ConnectionBudget::inbound(),
// tighter for a while after the network comes back) is answered a fixed 503
// with Retry-After and closed: no request object, no parsing, no reply built.

#include <cstddef>
#include <cstdint>

class AsyncClient;

namespace SQM
{
    namespace InboundGate
    {
        // Held by a taken connection's request for its lifetime: copies share
        // one count, released with the last.
        class Ticket
        {
        public:
            Ticket();
            Ticket(const Ticket &other);
            Ticket &operator=(const Ticket &) = delete;
            ~Ticket();

        private:
            size_t slot;
        };

        // The listener's accept callback (async_tcp task): false when the
        // connection was answered busy and closed (`client` is then deleted).
        bool admit(AsyncClient *client, uint32_t largestBlock);

        // Any task.
        size_t live();            // taken, still open (10 s at most each)
        uint32_t refused();       // turned away since start
        uint32_t lastArrivalMs(); // millis() of the last connection (0: none yet)
    } // namespace InboundGate
} // namespace SQM
