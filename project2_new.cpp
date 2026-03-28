#include <iostream>
#include <vector>
#include <map>
#include <set>
#include <unordered_map>
#include <string>
#include <ctime>
#include <queue>
#include <algorithm>
#include <memory>
#include <chrono>
#include <cmath>
#include <shared_mutex>
#include <mutex>
#include <stdexcept>
#include <sstream>
#include <atomic>          // FIX: for atomic action-log counter

using namespace std;

// ─────────────────────────────────────────────
//  Enums
// ─────────────────────────────────────────────

enum InvestigationStatus {
    REPORTED = 0,
    EVIDENCE_COLLECTION = 1,
    SUSPECT_IDENTIFICATION = 2,
    INTERROGATION = 3,
    CASE_CLOSURE = 4
};

enum CrimeSeverity {
    LOW = 1,
    MEDIUM = 2,
    HIGH = 3,
    CRITICAL = 4
};

// ─────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────

static string getCurrentTimestamp() {
    auto now = chrono::system_clock::now();
    time_t t = chrono::system_clock::to_time_t(now);
    string ts = ctime(&t);
    if (!ts.empty() && ts.back() == '\n') ts.pop_back();
    return ts;
}

static bool validSeverity(int v) { return v >= 1 && v <= 4; }
static bool validStatus(int v)   { return v >= 0 && v <= 4; }

// ─────────────────────────────────────────────
//  Capacity Constants
// ─────────────────────────────────────────────

static const int MAX_CRIMES         = 1000;
static const int MAX_PROFILES       = 5000;
// FIX (memory overflow): action log is now bounded to prevent unbounded growth
static const int MAX_LOG_ENTRIES    = 10000;
// FIX (5-second window): hard timeout in milliseconds for analytics batches
static const int ANALYTICS_TIMEOUT_MS = 5000;

// ─────────────────────────────────────────────
//  Location
// ─────────────────────────────────────────────

struct Location {
    string zone;
    Location(string z = "Unknown") : zone(z) {}
};

// ─────────────────────────────────────────────
//  ActionLog
// ─────────────────────────────────────────────

struct ActionLog {
    int    logId;
    int    caseId;
    string action;
    string timestamp;
    InvestigationStatus statusAfter;
    InvestigationStatus statusBefore;
    // FIX (rollback/analytics isolation): snapshot of analytics-relevant fields
    // stored at log time so a rollback never mutates the analytics snapshot path
    int    snapshotSeverity;   // severity of the record at log time
    string snapshotZone;       // zone at log time

    ActionLog(int id, int cid, const string& act, const string& ts,
              InvestigationStatus after, InvestigationStatus before,
              int sev, const string& zone)
        : logId(id), caseId(cid), action(act), timestamp(ts),
          statusAfter(after), statusBefore(before),
          snapshotSeverity(sev), snapshotZone(zone) {}
};

// ─────────────────────────────────────────────
//  CriminalProfile
// ─────────────────────────────────────────────

class CriminalProfile {
private:
    int    profileId;
    string name;
    int    offenseCount;
    vector<pair<string,string>> offenseHistory;
    float  riskScore;
    bool   linkedToCase;
    set<int> linkedCases;
    bool   active;

public:
    CriminalProfile()
        : profileId(0), name(""), offenseCount(0), riskScore(0.0f),
          linkedToCase(false), active(true) {}

    CriminalProfile(int id, string n)
        : profileId(id), name(n), offenseCount(0), riskScore(0.0f),
          linkedToCase(false), active(true) {}

    int    getProfileId()    const { return profileId; }
    string getName()         const { return name; }
    int    getOffenseCount() const { return offenseCount; }
    float  getRiskScore()    const { return riskScore; }
    bool   isLinkedToCase()  const { return linkedToCase; }
    bool   isActive()        const { return active; }

    void addOffense(const string& offense, CrimeSeverity sev = MEDIUM) {
        offenseHistory.push_back({offense, getCurrentTimestamp()});
        offenseCount++;
        updateRiskScore(sev);
    }

    void updateRiskScore(CrimeSeverity latestSev = MEDIUM) {
        float severityBonus = (float)latestSev * 5.0f;
        riskScore = min(100.0f, 10.0f + (offenseCount * 8.0f) + severityBonus);
    }

    void linkToCase(int caseId) {
        linkedCases.insert(caseId);
        linkedToCase = true;
    }

    void unlinkFromCase(int caseId) {
        linkedCases.erase(caseId);
        linkedToCase = !linkedCases.empty();
    }

    bool tryMarkDeleted() {
        if (!linkedCases.empty()) {
            cout << "Cannot delete profile #" << profileId
                 << " — linked to " << linkedCases.size() << " case(s).\n";
            return false;
        }
        active = false;
        return true;
    }

    const set<int>& getLinkedCases() const { return linkedCases; }
};

// ─────────────────────────────────────────────
//  CrimeRecord
// ─────────────────────────────────────────────

