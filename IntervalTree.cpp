#include "IntervalTree.h"
#include <limits>

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------

int IntervalTree::height(const std::shared_ptr<Node>& n) {
    return n ? n->height : 0;
}

int IntervalTree::balance(const std::shared_ptr<Node>& n) {
    return n ? height(n->left) - height(n->right) : 0;
}

void IntervalTree::refreshNode(std::shared_ptr<Node>& n) {
    if (!n) return;
    n->height   = 1 + std::max(height(n->left), height(n->right));
    int mx      = n->event.high;
    if (n->left)  mx = std::max(mx, n->left->max_high);
    if (n->right) mx = std::max(mx, n->right->max_high);
    n->max_high = mx;
}

std::shared_ptr<Node> IntervalTree::rotateRight(std::shared_ptr<Node> y) {
    auto x  = y->left;
    auto T2 = x->right;
    x->right = y;
    y->left  = T2;
    refreshNode(y);
    refreshNode(x);
    return x;
}

std::shared_ptr<Node> IntervalTree::rotateLeft(std::shared_ptr<Node> x) {
    auto y  = x->right;
    auto T2 = y->left;
    y->left  = x;
    x->right = T2;
    refreshNode(x);
    refreshNode(y);
    return y;
}

std::shared_ptr<Node> IntervalTree::rebalance(std::shared_ptr<Node> n) {
    refreshNode(n);
    int bf = balance(n);

    // Left-heavy
    if (bf > 1) {
        if (balance(n->left) < 0)
            n->left = rotateLeft(n->left);  // Left-Right
        return rotateRight(n);
    }
    // Right-heavy
    if (bf < -1) {
        if (balance(n->right) > 0)
            n->right = rotateRight(n->right);  // Right-Left
        return rotateLeft(n);
    }
    return n;
}

bool IntervalTree::overlap(const Event& a, const Event& b) {
    // Half-open intervals [low, high): overlap iff a.low < b.high && b.low < a.high
    return a.low < b.high && b.low < a.high;
}

// ---------------------------------------------------------------------------
// Insert
// ---------------------------------------------------------------------------

std::shared_ptr<Node> IntervalTree::insert(std::shared_ptr<Node> n,
                                            const Event& e,
                                            bool& conflict) const {
    if (!n) return std::make_shared<Node>(e);

    // Conflict check at this node
    if (overlap(n->event, e)) {
        conflict = true;
        return n;  // abort insertion, tree unchanged
    }

    if (e.low < n->event.low) {
        n->left  = insert(n->left,  e, conflict);
    } else if (e.low > n->event.low) {
        n->right = insert(n->right, e, conflict);
    } else {
        // Same low — disambiguate by id to maintain strict BST property
        if (e.id < n->event.id)
            n->left  = insert(n->left,  e, conflict);
        else
            n->right = insert(n->right, e, conflict);
    }

    if (conflict) return n;

    return rebalance(n);
}

// ---------------------------------------------------------------------------
// Remove
// ---------------------------------------------------------------------------

const Node* IntervalTree::minNode(const std::shared_ptr<Node>& n) {
    const Node* cur = n.get();
    while (cur && cur->left) cur = cur->left.get();
    return cur;
}

std::shared_ptr<Node> IntervalTree::remove(std::shared_ptr<Node> n,
                                            int id,
                                            bool& found) {
    if (!n) return nullptr;

    // Search: we don't know the key, so we do a full-tree id search
    // Try left subtree first, then right
    n->left  = remove(n->left,  id, found);
    if (!found && n->event.id == id) {
        found = true;
        if (!n->left && !n->right) return nullptr;
        if (!n->left)  return n->right;
        if (!n->right) return n->left;

        // Two children: replace with in-order successor
        const Node* successor = minNode(n->right);
        n->event = successor->event;
        bool dummy = false;
        n->right = remove(n->right, successor->event.id, dummy);
    } else if (!found) {
        n->right = remove(n->right, id, found);
    }

    return found ? rebalance(n) : n;
}

