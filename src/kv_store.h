#pragma once
#include "wal.h"
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
