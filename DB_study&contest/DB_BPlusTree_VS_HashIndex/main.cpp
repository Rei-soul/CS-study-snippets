// 纯 B+ 树  vs  B+ 树 + 自适应哈希：同一批数据的插入 / 点查 / 范围查对比
// 编译：g++ -O2 -std=c++17 main.cpp -o index_compare
#include <cstdio>
#include <cstdint>
#include <cassert>
#include <chrono>
#include <random>
#include <vector>
#include <algorithm>
#include <utility>
#include "index_compare.h"
static const int KEY_COUNT         = 100000;  // 关键字个数
static const int HOT_KEY_COUNT     = 10000;   // 热点 key 个数
static const int POINT_QUERY_COUNT = 100000;  // 点查次数
static const int RANGE_QUERY_COUNT = 10000;    // 范围查次数
static const int RANGE_SPAN_MIN    = 1;        // 范围查最小跨度（key 个数）
static const int RANGE_SPAN_MAX    = 60;       // 范围查最大跨度（key 个数）
static const int WARMUP_COUNT      = 1000;    // 预热次数（两方案完全一致）
static const int RUNS              = 3;      // 每类测量跑几轮取最好
static const int TREE_ORDER        = 8;      // B+ 树阶数
static const int HASH_THRESHOLD    = 2;      // 同一个 key 访问几次后进哈希
static const int HASH_BUCKETS      = 10000;   // 哈希桶数
static const int HOT_PERCENT       = 80;     // 80% 的查询打热点 key
static const int INSERT_TOTAL      = 20000;  // 实验二：插入序列长度（key 空间 = KEY_COUNT）
static const uint64_t POINT_SEED   = 777001ULL;  // 实验一：点查种子，同一配置下 A/B 共用
static const uint64_t INSERT_SEED  = 555001ULL;  // 实验二：插入序列种子
typedef std::chrono::steady_clock Clock;
static double msSince(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}
// 中文在终端占 2 列，按“显示宽度”补空格才能真正对齐
static int displayWidth(const char* s) {
    int w = 0;
    const unsigned char* p = (const unsigned char*)s;
    while (*p != 0) {
        if (*p < 0x80) { ++w; ++p; }
        else if ((*p >> 5) == 0x06) { w += 1; p += 2; }
        else if ((*p >> 4) == 0x0E) { w += 2; p += 3; }
        else { w += 2; p += 4; }
    }
    return w;
}
// 通用 N 列打印：按“显示宽度”补空格，所以中文表格也能对齐
static void printRowN(const char* const* cell, const int* width, int n) {
    for (int i = 0; i < n; ++i) {
        fputs(cell[i], stdout);
        for (int k = displayWidth(cell[i]); k < width[i]; ++k) putchar(' ');
    }
    putchar('\n');
}
static void printRow(const char* c0, const char* c1, const char* c2, const char* c3) {
    const char* cell[4] = {c0, c1, c2, c3};
    const int width[4] = {30, 19, 19, 12};
    printRowN(cell, width, 4);
}
static void printLine() { for (int i = 0; i < 80; ++i) putchar('-'); putchar('\n'); }
static void printTitle(const char* title) { printf("\n%s\n", title); printLine(); }
struct DataSet {
    std::vector<int64_t> keys;      // 10000 个不重复 key（顺序已打乱）
    std::vector<int> values;        // 对应的行号
    std::vector<int64_t> hot_keys;  // 1000 个热点 key
};
struct RangeQuery { int64_t lo; int64_t hi; };
struct BenchResult {
    double insert_ms, point_ms, range_ms, delete_ms, avg_ns;
    long long checksum;
    int tree_height, hash_entries, remaining;
    double hit_rate;
};
// ---------------- 数据生成（固定种子，可复现） ----------------
/*
先排列再放大：保证 key 不重复且分布散乱。
*100003+7：让 key 像真实的大整数（非连续），避免"连续 key 对哈希/树都太友好"的假象。
*/
static DataSet makeData() {
    DataSet d;
    std::mt19937_64 rng(20260520ULL);
    std::vector<int> perm(KEY_COUNT);                 // 先做 0..N-1 的排列，保证 key 不重复
    for (int i = 0; i < KEY_COUNT; ++i) perm[i] = i;
    std::shuffle(perm.begin(), perm.end(), rng);
    d.keys.resize(KEY_COUNT);
    d.values.resize(KEY_COUNT);
    for (int i = 0; i < KEY_COUNT; ++i) {
        d.keys[i] = (int64_t)perm[i] * 100003 + 7;    // 放大取值，像随机大整数
        d.values[i] = i;
    }
    d.hot_keys.assign(d.keys.begin(), d.keys.begin() + HOT_KEY_COUNT);
    return d;
}
/*
点查序列生成（实验一按这个函数扫描热点集中度）。
hot_percent  ：多大概率打热点 key
hot_key_count：热点区多大（只从 d.keys 的前 hot_key_count 个里取，那批 key 已打乱）
seed         ：固定种子，同一配置下 A/B 必须共用同一份序列

注意：hot_key_count 上限是 KEY_COUNT（原来固定用 HOT_KEY_COUNT=10000 的 hot_keys，
而 hot_keys 就是 d.keys 的前 10000 个，所以 d.keys[rng()%10000] 与 d.hot_keys[rng()%10000] 等价，
单配置实验的查询序列和改动前逐字节相同）。
*/
static std::vector<int64_t> makePointQueries(const DataSet& d, int hot_percent,
                                             int hot_key_count, uint64_t seed) {
    assert(hot_percent >= 0 && hot_percent <= 100);
    assert(hot_key_count >= 0 && hot_key_count <= KEY_COUNT);
    std::vector<int64_t> q(POINT_QUERY_COUNT);
    std::mt19937_64 rng(seed);
    const bool has_hot = (hot_percent > 0 && hot_key_count > 0);
    for (int i = 0; i < POINT_QUERY_COUNT; ++i) {
        if (has_hot && (int)(rng() % 100ULL) < hot_percent)
            q[i] = d.keys[rng() % (uint64_t)hot_key_count];   // 热区：反复打这 hot_key_count 个
        else
            q[i] = d.keys[rng() % (uint64_t)KEY_COUNT];       // 冷区：全库随机
    }
    return q;
}
// 每个 key 最多被查一次：用来模拟“完全没有重复访问”的理想均匀分布，
// 这样命中率必然是 0，可以单独隔离出“多一次哈希查找”的固定开销。
static std::vector<int64_t> makeUniquePointQueries(const DataSet& d, uint64_t seed) {
    assert(POINT_QUERY_COUNT <= KEY_COUNT);
    std::vector<int> perm(KEY_COUNT);
    for (int i = 0; i < KEY_COUNT; ++i) perm[i] = i;
    std::mt19937_64 rng(seed);
    std::shuffle(perm.begin(), perm.end(), rng);
    std::vector<int64_t> q(POINT_QUERY_COUNT);
    for (int i = 0; i < POINT_QUERY_COUNT; ++i) q[i] = d.keys[perm[i]];
    return q;
}
// 单配置对比实验沿用原来那套参数
static std::vector<int64_t> makePointQueries(const DataSet& d) {
    return makePointQueries(d, HOT_PERCENT, HOT_KEY_COUNT, POINT_SEED);
}
static std::vector<RangeQuery> makeRangeQueries(const DataSet& d) {
    std::vector<RangeQuery> q(RANGE_QUERY_COUNT);
    std::mt19937_64 rng(777002ULL);
    for (int i = 0; i < RANGE_QUERY_COUNT; ++i) {
        q[i].lo = d.keys[rng() % KEY_COUNT];
        q[i].hi = q[i].lo + (int64_t)(RANGE_SPAN_MIN + rng() % (RANGE_SPAN_MAX - RANGE_SPAN_MIN + 1)) * 100003;   // 跨度按常量生成
    }
    return q;
}
// ---------------- 实验二用的插入序列（含重复 key） ----------------
struct InsertSeq {
    std::vector<int64_t> keys;           // 插入序列（长度 total，可能含重复）
    std::vector<int64_t> distinct_keys;  // 其中出现过的不同 key
    std::vector<int> first_value;        // distinct_keys[i] 首次插入时的 value
};
/*
dup_percent：目标重复率（%），重复率 = 1 - 不同key数 / 插入总数
total      ：序列长度
key_space  ：key 空间大小（不同 key 不能超过它）
seed       ：固定种子，保证可复现

做法：先按目标重复率算出“应该有多少个不同 key”，随机抽这么多 key 各放一次
（这一步保证不同 key 数精确达到目标，重复率不会因为随机性跑偏），
剩下的位置全部从这些 key 里重复抽。value 用“首次出现的下标”，
这样抽查时只要 value == first_value[i] 就证明重复插入没有覆盖原值。
*/
static InsertSeq makeInsertSequence(int dup_percent, int total, int key_space, uint64_t seed) {
    assert(dup_percent >= 0 && dup_percent <= 100);
    assert(total > 0 && key_space > 0);
    int want = (int)(((long long)total * (100 - dup_percent) + 50) / 100);  // 四舍五入
    if (want < 1) want = 1;
    if (want > key_space) want = key_space;
    if (want > total) want = total;
    std::mt19937_64 rng(seed);
    std::vector<int> perm(key_space);                     // 抽哪 want 个 key
    for (int i = 0; i < key_space; ++i) perm[i] = i;
    std::shuffle(perm.begin(), perm.end(), rng);
    std::vector<int> order(want);                         // 这些 key 首次出现的先后
    for (int i = 0; i < want; ++i) order[i] = i;
    std::shuffle(order.begin(), order.end(), rng);
    InsertSeq s;
    s.distinct_keys.resize(want);
    s.first_value.resize(want);
    for (int i = 0; i < want; ++i) s.distinct_keys[i] = (int64_t)perm[i] * 100003 + 7;
    s.keys.resize(total);
    for (int i = 0; i < want; ++i) {                      // 先把每个不同 key 放一次
        s.keys[i] = s.distinct_keys[order[i]];
        s.first_value[order[i]] = i;
    }
    for (int i = want; i < total; ++i)                    // 其余位置全部是重复 key
        s.keys[i] = s.distinct_keys[order[rng() % (uint64_t)want]];
    return s;
}

