// 内存版 B+ 树（教学代码）
//   order : 每个节点最多几个孩子；每个节点最多 order-1 个 key
//   key = int64_t，value = int（行号）；只做插入/点查/范围查，不实现删除
//   内部节点的 key 是分隔键 = 右子树的最小 key（子节点分裂时上移的那个）
#pragma once
#include <cstdint>
#include <utility>
#include <vector>
class BPlusTree {
public:
    explicit BPlusTree(int order = 8);
    ~BPlusTree();
    bool insert(int64_t key, int value);                        // 重复 key 返回 false
    bool erase(int64_t key);                                    // 不存在返回 false
    bool search(int64_t key, int& value) const;                 // 找到返回 true
    void rangeScan(int64_t lo, int64_t hi,
                   std::vector<std::pair<int64_t, int> >& out) const;   // [lo, hi)
    int size() const { return size_; }
    int treeHeight() const;
private:
    struct Node {
        bool is_leaf;
        int count;          // 当前 key 个数
        int64_t* keys;      // 长度 order（多留一格，插入后再分裂）
        int* values;        // 只有叶子用
        Node** children;    // 只有内部节点用，长度 order+1
        Node* next;         // 只有叶子用：右兄弟，范围扫描靠它
    };
    int order_, max_keys_;
    Node* root_;
    int size_;
    Node* createNode(bool is_leaf);
    void freeNode(Node* node);
    int lowerBoundIndex(const Node* node, int64_t key) const;   // 第一个 keys[i] >= key
    int upperBoundIndex(const Node* node, int64_t key) const;   // 第一个 keys[i] >  key
    const Node* findLeaf(const Node* node, int64_t key) const;
    // 返回 true 表示这棵子树分裂了；promoted_key / new_right 是分裂结果
    bool insertRec(Node* node, int64_t key, int value,
                   int64_t& promoted_key, Node*& new_right, bool& duplicate);
    // 删除用：最少 key 个数、子树最小 key、借位/合并、修复下溢
    int minKeys(const Node* node) const;
    int64_t minKeyOf(const Node* node) const;
    bool borrowFromLeft(Node* parent, int ci);
    bool borrowFromRight(Node* parent, int ci);
    void mergeNodes(Node* parent, int li);
    void rebalance(Node* parent, int ci);
};
inline BPlusTree::BPlusTree(int order) {
    order_ = (order < 3) ? 3 : order;     // 最小 3 阶，保证分裂后两边都非空
    max_keys_ = order_ - 1;
    size_ = 0;
    root_ = createNode(true);
}
inline BPlusTree::~BPlusTree() { freeNode(root_); }
inline BPlusTree::Node* BPlusTree::createNode(bool is_leaf) {
    Node* node = new Node();
    node->is_leaf = is_leaf;
    node->count = 0;
    node->keys = new int64_t[max_keys_ + 1];
    node->values = is_leaf ? new int[max_keys_ + 1] : 0;
    node->children = is_leaf ? 0 : new Node*[max_keys_ + 2];
    if (!is_leaf) for (int i = 0; i < max_keys_ + 2; ++i) node->children[i] = 0;
    node->next = 0;
    return node;
}
inline void BPlusTree::freeNode(Node* node) {
    if (node == 0) return;
    if (!node->is_leaf) for (int i = 0; i <= node->count; ++i) freeNode(node->children[i]);
    delete[] node->keys;
    if (node->values) delete[] node->values;
    if (node->children) delete[] node->children;
    delete node;
}
inline int BPlusTree::lowerBoundIndex(const Node* node, int64_t key) const {
    int lo = 0, hi = node->count;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (node->keys[mid] < key) lo = mid + 1; else hi = mid;
    }
    return lo;
}
inline int BPlusTree::upperBoundIndex(const Node* node, int64_t key) const {
    int lo = 0, hi = node->count;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (node->keys[mid] <= key) lo = mid + 1; else hi = mid;
    }
    return lo;
}
// 每层都选"分隔键 > key 的第一个位置"对应的孩子
inline const BPlusTree::Node* BPlusTree::findLeaf(const Node* node, int64_t key) const {
    while (!node->is_leaf) node = node->children[upperBoundIndex(node, key)];
    return node;
}
inline int BPlusTree::treeHeight() const {
    int h = 1;
    const Node* node = root_;
    while (!node->is_leaf) { node = node->children[0]; ++h; }
    return h;
}
inline bool BPlusTree::search(int64_t key, int& value) const {
    const Node* leaf = findLeaf(root_, key);
    int pos = lowerBoundIndex(leaf, key);
    if (pos < leaf->count && leaf->keys[pos] == key) { value = leaf->values[pos]; return true; }
    return false;
}
inline void BPlusTree::rangeScan(int64_t lo, int64_t hi,
                                 std::vector<std::pair<int64_t, int> >& out) const {
    if (lo >= hi) return;
    const Node* leaf = findLeaf(root_, lo);
    while (leaf != 0) {
        for (int i = 0; i < leaf->count; ++i) {
            int64_t k = leaf->keys[i];
            if (k < lo) continue;   // 起始叶子里可能有比 lo 小的 key
            if (k >= hi) return;    // 右开区间，可以结束
            out.push_back(std::make_pair(k, leaf->values[i]));
        }
        leaf = leaf->next;          // 顺着叶子链表往右走
    }
}
inline bool BPlusTree::insert(int64_t key, int value) {
    int64_t promoted_key = 0;
    Node* new_right = 0;
    bool duplicate = false;
    bool split = insertRec(root_, key, value, promoted_key, new_right, duplicate);
    if (duplicate) return false;
    if (split) {                       // 根也满了：造新根，树长高一层
        Node* new_root = createNode(false);
        new_root->keys[0] = promoted_key;
        new_root->children[0] = root_;
        new_root->children[1] = new_right;
        new_root->count = 1;
        root_ = new_root;
    }
    ++size_;
    return true;
}
inline bool BPlusTree::insertRec(Node* node, int64_t key, int value,
                                 int64_t& promoted_key, Node*& new_right,
                                 bool& duplicate) {
    if (node->is_leaf) {
        int pos = lowerBoundIndex(node, key);
        if (pos < node->count && node->keys[pos] == key) {
            duplicate = true;                       // 唯一索引：重复 key 直接拒绝
            return false;
        }
        for (int i = node->count; i > pos; --i) {   // 右移腾出 pos
            node->keys[i] = node->keys[i - 1]; node->values[i] = node->values[i - 1];
        }
        node->keys[pos] = key;
        node->values[pos] = value;
        ++node->count;
        if (node->count <= max_keys_) return false; // 没满，不用分裂
        // 叶子分裂：左半留下，右半搬到新叶子，最右指针接上
        int total = node->count;                    // = max_keys_ + 1
        int left_count = total / 2;
        Node* right = createNode(true);
        right->count = total - left_count;
        for (int i = 0; i < right->count; ++i) {
            right->keys[i] = node->keys[left_count + i];
            right->values[i] = node->values[left_count + i];
        }
        node->count = left_count; right->next = node->next; node->next = right;
        promoted_key = right->keys[0];              // 右叶子的最小 key 上移
        new_right = right;
        return true;
    }
    int child_pos = upperBoundIndex(node, key);     // 先决定进哪个孩子
    int64_t child_promoted = 0;
    Node* child_right = 0;
    bool child_split = insertRec(node->children[child_pos], key, value,
                                 child_promoted, child_right, duplicate);
    if (duplicate || !child_split) return false;
    // 把孩子分裂上来的分隔键 + 新孩子插进本节点
    for (int i = node->count; i > child_pos; --i) node->keys[i] = node->keys[i - 1];
    for (int i = node->count + 1; i > child_pos + 1; --i) node->children[i] = node->children[i - 1];
    node->keys[child_pos] = child_promoted;
    node->children[child_pos + 1] = child_right;
    ++node->count;
    if (node->count <= max_keys_) return false;
    // 内部节点分裂：中间那个 key 上移，不再留在本层
    int total = node->count;                        // = max_keys_ + 1
    int left_count = total / 2;
    Node* right = createNode(false);
    promoted_key = node->keys[left_count];
    right->count = total - 1 - left_count;
    for (int i = 0; i < right->count; ++i) right->keys[i] = node->keys[left_count + 1 + i];
    for (int i = 0; i <= right->count; ++i) right->children[i] = node->children[left_count + 1 + i];
    node->count = left_count;
    new_right = right;
    return true;
}
// 非根节点最少要有多少 key（分布与分裂规则配套，保证合并时一定放得下）
inline int BPlusTree::minKeys(const Node* node) const {
    if (node == root_) return 0;                       // 根可以很空，不参与下溢判断
    return node->is_leaf ? (order_ / 2) : ((order_ - 1) / 2);
}
// 子树里的最小 key：一路往最左走
inline int64_t BPlusTree::minKeyOf(const Node* node) const {
    while (!node->is_leaf) node = node->children[0];
    return node->keys[0];
}
// 把 parent 的 children[li] 与 children[li+1] 合并进 left，并删掉分隔键 li
inline void BPlusTree::mergeNodes(Node* parent, int li) {
    Node* left = parent->children[li];
    Node* right = parent->children[li + 1];
    int base = left->count;
    if (left->is_leaf) {
        for (int i = 0; i < right->count; ++i) {
            left->keys[base + i] = right->keys[i];
            left->values[base + i] = right->values[i];
        }
        left->count = base + right->count;
        left->next = right->next;                       // 叶子链表也要接上
    } else {
        left->keys[base] = parent->keys[li];            // 分隔键下沉到左节点
        for (int i = 0; i < right->count; ++i) left->keys[base + 1 + i] = right->keys[i];
        for (int i = 0; i <= right->count; ++i) left->children[base + 1 + i] = right->children[i];
        left->count = base + 1 + right->count;
    }
    for (int i = li + 1; i < parent->count; ++i) parent->keys[i - 1] = parent->keys[i];
    for (int i = li + 2; i <= parent->count; ++i) parent->children[i - 1] = parent->children[i];
    --parent->count;
    delete[] right->keys;
    if (right->values) delete[] right->values;
    if (right->children) delete[] right->children;
    delete right;
}