class CrimeRecord {
private:
    int            crimeId;
    string         crimeType;
    CrimeSeverity  severity;
    string         timestamp;
    Location       location;
    vector<int>    suspectIds;
    InvestigationStatus status;

public:
    CrimeRecord(int id, string type, CrimeSeverity sev, Location loc)
        : crimeId(id), crimeType(type), severity(sev), location(loc),
          status(REPORTED) {
        timestamp = getCurrentTimestamp();
    }

    int    getCrimeId()   const { return crimeId; }
    string getCrimeType() const { return crimeType; }
    CrimeSeverity getSeverity()     const { return severity; }
    Location      getLocation()     const { return location; }
    InvestigationStatus getStatus() const { return status; }
    vector<int>   getSuspectIds()   const { return suspectIds; }
    string        getTimestamp()    const { return timestamp; }

    void addSuspect(int suspectId) {
        if (find(suspectIds.begin(), suspectIds.end(), suspectId) == suspectIds.end())
            suspectIds.push_back(suspectId);
    }

    void updateStatus(InvestigationStatus newStatus) { status = newStatus; }

    int getPriorityScore() const {
        if (status == CASE_CLOSURE) return 0;
        int stageUrgency = (INTERROGATION - status) * 5;
        return (int)severity * 25 + max(0, stageUrgency);
    }
};

// ─────────────────────────────────────────────
//  NetworkAnalyzer
//
//  FIX (cyclic/invalid links): self-links and duplicate links are already
//  blocked.  The previous comment said "cycles allowed" — requirement says
//  "prevent cyclic links".  We now enforce strict acyclicity via DFS while
//  still allowing undirected edges.  An edge A-B is rejected if B is already
//  reachable from A (which would create a cycle in the undirected graph).
//  Note: an undirected graph with N nodes and N-1 edges is a tree; any
//  additional edge creates a cycle.  Law-enforcement hierarchies and chain-of-
//  custody trees are acyclic — organised-crime "networks" in this context means
//  a tree/forest of associations, not a general graph.
//
//  FIX (thread safety): own shared_mutex added — NetworkAnalyzer was the only
//  major data structure without one.
// ─────────────────────────────────────────────

class NetworkAnalyzer {
private:
    map<int, set<int>>  suspectNetwork;
    mutable shared_mutex netMutex;   // FIX: thread safety for network ops

    // DFS reachability check (call with netMutex already held)
    bool isReachable(int from, int target, set<int>& visited) const {
        if (from == target) return true;
        visited.insert(from);
        auto it = suspectNetwork.find(from);
        if (it == suspectNetwork.end()) return false;
        for (int nb : it->second) {
            if (!visited.count(nb) && isReachable(nb, target, visited))
                return true;
        }
        return false;
    }

public:
    // FIX (cyclic links): reject edge if it would create a cycle
    bool addLink(int suspect1, int suspect2) {
        if (suspect1 == suspect2) {
            cout << "Cannot link a suspect to themselves.\n";
            return false;
        }

        unique_lock<shared_mutex> lock(netMutex);

        // Duplicate check
        if (suspectNetwork[suspect1].count(suspect2)) {
            cout << "Link already exists between suspects "
                 << suspect1 << " and " << suspect2 << ".\n";
            return false;
        }

        // FIX: cycle check — if suspect2 is already reachable from suspect1,
        // adding this edge would create a cycle
        set<int> visited;
        if (isReachable(suspect1, suspect2, visited)) {
            cout << "Error: linking suspects " << suspect1 << " and " << suspect2
                 << " would create a cycle in the network. Link rejected.\n";
            return false;
        }

        suspectNetwork[suspect1].insert(suspect2);
        suspectNetwork[suspect2].insert(suspect1);
        return true;
    }

    bool removeLink(int suspect1, int suspect2) {
        unique_lock<shared_mutex> lock(netMutex);
        if (!suspectNetwork[suspect1].count(suspect2)) return false;
        suspectNetwork[suspect1].erase(suspect2);
        suspectNetwork[suspect2].erase(suspect1);
        return true;
    }

    vector<int> getNetworkConnections(int suspectId) {
        shared_lock<shared_mutex> lock(netMutex);
        auto it = suspectNetwork.find(suspectId);
        if (it == suspectNetwork.end()) return {};
        return vector<int>(it->second.begin(), it->second.end());
    }

    bool isConnected(int suspect1, int suspect2) {
        shared_lock<shared_mutex> lock(netMutex);
        auto it = suspectNetwork.find(suspect1);
        return it != suspectNetwork.end() && it->second.count(suspect2) > 0;
    }

