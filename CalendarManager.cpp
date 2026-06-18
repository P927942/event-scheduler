#include "CalendarManager.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

CalendarManager::UserCalendar& CalendarManager::getOrCreate(const std::string& userId) {
    // Caller must already hold a write lock on managerMutex_
    auto it = userCalendars_.find(userId);
    if (it == userCalendars_.end()) {
        auto [ins, _] = userCalendars_.emplace(userId, std::make_unique<UserCalendar>());
        return *ins->second;
    }
    return *it->second;
}

const CalendarManager::UserCalendar* CalendarManager::find(const std::string& userId) const {
    // Caller must hold at least a read lock on managerMutex_
    auto it = userCalendars_.find(userId);
    return (it != userCalendars_.end()) ? it->second.get() : nullptr;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

BookingResult CalendarManager::bookEvent(const std::string& userId,
                                          int id, int low, int high,
                                          const std::string& title) {
    if (userId.empty())
        throw std::invalid_argument("userId must not be empty");
    if (low >= high)
        throw std::invalid_argument("Event start must be strictly before end");
    if (title.empty())
        throw std::invalid_argument("Event title must not be empty");

    // Acquire write lock on manager to safely get/create the user calendar
    std::unique_lock<std::shared_mutex> mapLock(managerMutex_);
    UserCalendar& cal = getOrCreate(userId);
    mapLock.unlock();  // release map lock before acquiring per-user lock

    // Write lock on this user's tree
    std::unique_lock<std::shared_mutex> treeLock(cal.mu);

    bool ok = cal.tree.addEvent(id, low, high, title);
    if (ok) {
        return {true, "Event booked successfully", std::nullopt, std::nullopt, std::nullopt};
    }

    // Conflict — enrich the response
    const Event* conflict = cal.tree.checkConflict(low, high);
    int altStart = cal.tree.findNearestFreeSlot(low, high);
    int duration = high - low;

    BookingResult res;
    res.success  = false;
    res.message  = "Conflict detected";
    if (conflict) res.conflictingEvent = *conflict;
    res.suggestedStart = altStart;
    res.suggestedEnd   = altStart + duration;
    return res;
}

bool CalendarManager::cancelEvent(const std::string& userId, int eventId) {
    std::shared_lock<std::shared_mutex> mapLock(managerMutex_);
    UserCalendar* cal = const_cast<UserCalendar*>(find(userId));
    if (!cal) return false;
    mapLock.unlock();

    std::unique_lock<std::shared_mutex> treeLock(cal->mu);
    return cal->tree.removeEvent(eventId);
}

std::optional<Event> CalendarManager::queryConflict(const std::string& userId,
                                                      int low, int high) const {
    std::shared_lock<std::shared_mutex> mapLock(managerMutex_);
    const UserCalendar* cal = find(userId);
    if (!cal) return std::nullopt;

    std::shared_lock<std::shared_mutex> treeLock(cal->mu);
    const Event* e = cal->tree.checkConflict(low, high);
    if (e) return *e;
    return std::nullopt;
}

std::vector<Event> CalendarManager::listEvents(const std::string& userId) const {
    std::shared_lock<std::shared_mutex> mapLock(managerMutex_);
    const UserCalendar* cal = find(userId);
    if (!cal) return {};

    std::shared_lock<std::shared_mutex> treeLock(cal->mu);
    std::vector<Event> out;
    cal->tree.collectEvents(out);
    return out;
}

std::optional<int> CalendarManager::suggestSlot(const std::string& userId,
                                                  int low, int high) const {
    std::shared_lock<std::shared_mutex> mapLock(managerMutex_);
    const UserCalendar* cal = find(userId);
    if (!cal) return low;  // no events at all — requested slot is free

    std::shared_lock<std::shared_mutex> treeLock(cal->mu);
    return cal->tree.findNearestFreeSlot(low, high);
}

// ---------------------------------------------------------------------------
// findGroupSlot — sweep-line merge across all users' busy intervals
//
// BUG FIX (original):
//   The original code called collectIntervals(tree->checkConflict(0,0) ? nullptr : nullptr, ...)
//   which ALWAYS passed nullptr as the node, so the vector was always empty.
//   The fix: call collectIntervals() on each user's tree with its actual root
//   via the public collectIntervals(vector&) method.
// ---------------------------------------------------------------------------

GroupSlotResult CalendarManager::findGroupSlot(const std::vector<std::string>& users,
                                                int duration,
                                                int searchStart,
                                                int searchEnd) const {
    if (duration <= 0)
        throw std::invalid_argument("Duration must be positive");
    if (searchStart >= searchEnd)
        throw std::invalid_argument("searchStart must be < searchEnd");
    if (searchEnd - searchStart < duration)
        return {false, 0, 0};

    // Step 1: collect all busy intervals across all users
    std::vector<std::pair<int,int>> combinedBusy;

    {
        std::shared_lock<std::shared_mutex> mapLock(managerMutex_);
        for (const auto& userId : users) {
            const UserCalendar* cal = find(userId);
            if (!cal) continue;

            std::shared_lock<std::shared_mutex> treeLock(cal->mu);
            cal->tree.collectIntervals(combinedBusy);  // ← FIX: real tree, not nullptr
        }
    }

    // Clip intervals to [searchStart, searchEnd)
    std::vector<std::pair<int,int>> clipped;
    for (auto& [s, e] : combinedBusy) {
        int cs = std::max(s, searchStart);
        int ce = std::min(e, searchEnd);
        if (cs < ce) clipped.push_back({cs, ce});
    }

    // Step 2: sort and merge overlapping busy intervals
    std::sort(clipped.begin(), clipped.end());

    std::vector<std::pair<int,int>> merged;
    for (auto& seg : clipped) {
        if (!merged.empty() && seg.first < merged.back().second) {
            merged.back().second = std::max(merged.back().second, seg.second);
        } else {
            merged.push_back(seg);
        }
    }

    // Step 3: scan gaps for the first gap >= duration
    int cursor = searchStart;
    for (auto& [busyStart, busyEnd] : merged) {
        int gapSize = busyStart - cursor;
        if (gapSize >= duration) {
            return {true, cursor, cursor + duration};
        }
        cursor = std::max(cursor, busyEnd);
    }

    // Check trailing gap after last busy block
    if (searchEnd - cursor >= duration) {
        return {true, cursor, cursor + duration};
    }

    return {false, 0, 0};
}