// 向 left 兄弟借一个 key；借到返回 true
inline bool BPlusTree::borrowFromLeft(Node* parent, int ci) {
    Node* node = parent->children[ci];
    Node* left = parent->children[ci - 1];
    if (left->count <= minKeys(left)) return false;      // 兄弟也不宽裕
    if (node->is_leaf) {
        for (int i = node->count; i > 0; --i) {
            node->keys[i] = node->keys[i - 1];
            node->values[i] = node->values[i - 1];
        }
        node->keys[0] = left->keys[left->count - 1];
        node->values[0] = left->values[left->count - 1];
        ++node->count;
        --left->count;
        parent->keys[ci - 1] = node->keys[0];             // 分隔键 = 右子树最小 key
    } else {
        for (int i = node->count; i > 0; --i) node->keys[i] = node->keys[i - 1];
        for (int i = node->count + 1; i > 0; --i) node->children[i] = node->children[i - 1];
        node->keys[0] = parent->keys[ci - 1];             // 父节点的分隔键下移
        node->children[0] = left->children[left->count];
        ++node->count;
        --left->count;
        parent->keys[ci - 1] = left->keys[left->count];   // 左兄弟最后一个 key 上移
    }
    return true;
}
// 向 right 兄弟借一个 key；借到返回 true
inline bool BPlusTree::borrowFromRight(Node* parent, int ci) {
    Node* node = parent->children[ci];
    Node* right = parent->children[ci + 1];
    if (right->count <= minKeys(right)) return false;
    if (node->is_leaf) {
        node->keys[node->count] = right->keys[0];
        node->values[node->count] = right->values[0];
        ++node->count;
        for (int i = 0; i + 1 < right->count; ++i) {
            right->keys[i] = right->keys[i + 1];
            right->values[i] = right->values[i + 1];
        }
        --right->count;
        parent->keys[ci] = right->keys[0];
    } else {
        node->keys[node->count] = parent->keys[ci];       // 父节点的分隔键下移到末尾
        node->children[node->count + 1] = right->children[0];
        ++node->count;
        parent->keys[ci] = right->keys[0];
        for (int i = 0; i + 1 < right->count; ++i) right->keys[i] = right->keys[i + 1];
        for (int i = 0; i < right->count; ++i) right->children[i] = right->children[i + 1];
        --right->count;
    }
    return true;
}
// 孩子 ci 下溢：先借，借不到就合并
inline void BPlusTree::rebalance(Node* parent, int ci) {
    if (ci > 0 && borrowFromLeft(parent, ci)) return;
    if (ci + 1 <= parent->count && borrowFromRight(parent, ci)) return;
    if (ci > 0) mergeNodes(parent, ci - 1);
    else        mergeNodes(parent, ci);
}

