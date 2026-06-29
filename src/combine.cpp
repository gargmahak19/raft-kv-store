#include <iostream>
#include <string>
#include <unordered_map>
#include <optional>
#include <mutex>
#include <thread>
#include <atomic>
#include <vector>
#include <fstream>
#include <cstdint>
#include <cstdio>

using namespace std;


// One entry in the log — what we read back during replay.
struct WALEntry {
    char op;        // 'p' for put, 'd' for delete
    string key;
    string value;   // empty for delete
};

class WAL {
public:
    // Open (or create) the log file at this path.
    WAL(const string& path) {
        filename = path;                              // save path so readAll can reopen it
        out.open(path, ios::binary | ios::app);       // binary = raw bytes, app = append at end
    }

    // Append a put operation to the log file.
    void appendPut(const string& key, const string& value) {
        char op = 'p';
        out.write(&op, 1);                                       // op byte

        uint32_t klen = key.size();
        out.write(reinterpret_cast<char*>(&klen), sizeof(klen)); // key length
        out.write(key.data(), klen);                             // key bytes

        uint32_t vlen = value.size();
        out.write(reinterpret_cast<char*>(&vlen), sizeof(vlen)); // value length
        out.write(value.data(), vlen);                           // value bytes

        out.flush();
    }

    // Append a delete operation to the log file.
    void appendDelete(const string& key) {
        char op = 'd';
        out.write(&op, 1);                                       // op byte

        uint32_t klen = key.size();
        out.write(reinterpret_cast<char*>(&klen), sizeof(klen)); // key length
        out.write(key.data(), klen);                             // key bytes

        out.flush();
    }

    // Read every entry back from the file, in order.
    // Used on startup to rebuild the map.
    vector<WALEntry> readAll() {
        vector<WALEntry> entries;
        ifstream in(filename, ios::binary);   // separate stream for reading

        while (true) {
            char op;
            if (!in.read(&op, 1)) break;       // read op byte; if it fails -> end of file

            // read the key: length first, then that many bytes
            uint32_t klen;
            in.read(reinterpret_cast<char*>(&klen), sizeof(klen));
            string key(klen, '\0');
            in.read(key.data(), klen);

            WALEntry e;
            e.op  = op;
            e.key = key;

            if (op == 'p') {
                // a put also has a value — read it the same way as the key
                uint32_t vlen;
                in.read(reinterpret_cast<char*>(&vlen), sizeof(vlen));
                string value(vlen, '\0');
                in.read(value.data(), vlen);
                e.value = value;
            }
            // op == 'd' -> no value, e.value stays empty. Nothing to read.

            entries.push_back(e);
        }

        return entries;
    }

private:
    string filename;
    ofstream out;   // for appending
};

class KVStore {
    public:
        KVStore(const string& filename = "kv.wal"): wal(filename){
            auto entries = wal.readAll();      // read everything logged previously
                for (auto& e : entries) {
                if (e.op == 'p') data[e.key] = e.value;   
                else if (e.op == 'd') data.erase(e.key);
            }
        }
        optional<string> get(const string& key){
            lock_guard<mutex> lk(mu_); // lock — other threads wait
            auto it=data.find(key); // ONE lookup, returns an iterator
            if (it!=data.end()) return it->second;// found → return the value
            else return nullopt; // not found → empty box
        }
        void put(const string& key , const string& value){
            lock_guard<mutex> lk(mu_);
            wal.appendPut(key, value);
            data[key]=value;
        }
        void deleteKey(const string& key){
            lock_guard<mutex> lk(mu_);
            wal.appendDelete(key);
            data.erase(key); // erase on a missing key is a harmless no-op — returns 0,
        }
        size_t size(){
            lock_guard<mutex> lk(mu_);
            return data.size();
        }

    private:
        string filename;
        unordered_map<string, string> data;
        mutex mu_;
        WAL wal;
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
    remove("test_basics.wal");           // start clean
    KVStore kv("test_basics.wal");
 
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
    remove("test_size.wal");           // start clean
    KVStore kv("test_size.wal");
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
    remove("test_edge_cases.wal");          
    KVStore kv("test_edge_cases.wal");
 
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
    remove("test_concurrent_writes.wal");          
    KVStore kv("test_concurrent_writes.wal");
 
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
    remove("test_concurrent_mixed.wal");          
    KVStore kv("test_concurrent_mixed.wal");
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

        
    {
        WAL wal("WAtest.log");
        wal.appendPut("city", "New York");
        wal.appendPut("lang", "cpp");
        wal.appendDelete("city");
    }   // scope ends -> wal is destroyed -> file flushes and closes

    // A fresh WAL object reopens the same file and replays it
    WAL wal2("WAtest.log");
    auto entries = wal2.readAll();
    for (auto& e : entries)
        cout << e.op << " | " << e.key << " | " << e.value << "\n";

    return failed > 0 ? 1 : 0;
}