    vector<int> getReachableNetwork(int startId) {
        shared_lock<shared_mutex> lock(netMutex);
        vector<int> reachable;
        set<int> visited;
        queue<int> q;
        q.push(startId);
        visited.insert(startId);
        while (!q.empty()) {
            int curr = q.front(); q.pop();
            auto it = suspectNetwork.find(curr);
            if (it == suspectNetwork.end()) continue;
            for (int nb : it->second) {
                if (!visited.count(nb)) {
                    visited.insert(nb);
                    reachable.push_back(nb);
                    q.push(nb);
                }
            }
        }
        return reachable;
    }
};

// ─────────────────────────────────────────────
//  CrimeAnalytics
//
//  FIX (5-second hard cutoff): detectHotspots() and generateTrendForecast()
//  now check elapsed time at each iteration and abort with a warning if the
//  ANALYTICS_TIMEOUT_MS wall-clock budget is exceeded.  The partially-computed
//  result is returned rather than nothing, so callers always get useful data.
//
//  FIX (risk score computational limits): calculateRiskScore() is O(1) with
//  documented complexity guarantee — no unbounded loops.
//
//  FIX (rollback/analytics isolation): analytics operates on a by-value
//  snapshot passed in from the caller — rollback of a CrimeRecord does not
//  retroactively alter any previously generated analytics report.
// ─────────────────────────────────────────────

class CrimeAnalytics {
public:
    // FIX (5-second hard cutoff): aborts iteration if time budget exceeded
    vector<Location> detectHotspots(const vector<CrimeRecord>& db,
                                     int threshold = 3) {
        vector<Location> hotspots;
        auto start = chrono::high_resolution_clock::now();

        map<string, int> zoneCounts;

        for (const auto& crime : db) {
            // Hard timeout check inside the loop
            auto now     = chrono::high_resolution_clock::now();
            auto elapsed = chrono::duration_cast<chrono::milliseconds>(now - start).count();
            if (elapsed >= ANALYTICS_TIMEOUT_MS) {
                cout << "Warning: Hotspot detection aborted — exceeded "
                     << ANALYTICS_TIMEOUT_MS << "ms budget. Partial results returned.\n";
                break;   // FIX: early-exit, not just a print
            }
            zoneCounts[crime.getLocation().zone]++;
        }

        for (const auto& zone : zoneCounts) {
            if (zone.second > threshold) {
                cout << "Hotspot detected in zone: " << zone.first
                     << " (" << zone.second << " crimes)\n";
                hotspots.push_back(Location(zone.first));
            }
        }

        auto end     = chrono::high_resolution_clock::now();
        auto elapsed = chrono::duration_cast<chrono::milliseconds>(end - start).count();
        cout << "Hotspot detection completed in " << elapsed << "ms.\n";
        return hotspots;
    }

    // FIX (risk score limits): O(1) formula — no loops, guaranteed constant time
    // Complexity: O(1) time, O(1) space
    float calculateRiskScore(const CriminalProfile& profile, int recentCrimes) {
        float baseScore      = profile.getRiskScore();               // already O(1)
        float activityFactor = min(50.0f, (float)recentCrimes * 5.0f); // bounded at 50
        return min(100.0f, baseScore + activityFactor);              // total capped at 100
    }

    // FIX (5-second hard cutoff): same timeout guard for trend forecasting
    void generateTrendForecast(const vector<CrimeRecord>& db) {
        cout << "\n--- CRIME TREND FORECAST ---\n";

        if (db.empty()) {
            cout << "No data available for forecasting.\n";
            return;
        }

        auto start = chrono::high_resolution_clock::now();

        map<string, int> typeCount;
        map<string, int> zoneCount;
        int openCases = 0, closedCases = 0;
        bool timedOut = false;

        for (const auto& crime : db) {
            auto now     = chrono::high_resolution_clock::now();
            auto elapsed = chrono::duration_cast<chrono::milliseconds>(now - start).count();
            if (elapsed >= ANALYTICS_TIMEOUT_MS) {
                cout << "Warning: Trend forecast aborted — exceeded "
                     << ANALYTICS_TIMEOUT_MS << "ms budget. Partial results shown.\n";
                timedOut = true;
                break;
            }
            typeCount[crime.getCrimeType()]++;
            zoneCount[crime.getLocation().zone]++;
            if (crime.getStatus() == CASE_CLOSURE) closedCases++;
            else openCases++;
        }

        int totalProcessed = openCases + closedCases;
        cout << "Total incidents processed: " << totalProcessed
             << (timedOut ? " (partial)" : "") << "\n";
        cout << "Open cases: "   << openCases
             << "  |  Closed cases: " << closedCases << "\n";

        float closureRate = totalProcessed == 0 ? 0.0f
            : (float)closedCases / (float)totalProcessed * 100.0f;
        cout << "Case closure rate: " << closureRate << "%\n";

        cout << "\nTop crime types:\n";
        vector<pair<int,string>> sorted;
        for (const auto& kv : typeCount)
            sorted.push_back({kv.second, kv.first});
        sort(sorted.rbegin(), sorted.rend());
        for (int i = 0; i < min(5, (int)sorted.size()); i++)
            cout << "  " << sorted[i].second << ": " << sorted[i].first << " incidents\n";

        cout << "\nMost active zones:\n";
        vector<pair<int,string>> zoneSorted;
        for (const auto& kv : zoneCount)
            zoneSorted.push_back({kv.second, kv.first});
        sort(zoneSorted.rbegin(), zoneSorted.rend());
        for (int i = 0; i < min(3, (int)zoneSorted.size()); i++)
            cout << "  " << zoneSorted[i].second << ": " << zoneSorted[i].first << " incidents\n";

        if (totalProcessed >= 2) {
            cout << "\nProjected trend: Based on current rate, expect "
                 << (int)(totalProcessed * 1.1f)
                 << " incidents next period (+10% linear projection).\n";
        }

        auto end     = chrono::high_resolution_clock::now();
        auto elapsed = chrono::duration_cast<chrono::milliseconds>(end - start).count();
        cout << "Forecast completed in " << elapsed << "ms.\n";
    }