// 删除：先下降记录路径，再从叶子往上修分隔键和下溢
inline bool BPlusTree::erase(int64_t key) {
    Node* path[64];                       // path[d] = 第 d 层节点（path[0] 是根）
    int idx[64];                          // idx[d]  = 在 path[d] 里选中的孩子下标
    int depth = 0;
    Node* node = root_;
    while (!node->is_leaf) {
        int i = upperBoundIndex(node, key);
        path[depth] = node;
        idx[depth] = i;
        ++depth;
        node = node->children[i];
    }
    int pos = lowerBoundIndex(node, key);
    if (pos >= node->count || node->keys[pos] != key) return false;   // 不存在
    for (int i = pos; i + 1 < node->count; ++i) {                     // 从叶子里删掉
        node->keys[i] = node->keys[i + 1];
        node->values[i] = node->values[i + 1];
    }
    --node->count;
    --size_;

    Node* child = node;
    for (int d = depth - 1; d >= 0; --d) {
        Node* parent = path[d];
        int ci = idx[d];
        // 孩子的最小 key 可能变了，先修分隔键（分隔键 = 右子树最小 key）
        if (ci > 0 && child->count > 0) parent->keys[ci - 1] = minKeyOf(child);
        if (child->count >= minKeys(child)) return true;               // 没下溢，结束
        rebalance(parent, ci);                                          // 借位或合并
        child = parent;                                                 // 父节点可能连锁下溢
    }
    while (!root_->is_leaf && root_->count == 0) {                     // 根收缩，树变矮
        Node* old = root_;
        root_ = old->children[0];
        delete[] old->keys;
        delete[] old->children;
        delete old;
    }
    return true;
}

