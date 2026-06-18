# Event Calendar Scheduler

An in-memory calendar scheduling backend written in **C++17**, built around an augmented AVL interval tree for O(log n) conflict detection and a sweep-line algorithm for multi-user group scheduling.

---

## What it does

- **Book events** for individual users with conflict detection
- **Cancel events** by ID
- **List events** for a user in chronological order
- **Check conflicts** without booking (dry-run)
- **Suggest the nearest free slot** when a requested time is taken
- **Find a group meeting slot** — the earliest window free for all participants

---

## Core data structure

Each user's calendar is an **augmented AVL interval tree**. Each node stores an event `[low, high)` and a `max_high` value tracking the largest endpoint in its subtree. This allows the overlap search to prune entire subtrees in O(log n) average time.

```
Node {
    Event   event       // [low, high), id, title
    int     max_high    // max(event.high, left.max_high, right.max_high)
    int     height      // for AVL balance factor
}
```

**Overlap definition (half-open intervals):** two events conflict if and only if `a.low < b.high && b.low < a.high`. Adjacent events (e.g. `[0,100)` and `[100,200)`) do not conflict.

### Complexity

| Operation | Complexity | Notes |
|---|---|---|
| Insert / delete | O(log n) | AVL rotation keeps height ≤ 1.44 log₂ n |
| Overlap search | O(log n) | max_high prunes non-candidate subtrees |
| Free slot lookup | O(n) | In-order traversal + linear gap scan |
| Group scheduling | O(U·n + M log M) | U users, n events each, M merged segments |

---

## Concurrency model

```
Global shared_mutex (managerMutex_)
  └─ protects the user→calendar map (insertions, lookups)

Per-user shared_mutex (UserCalendar::mu)
  └─ protects each individual tree
     ├─ Reads  → shared_lock  (concurrent across threads)
     └─ Writes → unique_lock  (exclusive per user only)
```

A write to user A does not block reads for user B. The global lock is held only for map operations, not for tree operations.

---

## HTTP API

All endpoints accept and return `application/json`. Time values are integers (e.g. minutes since midnight, or any consistent unit your client chooses).

### `POST /book`
Book an event. Returns 409 with a suggested alternative if a conflict is detected.

```json
// Request
{ "user": "alice", "id": 1, "start": 540, "end": 600, "title": "Standup" }

// 200 OK
{ "status": "success", "message": "Event booked successfully" }

// 409 Conflict
{
  "status": "conflict",
  "conflict": { "id": 1, "start": 540, "end": 600, "title": "Standup" },
  "suggested_start": 600,
  "suggested_end": 660
}
```

### `DELETE /cancel`
Cancel an event by user and event ID.

```json
{ "user": "alice", "id": 1 }
// 200: { "status": "success" }
// 404: { "status": "error", "message": "Event not found..." }
```

### `GET /events?user=alice`
List all events for a user, sorted by start time.

```json
{
  "user": "alice",
  "events": [
    { "id": 1, "start": 540, "end": 600, "title": "Standup" }
  ]
}
```

### `POST /check`
Dry-run conflict check — does not book anything.

```json
{ "user": "alice", "start": 540, "end": 600 }
// { "conflict": false }
// { "conflict": true, "conflicting_event": { ... } }
```

### `POST /suggest`
Get the nearest free slot of the same duration.

```json
{ "user": "alice", "start": 540, "end": 600 }
// { "suggested_start": 600, "suggested_end": 660 }
```

### `POST /group-schedule`
Find the earliest slot of `duration` units within `[search_start, search_end)` that is free for all listed users.

```json
{
  "users": ["alice", "bob", "carol"],
  "duration": 60,
  "search_start": 480,
  "search_end": 1080
}
// 200: { "status": "found", "optimal_start": 660, "optimal_end": 720 }
// 404: { "status": "no_slot_available" }
```

### `GET /health`
Liveness probe.

---

## Build

```bash
git clone https://github.com/yourusername/EventCalendarScheduler.git
cd EventCalendarScheduler

# Download Crow (header-only HTTP framework)
wget https://github.com/CrowCpp/Crow/releases/download/v1.0+5/crow_all.h

mkdir build && cd build
cmake ..
make
```

### Run the server
```bash
./calendar_engine
# Listening on port 8080
```

### Run the tests
```bash
./run_tests
# --- 33 / 33 tests passed ---
```

Or via CTest:
```bash
ctest --output-on-failure
```

---

## Project structure

```
.
├── IntervalTree.h       # AVL interval tree — header
├── IntervalTree.cpp     # AVL interval tree — implementation
├── CalendarManager.h    # Multi-user calendar manager — header
├── CalendarManager.cpp  # Multi-user calendar manager — implementation
├── main.cpp             # Crow HTTP server + route handlers
├── tests.cpp            # 33 unit tests (no external test framework)
├── CMakeLists.txt       # Build configuration
└── crow_all.h           # Crow HTTP library (download separately)
```

---

## Known limitations

- **In-memory only** — all data is lost on restart. Adding a persistence layer (SQLite, RocksDB, or a flat file journal) would be the natural next step.
- **No authentication** — the server trusts `"user"` strings from the request body. A real deployment would need JWT or session tokens.
- **Single process** — "distributed" in the original name was aspirational. This is a single-node server; horizontal scaling would require a shared data store.