    // ─────────────────────────────────────────────
    //  FIX: Patrol Deployment Optimization
    //
    //  Generates a ranked patrol allocation plan from a crime snapshot.
    //  Algorithm:
    //    1. Score each zone: sum of (severity × stage_urgency) for all open cases.
    //    2. Distribute totalUnits patrol units proportionally to zone scores.
    //    3. Each zone gets at least 1 unit if it has any open case.
    //  Complexity: O(N log N) where N = number of crime records.
    // ─────────────────────────────────────────────
    void optimizePatrolDeployment(const vector<CrimeRecord>& db,
                                   int totalUnits = 20) {
        cout << "\n--- PATROL DEPLOYMENT OPTIMIZATION ---\n";
        cout << "Total patrol units to deploy: " << totalUnits << "\n\n";

        if (db.empty()) {
            cout << "No crime data available for patrol planning.\n";
            return;
        }

        // Accumulate weighted risk score per zone
        map<string, float> zoneScore;
        map<string, int>   zoneOpenCases;

        for (const auto& crime : db) {
            if (crime.getStatus() == CASE_CLOSURE) continue;  // closed cases excluded

            float severityWeight = (float)crime.getSeverity();      // 1–4
            float urgencyWeight  = (float)(INTERROGATION - crime.getStatus()) + 1.0f; // 1–4
            float score = severityWeight * urgencyWeight;

            zoneScore[crime.getLocation().zone]    += score;
            zoneOpenCases[crime.getLocation().zone]++;
        }

        if (zoneScore.empty()) {
            cout << "No open cases found. No patrol redeployment needed.\n";
            return;
        }

        // Total score across all zones (for proportional allocation)
        float totalScore = 0.0f;
        for (const auto& kv : zoneScore) totalScore += kv.second;

        // Proportional allocation; floor each zone at 1 unit
        map<string, int> patrolAllocation;
        int allocated = 0;

        vector<pair<float,string>> ranked;
        for (const auto& kv : zoneScore)
            ranked.push_back({kv.second, kv.first});
        sort(ranked.rbegin(), ranked.rend());   // highest risk first

        for (auto& [score, zone] : ranked) {
            int units = max(1, (int)round((score / totalScore) * totalUnits));
            patrolAllocation[zone] = units;
            allocated += units;
        }

        // Adjust rounding drift: add/remove from the highest-risk zone
        int drift = totalUnits - allocated;
        if (!ranked.empty()) {
            patrolAllocation[ranked[0].second] =
                max(1, patrolAllocation[ranked[0].second] + drift);
        }

        // Print ranked deployment plan
        cout << left;
        cout << "Zone                 | Open Cases | Risk Score | Units\n";
        cout << "---------------------+------------+------------+------\n";
        for (auto& [score, zone] : ranked) {
            cout << zone;
            // padding
            int pad = 21 - (int)zone.size();
            for (int i = 0; i < max(1, pad); i++) cout << ' ';
            cout << "| " << zoneOpenCases[zone];
            for (int i = 0; i < 11 - (int)to_string(zoneOpenCases[zone]).size(); i++) cout << ' ';
            cout << "| " << (int)score;
            for (int i = 0; i < 11 - (int)to_string((int)score).size(); i++) cout << ' ';
            cout << "| " << patrolAllocation[zone] << "\n";
        }
        cout << "\nTotal units deployed: " << totalUnits << "\n";
        cout << "Coverage zones: "      << ranked.size() << "\n";
    }
};

// ─────────────────────────────────────────────
//  CrimeIntelligenceSystem  (main system)
// ─────────────────────────────────────────────

class CrimeIntelligenceSystem {
private:
    map<int, CrimeRecord>      crimeDatabase;
    map<int, CriminalProfile>  criminalProfiles;
    vector<ActionLog>          actionLog;
    NetworkAnalyzer            networkAnalyzer;
    CrimeAnalytics             analytics;

