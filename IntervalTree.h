#ifndef INTERVAL_TREE_H
#define INTERVAL_TREE_H

#include <algorithm>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Domain types
// ---------------------------------------------------------------------------

struct Event {
    int         id;
    int         low;   // inclusive start  (minutes-since-epoch or any int unit)
    int         high;  // exclusive end
    std::string title;
};

// ---------------------------------------------------------------------------
// AVL Node — augmented with max_high for O(log n) overlap queries
// ---------------------------------------------------------------------------

struct Node {
    Event                  event;
    int                    max_high; // max 'high' in the subtree rooted here
    int                    height;
    std::shared_ptr<Node>  left;
    std::shared_ptr<Node>  right;

    explicit Node(const Event& e)
        : event(e), max_high(e.high), height(1), left(nullptr), right(nullptr) {}
};

// ---------------------------------------------------------------------------
// IntervalTree
//
// Invariants:
//   • Tree is keyed on event.low (BST order).
//   • Each node stores max_high = max(event.high, left.max_high, right.max_high).
//   • Height-balanced via AVL rotations after every insertion / deletion.
//   • Overlap is HALF-OPEN: [low, high) — two events conflict iff
//       e1.low < e2.high  &&  e2.low < e1.high
// ---------------------------------------------------------------------------

class IntervalTree {
public:
    IntervalTree() : root_(nullptr), size_(0) {}

    // Returns true on success; false if the new event overlaps an existing one.
    bool addEvent(int id, int low, int high, const std::string& title);

    // Returns a pointer to the first conflicting event, or nullptr.
    const Event* checkConflict(int low, int high) const;

    // Nearest free slot of the requested duration, searching outward from [low, high).
    // Returns the start of that slot (same duration guaranteed).
    int findNearestFreeSlot(int low, int high) const;

    // Remove an event by id.  Returns true if found and removed.
    bool removeEvent(int id);

    // Collect all stored [low, high) pairs in sorted order.
    void collectIntervals(std::vector<std::pair<int,int>>& out) const;

    // Collect ALL Event objects in sorted order.
    void collectEvents(std::vector<Event>& out) const;

    std::size_t size() const { return size_; }
    bool        empty() const { return size_ == 0; }

private:
    std::shared_ptr<Node> root_;
    std::size_t           size_;

    // ---- helpers -----------------------------------------------------------
    static int  height(const std::shared_ptr<Node>& n);
    static int  balance(const std::shared_ptr<Node>& n);
    static void refreshNode(std::shared_ptr<Node>& n);

    static std::shared_ptr<Node> rotateRight(std::shared_ptr<Node> y);
    static std::shared_ptr<Node> rotateLeft (std::shared_ptr<Node> x);
    static std::shared_ptr<Node> rebalance  (std::shared_ptr<Node> n);

    static bool overlap(const Event& a, const Event& b);

    // recursive insert: sets conflict=true and returns unchanged tree if overlap found
    std::shared_ptr<Node> insert(std::shared_ptr<Node> n,
                                  const Event& e,
                                  bool& conflict) const;

    // recursive remove by id
    std::shared_ptr<Node> remove(std::shared_ptr<Node> n, int id, bool& found);

    // find the node with the smallest key in subtree (used by delete)
    static const Node* minNode(const std::shared_ptr<Node>& n);

    // overlap search
    static const Node* searchOverlap(const std::shared_ptr<Node>& n, const Event& e);

    // in-order traversal helpers
    static void inorder(const std::shared_ptr<Node>& n,
                        std::vector<std::pair<int,int>>& out);
    static void inorderEvents(const std::shared_ptr<Node>& n,
                              std::vector<Event>& out);
};

#endif // INTERVAL_TREE_H
