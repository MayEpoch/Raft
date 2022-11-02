//
// Created by epoch on 10/24/22.
//

#ifndef MIT6_824_C_SERVER_HPP
#define MIT6_824_C_SERVER_HPP
#include <vector>
#include <cstddef>
#include <stdlib.h>
#include <string>
#include <shared_mutex>

#include "Timer.hpp"
#include "buttonrpc.hpp"

/* debug */
#include <iostream>
#include <queue>


using std::cout, std::endl;
/*TODO: delete above*/

using ms = std::chrono::milliseconds;
using s = std::chrono::seconds;

/* provided by application layer */
void execute_command(const string& command);

constexpr auto delay = 1000;
constexpr size_t null = 0;
constexpr ms client_request_frequency(100);
constexpr size_t max_send_log_size = 5;

class LogEntry {
public:
    size_t term{0};
    size_t index{0};
    std::string command{""};

    LogEntry() = default;
    LogEntry(size_t term, size_t index, const string &command);
};

ostream& operator<<(ostream& out, const LogEntry& entry);

struct VoteResult {
    size_t term{0};
    bool vote_granted{false};
};

struct AppendResult {
    size_t term{0};
    bool success{false};
};

class NetAddress{
public:
    NetAddress() = default;

    NetAddress(const string& ip, const size_t& port): ip(ip), port(port) {}
    std::string ip;
    size_t port;
};


class Server {
public:
    enum class State {Follower, Candidate, Leader};

    /*format: ip:port, like "127.0.0.1:5555" */
    Server(size_t id, const std::string &pair);
    Server(size_t id, const std::string &IP, const size_t &port);

    /* RPC */
    std::string Hello(size_t id);

    VoteResult request_vote(size_t term, size_t candidate_id, size_t last_log_index, size_t last_log_term);
    AppendResult append_entries(size_t term, size_t leader_id, size_t prev_log_index, size_t prev_log_term, const string &entries, size_t leader_commit);



    void as_candidate();    /* be a candidate */
    void as_leader();    /* be a leader */

    /* help function*/
    void read_config();
    void starts_up();


private: /* Data mentioned in paper. */

    /* persistent part */
    size_t current_term{0};
    size_t vote_for{null};

    std::shared_mutex logs_mutex;
    std::vector<LogEntry> logs{LogEntry()}; // start from 1.

    /* volatile part */
    std::mutex commit_index_mutex; //TODO: this only for leader commit_index;
    size_t commit_index{0};
    size_t last_applied{0};


private: /* extra information*/

    /* Log Part */
    size_t last_log_term{0};
    size_t last_log_index{0};

    /* dynamically change when member change */
    size_t majority_count{2};


    /* state information */
    std::atomic<State> state{State::Follower};


    /* election timer relevant property */
    ms election_timer_base{delay};
    ms election_timer_fluctuate{delay};
    Timer<ms> election_timer{delay};

    /* election timer relevant function */
    void start_election_timer();

    size_t id;

    /* connection information of other server */
    std::unordered_map<size_t, NetAddress> other_servers;
    std::unordered_map<size_t, unique_ptr<buttonrpc>> other_server_connections;

    /* leader unique*/
    unordered_map<size_t, size_t> next_index;
    unordered_map<size_t, atomic<size_t>> match_index;


private:

    /* when receive more up-to-date term, update current_term & be a follower*/
    void update_current_term(size_t term) {
        this->vote_for = null;
        this->current_term = term;
        be_follower();
    }

    void be_follower() {
        if (this->state != State::Follower) {
            this->state = State::Follower;
            start_election_timer();
        } else {
            this->election_timer.restart();
        }
    }

    void send_log_heartbeat(size_t server_id);


    inline size_t cluster_size() {
        return this->other_server_connections.size() + 1;
    }

    pair<size_t, size_t> get_last_log_info() {
        std::shared_lock<std::shared_mutex> get_last_log_info_lock(this->logs_mutex);
        return {last_log_term, last_log_index};
    }