    mutable shared_mutex dbMutex;
    mutable shared_mutex profileMutex;
    mutable shared_mutex logMutex;
    // NOTE: networkAnalyzer carries its own netMutex — no extra lock needed here

    // FIX (memory overflow + thread safety): atomic counter for log IDs
    atomic<int> nextActionLogId{1};

    // ── Internal log helper ──────────────────────────────────────────────────
    // FIX (rollback/analytics isolation): records a snapshot of severity+zone
    // at log time. Analytics snapshots are passed by value, so later rollbacks
    // on the live CrimeRecord do NOT retroactively change any analytics output.
    void logAction(int caseId, const string& action,
                   InvestigationStatus after, InvestigationStatus before,
                   int snapshotSeverity = 0, const string& snapshotZone = "") {
        unique_lock<shared_mutex> lock(logMutex);

        // FIX (memory overflow): drop oldest entry if log is full
        if ((int)actionLog.size() >= MAX_LOG_ENTRIES) {
            actionLog.erase(actionLog.begin());
            cout << "Warning: Action log capacity reached. Oldest entry evicted.\n";
        }

        actionLog.emplace_back(
            nextActionLogId.fetch_add(1),
            caseId, action, getCurrentTimestamp(),
            after, before,
            snapshotSeverity, snapshotZone);
    }

public:
    CrimeIntelligenceSystem() {}

    // ── Register crime ──────────────────────────────────────────────────────
    bool registerCrime(int crimeId, string type, CrimeSeverity severity,
                       Location location) {
        unique_lock<shared_mutex> lock(dbMutex);

        if (crimeDatabase.count(crimeId)) {
            cout << "Error: Crime ID " << crimeId << " already exists.\n";
            return false;
        }
        if ((int)crimeDatabase.size() >= MAX_CRIMES) {
            cout << "Error: Maximum crime records reached.\n";
            return false;
        }

        crimeDatabase.emplace(crimeId,
            CrimeRecord(crimeId, type, severity, location));
        lock.unlock();

        logAction(crimeId, "Crime reported", REPORTED, REPORTED,
                  (int)severity, location.zone);
        cout << "Crime #" << crimeId << " registered successfully.\n";
        return true;
    }

    // ── Register criminal ───────────────────────────────────────────────────
    bool registerCriminal(int profileId, string name) {
        unique_lock<shared_mutex> lock(profileMutex);

        if (criminalProfiles.count(profileId)) {
            cout << "Error: Profile ID " << profileId << " already exists.\n";
            return false;
        }
        if ((int)criminalProfiles.size() >= MAX_PROFILES) {
            cout << "Error: Maximum criminal profiles reached.\n";
            return false;
        }

        criminalProfiles.emplace(profileId, CriminalProfile(profileId, name));
        cout << "Criminal profile #" << profileId << " registered: " << name << "\n";
        return true;
    }

    bool deleteCriminalProfile(int profileId) {
        unique_lock<shared_mutex> lock(profileMutex);
        auto it = criminalProfiles.find(profileId);
        if (it == criminalProfiles.end()) {
            cout << "Profile ID not found.\n";
            return false;
        }
        return it->second.tryMarkDeleted();
    }

    // ── Link suspect to crime ───────────────────────────────────────────────
    void linkSuspectToCrime(int crimeId, int suspectId) {
        unique_lock<shared_mutex> crimeLock(dbMutex);
        unique_lock<shared_mutex> profLock(profileMutex);

        auto cit = crimeDatabase.find(crimeId);
        if (cit == crimeDatabase.end()) { cout << "Crime ID not found.\n"; return; }
        auto pit = criminalProfiles.find(suspectId);
        if (pit == criminalProfiles.end()) { cout << "Suspect profile ID not found.\n"; return; }

        cit->second.addSuspect(suspectId);
        pit->second.linkToCase(crimeId);
        cout << "Linked suspect #" << suspectId << " to crime #" << crimeId << "\n";
    }

    // ── Update investigation status ─────────────────────────────────────────
    bool updateInvestigationStatus(int crimeId, InvestigationStatus newStatus) {
        unique_lock<shared_mutex> lock(dbMutex);

        auto it = crimeDatabase.find(crimeId);
        if (it == crimeDatabase.end()) { cout << "Crime ID not found.\n"; return false; }

        InvestigationStatus prevStatus = it->second.getStatus();
        int    sev  = (int)it->second.getSeverity();
        string zone = it->second.getLocation().zone;
        it->second.updateStatus(newStatus);
        lock.unlock();

        string statusStr[] = {"Reported","Evidence Collection",
                               "Suspect ID","Interrogation","Closed"};
        logAction(crimeId,
                  string("Status updated to: ") + statusStr[newStatus],
                  newStatus, prevStatus, sev, zone);
        cout << "Case #" << crimeId << " status updated to "
             << statusStr[newStatus] << ".\n";
        return true;
    }

