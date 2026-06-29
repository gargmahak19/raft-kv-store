#pragma once
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