// ---------------------------------------------------------------------------
// Overlap search
// ---------------------------------------------------------------------------

const Node* IntervalTree::searchOverlap(const std::shared_ptr<Node>& n,
                                         const Event& e) {
    if (!n) return nullptr;

    if (overlap(n->event, e)) return n.get();

    // If left subtree's max_high > e.low, there MIGHT be an overlap there
    if (n->left && n->left->max_high > e.low)
        return searchOverlap(n->left, e);

    return searchOverlap(n->right, e);
}

// ---------------------------------------------------------------------------
// In-order traversals
// ---------------------------------------------------------------------------

void IntervalTree::inorder(const std::shared_ptr<Node>& n,
                            std::vector<std::pair<int,int>>& out) {
    if (!n) return;
    inorder(n->left, out);
    out.push_back({n->event.low, n->event.high});
    inorder(n->right, out);
}

void IntervalTree::inorderEvents(const std::shared_ptr<Node>& n,
                                  std::vector<Event>& out) {
    if (!n) return;
    inorderEvents(n->left, out);
    out.push_back(n->event);
    inorderEvents(n->right, out);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool IntervalTree::addEvent(int id, int low, int high, const std::string& title) {
    if (low >= high)
        throw std::invalid_argument("Event must have low < high");

    Event e{id, low, high, title};
    bool conflict = false;
    root_ = insert(root_, e, conflict);
    if (!conflict) ++size_;
    return !conflict;
}

const Event* IntervalTree::checkConflict(int low, int high) const {
    if (low >= high) return nullptr;
    Event dummy{-1, low, high, ""};
    const Node* n = searchOverlap(root_, dummy);
    return n ? &n->event : nullptr;
}

bool IntervalTree::removeEvent(int id) {
    bool found = false;
    root_ = remove(root_, id, found);
    if (found) --size_;
    return found;
}

void IntervalTree::collectIntervals(std::vector<std::pair<int,int>>& out) const {
    inorder(root_, out);
}

void IntervalTree::collectEvents(std::vector<Event>& out) const {
    inorderEvents(root_, out);
}

// ---------------------------------------------------------------------------
// findNearestFreeSlot
//
// Algorithm:
//   1. Collect all existing intervals in sorted order (in-order traversal).
//   2. Evaluate every gap (before-first, between-events, after-last).
//   3. Among gaps large enough to fit `duration`, return the start that
//      minimises |candidate_start - requested_low|.
// ---------------------------------------------------------------------------

int IntervalTree::findNearestFreeSlot(int low, int high) const {
    if (low >= high)
        throw std::invalid_argument("Slot query must have low < high");

    const int duration = high - low;

    std::vector<std::pair<int,int>> intervals;
    collectIntervals(intervals);

    // No events at all — the requested slot is free
    if (intervals.empty()) return low;

    int bestStart   = -1;
    int minDistance = std::numeric_limits<int>::max();

    auto consider = [&](int candidate) {
        int dist = std::abs(low - candidate);
        if (dist < minDistance) {
            minDistance = dist;
            bestStart   = candidate;
        }
    };

    // Gap BEFORE the first event
    {
        int gapEnd = intervals.front().first;
        if (gapEnd >= duration) {
            // best position inside [0, gapEnd-duration]: closest to `low`
            int clamped = std::max(0, std::min(low, gapEnd - duration));
            consider(clamped);
        }
    }

    // Gaps BETWEEN consecutive events
    for (std::size_t i = 0; i + 1 < intervals.size(); ++i) {
        int gapStart = intervals[i].second;
        int gapEnd   = intervals[i+1].first;
        if (gapEnd - gapStart >= duration) {
            int clamped = std::max(gapStart, std::min(low, gapEnd - duration));
            consider(clamped);
        }
    }

    // Gap AFTER the last event (unbounded)
    {
        int gapStart = intervals.back().second;
        int candidate = std::max(gapStart, low);
        consider(candidate);
    }

    return bestStart;  // always set (after-last gap always exists)
}
