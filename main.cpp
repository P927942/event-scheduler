#include "crow_all.h"
#include "CalendarManager.h"

#include <stdexcept>
#include <string>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static crow::response badRequest(const std::string& msg) {
    crow::json::wvalue body;
    body["status"]  = "error";
    body["message"] = msg;
    return crow::response(400, body);
}

static crow::response notFound(const std::string& msg) {
    crow::json::wvalue body;
    body["status"]  = "error";
    body["message"] = msg;
    return crow::response(404, body);
}

// Validates that a required string field is present and non-empty
static bool requireString(const crow::json::rvalue& json,
                           const char* key,
                           std::string& out,
                           crow::response& err) {
    if (!json.has(key) || json[key].t() != crow::json::type::String) {
        err = badRequest(std::string("Missing or invalid field: ") + key);
        return false;
    }
    out = json[key].s();
    if (out.empty()) {
        err = badRequest(std::string("Field must not be empty: ") + key);
        return false;
    }
    return true;
}

// Validates that a required integer field is present
static bool requireInt(const crow::json::rvalue& json,
                        const char* key,
                        int& out,
                        crow::response& err) {
    if (!json.has(key) || json[key].t() != crow::json::type::Number) {
        err = badRequest(std::string("Missing or invalid field: ") + key);
        return false;
    }
    out = json[key].i();
    return true;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    crow::SimpleApp app;
    CalendarManager manager;

    // -------------------------------------------------------------------------
    // POST /book
    // Book an event for a user.
    //
    // Body: { "user": "alice", "id": 1, "start": 540, "end": 600, "title": "Standup" }
    // 200: { "status": "success", "message": "..." }
    // 409: { "status": "conflict", "conflict": {...}, "suggested_start": N, "suggested_end": N }
    // -------------------------------------------------------------------------
    CROW_ROUTE(app, "/book").methods(crow::HTTPMethod::POST)
    ([&manager](const crow::request& req) -> crow::response {
        auto json = crow::json::load(req.body);
        if (!json) return badRequest("Invalid JSON body");

        std::string userId, title;
        int id, start, end;
        crow::response err(200);

        if (!requireString(json, "user",  userId, err)) return err;
        if (!requireString(json, "title", title,  err)) return err;
        if (!requireInt(json, "id",    id,    err)) return err;
        if (!requireInt(json, "start", start, err)) return err;
        if (!requireInt(json, "end",   end,   err)) return err;

        if (start >= end)
            return badRequest("'start' must be strictly less than 'end'");

        try {
            BookingResult result = manager.bookEvent(userId, id, start, end, title);
            crow::json::wvalue res;

            if (result.success) {
                res["status"]  = "success";
                res["message"] = result.message;
                return crow::response(200, res);
            }

            res["status"]  = "conflict";
            res["message"] = result.message;
            if (result.conflictingEvent) {
                crow::json::wvalue conf;
                conf["id"]    = result.conflictingEvent->id;
                conf["start"] = result.conflictingEvent->low;
                conf["end"]   = result.conflictingEvent->high;
                conf["title"] = result.conflictingEvent->title;
                res["conflict"] = std::move(conf);
            }
            if (result.suggestedStart) {
                res["suggested_start"] = *result.suggestedStart;
                res["suggested_end"]   = *result.suggestedEnd;
            }
            return crow::response(409, res);

        } catch (const std::invalid_argument& ex) {
            return badRequest(ex.what());
        }
    });

    // -------------------------------------------------------------------------
    // DELETE /cancel
    // Cancel an event for a user.
    //
    // Body: { "user": "alice", "id": 1 }
    // 200: { "status": "success" }
    // 404: { "status": "error", "message": "Event not found" }
    // -------------------------------------------------------------------------
    CROW_ROUTE(app, "/cancel").methods(crow::HTTPMethod::DELETE)
    ([&manager](const crow::request& req) -> crow::response {
        auto json = crow::json::load(req.body);
        if (!json) return badRequest("Invalid JSON body");

        std::string userId;
        int id;
        crow::response err(200);

        if (!requireString(json, "user", userId, err)) return err;
        if (!requireInt(json, "id", id, err))          return err;

        bool ok = manager.cancelEvent(userId, id);
        if (!ok) return notFound("Event not found for user '" + userId + "'");

        crow::json::wvalue res;
        res["status"]  = "success";
        res["message"] = "Event cancelled";
        return crow::response(200, res);
    });

    // -------------------------------------------------------------------------
    // GET /events?user=alice
    // List all events for a user in sorted order.
    //
    // 200: { "user": "alice", "events": [ { "id":1, "start":540, "end":600, "title":"..." } ] }
    // -------------------------------------------------------------------------
    CROW_ROUTE(app, "/events").methods(crow::HTTPMethod::GET)
    ([&manager](const crow::request& req) -> crow::response {
        auto userId = req.url_params.get("user");
        if (!userId || std::string(userId).empty())
            return badRequest("Query parameter 'user' is required");

        auto events = manager.listEvents(userId);
        crow::json::wvalue res;
        res["user"] = std::string(userId);

        std::vector<crow::json::wvalue> arr;
        arr.reserve(events.size());
        for (const auto& e : events) {
            crow::json::wvalue ev;
            ev["id"]    = e.id;
            ev["start"] = e.low;
            ev["end"]   = e.high;
            ev["title"] = e.title;
            arr.push_back(std::move(ev));
        }
        res["events"] = std::move(arr);
        return crow::response(200, res);
    });

    // -------------------------------------------------------------------------
    // POST /check
    // Check if a time range conflicts for a user WITHOUT booking.
    //
    // Body: { "user": "alice", "start": 540, "end": 600 }
    // 200: { "conflict": false }
    // 200: { "conflict": true, "conflicting_event": {...} }
    // -------------------------------------------------------------------------
    CROW_ROUTE(app, "/check").methods(crow::HTTPMethod::POST)
    ([&manager](const crow::request& req) -> crow::response {
        auto json = crow::json::load(req.body);
        if (!json) return badRequest("Invalid JSON body");

        std::string userId;
        int start, end;
        crow::response err(200);

        if (!requireString(json, "user",  userId, err)) return err;
        if (!requireInt(json, "start", start, err))     return err;
        if (!requireInt(json, "end",   end,   err))     return err;

        if (start >= end) return badRequest("'start' must be < 'end'");

        auto conflict = manager.queryConflict(userId, start, end);
        crow::json::wvalue res;

        if (!conflict) {
            res["conflict"] = false;
            return crow::response(200, res);
        }

        res["conflict"] = true;
        crow::json::wvalue conf;
        conf["id"]    = conflict->id;
        conf["start"] = conflict->low;
        conf["end"]   = conflict->high;
        conf["title"] = conflict->title;
        res["conflicting_event"] = std::move(conf);
        return crow::response(200, res);
    });

    // -------------------------------------------------------------------------
    // POST /suggest
    // Suggest the nearest free slot for a user.
    //
    // Body: { "user": "alice", "start": 540, "end": 600 }
    // 200: { "suggested_start": N, "suggested_end": N }
    // -------------------------------------------------------------------------
    CROW_ROUTE(app, "/suggest").methods(crow::HTTPMethod::POST)
    ([&manager](const crow::request& req) -> crow::response {
        auto json = crow::json::load(req.body);
        if (!json) return badRequest("Invalid JSON body");

        std::string userId;
        int start, end;
        crow::response err(200);

        if (!requireString(json, "user",  userId, err)) return err;
        if (!requireInt(json, "start", start, err))     return err;
        if (!requireInt(json, "end",   end,   err))     return err;

        if (start >= end) return badRequest("'start' must be < 'end'");

        try {
            auto slot = manager.suggestSlot(userId, start, end);
            int duration = end - start;
            int altStart = slot.value_or(start);

            crow::json::wvalue res;
            res["suggested_start"] = altStart;
            res["suggested_end"]   = altStart + duration;
            return crow::response(200, res);
        } catch (const std::invalid_argument& ex) {
            return badRequest(ex.what());
        }
    });

    // -------------------------------------------------------------------------
    // POST /group-schedule
    // Find the first available slot of `duration` within [search_start, search_end)
    // that is free for ALL listed users.
    //
    // Body: { "users": ["alice","bob"], "duration": 60,
    //         "search_start": 480, "search_end": 1080 }
    // 200: { "status": "found", "optimal_start": N, "optimal_end": N }
    // 404: { "status": "no_slot_available" }
    // -------------------------------------------------------------------------
    CROW_ROUTE(app, "/group-schedule").methods(crow::HTTPMethod::POST)
    ([&manager](const crow::request& req) -> crow::response {
        auto json = crow::json::load(req.body);
        if (!json) return badRequest("Invalid JSON body");

        if (!json.has("users") || json["users"].t() != crow::json::type::List)
            return badRequest("'users' must be a JSON array");

        std::vector<std::string> users;
        for (const auto& u : json["users"]) {
            if (u.t() != crow::json::type::String || std::string(u.s()).empty())
                return badRequest("Each entry in 'users' must be a non-empty string");
            users.push_back(u.s());
        }
        if (users.empty())
            return badRequest("'users' array must not be empty");

        int duration, searchStart, searchEnd;
        crow::response err(200);
        if (!requireInt(json, "duration",     duration,    err)) return err;
        if (!requireInt(json, "search_start", searchStart, err)) return err;
        if (!requireInt(json, "search_end",   searchEnd,   err)) return err;

        if (duration <= 0)       return badRequest("'duration' must be positive");
        if (searchStart >= searchEnd) return badRequest("'search_start' must be < 'search_end'");

        try {
            auto result = manager.findGroupSlot(users, duration, searchStart, searchEnd);
            crow::json::wvalue res;

            if (result.found) {
                res["status"]        = "found";
                res["optimal_start"] = result.start;
                res["optimal_end"]   = result.end;
                return crow::response(200, res);
            }

            res["status"] = "no_slot_available";
            return crow::response(404, res);

        } catch (const std::invalid_argument& ex) {
            return badRequest(ex.what());
        }
    });

    // -------------------------------------------------------------------------
    // GET /health  — liveness probe
    // -------------------------------------------------------------------------
    CROW_ROUTE(app, "/health").methods(crow::HTTPMethod::GET)
    ([]() -> crow::response {
        crow::json::wvalue res;
        res["status"] = "ok";
        return crow::response(200, res);
    });

    app.port(8080).multithreaded().run();
}