    size_t get_send_log_size(size_t server_id, bool is_conflict = false) {
        if (is_conflict) {
            return 0;
        }
        auto current_last_log_index = get_last_log_info().second;
        return min(max_send_log_size, current_last_log_index - next_index[server_id] + 1);
    }


private: /* debug part */

    void update_commit_index(size_t value) {
        std::unique_lock<std::mutex> lock(this->commit_index_mutex);
        if (value > this->commit_index) {
            printf("commit_index update success [%lu]->[%lu]\n", this->commit_index, value);
        }
        commit_index = max(this->commit_index, value);
    }

    void append_log_simulate() {
        unique_lock<std::shared_mutex> lock(logs_mutex);
        this->logs.emplace_back(this->current_term, logs.size(), "Hello");
        this->last_log_term = logs.back().term;
        this->last_log_index = logs.back().index;
    }

    bool find_match_index_median_check(size_t mid) {
        int count = 1; // 1: the leader one.
        for (const auto& [k, v]: this->match_index) {
            if (v >= mid) {
                ++count;
            }
        }

        return count >= majority_count;
    }

    size_t find_match_index_median() {
        int l = min_element(match_index.begin(), match_index.end(), [] (const auto& p1, const auto& p2) ->bool {
            return p1.second < p2.second;
        })->second;
        int r = max_element(match_index.begin(), match_index.end(), [] (const auto& p1, const auto& p2) ->bool {
            return p1.second < p2.second;
        })->second;
        if (l == r) {
            return l;
        }
        int time = 0;

        int res = l;
        while (l <= r) {
            ++time;
            int mid = (r - l) / 2 + l;
            if (time > 200) {
                printf("over: l->%d r->%d mid->%d time: %d, check %d \n", l, r, mid, time, find_match_index_median_check(mid));
                exit(-1);
            }
            if (find_match_index_median_check(mid)) {
                res = mid;
                l = mid + 1;
            } else {
                r = mid - 1;
            }
        }
        cout << "\nbinary search:  " << time << endl << endl;
        return res;
    }

    std::string get_log_string(size_t start, size_t end) {
        string res;
        for (auto& i = start; i < end; ++i) {
            res = res + " " + to_string(logs[i].term) + " " + to_string(logs[i].index) + " " + logs[i].command;
        }
        return res;
    }

    vector<LogEntry> parse_string_logs(const string& s) {
        vector<LogEntry> res;
        istringstream read(s);
        LogEntry entry;
        while (read >> entry.term >> entry.index >> entry.command) {
            res.emplace_back(entry);
        }
        return res;
    }

    vector<string> get_logs_command(size_t start_index, size_t end_index) {
        vector<string> commands;
        std::shared_lock<std::shared_mutex> log_lock(this->logs_mutex);
        //todo: udpate it later after log compaction.
        for (size_t i = start_index; i < end_index; ++i) {
            commands.emplace_back(logs[i].command);
        }
        return commands;
    }

    void apply_entries() {
        vector<string> commands = get_logs_command(this->last_applied + 1, this->commit_index + 1);
        for (const auto& command: commands) {
            execute_command(command);
        }
        this->last_applied += commands.size();
    }

    size_t get_log_term(size_t index) {
        //TODO: update it after log compaction(use base + offset)
        return logs[index].term;
    }

    bool match_prev_log_term(size_t index, size_t term) {
        return get_total_log_size() > index && get_log_term(index) == term;
    }

    size_t get_total_log_size() {
        //TODO: update it after log compaction(use base + size());
        return logs.size();
    }

    bool log_conflict(size_t index, size_t term)  {
        //TODO: update if after change way of log store.
        return get_log_term(index) != term;
    }

    void remove_conflict_logs(size_t index);

    void append_logs(const vector<LogEntry>& entries);
};


#endif //MIT6_824_C_SERVER_HPP
