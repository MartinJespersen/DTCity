TEST_CASE("event snapshots select the latest event independently of playback history")
{
    sqlite3* db = nullptr;
    int result = sqlite3_open(":memory:", &db);
    defer(sqlite3_close(db));
    REQUIRE(result == SQLITE_OK);
    const char* fixture =
        "CREATE TABLE agent_events(agent_info_rowid INTEGER PRIMARY KEY, time REAL, event_type INTEGER, "
        "agent_id TEXT, node_from INTEGER, node_to INTEGER, node_from_lon REAL, node_from_lat REAL, "
        "node_to_lon REAL, node_to_lat REAL);"
        "INSERT INTO agent_events VALUES"
        "(1,10,1,'a',13254168275,13254168276,16.1,59.1,16.2,59.2),"
        "(2,20,2,'a',13254168275,13254168276,16.1,59.1,16.2,59.2),"
        "(3,20,3,'a',13254168275,13254168276,16.1,59.1,16.2,59.2),"
        "(4,15,1,'b',1,2,10,50,11,51);";
    result = sqlite3_exec(db, fixture, nullptr, nullptr, nullptr);
    REQUIRE(result == SQLITE_OK);
    sqlite3_stmt* stmt = nullptr;
    result = sqlite3_prepare_v2(db, simulator_event_snapshot_sql, -1, &stmt, nullptr);
    defer(sqlite3_finalize(stmt));
    REQUIRE(result == SQLITE_OK);

    // Seek forward, then backward, then before the first event.
    const double times[] = {20, 12, 9};
    const int expected_counts[] = {2, 1, 0};
    for (int seek = 0; seek < 3; ++seek)
    {
        sqlite3_reset(stmt);
        sqlite3_bind_double(stmt, 1, times[seek]);
        sqlite3_bind_int(stmt, 2, 200);
        int count = 0;
        while ((result = sqlite3_step(stmt)) == SQLITE_ROW)
        {
            if (count == 0)
            {
                int event_type = sqlite3_column_int(stmt, 2);
                CHECK(event_type == (seek == 0 ? 3 : 1));
            }
            ++count;
        }
        CHECK(result == SQLITE_DONE);
        CHECK(count == expected_counts[seek]);
    }

    sqlite3_reset(stmt);
    sqlite3_bind_double(stmt, 1, 20);
    sqlite3_bind_int(stmt, 2, 1);
    result = sqlite3_step(stmt);
    REQUIRE(result == SQLITE_ROW);
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    defer(yyjson_mut_doc_free(doc));
    REQUIRE(doc);
    yyjson_mut_val* events = yyjson_mut_arr(doc);
    bool appended = _simulator_event_append(doc, events, stmt);
    REQUIRE(appended);
    yyjson_mut_val* event = yyjson_mut_arr_get(events, 0);
    size_t fields = yyjson_mut_obj_size(event);
    CHECK(fields == 9);
    yyjson_mut_val* id = yyjson_mut_obj_get(event, "id");
    const char* id_text = yyjson_mut_get_str(id);
    CHECK(doctest::String(id_text) == "a");
    const char* names[] = {"time", "event_type", "node_from_id", "node_to_id", "lon_from", "lat_from", "lon_to", "lat_to"};
    const double values[] = {20, 3, 13254168275., 13254168276., 16.1, 59.1, 16.2, 59.2};
    for (int field = 0; field < 8; ++field)
    {
        yyjson_mut_val* value = yyjson_mut_obj_get(event, names[field]);
        REQUIRE(value);
        double number = yyjson_mut_get_num(value);
        CHECK(number == doctest::Approx(values[field]));
    }
    result = sqlite3_step(stmt);
    CHECK(result == SQLITE_DONE);

    sqlite3_stmt* delta_stmt = nullptr;
    result = sqlite3_prepare_v2(db, simulator_event_delta_sql, -1, &delta_stmt, nullptr);
    defer(sqlite3_finalize(delta_stmt));
    REQUIRE(result == SQLITE_OK);
    // Both events at the upper boundary must survive, in source order.
    // Repeating the interval after a failed send must produce the same events.
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        sqlite3_reset(delta_stmt);
        sqlite3_bind_double(delta_stmt, 1, 15);
        sqlite3_bind_double(delta_stmt, 2, 20);
        for (int expected_type = 2; expected_type <= 3; ++expected_type)
        {
            result = sqlite3_step(delta_stmt);
            REQUIRE(result == SQLITE_ROW);
            int event_type = sqlite3_column_int(delta_stmt, 2);
            CHECK(event_type == expected_type);
        }
        result = sqlite3_step(delta_stmt);
        CHECK(result == SQLITE_DONE);
    }
    sqlite3_reset(delta_stmt);
    sqlite3_bind_double(delta_stmt, 1, 20);
    sqlite3_bind_double(delta_stmt, 2, 21);
    result = sqlite3_step(delta_stmt);
    CHECK(result == SQLITE_DONE);
}

TEST_CASE("stream cursor requests snapshots only when its baseline changes")
{
    SimulatorEventCursor cursor = {};
    bool reset = _simulator_event_needs_reset(cursor, 10, 1, 1);
    CHECK(reset);
    cursor = {true, false, 10, 1, 1};
    reset = _simulator_event_needs_reset(cursor, 11, 1, 1);
    CHECK_FALSE(reset);
    reset = _simulator_event_needs_reset(cursor, 10, 1, 1);
    CHECK_FALSE(reset);
    reset = _simulator_event_needs_reset(cursor, 9, 1, 1);
    CHECK(reset);
    reset = _simulator_event_needs_reset(cursor, 11, 2, 1);
    CHECK(reset);
    reset = _simulator_event_needs_reset(cursor, 11, 1, 2);
    CHECK(reset);
    cursor.reset_pending = true;
    reset = _simulator_event_needs_reset(cursor, 30, 1, 1);
    CHECK(reset);
}

TEST_CASE("client batches preserve deltas and discard events preceding a reset")
{
    Arena* arena = arena_alloc();
    defer(arena_release(arena));
    city::CoordinateBatch batch = {};
    Buffer<city::Coordinate> first = buffer_alloc<city::Coordinate>(arena, 1);
    first.data[0].time = 10;
    Buffer<city::Coordinate> second = buffer_alloc<city::Coordinate>(arena, 1);
    second.data[0].time = 11;
    city::simulator_coordinate_batch_append(arena, &batch, first, false);
    city::simulator_coordinate_batch_append(arena, &batch, second, false);
    REQUIRE(batch.coordinates.size == 2);
    CHECK(batch.coordinates.data[0].time == 10);
    CHECK(batch.coordinates.data[1].time == 11);
    CHECK_FALSE(batch.reset);
    city::simulator_coordinate_batch_append(arena, &batch, first, true);
    city::simulator_coordinate_batch_append(arena, &batch, second, false);
    REQUIRE(batch.coordinates.size == 2);
    CHECK(batch.reset);
    CHECK(batch.coordinates.data[1].time == 11);
    city::simulator_coordinate_batch_append(arena, &batch, {}, true);
    CHECK(batch.coordinates.size == 0);
    CHECK(batch.reset);
}
