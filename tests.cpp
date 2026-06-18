// =============================================================================
// tests.cpp — Unit tests for IntervalTree and CalendarManager
//
// Compile and run:
//   g++ -std=c++17 -o run_tests tests.cpp IntervalTree.cpp CalendarManager.cpp && ./run_tests
// =============================================================================

#include "IntervalTree.h"
#include "CalendarManager.h"

#include <cassert>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Minimal test harness
// ---------------------------------------------------------------------------

static int tests_run    = 0;
static int tests_passed = 0;

#define TEST(name) void name()
#define RUN(name)  do { \
    ++tests_run; \
    try { name(); ++tests_passed; std::cout << "  PASS  " #name "\n"; } \
    catch (const std::exception& ex) { \
        std::cout << "  FAIL  " #name " — " << ex.what() << "\n"; \
    } catch (...) { \
        std::cout << "  FAIL  " #name " — unknown exception\n"; \
    } \
} while(0)

#define REQUIRE(cond) \
    do { if (!(cond)) throw std::runtime_error("assertion failed: " #cond); } while(0)

// ---------------------------------------------------------------------------
// IntervalTree tests
// ---------------------------------------------------------------------------

TEST(tree_add_single) {
    IntervalTree t;
    REQUIRE(t.addEvent(1, 100, 200, "Meeting"));
    REQUIRE(t.size() == 1);
}

TEST(tree_no_conflict_adjacent) {
    IntervalTree t;
    REQUIRE(t.addEvent(1, 100, 200, "A"));
    // [200, 300) is adjacent but not overlapping with [100, 200) — half-open
    REQUIRE(t.addEvent(2, 200, 300, "B"));
    REQUIRE(t.size() == 2);
}

TEST(tree_conflict_overlap) {
    IntervalTree t;
    REQUIRE(t.addEvent(1, 100, 300, "A"));
    // [200, 400) overlaps [100, 300)
    REQUIRE(!t.addEvent(2, 200, 400, "B"));
    REQUIRE(t.size() == 1);
}

TEST(tree_conflict_contained) {
    IntervalTree t;
    REQUIRE(t.addEvent(1, 100, 400, "Outer"));
    REQUIRE(!t.addEvent(2, 150, 250, "Inner"));
}

TEST(tree_conflict_starts_at_same_low) {
    IntervalTree t;
    REQUIRE(t.addEvent(1, 100, 200, "A"));
    REQUIRE(!t.addEvent(2, 100, 150, "B"));
}

TEST(tree_check_conflict_returns_event) {
    IntervalTree t;
    t.addEvent(1, 100, 200, "A");
    const Event* e = t.checkConflict(150, 250);
    REQUIRE(e != nullptr);
    REQUIRE(e->title == "A");
}

TEST(tree_check_no_conflict) {
    IntervalTree t;
    t.addEvent(1, 100, 200, "A");
    const Event* e = t.checkConflict(200, 300);
    REQUIRE(e == nullptr);
}

TEST(tree_remove_existing) {
    IntervalTree t;
    t.addEvent(1, 100, 200, "A");
    REQUIRE(t.removeEvent(1));
    REQUIRE(t.size() == 0);
    // After removal the slot should be free
    REQUIRE(t.checkConflict(100, 200) == nullptr);
}

TEST(tree_remove_nonexistent) {
    IntervalTree t;
    t.addEvent(1, 100, 200, "A");
    REQUIRE(!t.removeEvent(99));
    REQUIRE(t.size() == 1);
}

TEST(tree_remove_then_reinsert) {
    IntervalTree t;
    t.addEvent(1, 100, 200, "A");
    t.removeEvent(1);
    REQUIRE(t.addEvent(1, 100, 200, "A again"));
}

TEST(tree_inorder_sorted) {
    IntervalTree t;
    t.addEvent(3, 600, 700, "C");
    t.addEvent(1, 100, 200, "A");
    t.addEvent(2, 300, 400, "B");

    std::vector<std::pair<int,int>> intervals;
    t.collectIntervals(intervals);

    REQUIRE(intervals.size() == 3);
    REQUIRE(intervals[0].first == 100);
    REQUIRE(intervals[1].first == 300);
    REQUIRE(intervals[2].first == 600);
}

TEST(tree_free_slot_before_all) {
    IntervalTree t;
    t.addEvent(1, 200, 300, "A");
    // Requested [50, 100) — duration 50.  Gap before 200 is [0, 200) = 200 units.
    int slot = t.findNearestFreeSlot(50, 100);
    REQUIRE(slot >= 0);
    REQUIRE(slot + 50 <= 200);  // fits before the event
}

TEST(tree_free_slot_between_events) {
    IntervalTree t;
    t.addEvent(1, 0,   100, "A");
    t.addEvent(2, 200, 300, "B");
    // Gap [100, 200) is 100 units wide; request duration 60
    int slot = t.findNearestFreeSlot(120, 180);
    REQUIRE(slot >= 100);
    REQUIRE(slot + 60 <= 200);
}

TEST(tree_free_slot_after_all) {
    IntervalTree t;
    t.addEvent(1, 0, 100, "A");
    int slot = t.findNearestFreeSlot(200, 260);
    REQUIRE(slot >= 100);  // must be after the event
}

TEST(tree_free_slot_empty_tree) {
    IntervalTree t;
    int slot = t.findNearestFreeSlot(100, 160);
    REQUIRE(slot == 100);  // no events — requested slot is free
}

TEST(tree_avl_balance_many_inserts) {
    // Insert 100 non-overlapping events and verify all are retrievable
    IntervalTree t;
    for (int i = 0; i < 100; ++i) {
        bool ok = t.addEvent(i, i * 10, i * 10 + 5, "E" + std::to_string(i));
        REQUIRE(ok);
    }
    REQUIRE(t.size() == 100);

    // Spot-check a few
    REQUIRE(t.checkConflict(15, 18) == nullptr);  // [10,15) and [15,18) are adjacent — no conflict
    REQUIRE(t.checkConflict(10, 14) != nullptr);  // inside event 1 [10,15)
    REQUIRE(t.checkConflict(500, 505) != nullptr); // event 50 occupies exactly [500,505)
    REQUIRE(t.checkConflict(505, 510) == nullptr); // gap between event 50 and 51
}

TEST(tree_invalid_event_throws) {
    IntervalTree t;
    bool threw = false;
    try { t.addEvent(1, 200, 100, "Bad"); }
    catch (const std::invalid_argument&) { threw = true; }
    REQUIRE(threw);
}

// ---------------------------------------------------------------------------
// CalendarManager tests
// ---------------------------------------------------------------------------

TEST(mgr_book_success) {
    CalendarManager m;
    auto r = m.bookEvent("alice", 1, 100, 200, "Meeting");
    REQUIRE(r.success);
}

TEST(mgr_book_conflict) {
    CalendarManager m;
    m.bookEvent("alice", 1, 100, 200, "A");
    auto r = m.bookEvent("alice", 2, 150, 250, "B");
    REQUIRE(!r.success);
    REQUIRE(r.conflictingEvent.has_value());
    REQUIRE(r.suggestedStart.has_value());
}

TEST(mgr_different_users_no_conflict) {
    CalendarManager m;
    auto r1 = m.bookEvent("alice", 1, 100, 200, "A");
    auto r2 = m.bookEvent("bob",   1, 100, 200, "B");  // same slot, different user — OK
    REQUIRE(r1.success);
    REQUIRE(r2.success);
}

TEST(mgr_cancel_event) {
    CalendarManager m;
    m.bookEvent("alice", 1, 100, 200, "A");
    REQUIRE(m.cancelEvent("alice", 1));
    // Slot is now free
    auto r = m.bookEvent("alice", 2, 100, 200, "A again");
    REQUIRE(r.success);
}

TEST(mgr_cancel_nonexistent) {
    CalendarManager m;
    REQUIRE(!m.cancelEvent("alice", 99));
    REQUIRE(!m.cancelEvent("nobody", 1));
}

TEST(mgr_list_events_sorted) {
    CalendarManager m;
    m.bookEvent("alice", 3, 600, 700, "C");
    m.bookEvent("alice", 1, 100, 200, "A");
    m.bookEvent("alice", 2, 300, 400, "B");

    auto events = m.listEvents("alice");
    REQUIRE(events.size() == 3);
    REQUIRE(events[0].low == 100);
    REQUIRE(events[1].low == 300);
    REQUIRE(events[2].low == 600);
}

TEST(mgr_list_events_empty_user) {
    CalendarManager m;
    auto events = m.listEvents("nobody");
    REQUIRE(events.empty());
}

TEST(mgr_query_conflict) {
    CalendarManager m;
    m.bookEvent("alice", 1, 100, 200, "A");
    auto c = m.queryConflict("alice", 150, 250);
    REQUIRE(c.has_value());
    REQUIRE(c->title == "A");
}

TEST(mgr_query_no_conflict) {
    CalendarManager m;
    m.bookEvent("alice", 1, 100, 200, "A");
    auto c = m.queryConflict("alice", 200, 300);
    REQUIRE(!c.has_value());
}

TEST(mgr_suggest_slot) {
    CalendarManager m;
    m.bookEvent("alice", 1, 100, 200, "A");
    auto slot = m.suggestSlot("alice", 150, 210);  // duration 60, conflicts
    REQUIRE(slot.has_value());
    int s = *slot;
    REQUIRE(s + 60 <= 100 || s >= 200);  // must be outside [100, 200)
}

TEST(mgr_group_slot_no_events) {
    CalendarManager m;
    auto r = m.findGroupSlot({"alice", "bob"}, 60, 0, 480);
    REQUIRE(r.found);
    REQUIRE(r.start == 0);
    REQUIRE(r.end == 60);
}

TEST(mgr_group_slot_finds_gap) {
    CalendarManager m;
    m.bookEvent("alice", 1, 0,   100, "Alice morning");
    m.bookEvent("bob",   1, 200, 300, "Bob afternoon");
    // Gap [100, 200) is 100 units — fits a 60-unit meeting
    auto r = m.findGroupSlot({"alice", "bob"}, 60, 0, 480);
    REQUIRE(r.found);
    REQUIRE(r.start >= 100);
    REQUIRE(r.end   <= 200);
}

TEST(mgr_group_slot_no_gap) {
    CalendarManager m;
    // Alice covers [0, 300), Bob covers [0, 300) — no gap of 60 in [0, 300)
    m.bookEvent("alice", 1, 0, 300, "All day alice");
    m.bookEvent("bob",   1, 0, 300, "All day bob");
    auto r = m.findGroupSlot({"alice", "bob"}, 60, 0, 300);
    REQUIRE(!r.found);
}

TEST(mgr_group_slot_respects_search_window) {
    CalendarManager m;
    // Both free from 0–1000, but search window is [500, 560) — exactly 60 units
    auto r = m.findGroupSlot({"alice", "bob"}, 60, 500, 560);
    REQUIRE(r.found);
    REQUIRE(r.start == 500);
    REQUIRE(r.end   == 560);
}

TEST(mgr_group_slot_window_too_small) {
    CalendarManager m;
    // Window [0, 30) cannot fit duration 60
    auto r = m.findGroupSlot({"alice"}, 60, 0, 30);
    REQUIRE(!r.found);
}

TEST(mgr_invalid_input_throws) {
    CalendarManager m;
    bool threw = false;
    try { m.bookEvent("", 1, 100, 200, "A"); }
    catch (const std::invalid_argument&) { threw = true; }
    REQUIRE(threw);

    threw = false;
    try { m.bookEvent("alice", 1, 200, 100, "A"); }
    catch (const std::invalid_argument&) { threw = true; }
    REQUIRE(threw);

    threw = false;
    try { m.bookEvent("alice", 1, 100, 200, ""); }
    catch (const std::invalid_argument&) { threw = true; }
    REQUIRE(threw);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    std::cout << "\n=== IntervalTree tests ===\n";
    RUN(tree_add_single);
    RUN(tree_no_conflict_adjacent);
    RUN(tree_conflict_overlap);
    RUN(tree_conflict_contained);
    RUN(tree_conflict_starts_at_same_low);
    RUN(tree_check_conflict_returns_event);
    RUN(tree_check_no_conflict);
    RUN(tree_remove_existing);
    RUN(tree_remove_nonexistent);
    RUN(tree_remove_then_reinsert);
    RUN(tree_inorder_sorted);
    RUN(tree_free_slot_before_all);
    RUN(tree_free_slot_between_events);
    RUN(tree_free_slot_after_all);
    RUN(tree_free_slot_empty_tree);
    RUN(tree_avl_balance_many_inserts);
    RUN(tree_invalid_event_throws);

    std::cout << "\n=== CalendarManager tests ===\n";
    RUN(mgr_book_success);
    RUN(mgr_book_conflict);
    RUN(mgr_different_users_no_conflict);
    RUN(mgr_cancel_event);
    RUN(mgr_cancel_nonexistent);
    RUN(mgr_list_events_sorted);
    RUN(mgr_list_events_empty_user);
    RUN(mgr_query_conflict);
    RUN(mgr_query_no_conflict);
    RUN(mgr_suggest_slot);
    RUN(mgr_group_slot_no_events);
    RUN(mgr_group_slot_finds_gap);
    RUN(mgr_group_slot_no_gap);
    RUN(mgr_group_slot_respects_search_window);
    RUN(mgr_group_slot_window_too_small);
    RUN(mgr_invalid_input_throws);

    std::cout << "\n--- " << tests_passed << " / " << tests_run << " tests passed ---\n\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