// 固定不同 key 数，改变总插入次数：用来单独量化"重复 insert 被拒"的开销
static InsertSeq makeInsertSequenceFixedDistinct(int distinct, int total,
                                                 int key_space, uint64_t seed) {
    assert(distinct > 0 && distinct <= key_space);
    assert(total >= distinct);
    std::mt19937_64 rng(seed);
    std::vector<int> perm(key_space);
    for (int i = 0; i < key_space; ++i) perm[i] = i;
    std::shuffle(perm.begin(), perm.end(), rng);
    std::vector<int> order(distinct);
    for (int i = 0; i < distinct; ++i) order[i] = i;
    std::shuffle(order.begin(), order.end(), rng);

    InsertSeq s;
    s.distinct_keys.resize(distinct);
    s.first_value.resize(distinct);
    for (int i = 0; i < distinct; ++i)
        s.distinct_keys[i] = (int64_t)perm[i] * 100003 + 7;
    s.keys.resize(total);
    for (int i = 0; i < distinct; ++i) {          // 先各放一次
        s.keys[i] = s.distinct_keys[order[i]];
        s.first_value[order[i]] = i;
    }
    for (int i = distinct; i < total; ++i)        // 其余全是重复
        s.keys[i] = s.distinct_keys[order[rng() % (uint64_t)distinct]];
    return s;
}
// ---------------- 自检：先用小数据把正确性钉死 ----------------
static void selfTest() {
    printf("================ 自检 ================\n");
    {   // 插入 10 个并逐个查回；越界查不到；重复 key 被拒绝且不改原值
        BPlusTree t(8);
        for (int i = 0; i < 10; ++i) assert(t.insert((int64_t)i * 10, i * 100));
        for (int i = 0; i < 10; ++i) { int v = -1; assert(t.search((int64_t)i * 10, v) && v == i * 100); }
        int v = -1;
        assert(!t.search(5, v) && !t.search(1000, v));
        assert(!t.insert(30, 999));                    // 重复 key 必须返回 false
        assert(t.search(30, v) && v == 300);           // 原值不能被改
        assert(t.size() == 10);
    }
    {   // 范围查 [lo, hi)：左闭右开
        BPlusTree t(8);
        for (int i = 0; i < 10; ++i) t.insert((int64_t)i * 10, i);
        std::vector<std::pair<int64_t, int> > out;
        t.rangeScan(20, 60, out);                                       // 20 30 40 50
        assert(out.size() == 4 && out[0].first == 20 && out[3].first == 50);
        out.clear(); t.rangeScan(21, 30, out); assert(out.empty());      // 中间没有 key
        out.clear(); t.rangeScan(20, 30, out); assert(out.size() == 1);  // 30 被右开排除
        out.clear(); t.rangeScan(60, 20, out); assert(out.empty());      // lo >= hi
    }
    {   // 删除：删掉的查不到，剩下的还在，范围查同步更新
        BPlusTree t(8);
        for (int i = 0; i < 10; ++i) t.insert((int64_t)i * 10, i);
        assert(t.erase(0) && t.erase(50) && t.erase(90));
        int v = -1;
        assert(!t.search(0, v) && !t.search(50, v) && !t.search(90, v));
        assert(t.search(10, v) && v == 1);
        assert(!t.erase(50));                     // 已经删过了
        assert(!t.erase(35));                     // 本来就没有
        assert(t.size() == 7);
        std::vector<std::pair<int64_t, int> > out;
        t.rangeScan(0, 100, out);
        assert(out.size() == 7 && out[0].first == 10 && out[6].first == 80);
    }
    {   // 全部删空后还能继续插入（根收缩 / 空树边界）
        BPlusTree t(4);
        for (int i = 0; i < 20; ++i) t.insert(i, i);
        for (int i = 0; i < 20; ++i) assert(t.erase(i));
        assert(t.size() == 0);
        int v = -1;
        assert(!t.search(0, v) && !t.search(19, v));
        for (int i = 0; i < 20; ++i) assert(t.insert(i * 2, i));
        assert(t.search(38, v) && v == 19);
    }
    // 暴力数组对拍：插入和删除混跑 6000 步，3..8 阶都覆盖（分裂/借位/合并/根收缩）
    for (int order = 3; order <= 8; ++order) {
        const int span = 800;
        std::vector<char> has(span, 0);
        std::vector<int> val(span, 0);
        std::mt19937_64 rng(12345ULL + order);
        BPlusTree t(order);
        int live = 0;
        for (int step = 0; step < 6000; ++step) {
            int k = (int)(rng() % span);
            if (rng() % 2 == 0) {
                bool ok = t.insert(k, k * 7);
                assert(ok == (has[k] == 0));
                if (ok) { has[k] = 1; val[k] = k * 7; ++live; }
            } else {
                bool ok = t.erase(k);
                assert(ok == (has[k] != 0));
                if (ok) { has[k] = 0; --live; }
            }
            assert(t.size() == live);
        }
        for (int k = 0; k < span; ++k) {                        // 最终逐 key 点查
            int v = -1; bool found = t.search(k, v);
            assert(found == (has[k] != 0));
            if (found) assert(v == val[k]);
        }
        for (int q = 0; q < 30; ++q) {                          // 范围查 vs 暴力过滤
            int lo = (int)(rng() % span), hi = lo + (int)(rng() % 100);
            std::vector<std::pair<int64_t, int> > got, exp;
            t.rangeScan(lo, hi, got);
            for (int k = lo; k < hi && k < span; ++k)
                if (has[k]) exp.push_back(std::make_pair((int64_t)k, val[k]));
            assert(got.size() == exp.size());
            for (size_t i = 0; i < got.size(); ++i)
                assert(got[i].first == exp[i].first && got[i].second == exp[i].second);
        }
    }
    {   // 方案 A / B：插入 → 查询 → 删除 → 再插入，结果必须始终一致
        const int span = 3000;
        std::mt19937_64 rng(777ULL);
        IndexPlanA a(8);
        IndexPlanB b(8, 2, 1024);
        std::vector<int64_t> keys;
        for (int i = 0; i < 600; ++i) {
            int64_t k = (int64_t)(rng() % span);
            if (a.insert(k, i)) { assert(b.insert(k, i)); keys.push_back(k); }
        }
        for (int i = 0; i < 2000; ++i) {                        // 点查一致（含查不到）
            int64_t k = (int64_t)(rng() % (span * 2));
            int va = -1, vb = -1;
            bool fa = a.search(k, va), fb = b.search(k, vb);
            assert(fa == fb);
            if (fa) assert(va == vb);
        }
        for (int q = 0; q < 20; ++q) {                          // 范围查一致
            int64_t lo = (int64_t)(rng() % span), hi = lo + (int64_t)(rng() % 400);
            std::vector<std::pair<int64_t, int> > ra, rb;
            a.rangeScan(lo, hi, ra);
            b.rangeScan(lo, hi, rb);
            assert(ra.size() == rb.size());
            for (size_t i = 0; i < ra.size(); ++i)
                assert(ra[i].first == rb[i].first && ra[i].second == rb[i].second);
        }
        for (size_t i = 0; i < keys.size(); ++i) {               // 全删：B 必须让缓存失效
            assert(a.erase(keys[i]) == b.erase(keys[i]));
        }
        for (int k = 0; k < span; ++k) {
            int va = -1, vb = -1;
            assert(!a.search((int64_t)k, va));
            assert(!b.search((int64_t)k, vb));                   // 缓存没清这里就会失败
        }
        for (size_t i = 0; i < keys.size(); ++i) {               // 再插回去：不能读到旧值
            assert(a.insert(keys[i], (int)i + 1000));
            assert(b.insert(keys[i], (int)i + 1000));
        }
        for (size_t i = 0; i < keys.size(); ++i) {
            int va = -1, vb = -1;
            assert(a.search(keys[i], va) && b.search(keys[i], vb));
            assert(va == (int)i + 1000 && vb == va);
        }
    }
    printf("自检全部通过：插入 / 点查 / 范围查 / 删除 / 重复key / 方案A与B一致\n\n");
}
// ---------------- 两方案跑完全相同的流程，只有“查表方式”不同 ----------------
template <typename PlanType>
static BenchResult runPlan(PlanType& plan, const DataSet& d,
                           const std::vector<int64_t>& point_q,
                           const std::vector<RangeQuery>& range_q) 
{
    BenchResult r;
    Clock::time_point t0 = Clock::now();
    for (int i = 0; i < KEY_COUNT; ++i) plan.insert(d.keys[i], d.values[i]);// 插入一万条
    r.insert_ms = msSince(t0);//记录插入耗时
    r.tree_height = plan.treeHeight();
    int v = 0;                                      // 预热：两方案都用前 100 条
    for (int i = 0; i < WARMUP_COUNT; ++i) plan.search(point_q[i], v);
    //开始点查计时
    long long sum = 0;
    /*
    sum：累加所有查到的 value。作用有二：
    防止编译器优化掉查询——如果查询结果没人用，-O2 可能把整个 search 调用删掉，测出假快；
    作为 checksum，最后校验 A、B 结果一致。
    */
    t0 = Clock::now();
    //点查开始10000次
    /*
    跑 POINT_QUERY_COUNT（1 万）次点查，key 来自预生成的 point_q（80% 热点、20% 随机）。
    命中就 sum += v；未命中不计。
    对 B：前几次未命中的 key 会回退 B+ 树，访问次数达到阈值后进哈希，后续命中走哈希——这正是"自适应"生效的过程。
    */
    for (int i = 0; i < POINT_QUERY_COUNT; ++i) {
        if (plan.search(point_q[i], v)) sum += v;
    }
    r.point_ms = msSince(t0);
    r.avg_ns = r.point_ms * 1000000.0 / POINT_QUERY_COUNT;
    /*
    point_ms：1 万次总耗时（毫秒）。
    avg_ns：平均每次纳秒 = 毫秒 × 1e6 / 次数（1 ms = 1e6 ns）。用 double 保证精度。
    */


    /*
    out：范围查结果的输出容器，元素是 (key, value) 对。
    reserve(256)：预分配容量，避免每次 rangeScan 里 push_back 触发重新分配、把分配开销混进计时。范围跨度 1..60 个 key，256 足够。
    注意：out 在循环内 clear() 复用，不重新构造，减少噪声。
    */
    std::vector<std::pair<int64_t, int> > out; out.reserve(256);
    t0 = Clock::now();
    
    
    /*
    跑 RANGE_QUERY_COUNT（100）次范围查，区间来自 range_q（左闭右开 [lo, hi)）。
    out.clear()：清空上次结果，保留容量。
    plan.rangeScan(lo, hi, out)：把 [lo,hi) 内的 (key,value) 追加进 out。A、B 都走同一棵 B+ 树，哈希不参与范围查。
    内层 for 把结果 value 累加进 sum：同样防优化 + 并进 checksum。
    */
    for (int i = 0; i < RANGE_QUERY_COUNT; ++i) {
        out.clear();
        plan.rangeScan(range_q[i].lo, range_q[i].hi, out);
        for (size_t k = 0; k < out.size(); ++k) sum += out[k].second;
    }
    r.range_ms = msSince(t0);
    /*
    checksum = sum：点查 + 范围查所有 value 的累加。A、B 必须相同，否则说明结果不一致。
    hash_entries：哈希表条目数（B 有效，A 返回 0）——反映有多少 key 因访问达标进了哈希。
    hit_rate：哈希命中率（B 有效，A 返回 0）——点查里多少比例直接命中哈希、没回退 B+ 树。
    */
    r.checksum = sum; r.hash_entries = plan.hashEntries(); r.hit_rate = plan.hitRate();

    /*
    按原插入顺序把 1 万个 key 全删掉。
    对 A：B+ 树删除（触发合并/借位/根收缩）。
    对 B：除了删 B+ 树，还必须让哈希缓存失效——否则之后 search 会返回已删 key 的旧值。这也是 selfTest 里专门断言的点。
    */
    t0 = Clock::now();                               // 删除：两边都删光，计时对比
    for (int i = 0; i < KEY_COUNT; ++i) plan.erase(d.keys[i]);
    r.delete_ms = msSince(t0);
    r.remaining = plan.size();                       // 应该删到 0
    return r;
}
// 同一时刻只让一棵树活着，避免两棵树同时占 cache 影响对比公平性
static BenchResult runBench(const DataSet& d, const std::vector<int64_t>& point_q,
                            const std::vector<RangeQuery>& range_q, bool use_hash) {
    if (use_hash) { IndexPlanB plan(TREE_ORDER, HASH_THRESHOLD, HASH_BUCKETS); return runPlan(plan, d, point_q, range_q); }
    IndexPlanA plan(TREE_ORDER);
    return runPlan(plan, d, point_q, range_q);
}
// 只插入、不查询：验证 B 方案没有额外写入代价
static double insertOnly(const DataSet& d, bool use_hash) {
    double best = 1e18;
    for (int r = 0; r < RUNS; ++r) {
        Clock::time_point t0 = Clock::now();
        if (use_hash) {
            IndexPlanB plan(TREE_ORDER, HASH_THRESHOLD, HASH_BUCKETS);
            for (int i = 0; i < KEY_COUNT; ++i) plan.insert(d.keys[i], d.values[i]);
        } else {
            IndexPlanA plan(TREE_ORDER);
            for (int i = 0; i < KEY_COUNT; ++i) plan.insert(d.keys[i], d.values[i]);
        }
        double ms = msSince(t0);
        if (ms < best) best = ms;
    }
    return best;
}
// 每个指标取多轮里最好的一次（微秒级测量噪声大，best-of-N 更稳）
static BenchResult better(BenchResult x, const BenchResult& y) {
    if (y.insert_ms < x.insert_ms) x.insert_ms = y.insert_ms;
    if (y.point_ms < x.point_ms) x.point_ms = y.point_ms;
    if (y.range_ms < x.range_ms) x.range_ms = y.range_ms;
    if (y.delete_ms < x.delete_ms) x.delete_ms = y.delete_ms;
    x.avg_ns = x.point_ms * 1000000.0 / POINT_QUERY_COUNT;
    return x;
}
static void printNumRow(const char* name, double a, double b, int prec) {
    char ba[32], bb[32], bd[32];
    snprintf(ba, sizeof(ba), "%.*f", prec, a);
    snprintf(bb, sizeof(bb), "%.*f", prec, b);
    snprintf(bd, sizeof(bd), "%+.1f%%", (b - a) / a * 100.0);
    printRow(name, ba, bb, bd);
}
// ================= 实验一：查询热点集中度扫描 =================
/*
固定数据量与点查次数，只改两件事：
  热点占比 hot_percent     —— 多少比例的点查打热点 key
  热点区大小 hot_key_count —— 热点 key 有几个
每档配置只生成一次点查序列，A/B 共用；每档跑 RUNS 轮，逐指标取最好。
热点 key 都取自 d.keys 的前 hot_key_count 个（这批 key 已经打乱，相当于随机挑的）。
*/
static void hotSpotScan(const DataSet& d, const std::vector<RangeQuery>& range_q) {
    static const int HOT_PERCENTS[5]   = {0, 0, 50, 80, 80};
    /*
    与 hot_percent 配套的热点区大小（热点 key 越多，每个 key 被重复访问的次数越少）：
      0%  → 0      没有热点，点查均匀落在全库 KEY_COUNT 个 key 上（会有生日悖论式的偶然重复）
      0%  → 0      同上但每个 key 最多查一次，理想无重复，用来隔离“多一次哈希查找”的固定开销
      50% → 5000   5000 个 key 吃下 50% 的查询，平均每个 key 约 10 次访问
      80% → 1000   1000 个 key 吃下 80% 的查询，平均每个 key 约 80 次访问
      95% → 100    100 个 key 吃下 95% 的查询，平均每个 key 约 950 次访问
    */
    static const int HOT_KEY_COUNTS[5] = {0, 0, 5000, 5000, 10000};
    static const int NO_REPEAT[5]      = {0, 1, 0, 0, 0};      // 1 = 用“每个 key 只查一次”的序列
    static const int W[8] = {12, 10, 10, 10, 8, 10, 10, 10};   // 合计 80，和 printLine() 对齐
    const int cfg_n = (int)(sizeof(HOT_PERCENTS) / sizeof(HOT_PERCENTS[0]));
    printTitle("实 验 一：查询热点集中度扫描");
    printf("关键字个数 : %d ；点查次数 : %d ；哈希阈值 : %d ；B+树阶数 : %d\n",
           KEY_COUNT, POINT_QUERY_COUNT, HASH_THRESHOLD, TREE_ORDER);
    printf("哈希桶数   : %d；\n", HASH_BUCKETS);
    printf("             热点集中时缓存只有百~千条，装填率远低于 1，哈希查找接近 O(1)）\n");
    printf("每档配置只生成一次点查序列，A/B 共用同一份；每档跑 %d 轮取最好\n", RUNS);
    printLine();
    const char* head[8] = {"热点占比", "热点key数", "A点查ns", "B点查ns",
                           "提速x", "命中率", "哈希条目", "checksum"};
    printRowN(head, W, 8);
    printLine();
    for (int c = 0; c < cfg_n; ++c) {
        const int hp = HOT_PERCENTS[c];
        const int hk = HOT_KEY_COUNTS[c];
        const bool norep = (NO_REPEAT[c] != 0);
        std::vector<int64_t> pq = norep ? makeUniquePointQueries(d, POINT_SEED)
                                        : makePointQueries(d, hp, hk, POINT_SEED);
        BenchResult a = runBench(d, pq, range_q, false);
        BenchResult b = runBench(d, pq, range_q, true);
        for (int r = 1; r < RUNS; ++r) {
            a = better(a, runBench(d, pq, range_q, false));
            b = better(b, runBench(d, pq, range_q, true));
        }
        assert(a.checksum == b.checksum);                  // 同一批查询，结果必须一模一样
        assert(a.remaining == 0 && b.remaining == 0);      // 删除阶段必须清空
        char c0[16], c1[16], c2[16], c3[16], c4[16], c5[16], c6[16], c7[16];
        if (norep) snprintf(c0, sizeof(c0), "0%%(无重复)");
        else       snprintf(c0, sizeof(c0), "%d%%", hp);
        snprintf(c1, sizeof(c1), "%d", hk);
        snprintf(c2, sizeof(c2), "%.1f", a.avg_ns);
        snprintf(c3, sizeof(c3), "%.1f", b.avg_ns);
        snprintf(c4, sizeof(c4), "%.2fx", a.avg_ns / b.avg_ns);
        snprintf(c5, sizeof(c5), "%.1f%%", b.hit_rate);
        snprintf(c6, sizeof(c6), "%d", b.hash_entries);
        snprintf(c7, sizeof(c7), "%s", (a.checksum == b.checksum) ? "一致" : "不一致");
        const char* cell[8] = {c0, c1, c2, c3, c4, c5, c6, c7};
        printRowN(cell, W, 8);
    }
    printLine();
    
}

