/* -----------------------PROBLEM Statementr 1----------------------------------------
Write a C++ class called KVStore that can store, retrieve, and delete string key-value pairs in memory. A client should be able to call put("city", "pune"), then get("city") and receive "pune" back. Calling get on a key that doesn't exist should signal that clearly — not crash.

Constraints

Keys and values are both
std::string
get
must handle missing keys gracefully — think about what C++ type lets you return "either a value or nothing"
Multiple threads may call these methods simultaneously — it must not corrupt data
Write a
size()
method that returns how many keys are stored
Think about first

What C++ container maps strings to strings? What does C++17 give you for "maybe has a value"? What primitive do you need to make it thread-safe?
*/

#include <iostream>
#include <string>
#include <unordered_map>
#include <optional>
#include <mutex>
#include <thread>
#include <atomic>
#include <vector>


using namespace std;

class KVStore {
    public:
        optional<string> get(const string& key){
            lock_guard<mutex> lk(mu_); // lock — other threads wait
            auto it=data.find(key); // ONE lookup, returns an iterator
            if (it!=data.end()) return it->second;// found → return the value
            else return nullopt; // not found → empty box
        }
        void put(const string& key , const string& value){
            lock_guard<mutex> lk(mu_);
            data[key]=value;
        }
        void deleteKey(const string& key){
            lock_guard<mutex> lk(mu_);
            data.erase(key); // erase on a missing key is a harmless no-op — returns 0,
        }
        size_t size(){
            lock_guard<mutex> lk(mu_);
            return data.size();
        }

    private:
        unordered_map<string, string> data;
        mutex mu_;
};


// ── Tiny test framework — no dependencies ───────────────────────────────────
static int passed = 0, failed = 0;
#define CHECK(name, cond) \
    do { \
        if (cond) { cout << "  [PASS] " << name << "\n"; passed++; } \
        else      { cout << "  [FAIL] " << name << "\n"; failed++; } \
    } while(0)
 
// ── Group 1: basic correctness ──────────────────────────────────────────────
void test_basics() {
    cout << "\n== Basic operations ==\n";
    KVStore kv;
 
    CHECK("get on empty store returns nullopt", !kv.get("missing").has_value());
 
    kv.put("city", "pune");
    CHECK("get returns stored value", kv.get("city") == "pune");
 
    kv.put("city", "mumbai");
    CHECK("put overwrites existing value", kv.get("city") == "mumbai");
 
    kv.deleteKey("city");
    CHECK("get after delete returns nullopt", !kv.get("city").has_value());
}
 
// ── Group 2: size tracking ──────────────────────────────────────────────────
void test_size() {
    cout << "\n== Size tracking ==\n";
    KVStore kv;
    CHECK("empty store size is 0", kv.size() == 0);
 
    kv.put("a", "1");
    kv.put("b", "2");
    kv.put("c", "3");
    CHECK("size after 3 puts is 3", kv.size() == 3);
 
    kv.put("a", "99");  // overwrite, NOT a new key
    CHECK("overwrite does not increase size", kv.size() == 3);
 
    kv.deleteKey("a");
    CHECK("size after delete is 2", kv.size() == 2);
}
 
// ── Group 3: edge cases ─────────────────────────────────────────────────────
void test_edge_cases() {
    cout << "\n== Edge cases ==\n";
    KVStore kv;
 
    kv.deleteKey("never_existed");
    CHECK("delete missing key is safe no-op", kv.size() == 0);
 
    kv.put("", "empty-key-value");
    CHECK("empty string key is allowed", kv.get("") == "empty-key-value");
 
    kv.put("key", "");
    CHECK("empty string value is stored", kv.get("key") == "");
    CHECK("empty value is distinct from missing",
          kv.get("key").has_value() && !kv.get("absent").has_value());
 
    string big(1024 * 1024, 'X');  // 1 MB value
    kv.put("big", big);
    CHECK("large value stored correctly", kv.get("big") == big);
}
 
// ── Group 4: concurrency (the important one) ────────────────────────────────
void test_concurrent_writes() {
    cout << "\n== Concurrent writes ==\n";
    KVStore kv;
    const int THREADS = 8, PER_THREAD = 5000;
 
    vector<thread> ts;
    for (int t = 0; t < THREADS; t++) {
        ts.emplace_back([&kv, t, PER_THREAD]() {
            for (int i = 0; i < PER_THREAD; i++)
                kv.put("k-" + to_string(t) + "-" + to_string(i), to_string(i));
        });
    }
    for (auto& th : ts) th.join();
 
    CHECK("all concurrent writes landed (no lost updates)",
          kv.size() == (THREADS * PER_THREAD));
}
 
void test_concurrent_mixed() {
    cout << "\n== Concurrent readers + writers ==\n";
    KVStore kv;
    for (int i = 0; i < 1000; i++) kv.put("k" + to_string(i), to_string(i));
 
    atomic<bool> corrupted{false};
    vector<thread> ts;
 
    // 4 readers hammering reads while...
    for (int r = 0; r < 4; r++) {
        ts.emplace_back([&kv, &corrupted]() {
            for (int i = 0; i < 10000; i++) {
                auto v = kv.get("k" + to_string(i % 1000));
                if (v.has_value() && v->empty()) corrupted = true;
            }
        });
    }
    // ...2 writers keep updating
    for (int w = 0; w < 2; w++) {
        ts.emplace_back([&kv]() {
            for (int i = 0; i < 5000; i++)
                kv.put("k" + to_string(i % 1000), to_string(i));
        });
    }
    for (auto& th : ts) th.join();
 
    CHECK("no torn / corrupted reads under concurrency", !corrupted.load());
}
 
int main() {
    cout << "KVStore test suite\n";
    test_basics();
    test_size();
    test_edge_cases();
    test_concurrent_writes();
    test_concurrent_mixed();
 
    cout << "\n── Results ─────────────\n";
    cout << "  Passed: " << passed << "\n";
    cout << "  Failed: " << failed << "\n";
    return failed > 0 ? 1 : 0;
}