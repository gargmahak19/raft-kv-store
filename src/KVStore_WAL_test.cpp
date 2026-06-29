#include "kv_store.h"  // brings in kv_store (which itself brings in .h)
#include <iostream>
#include <thread>
#include <atomic>
#include <vector>
#include <cstdio>       // for remove()
using namespace std;


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
    remove("test_basics.");           // start clean
    KVStore kv("test_basics.");
 
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
    remove("test_size.");           // start clean
    KVStore kv("test_size.");
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
    remove("test_edge_cases.");          
    KVStore kv("test_edge_cases.");
 
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
    remove("test_concurrent_writes.");          
    KVStore kv("test_concurrent_writes.");
 
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
    remove("test_concurrent_mixed.");          
    KVStore kv("test_concurrent_mixed.");
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
void test_crash_recovery() {
    cout << "\n== Crash recovery (durability) ==\n";
    remove("test_recovery.");
    {
        KVStore kv("test_recovery.");
        kv.put("name", "mahak");
        kv.put("lastname", "garg");
        kv.deleteKey("lastname");
    }   // store destroyed — simulates shutdown
    KVStore kv2("test_recovery.");   // fresh store, same file
    CHECK("put survived restart", kv2.get("name") == "mahak");
    CHECK("delete survived restart", !kv2.get("lastname").has_value());
    CHECK("recovered size correct", kv2.size() == 1);
}
 
int main() {
    cout << "KVStore test suite\n";
    test_basics();
    test_size();
    test_edge_cases();
    test_concurrent_writes();
    test_concurrent_mixed();
    test_crash_recovery();   // the new one

    cout << "\n-- Results --\n";
    cout << "  Passed: " << passed << "\n";
    cout << "  Failed: " << failed << "\n";
    return failed > 0 ? 1 : 0;
}