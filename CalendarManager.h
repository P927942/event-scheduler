#ifndef CALENDAR_MANAGER_H
#define CALENDAR_MANAGER_H

#include "IntervalTree.h"

#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <optional>
#include <stdexcept>

// ---------------------------------------------------------------------------
// BookingResult — returned by bookEvent so callers get rich error info
// ---------------------------------------------------------------------------

struct BookingResult {
    bool        success;
    std::string message;

    // Populated only when success == false (conflict path)
    std::optional<Event> conflictingEvent;
    std::optional<int>   suggestedStart;
    std::optional<int>   suggestedEnd;
};

// ---------------------------------------------------------------------------
// GroupSlotResult
// ---------------------------------------------------------------------------

struct GroupSlotResult {
    bool found;
    int  start;  // valid iff found == true
    int  end;
};

// ---------------------------------------------------------------------------
// CalendarManager
//
// Concurrency model
// -----------------
//   • Global shared_mutex (managerMutex_) protects the userCalendars_ map
//     itself (insertions / lookups of the outer map).
//   • Each user has their own per-tree shared_mutex, allowing concurrent
//     reads across different users and a single writer per user without
//     blocking others.
//   • This eliminates the original design's bottleneck where ANY write to
//     ANY user blocked ALL reads globally.
// ---------------------------------------------------------------------------

class CalendarManager {
public:
    // Book an event for a user.
    // Throws std::invalid_argument on bad input.
    BookingResult bookEvent(const std::string& userId,
                            int id, int low, int high,
                            const std::string& title);

    // Cancel an event.  Returns false if not found.
    bool cancelEvent(const std::string& userId, int eventId);

    // Query conflict without booking.
    std::optional<Event> queryConflict(const std::string& userId,
                                       int low, int high) const;

    // Get all events for a user (sorted by start time).
    std::vector<Event> listEvents(const std::string& userId) const;

    // Suggest alternative slot for a user.
    std::optional<int> suggestSlot(const std::string& userId,
                                   int low, int high) const;

    // Find the earliest free slot of `duration` units within [searchStart,
    // searchEnd) that is free for ALL listed users.
    //
    // FIX: original code passed nullptr to collectIntervals — the busy-slot
    // collection was completely broken and always returned empty.
    // This version correctly walks each user's tree.
    GroupSlotResult findGroupSlot(const std::vector<std::string>& users,
                                  int duration,
                                  int searchStart,
                                  int searchEnd) const;

private:
    // Wrapper keeping a tree and its own mutex together
    struct UserCalendar {
        IntervalTree              tree;
        mutable std::shared_mutex mu;
    };

    mutable std::shared_mutex                                      managerMutex_;
    std::unordered_map<std::string, std::unique_ptr<UserCalendar>> userCalendars_;

    // Returns existing or newly-created calendar.
    // Caller must hold a WRITE lock on managerMutex_ before calling.
    UserCalendar& getOrCreate(const std::string& userId);

    // Read-only lookup — caller must hold at least a READ lock on managerMutex_.
    const UserCalendar* find(const std::string& userId) const;
};

#endif // CALENDAR_MANAGER_H
