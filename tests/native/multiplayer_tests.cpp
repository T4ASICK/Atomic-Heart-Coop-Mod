#include "ghost/core/protocol.hpp"
#include "ghost/core/interpolation.hpp"
#include <iostream>
#include <stdexcept>

void check(bool result, const char* message)
{
    if (!result)
        throw std::runtime_error(message);
}

int main()
{
    try
    {
        using namespace ghost;
        std::string rows;
        for (std::size_t i = 0; i < max_players - 1; ++i)
        {
            rows += "p" + std::to_string(i) + "\tSameName\ts\t1\t/Game/Test\t0\t0\t90\t0\t0\t0\t0";
            for (int column = 0; column < 25; ++column)
                rows += "\t0";
            rows += '\n';
        }
        auto roster = parse_roster("AHGR3\tb\t1\t7\n" + rows);
        check(roster && roster->players.size() == 7, "seven remote players accepted");
        check(!parse_roster("AHGR3\tb\t1\t8\n" + rows), "eight remotes rejected");
        auto duplicates = rows;
        duplicates.replace(duplicates.find("p1"), 2, "p0");
        check(!parse_roster("AHGR3\tb\t1\t7\n" + duplicates), "duplicate identities rejected");
        RosterFeed feed;
        feed.push(*roster, 1.0);
        auto next = *roster;
        next.seq = 2;
        std::reverse(next.players.begin(), next.players.end());
        next.players[3].seq = 2;
        next.players[3].position.x = 100;
        feed.push(next, 1.5);
        for (const auto& player : feed.roster.players)
            check(player.received_at == (player.id == "p3" ? 1.5 : 1.0),
                  "freshness belongs to peer identity, independently of roster order");
        next = feed.roster;
        next.seq = 3;
        next.players[3].seq = 1;
        next.players[3].position.x = 999;
        feed.push(next, 1.7);
        check(feed.roster.players[3].seq == 2 && feed.roster.players[3].position.x == 100,
              "reordering cannot roll back one peer");
        Track stale, fresh;
        stale.push(feed.roster.players[0], feed.roster.players[0].received_at);
        fresh.push(feed.roster.players[3], feed.roster.players[3].received_at);
        check(!stale.evaluate(3.1) && fresh.evaluate(3.1).has_value(),
              "active peer cannot keep a frozen peer alive");
        next = feed.roster;
        next.seq = 4;
        next.players[0].world = "/Game/Other";
        next.players[0].seq = 1;
        feed.push(next, 3.2);
        check(feed.roster.players[0].received_at == 3.2 &&
                  feed.roster.players[1].received_at == 1.0,
              "world transitions reset only the affected peer");
        auto empty = feed.roster;
        empty.seq = 5;
        empty.players.clear();
        feed.push(empty, 3.3);
        next.seq = 6;
        feed.push(next, 3.4);
        check(feed.roster.players[1].received_at == 1.0,
              "leave and reappear cannot refresh an unchanged peer sample");
        std::cout << "Eight-player protocol and independent sample freshness tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
