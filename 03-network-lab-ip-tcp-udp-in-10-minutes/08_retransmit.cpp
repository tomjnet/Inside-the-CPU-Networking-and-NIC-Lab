// IP, TCP and UDP: What Actually Happens to a Packet - slide 8: sequence numbers, acks and retransmission
// Build: make 08_retransmit
#include <cstdint>
#include <cstdio>
#include <iostream>

struct Receiver {
    std::uint32_t next;        // the next byte of the stream it expects
    std::uint32_t delivered;   // bytes handed to the application, in order
};

// receiver: the cumulative ACK is the next byte it expects
std::uint32_t on_segment(Receiver& r, std::uint32_t seq,
                         std::uint32_t len) {
    if (seq == r.next) {          // in order: deliver to the app
        r.next += len;
        r.delivered += len;
    }                             // a gap: keep asking for r.next
    return r.next;                // the ACK number that goes back
}
// sender: no ACK beyond seq when the timer fires = send it again
// cost of one loss: at least one round trip, often a timeout

struct Result {
    int rounds;
    std::uint32_t sent;
    std::uint32_t delivered;
};

// A deliberately small sender: a fixed window of 4 segments per round trip, and after a loss it resends from the
// first byte that was not acknowledged. A real stack keeps the out of order segments (SACK) and resends only the hole.
static Result transfer(bool lose_one) {
    const std::uint32_t first = 1000, len = 1000, segments = 8, window = 4;
    const std::uint32_t end = first + segments * len;
    Receiver rx{first, 0};
    Result res{0, 0, 0};
    std::uint32_t acked = first;          // everything below this is confirmed
    bool lost_already = !lose_one;

    while (acked < end) {
        ++res.rounds;
        std::printf("  round trip %d\n", res.rounds);
        std::uint32_t best_ack = acked;
        std::uint32_t dup_acks = 0;
        for (std::uint32_t i = 0; i < window; ++i) {
            const std::uint32_t seq = acked + i * len;
            if (seq >= end) break;
            res.sent += len;
            if (seq == 3000 && !lost_already) {           // the network drops this one, once
                lost_already = true;
                std::printf("    send seq=%u len=%u   LOST on the way\n", static_cast<unsigned>(seq),
                            static_cast<unsigned>(len));
                continue;
            }
            const std::uint32_t ack = on_segment(rx, seq, len);
            if (ack == best_ack) ++dup_acks;
            if (ack > best_ack) best_ack = ack;
            std::printf("    send seq=%u len=%u   ack=%u%s\n", static_cast<unsigned>(seq), static_cast<unsigned>(len),
                        static_cast<unsigned>(ack), ack <= seq ? "   duplicate ACK: a gap, segment not delivered" : "");
        }
        if (dup_acks > 0)
            std::printf("    sender: ACK stuck at %u, retransmit from there\n", static_cast<unsigned>(best_ack));
        acked = best_ack;
    }
    res.delivered = rx.delivered;
    return res;
}

int main() {
    std::cout << "8 segments of 1000 bytes, window of 4 segments, no loss:\n";
    const Result clean = transfer(false);
    std::cout << "the same transfer, the segment with seq=3000 is lost once:\n";
    const Result lossy = transfer(true);

    std::printf("no loss:  %d round trips, %u bytes sent, %u delivered in order\n", clean.rounds,
                static_cast<unsigned>(clean.sent), static_cast<unsigned>(clean.delivered));
    std::printf("one loss: %d round trips, %u bytes sent, %u delivered in order\n", lossy.rounds,
                static_cast<unsigned>(lossy.sent), static_cast<unsigned>(lossy.delivered));
    std::cout << "the application saw every byte, in order, both times: the loss only shows up as latency\n";

    const bool ok = clean.delivered == 8000 && lossy.delivered == 8000 && lossy.rounds > clean.rounds;
    return ok ? 0 : 1;
}
