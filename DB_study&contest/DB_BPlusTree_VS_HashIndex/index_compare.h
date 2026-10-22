// 两种索引方案的封装，对外接口完全一致
//   方案 A：纯 B+ 树
//   方案 B：B+ 树 + 自适应哈希（插入只写 B+ 树；查询时按访问次数把热点 key 晋升进哈希）
#pragma once
#include <cstdint>
#include <utility>
#include <vector>
#include "bplus_tree.h"
#include "hash_index.h"
// ---------------- 方案 A：纯 B+ 树 ----------------
class IndexPlanA
{
public:
    explicit IndexPlanA(int order = 8) : tree_(order) {}
    bool insert(int64_t key, int value) { return tree_.insert(key, value); }
    bool erase(int64_t key) { return tree_.erase(key); }
    bool search(int64_t key, int &value) { return tree_.search(key, value); }
    void rangeScan(int64_t lo, int64_t hi,
                   std::vector<std::pair<int64_t, int>> &out) const
    {
        tree_.rangeScan(lo, hi, out);
    }
    int size() const { return tree_.size(); }
    int treeHeight() const { return tree_.treeHeight(); }
    int hashEntries() const { return 0; } // A 没有哈希，统一返回 0 / -1
    double hitRate() const { return -1.0; }

private:
    BPlusTree tree_;
};
// ---------------- 方案 B：B+ 树 + 自适应哈希 ----------------
class IndexPlanB
{
public:
    explicit IndexPlanB(int order = 8, int threshold = 2, int bucket_count = 4096)
        : tree_(order), cache_(bucket_count), counts_(bucket_count)
    {
        threshold_ = threshold;
        hash_hits_ = 0;
        point_queries_ = 0;
        promotions_ = 0;
    }
    // 插入阶段：只写 B+ 树，绝不在这里建哈希（写入没有额外代价）
    bool insert(int64_t key, int value) { return tree_.insert(key, value); }
    // 删除时必须同步失效缓存和计数，否则会从哈希里读到已删掉的旧值
    bool erase(int64_t key)
    {
        cache_.erase(key);
        counts_.erase(key);
        return tree_.erase(key);
    }
    bool search(int64_t key, int &value)
    {
        ++point_queries_;
        // 1) 先查哈希，命中就直接返回
        if (cache_.search(key, value))
        {
            ++hash_hits_;
            return true;
        }
        // 2) 未命中才回 B+ 树；key 不存在时不进缓存
        if (!tree_.search(key, value))
            return false;
        // 3) 访问次数 +1，达到阈值就晋升进哈希，下次直接命中
        if (counts_.bump(key) >= threshold_)
        {
            if (cache_.insert(key, value))
                ++promotions_;
        }
        return true;
    }
    // 范围查只走 B+ 树，哈希不参与
    void rangeScan(int64_t lo, int64_t hi,
                   std::vector<std::pair<int64_t, int>> &out) const
    {
        tree_.rangeScan(lo, hi, out);
    }
    int size() const { return tree_.size(); }
    int treeHeight() const { return tree_.treeHeight(); }
    int hashEntries() const { return cache_.size(); }
    int promotions() const { return promotions_; }
    double hitRate() const
    {
        if (point_queries_ == 0)
            return 0.0;
        return (double)hash_hits_ / (double)point_queries_ * 100.0; // 百分比
    }

private:
    BPlusTree tree_;  // 唯一的数据来源，保证 A/B 查询结果一致,相同的tree
    HashIndex cache_; // 热点 key -> value
    /*
    存热点 key 到 value 的映射，是 B+ 树的加速缓存。
    只有访问次数达阈值（threshold_）的 key 才会被 insert 进来。
    search 时先查它：命中就直接返回，不走树 → 这就是点查提速的来源。
    用 HashIndex 的 insert / search / erase 方法（value 语义 = 缓存值）。
    不是全量：只装热点，所以叫"自适应"。
    */

    HashIndex counts_; // key -> 访问次数（判断是否晋升）访问次数表存,每个 key 被查了几次，value 语义 = 次数。
    int threshold_;//晋升阈值
    long long hash_hits_;// 哈希命中次数
    long long point_queries_;//点查总次数
    int promotions_;//晋升次数
};