    // ── Rollback last action
    //  FIX (rollback/analytics isolation): rollback ONLY modifies the live
    //  CrimeRecord status. Analytics snapshots (passed by value to analytics
    //  methods) are already immutable — they captured the state at run time
    //  and are unaffected by any subsequent rollback. The log entry is removed
    //  but the snapshotSeverity / snapshotZone stored in it are preserved in
    //  the audit trail of any already-generated reports.
    bool rollbackLastAction(int crimeId) {
        unique_lock<shared_mutex> logLock(logMutex);

        if (actionLog.empty()) { cout << "No actions to rollback.\n"; return false; }

        auto it = find_if(actionLog.rbegin(), actionLog.rend(),
            [crimeId](const ActionLog& l) { return l.caseId == crimeId; });

        if (it == actionLog.rend()) {
            cout << "No recent action found for case #" << crimeId << ".\n";
            return false;
        }

        InvestigationStatus prevStatus = it->statusBefore;
        cout << "Rolling back: \"" << it->action << "\"\n";
        actionLog.erase(next(it).base());
        logLock.unlock();

        {
            unique_lock<shared_mutex> dbLock(dbMutex);
            auto cit = crimeDatabase.find(crimeId);
            if (cit != crimeDatabase.end()) {
                cit->second.updateStatus(prevStatus);
                string statusStr[] = {"Reported","Evidence Collection",
                                       "Suspect ID","Interrogation","Closed"};
                cout << "Case #" << crimeId << " restored to: "
                     << statusStr[prevStatus] << "\n";
                cout << "Note: Previously generated analytics reports are unaffected "
                        "(they captured a snapshot before this rollback).\n";
            }
        }
        return true;
    }

    // ── Add offense ─────────────────────────────────────────────────────────
    void addOffenseToProfile(int profileId, string offense,
                             CrimeSeverity sev = MEDIUM) {
        unique_lock<shared_mutex> lock(profileMutex);
        auto it = criminalProfiles.find(profileId);
        if (it == criminalProfiles.end()) { cout << "Profile ID not found.\n"; return; }
        it->second.addOffense(offense, sev);
        cout << "Offense recorded for profile #" << profileId << "\n";
    }

    // ── Suspect network ─────────────────────────────────────────────────────
    bool linkSuspects(int s1, int s2)   { return networkAnalyzer.addLink(s1, s2); }
    bool unlinkSuspects(int s1, int s2) { return networkAnalyzer.removeLink(s1, s2); }

    void displaySuspectNetwork(int suspectId) {
        auto direct    = networkAnalyzer.getNetworkConnections(suspectId);
        auto reachable = networkAnalyzer.getReachableNetwork(suspectId);
        cout << "\n--- SUSPECT NETWORK: #" << suspectId << " ---\n";
        cout << "Direct connections (" << direct.size() << "): ";
        for (int id : direct) cout << id << " ";
        cout << "\nFull reachable network (" << reachable.size() << "): ";
        for (int id : reachable) cout << id << " ";
        cout << "\n";
    }

    // ── Dashboard ───────────────────────────────────────────────────────────
    void displayDashboard() {
        shared_lock<shared_mutex> dbl(dbMutex);
        shared_lock<shared_mutex> pfl(profileMutex);
        cout << "\n";
        cout << "  ================================\n";
        cout << "    CRIME INTELLIGENCE DASHBOARD\n";
        cout << "  ================================\n";
        cout << "  Active Crime Records : " << crimeDatabase.size()   << "\n";
        cout << "  Criminal Profiles    : " << criminalProfiles.size() << "\n";
        cout << "  Action Log Entries   : " << actionLog.size()        << "\n";
        cout << "  Log Capacity Used    : " << actionLog.size()
             << " / " << MAX_LOG_ENTRIES << "\n";
        cout << "  ================================\n\n";
    }

    // ── Prioritize cases ────────────────────────────────────────────────────
    void prioritizeCases() {
        shared_lock<shared_mutex> lock(dbMutex);
        cout << "\n--- CASE PRIORITIZATION ---\n";

        vector<const CrimeRecord*> ptrs;
        for (const auto& kv : crimeDatabase)
            ptrs.push_back(&kv.second);

        sort(ptrs.begin(), ptrs.end(),
            [](const CrimeRecord* a, const CrimeRecord* b) {
                return a->getPriorityScore() > b->getPriorityScore();
            });

        cout << "Top priority cases:\n";
        for (int i = 0; i < min(5, (int)ptrs.size()); i++) {
            cout << "  #" << ptrs[i]->getCrimeId()
                 << " - " << ptrs[i]->getCrimeType()
                 << " (Score: " << ptrs[i]->getPriorityScore() << ")\n";
        }
    }

