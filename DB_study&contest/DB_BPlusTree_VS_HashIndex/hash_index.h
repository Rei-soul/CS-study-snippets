// 链式哈希表（手写，教学版）：固定桶数 + 桶内单链表，不扩容、不删除
// 既用来做"自适应哈希缓存"，也用来做"访问次数表"
#pragma once
#include <cstdint>
class HashIndex {
public:
    explicit HashIndex(int bucket_count = 4096) {
        bucket_count_ = (bucket_count > 0) ? bucket_count : 1;
        size_ = 0;
        buckets_ = new Entry*[bucket_count_];
        for (int i = 0; i < bucket_count_; ++i) buckets_[i] = 0;
    }
    ~HashIndex() {
        for (int i = 0; i < bucket_count_; ++i) {
            Entry* p = buckets_[i];
            while (p != 0) { Entry* next = p->next; delete p; p = next; }
        }
        delete[] buckets_;
    }
    bool insert(int64_t key, int value) {      // key 已存在返回 false
        int b = bucketOf(key);                  // 返回桶编号
        for (Entry* p = buckets_[b]; p != 0; p = p->next) 
            if (p->key == key) return false;
        Entry* e = new Entry();
        e->key = key; e->value = value; e->next = buckets_[b];
        buckets_[b] = e;
        ++size_;
        return true;
    }
    bool search(int64_t key, int& value) const {
        int b = bucketOf(key);
        for (const Entry* p = buckets_[b]; p != 0; p = p->next) {
            if (p->key == key) { value = p->value; return true; }
        }
        return false;
    }
    // 删除一个 key；不存在返回 false
    bool erase(int64_t key) {
        int b = bucketOf(key);
        Entry* prev = 0;
        for (Entry* p = buckets_[b]; p != 0; prev = p, p = p->next) {
            if (p->key != key) continue;
            if (prev != 0) prev->next = p->next; else buckets_[b] = p->next;
            delete p;
            --size_;
            return true;
        }
        return false;
    }
    // 只给"访问次数表"用：存在则 value+1，不存在则插入并设为 1；返回自增后的次数
    int bump(int64_t key) {
        int b = bucketOf(key);
        for (Entry* p = buckets_[b]; p != 0; p = p->next) {
            if (p->key == key) { ++p->value; return p->value; }
        }
        Entry* e = new Entry();
        e->key = key; e->value = 1; e->next = buckets_[b];
        buckets_[b] = e;
        ++size_;
        return 1;
    }
    int size() const { return size_; }
private:
    struct Entry { int64_t key; int value; Entry* next; };
    // 手写哈希：先做一次 64 位混合打散（相邻 key 不会挤进同一个桶），再对桶数取模
    int bucketOf(int64_t key) const {
        uint64_t x = (uint64_t)key;
        x ^= x >> 33; x *= 0xff51afd7ed558ccdULL;
        x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL;
        x ^= x >> 33;
        return (int)(x % (uint64_t)bucket_count_);
    }

    /*
    buckets_  ──►  [ Entry* ]  [ Entry* ]  [ Entry* ]  ...   ← 数组，每个格子是 Entry*
                  │           │           │
                  ▼           ▼           ▼
                Entry        Entry        0 (空桶)
                  │
                  ▼
                Entry → ..
    */
    Entry** buckets_;
    int bucket_count_;
    int size_; //记录哈希表里当前存了多少个键值对
};