int main() {
    selfTest();
    DataSet data = makeData();
    std::vector<int64_t> point_q = makePointQueries(data);
    std::vector<RangeQuery> range_q = makeRangeQueries(data);
    printTitle("实 验 参 数");
    printf("关键字个数 : %d（不重复，value = 行号）\n", KEY_COUNT);
    printf("热点 key   : %d（点查 %d%%指向这些数量的key）\n", HOT_KEY_COUNT, HOT_PERCENT);
    printf("点查次数   : %d\n", POINT_QUERY_COUNT);
    printf("范围查次数 : %d（左闭右开，跨度 %d..%d 个 key）\n",
           RANGE_QUERY_COUNT, RANGE_SPAN_MIN, RANGE_SPAN_MAX);
    printf("B+树阶数   : %d    哈希桶数 : %d\n", TREE_ORDER, HASH_BUCKETS);
    printf("哈希阈值   : 同一个 key 被访问 %d 次后进入哈希\n", HASH_THRESHOLD);
    BenchResult a = runBench(data, point_q, range_q, false);
    BenchResult b = runBench(data, point_q, range_q, true);
    for (int r = 1; r < RUNS; ++r) {
        a = better(a, runBench(data, point_q, range_q, false));
        b = better(b, runBench(data, point_q, range_q, true));
    }
    printTitle("对 比 结 果（同一批数据 / 同一批查询 / 同一顺序，3 轮取最好）");
    printRow("指标", "方案A(纯B+树)", "方案B(B+树+哈希)", "差异");
    printLine();
    // ---- 动态标签：按真实常量生成，避免写死"1万条"与实际数据不符 ----
    char lbl_insert[48], lbl_point[48], lbl_range[48], lbl_delete[48];
    snprintf(lbl_insert, sizeof(lbl_insert), "插入%d条耗时(ms)", KEY_COUNT);
    snprintf(lbl_point,  sizeof(lbl_point),  "点查%d次耗时(ms)", POINT_QUERY_COUNT);
    snprintf(lbl_range,  sizeof(lbl_range),  "范围查%d次耗时(ms)", RANGE_QUERY_COUNT);
    snprintf(lbl_delete, sizeof(lbl_delete), "删除%d条耗时(ms)", KEY_COUNT);
    printNumRow(lbl_insert, a.insert_ms, b.insert_ms, 3);
    printNumRow(lbl_point,  a.point_ms,  b.point_ms, 3);
    printNumRow("点查平均每次(ns)", a.avg_ns, b.avg_ns, 1);
    printNumRow(lbl_range,  a.range_ms,  b.range_ms, 3);
    printNumRow(lbl_delete, a.delete_ms, b.delete_ms, 3);
    char buf[32], buf2[32];
    snprintf(buf, sizeof(buf), "%.1f%%", b.hit_rate);
    printRow("哈希命中率", "-", buf, "-");
    snprintf(buf, sizeof(buf), "%d", b.hash_entries);
    printRow("哈希条目数", "-", buf, "-");
    snprintf(buf2, sizeof(buf2), "%lld", a.checksum);
    snprintf(buf, sizeof(buf), "%lld", b.checksum);
    printRow("结果校验和(必须相同)", buf2, buf, (a.checksum == b.checksum) ? "一致" : "不一致!");
    printLine();
    // ---- 两个扫描实验：各自独立分段、独立表格，互不干扰 ----
    hotSpotScan(data, range_q);
    return 0;
}