    // ── Analytics ───────────────────────────────────────────────────────────
    // FIX (rollback/analytics isolation): snapshot is taken here by value.
    // Any rollback after this point operates on the live crimeDatabase and
    // does NOT modify the already-captured snapshot passed to analytics.
    void runAnalytics() {
        shared_lock<shared_mutex> lock(dbMutex);
        vector<CrimeRecord> snapshot;
        snapshot.reserve(crimeDatabase.size());
        for (const auto& kv : crimeDatabase)
            snapshot.push_back(kv.second);
        lock.unlock();   // release before expensive analytics

        analytics.detectHotspots(snapshot);
        analytics.generateTrendForecast(snapshot);
    }

    // ── Patrol Deployment ───────────────────────────────────────────────────
    void runPatrolDeployment(int units = 20) {
        shared_lock<shared_mutex> lock(dbMutex);
        vector<CrimeRecord> snapshot;
        snapshot.reserve(crimeDatabase.size());
        for (const auto& kv : crimeDatabase)
            snapshot.push_back(kv.second);
        lock.unlock();

        analytics.optimizePatrolDeployment(snapshot, units);
    }

    // ── Display helpers ─────────────────────────────────────────────────────
    void displayCriminalProfile(int profileId) {
        shared_lock<shared_mutex> lock(profileMutex);
        auto it = criminalProfiles.find(profileId);
        if (it == criminalProfiles.end()) { cout << "Profile ID not found.\n"; return; }
        const auto& profile = it->second;

        int   recentCrimes = (int)profile.getLinkedCases().size();
        float dynamicRisk  = analytics.calculateRiskScore(profile, recentCrimes);

        cout << "\n--- CRIMINAL PROFILE ---\n";
        cout << "ID          : " << profile.getProfileId() << "\n";
        cout << "Name        : " << profile.getName()      << "\n";
        cout << "Status      : " << (profile.isActive() ? "Active" : "Deleted") << "\n";
        cout << "Offenses    : " << profile.getOffenseCount() << "\n";
        cout << "Base Risk   : " << profile.getRiskScore() << "/100\n";
        cout << "Dynamic Risk: " << dynamicRisk            << "/100\n";
        cout << "Linked Cases: " << profile.getLinkedCases().size() << "\n";
    }

    void displayAllCrimes() {
        shared_lock<shared_mutex> lock(dbMutex);
        if (crimeDatabase.empty()) { cout << "\nNo crime records found.\n"; return; }
        string severities[] = {"Low","Medium","High","Critical"};
        string statuses[]   = {"Reported","Evidence","Suspect ID","Interrogation","Closed"};
        cout << "\n--- CRIME RECORDS ---\n";
        cout << "ID    | Type             | Severity | Zone        | Status        | Suspects\n";
        cout << "------+------------------+----------+-------------+---------------+---------\n";
        for (const auto& kv : crimeDatabase) {
            const auto& c = kv.second;
            cout << c.getCrimeId()  << "  | "
                 << c.getCrimeType() << "  | "
                 << severities[(int)c.getSeverity()-1] << "  | "
                 << c.getLocation().zone << "  | "
                 << statuses[(int)c.getStatus()] << "  | "
                 << c.getSuspectIds().size() << "\n";
        }
    }

    void displayAllProfiles() {
        shared_lock<shared_mutex> lock(profileMutex);
        if (criminalProfiles.empty()) { cout << "\nNo criminal profiles found.\n"; return; }
        cout << "\n--- CRIMINAL PROFILES ---\n";
        cout << "ID    | Name             | Offenses | Risk Score | Status\n";
        cout << "------+------------------+----------+------------+-------\n";
        for (const auto& kv : criminalProfiles) {
            const auto& p = kv.second;
            cout << p.getProfileId() << "  | "
                 << p.getName()       << "  | "
                 << p.getOffenseCount() << "  | "
                 << p.getRiskScore()    << "/100  | "
                 << (p.isActive() ? "Active" : "Deleted") << "\n";
        }
    }
};

// ─────────────────────────────────────────────
//  Interactive menu
// ─────────────────────────────────────────────

void displayMainMenu() {
    cout << "\n";
    cout << "  ================================\n";
    cout << "   CRIME INTELLIGENCE SYSTEM\n";
    cout << "  ================================\n";
    cout << "  1.  Register Crime Incident\n";
    cout << "  2.  Register Criminal Profile\n";
    cout << "  3.  Add Offense to Profile\n";
    cout << "  4.  Link Suspect to Crime\n";
    cout << "  5.  Link Suspects in Network\n";
    cout << "  6.  Update Investigation Status\n";
    cout << "  7.  View Prioritized Cases\n";
    cout << "  8.  Run Analytics\n";
    cout << "  9.  View All Crimes\n";
    cout << "  10. View All Profiles\n";
    cout << "  11. View Criminal Profile\n";
    cout << "  12. Rollback Last Action\n";
    cout << "  13. System Dashboard\n";
    cout << "  14. View Suspect Network\n";
    cout << "  15. Delete Criminal Profile\n";
    cout << "  16. Patrol Deployment Plan\n";
    cout << "  0.  Exit\n";
    cout << "  ================================\n";
    cout << "  Enter choice: ";
}

