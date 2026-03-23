#include <iostream>
#include <vector>
#include <algorithm>
#include <random>
using namespace std;

class Player {
public:
    int id;
    string name;

    Player(int id, string name) {
        this->id = id;
        this->name = name;
    }
};

class Tournament {
private:
    vector<Player> players;
    vector<Player> eliminated;
    int pot = 0;
    int entryFee;

public:
    Tournament(int fee) {
        entryFee = fee;
    }

    void registerPlayers() {
        int n;
        cout << "\n";
        cout << "  ================================\n";
        cout << "    PLAYER REGISTRATION\n";
        cout << "  ================================\n";
        cout << "\n";
        cout << "  Enter number of players: ";
        cin >> n;
        cout << "\n";

        for (int i = 0; i < n; i++) {
            string name;
            cout << "  Player " << i + 1 << " name: ";
            cin >> name;
            cout << "\n";

            // Keep IDs stable even after shuffling.
            players.push_back(Player(i + 1, name));
        }

        // Everyone pays the same entry fee.
        pot = n * entryFee;
        cout << "\n";
        cout << "  ================================\n";
        cout << "  Total Pot: $" << pot << "\n";
        cout << "  ================================\n";
        cout << "\n\n";
    }

    void shufflePlayers() {
        random_device rd;
        mt19937 g(rd());
        // Randomize pairings before round 1.
        shuffle(players.begin(), players.end(), g);
    }

    Player playMatch(Player p1, Player p2) {
        random_device rd;
        mt19937 gen(rd());
        uniform_int_distribution<> dist(0, 1);

        int r = dist(gen);

        Player winner = (r == 0) ? p1 : p2;
        Player loser = (r == 0) ? p2 : p1;

        cout << "      " << p1.name << " vs " << p2.name
             << " -> Winner: " << winner.name << "\n\n";

        // Track elimination order for final ranking output.
        eliminated.push_back(loser);
        return winner;
    }

    void startTournament() {
        shufflePlayers();

        int round = 1;

        while (players.size() > 1) {
            cout << "\n\n";
            cout << "  ================================\n";
            cout << "          ROUND " << round << "\n";
            cout << "  ================================\n";
            cout << "\n";

            vector<Player> winners;

            for (size_t i = 0; i < players.size(); i += 2) {
                if (i + 1 < players.size()) {
                    winners.push_back(playMatch(players[i], players[i + 1]));
                } else {
                    // Odd player count: last player advances automatically.
                    cout << "      " << players[i].name << " (BYE)\n\n";
                    winners.push_back(players[i]);
                }
            }

            // Winners become the next round's player pool.
            players = winners;
            round++;
        }

        // Final Winner
        cout << "\n\n";
        cout << "  ================================\n";
        cout << "    TOURNAMENT CHAMPION\n";
        cout << "  ================================\n";
        cout << "\n  Winner: " << players[0].name << "\n";
        cout << "\n  ================================\n";
        cout << "\n\n";

        showRanking(players[0]);
        distributePot(players[0]);
    }

    void showRanking(Player winner) {
        cout << "  ================================\n";
        cout << "       FINAL RANKINGS\n";
        cout << "  ================================\n";
        cout << "\n";

        int rank = 1;
        cout << "    " << rank++ << ". " << winner.name << " (CHAMPION)\n";
        cout << "\n";

        // Last eliminated player is the runner-up, so reverse for ranking.
        reverse(eliminated.begin(), eliminated.end());

        for (auto &p : eliminated) {
            cout << "    " << rank++ << ". " << p.name << "\n";
        }
        cout << "\n";
    }

    void distributePot(Player winner) {
        cout << "  ================================\n";
        cout << "      POT DISTRIBUTION\n";
        cout << "  ================================\n";
        cout << "\n";
        // Basic winner-takes-all payout.
        cout << "  Total Pot: $" << pot << "\n";
        cout << "  Winner Prize ($" << pot << "): " << winner.name << "\n";
        cout << "\n  ================================\n";
        cout << "\n\n";
    }
};

int main() {
    int entryFee;

    cout << "\n\n";
    cout << "  ================================\n";
    cout << "      TOURNAMENT SYSTEM\n";
    cout << "  ================================\n";
    cout << "\n\n";
    cout << "  Entry fee per player: $";
    cin >> entryFee;
    cout << "\n";

    Tournament t(entryFee);

    t.registerPlayers();
    t.startTournament();

    return 0;
}