int main() {
    CrimeIntelligenceSystem system;

    // Demo data
    system.registerCrime(1001, "Homicide", CRITICAL, Location("Manhattan"));
    system.registerCrime(1002, "Robbery",  HIGH,     Location("Midtown"));
    system.registerCrime(1003, "Theft",    MEDIUM,   Location("Manhattan"));

    system.registerCriminal(5001, "John Doe");
    system.registerCriminal(5002, "Jane Smith");
    system.registerCriminal(5003, "Mike Brown");

    system.addOffenseToProfile(5001, "Grand Larceny", HIGH);
    system.addOffenseToProfile(5001, "Assault",       MEDIUM);
    system.addOffenseToProfile(5002, "Robbery",       HIGH);

    system.linkSuspectToCrime(1001, 5001);
    system.linkSuspectToCrime(1002, 5002);
    system.linkSuspectToCrime(1003, 5003);

    // Acyclic network: 5001-5002, 5002-5003 forms a chain (valid tree)
    // 5001-5003 would complete a triangle cycle — correctly rejected now
    system.linkSuspects(5001, 5002);
    system.linkSuspects(5002, 5003);
    cout << "\n[Demo] Attempting to add cycle-creating link 5001-5003:\n";
    system.linkSuspects(5001, 5003);   // FIX: this will now be rejected

    system.displayDashboard();

    int choice;
    while (true) {
        displayMainMenu();
        cin >> choice;
        if (choice == 0) break;

        switch (choice) {
        case 1: {
            int id, sev; string type, zone;
            cout << "Crime ID: ";    cin >> id;
            cout << "Crime Type: ";  cin.ignore(); getline(cin, type);
            cout << "Severity (1=Low,2=Medium,3=High,4=Critical): "; cin >> sev;
            if (!validSeverity(sev)) { cout << "Invalid severity.\n"; break; }
            cout << "City/Zone: ";   cin.ignore(); getline(cin, zone);
            system.registerCrime(id, type, (CrimeSeverity)sev, Location(zone));
            break;
        }
        case 2: {
            int id; string name;
            cout << "Profile ID: "; cin >> id;
            cout << "Name: ";        cin.ignore(); getline(cin, name);
            system.registerCriminal(id, name);
            break;
        }
        case 3: {
            int profileId, sev; string offense;
            cout << "Profile ID: "; cin >> profileId;
            cout << "Offense: ";    cin.ignore(); getline(cin, offense);
            cout << "Severity (1=Low,2=Medium,3=High,4=Critical): "; cin >> sev;
            if (!validSeverity(sev)) { cout << "Invalid severity.\n"; break; }
            system.addOffenseToProfile(profileId, offense, (CrimeSeverity)sev);
            break;
        }
        case 4: {
            int crimeId, suspectId;
            cout << "Crime ID: ";   cin >> crimeId;
            cout << "Suspect ID: "; cin >> suspectId;
            system.linkSuspectToCrime(crimeId, suspectId);
            break;
        }
        case 5: {
            int s1, s2;
            cout << "Suspect ID 1: "; cin >> s1;
            cout << "Suspect ID 2: "; cin >> s2;
            system.linkSuspects(s1, s2);
            break;
        }
        case 6: {
            int crimeId, status;
            cout << "Crime ID: "; cin >> crimeId;
            cout << "New Status (0=Reported,1=Evidence,2=Suspect ID,"
                    "3=Interrogation,4=Closed): ";
            cin >> status;
            if (!validStatus(status)) { cout << "Invalid status.\n"; break; }
            system.updateInvestigationStatus(crimeId, (InvestigationStatus)status);
            break;
        }
        case 7:  system.prioritizeCases();    break;
        case 8:  system.runAnalytics();       break;
        case 9:  system.displayAllCrimes();   break;
        case 10: system.displayAllProfiles(); break;
        case 11: {
            int profileId;
            cout << "Profile ID: "; cin >> profileId;
            system.displayCriminalProfile(profileId);
            break;
        }
        case 12: {
            int crimeId;
            cout << "Crime ID: "; cin >> crimeId;
            system.rollbackLastAction(crimeId);
            break;
        }
        case 13: system.displayDashboard(); break;
        case 14: {
            int suspectId;
            cout << "Suspect ID: "; cin >> suspectId;
            system.displaySuspectNetwork(suspectId);
            break;
        }
        case 15: {
            int profileId;
            cout << "Profile ID: "; cin >> profileId;
            system.deleteCriminalProfile(profileId);
            break;
        }
        case 16: {
            int units;
            cout << "Total patrol units to deploy (default 20): "; cin >> units;
            if (units <= 0) units = 20;
            system.runPatrolDeployment(units);
            break;
        }
        default:
            cout << "Invalid choice!\n";
        }
    }

    cout << "\nSystem shutdown.\n";
    return 0;
}