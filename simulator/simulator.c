#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_IMPLEMENTATION
#define NK_GLFW_GL3_IMPLEMENTATION

#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#endif

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <sqlite3.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <direct.h>
#include <io.h>
#include <windows.h>
#ifndef R_OK
#define R_OK 4
#endif
#define PROJECT_ACCESS _access
#define PROJECT_GETCWD _getcwd
#define PROJECT_STRDUP _strdup
#else
#include <pthread.h>
#include <unistd.h>
#define PROJECT_ACCESS access
#define PROJECT_GETCWD getcwd
#define PROJECT_STRDUP strdup
#endif

#include <GL/gl3w.h>
#include <GLFW/glfw3.h>

#include <civetweb.h>
#include <nuklear.h>
#include <nuklear_glfw_gl3.h>
#include <yyjson.h>

#define WINDOW_WIDTH 1024
#define WINDOW_HEIGHT 1024
#define MAX_VERTEX_BUFFER (4 * 1024 * 1024)
#define MAX_ELEMENT_BUFFER (1 * 1024 * 1024)
#define SERVER_PORT 8080
#define MAX_STATUS_TEXT 8192
#define DEFAULT_FPS 20
#define DEFAULT_VEHICLE_COUNT 200
#define DEFAULT_DB_PATH "simulator/database/eskiltuna_playback.sqlite"
#define CUSTOM_FONT_PATH "simulator/fonts/segoeuithis.ttf"
#define CUSTOM_FONT_SIZE 18.0f
#define DEFAULT_WS_PATH "/ws"
#define APP_BG_R 45
#define APP_BG_G 45
#define APP_BG_B 45

struct string_list
{
    char** items;
    int count;
    int capacity;
};

struct websocket_server
{
    struct mg_context* context;
    struct mg_connection* client_connection;
#ifdef _WIN32
    CRITICAL_SECTION mutex;
#else
    pthread_mutex_t mutex;
#endif
    char last_received[MAX_STATUS_TEXT];
    char last_sent[MAX_STATUS_TEXT];
    char status_line[256];
};

struct vehicle_stream
{
    bool streaming;
    bool isolate_selected_vehicles;
    int fps;
    int vehicle_count;
    uint64_t frames_sent;
    uint64_t packets_sent;
    int last_sent_vehicle_count;
    uint64_t last_tick_ms;
    double simulated_seconds;
    double next_frame_at_ms;
    char* last_snapshot_preview;
    size_t last_snapshot_preview_capacity;
    char vehicle_search[128];
    struct string_list visible_vehicle_ids;
    struct string_list selected_vehicle_ids;
};

struct playback_db
{
    sqlite3* db;
    sqlite3_stmt* active_start_stmt;
    sqlite3_stmt* active_end_stmt;
    bool ready;
    double min_time;
    double max_time;
    double midpoint_time;
    char path[512];
    char status_line[256];
    int last_active_count;
};

static void
stream_stop(struct vehicle_stream* stream);

static void
copy_status(char* dest, size_t dest_size, const char* src)
{
    if (!dest || !dest_size)
    {
        return;
    }

    if (!src)
    {
        dest[0] = '\0';
        return;
    }

    snprintf(dest, dest_size, "%s", src);
}

static bool
file_exists(const char* path)
{
    return path && PROJECT_ACCESS(path, R_OK) == 0;
}

static bool
get_project_root(char* out_path, size_t out_size)
{
    if (!out_path || out_size == 0)
    {
        return false;
    }

#ifdef _WIN32
    char exe_path[1024];
    char* last_backslash = NULL;
    DWORD result = 0;

    result = GetModuleFileNameA(NULL, exe_path, sizeof(exe_path));
    if (result == 0 || result == sizeof(exe_path))
    {
        return false;
    }

    last_backslash = strrchr(exe_path, '\\');
    if (last_backslash)
    {
        *last_backslash = '\0';
    }

    last_backslash = strrchr(exe_path, '\\');
    if (last_backslash)
    {
        *last_backslash = '\0';
    }

    last_backslash = strrchr(exe_path, '\\');
    if (last_backslash)
    {
        *last_backslash = '\0';
    }

    if (strlen(exe_path) >= out_size)
    {
        return false;
    }

    snprintf(out_path, out_size, "%s", exe_path);
    return true;
#else
    return PROJECT_GETCWD(out_path, out_size) != NULL;
#endif
}

static bool
resolve_project_path(const char* relative_path, char* out_path, size_t out_size)
{
    char project_root[1024];

    if (!relative_path || !out_path || out_size == 0)
    {
        return false;
    }

    if (!get_project_root(project_root, sizeof(project_root)))
    {
        return false;
    }

    snprintf(out_path, out_size, "%s/%s", project_root, relative_path);
    return true;
}

static bool
prepare_sql_statement(sqlite3* db, const char* sql, sqlite3_stmt** stmt)
{
    if (!db || !sql || !stmt)
    {
        return false;
    }

    if (*stmt)
    {
        sqlite3_finalize(*stmt);
        *stmt = NULL;
    }

    return sqlite3_prepare_v2(db, sql, -1, stmt, NULL) == SQLITE_OK;
}

static void
server_mutex_init(struct websocket_server* server)
{
#ifdef _WIN32
    InitializeCriticalSection(&server->mutex);
#else
    pthread_mutex_init(&server->mutex, NULL);
#endif
}

static void
server_mutex_destroy(struct websocket_server* server)
{
#ifdef _WIN32
    DeleteCriticalSection(&server->mutex);
#else
    pthread_mutex_destroy(&server->mutex);
#endif
}

static void
server_lock(struct websocket_server* server)
{
    if (!server)
    {
        return;
    }
#ifdef _WIN32
    EnterCriticalSection(&server->mutex);
#else
    pthread_mutex_lock(&server->mutex);
#endif
}

static void
server_unlock(struct websocket_server* server)
{
    if (!server)
    {
        return;
    }
#ifdef _WIN32
    LeaveCriticalSection(&server->mutex);
#else
    pthread_mutex_unlock(&server->mutex);
#endif
}

static bool
server_has_client(struct websocket_server* server)
{
    bool has_client = false;

    server_lock(server);
    has_client = server && server->client_connection != NULL;
    server_unlock(server);
    return has_client;
}

#ifdef __APPLE__
static bool
pick_sqlite_db_file(char* out_path, size_t out_size)
{
    const char* cmd = "osascript "
                      "-e 'set selectedFile to choose file with prompt \"Select "
                      "SQLite playback DB\"' "
                      "-e 'POSIX path of selectedFile'";
    FILE* fp = NULL;
    size_t len = 0;

    if (!out_path || out_size == 0)
    {
        return false;
    }

    fp = popen(cmd, "r");
    if (!fp)
    {
        return false;
    }

    if (!fgets(out_path, (int)out_size, fp))
    {
        pclose(fp);
        return false;
    }

    pclose(fp);
    len = strlen(out_path);

    while (len > 0 && (out_path[len - 1] == '\n' || out_path[len - 1] == '\r'))
    {
        out_path[len - 1] = '\0';
        len -= 1;
    }

    return len > 0;
}
#endif

static bool
text_contains_ci(const char* text, const char* pattern)
{
    size_t pattern_length = 0;
    const char* cursor = NULL;

    if (!pattern || pattern[0] == '\0')
    {
        return true;
    }

    if (!text || text[0] == '\0')
    {
        return false;
    }

    pattern_length = strlen(pattern);

    for (cursor = text; *cursor; ++cursor)
    {
        size_t i = 0;

        while (i < pattern_length && cursor[i] && tolower((unsigned char)cursor[i]) == tolower((unsigned char)pattern[i]))
        {
            i += 1;
        }

        if (i == pattern_length)
        {
            return true;
        }
    }

    return false;
}

static void
string_list_clear(struct string_list* list)
{
    int i = 0;

    if (!list)
    {
        return;
    }

    for (i = 0; i < list->count; ++i)
    {
        free(list->items[i]);
        list->items[i] = NULL;
    }

    list->count = 0;
}

static void
string_list_free(struct string_list* list)
{
    if (!list)
    {
        return;
    }

    string_list_clear(list);
    free(list->items);
    list->items = NULL;
    list->capacity = 0;
}

static int
string_list_index_of(const struct string_list* list, const char* value)
{
    int i = 0;

    if (!list || !value)
    {
        return -1;
    }

    for (i = 0; i < list->count; ++i)
    {
        if (list->items[i] && strcmp(list->items[i], value) == 0)
        {
            return i;
        }
    }

    return -1;
}

static bool
string_list_contains(const struct string_list* list, const char* value)
{
    return string_list_index_of(list, value) >= 0;
}

static bool
string_list_ensure_capacity(struct string_list* list, int required_count)
{
    int new_capacity = 0;
    char** resized_items = NULL;

    if (!list)
    {
        return false;
    }

    if (required_count <= list->capacity)
    {
        return true;
    }

    new_capacity = list->capacity > 0 ? list->capacity : 16;

    while (new_capacity < required_count)
    {
        new_capacity *= 2;
    }

    resized_items = (char**)realloc(list->items, (size_t)new_capacity * sizeof(char*));

    if (!resized_items)
    {
        return false;
    }

    memset(resized_items + list->capacity, 0, (size_t)(new_capacity - list->capacity) * sizeof(char*));
    list->items = resized_items;
    list->capacity = new_capacity;
    return true;
}

static bool
string_list_append_unique(struct string_list* list, const char* value)
{
    char* copy = NULL;

    if (!list || !value)
    {
        return false;
    }

    if (string_list_contains(list, value))
    {
        return true;
    }

    if (!string_list_ensure_capacity(list, list->count + 1))
    {
        return false;
    }

    copy = PROJECT_STRDUP(value);

    if (!copy)
    {
        return false;
    }

    list->items[list->count++] = copy;
    return true;
}

static bool
string_list_remove(struct string_list* list, const char* value)
{
    int index = 0;
    int i = 0;

    if (!list || !value)
    {
        return false;
    }

    index = string_list_index_of(list, value);

    if (index < 0)
    {
        return false;
    }

    free(list->items[index]);

    for (i = index; i < list->count - 1; ++i)
    {
        list->items[i] = list->items[i + 1];
    }

    list->items[list->count - 1] = NULL;
    list->count -= 1;
    return true;
}

static bool
string_list_toggle(struct string_list* list, const char* value)
{
    if (!list || !value)
    {
        return false;
    }

    if (string_list_contains(list, value))
    {
        return string_list_remove(list, value);
    }

    return string_list_append_unique(list, value);
}

static void
build_selected_summary(const struct string_list* selected_vehicle_ids, char* buffer, size_t buffer_size)
{
    size_t offset = 0;
    int i = 0;

    if (!buffer || buffer_size == 0)
    {
        return;
    }

    buffer[0] = '\0';

    if (!selected_vehicle_ids || selected_vehicle_ids->count == 0)
    {
        snprintf(buffer, buffer_size, "(none)");
        return;
    }

    for (i = 0; i < selected_vehicle_ids->count; ++i)
    {
        const char* vehicle_id = selected_vehicle_ids->items[i];
        int written = 0;

        written = snprintf(buffer + offset, buffer_size - offset, "%s%s", i == 0 ? "" : ", ", vehicle_id ? vehicle_id : "unknown");

        if (written < 0 || (size_t)written >= buffer_size - offset)
        {
            if (buffer_size > 4)
            {
                snprintf(buffer + buffer_size - 4, 4, "...");
            }
            return;
        }

        offset += (size_t)written;
    }
}

static void
server_close_client(struct websocket_server* server)
{
    server_lock(server);
    server->client_connection = NULL;
    copy_status(server->status_line, sizeof(server->status_line), "Waiting for WebSocket client. Map viewer: http://127.0.0.1:8080");
    server_unlock(server);
}

static bool
server_send_text(struct websocket_server* server, const char* text)
{
    struct mg_connection* connection = NULL;
    int sent = 0;

    if (!server || !text)
    {
        return false;
    }

    server_lock(server);
    connection = server->client_connection;
    if (connection)
    {
        mg_lock_connection(connection);
        sent = mg_websocket_write(connection, MG_WEBSOCKET_OPCODE_TEXT, text, strlen(text));
        mg_unlock_connection(connection);
    }
    if (sent > 0)
    {
        copy_status(server->last_sent, sizeof(server->last_sent), text);
        server_unlock(server);
        return true;
    }
    copy_status(server->status_line, sizeof(server->status_line), "Failed to send WebSocket frame to client.");
    server->client_connection = NULL;
    server_unlock(server);
    return false;
}

static int
server_health_handler(struct mg_connection* conn, void* cbdata)
{
    (void)cbdata;
    mg_printf(conn, "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/plain; charset=utf-8\r\n"
                    "Content-Length: 2\r\n"
                    "Connection: close\r\n"
                    "\r\n"
                    "ok");
    return 200;
}

static int
server_websocket_connect_handler(const struct mg_connection* conn, void* cbdata)
{
    struct websocket_server* server = (struct websocket_server*)cbdata;
    const struct mg_request_info* request_info = mg_get_request_info(conn);
    const char* user_agent = NULL;
    bool accepted = false;

    if (!server)
    {
        return 1;
    }

    if (request_info)
    {
        user_agent = mg_get_header(conn, "User-Agent");
    }

    server_lock(server);
    if (server->client_connection == NULL)
    {
        accepted = true;
        copy_status(server->status_line, sizeof(server->status_line), user_agent && strstr(user_agent, "Mozilla") ? "Browser viewer connecting..." : "External WebSocket client connecting...");
    }
    else
    {
        copy_status(server->status_line, sizeof(server->status_line), "Rejected WebSocket client: another client is already connected.");
    }
    server_unlock(server);

    return accepted ? 0 : 1;
}

static void
server_websocket_ready_handler(struct mg_connection* conn, void* cbdata)
{
    struct websocket_server* server = (struct websocket_server*)cbdata;
    const char* user_agent = mg_get_header(conn, "User-Agent");

    if (!server)
    {
        return;
    }

    server_lock(server);
    server->client_connection = conn;
    copy_status(server->status_line, sizeof(server->status_line),
                user_agent && strstr(user_agent, "Mozilla") ? "Browser viewer connected. Map server: http://127.0.0.1:8080" : "External WebSocket client connected on ws://127.0.0.1:8080/ws");
    server_unlock(server);
}

static int
server_websocket_data_handler(struct mg_connection* conn, int bits, char* data, size_t data_len, void* cbdata)
{
    struct websocket_server* server = (struct websocket_server*)cbdata;
    int opcode = bits & 0x0F;
    size_t copy_len = 0;
    char payload_text[MAX_STATUS_TEXT];

    (void)conn;

    if (!server)
    {
        return 0;
    }

    if (opcode == MG_WEBSOCKET_OPCODE_CONNECTION_CLOSE)
    {
        return 0;
    }

    if (opcode != MG_WEBSOCKET_OPCODE_TEXT || !data || data_len == 0)
    {
        return 1;
    }

    copy_len = data_len < sizeof(payload_text) - 1 ? data_len : sizeof(payload_text) - 1;
    memcpy(payload_text, data, copy_len);
    payload_text[copy_len] = '\0';

    server_lock(server);
    copy_status(server->last_received, sizeof(server->last_received), payload_text);
    copy_status(server->status_line, sizeof(server->status_line), "Message received from WebSocket client.");
    server_unlock(server);
    return 1;
}

static void
server_websocket_close_handler(const struct mg_connection* conn, void* cbdata)
{
    struct websocket_server* server = (struct websocket_server*)cbdata;

    if (!server)
    {
        return;
    }

    server_lock(server);
    if (server->client_connection == conn)
    {
        server->client_connection = NULL;
    }
    copy_status(server->status_line, sizeof(server->status_line), "Waiting for WebSocket client. Map viewer: http://127.0.0.1:8080");
    server_unlock(server);
}

static bool
server_init(struct websocket_server* server, uint16_t port)
{
    const char* options[11];
    char port_option[64];
    char document_root[1024];
    struct mg_callbacks callbacks;
    struct mg_init_data init_data;
    struct mg_error_data error_data;
    char error_text[256];
    int option_index = 0;

    memset(server, 0, sizeof(*server));
    server_mutex_init(server);

    if (!get_project_root(document_root, sizeof(document_root)))
    {
        copy_status(server->status_line, sizeof(server->status_line), "Could not resolve project directory for CivetWeb.");
        server_mutex_destroy(server);
        return false;
    }

    snprintf(port_option, sizeof(port_option), "%u", (unsigned int)port);
    memset(&callbacks, 0, sizeof(callbacks));
    memset(&init_data, 0, sizeof(init_data));
    memset(&error_data, 0, sizeof(error_data));
    error_data.text = error_text;
    error_data.text_buffer_size = sizeof(error_text);

    options[option_index++] = "document_root";
    options[option_index++] = document_root;
    options[option_index++] = "enable_directory_listing";
    options[option_index++] = "no";
    options[option_index++] = "index_files";
    options[option_index++] = "index.html";
    options[option_index++] = "listening_ports";
    options[option_index++] = port_option;
    options[option_index++] = "num_threads";
    options[option_index++] = "4";
    options[option_index] = NULL;
    init_data.callbacks = &callbacks;
    init_data.user_data = server;
    init_data.configuration_options = options;

    copy_status(server->status_line, sizeof(server->status_line), "Starting embedded HTTP/WebSocket server...");
    copy_status(server->last_received, sizeof(server->last_received), "(none)");
    copy_status(server->last_sent, sizeof(server->last_sent), "(none)");

    server->context = mg_start2(&init_data, &error_data);
    if (!server->context)
    {
        if (error_text[0] != '\0')
        {
            snprintf(server->status_line, sizeof(server->status_line), "Could not start embedded CivetWeb server: %s", error_text);
        }
        else
        {
            copy_status(server->status_line, sizeof(server->status_line), "Could not start embedded CivetWeb server on 127.0.0.1:8080.");
        }
        server_mutex_destroy(server);
        return false;
    }

    mg_set_request_handler(server->context, "/health", server_health_handler, server);
    mg_set_websocket_handler(server->context, DEFAULT_WS_PATH, server_websocket_connect_handler, server_websocket_ready_handler, server_websocket_data_handler, server_websocket_close_handler, server);

    copy_status(server->status_line, sizeof(server->status_line), "Listening on http://127.0.0.1:8080 and ws://127.0.0.1:8080/ws");
    return true;
}

static void
server_shutdown(struct websocket_server* server)
{
    server_close_client(server);
    if (server->context)
    {
        mg_stop(server->context);
        server->context = NULL;
    }
    server_mutex_destroy(server);
}

static void
server_poll(struct websocket_server* server)
{
    (void)server;
}

static void
stream_init(struct vehicle_stream* stream)
{
    memset(stream, 0, sizeof(*stream));
    stream->fps = DEFAULT_FPS;
    stream->vehicle_count = DEFAULT_VEHICLE_COUNT;
}

static void
playback_db_init(struct playback_db* playback_db, const char* db_path)
{
    sqlite3_stmt* range_stmt = NULL;
    bool using_legacy_index_plan = false;
    const char* active_start_sql = "WITH active AS ("
                                   "  SELECT rs.vehicle, rs.vehicle_numeric_id, rs.geometry_id, "
                                   "  CASE WHEN rs.duration <= 0 THEN g.total_distance "
                                   "  ELSE g.total_distance * max(0.0, min(1.0, (?1 - rs.time_start) / "
                                   "rs.duration)) END "
                                   "  AS target_distance "
                                   "  FROM route_segments AS rs INDEXED BY "
                                   "idx_route_segments_vehicle_numeric_start_end "
                                   "  JOIN geometries g ON g.geometry_id = rs.geometry_id "
                                   "  WHERE rs.time_start <= ?1 AND rs.time_end > ?1 "
                                   "  ORDER BY rs.vehicle_numeric_id, rs.vehicle "
                                   "  LIMIT ?2"
                                   "), bounds AS ("
                                   "  SELECT active.vehicle, active.vehicle_numeric_id, active.geometry_id, "
                                   "active.target_distance, "
                                   "  (SELECT point_index FROM geometry_points "
                                   "   WHERE geometry_id = active.geometry_id AND distance <= "
                                   "active.target_distance "
                                   "   ORDER BY distance DESC, point_index DESC LIMIT 1) AS p1_index, "
                                   "  (SELECT point_index FROM geometry_points "
                                   "   WHERE geometry_id = active.geometry_id AND distance >= "
                                   "active.target_distance "
                                   "   ORDER BY distance ASC, point_index ASC LIMIT 1) AS p2_index "
                                   "  FROM active"
                                   ") "
                                   "SELECT bounds.vehicle, "
                                   "p1.lat, p1.lon, p2.lat, p2.lon, "
                                   "bounds.target_distance, p1.distance, p2.distance "
                                   "FROM bounds "
                                   "JOIN geometry_points p1 ON p1.geometry_id = bounds.geometry_id AND "
                                   "p1.point_index = bounds.p1_index "
                                   "JOIN geometry_points p2 ON p2.geometry_id = bounds.geometry_id AND "
                                   "p2.point_index = bounds.p2_index "
                                   "ORDER BY bounds.vehicle_numeric_id, bounds.vehicle";
    const char* active_end_sql = "WITH active AS ("
                                 "  SELECT rs.vehicle, rs.vehicle_numeric_id, rs.geometry_id, "
                                 "  CASE WHEN rs.duration <= 0 THEN g.total_distance "
                                 "  ELSE g.total_distance * max(0.0, min(1.0, (?1 - rs.time_start) / "
                                 "rs.duration)) END "
                                 "  AS target_distance "
                                 "  FROM route_segments AS rs INDEXED BY "
                                 "idx_route_segments_vehicle_numeric_end_start "
                                 "  JOIN geometries g ON g.geometry_id = rs.geometry_id "
                                 "  WHERE rs.time_start <= ?1 AND rs.time_end > ?1 "
                                 "  ORDER BY rs.vehicle_numeric_id, rs.vehicle "
                                 "  LIMIT ?2"
                                 "), bounds AS ("
                                 "  SELECT active.vehicle, active.vehicle_numeric_id, active.geometry_id, "
                                 "active.target_distance, "
                                 "  (SELECT point_index FROM geometry_points "
                                 "   WHERE geometry_id = active.geometry_id AND distance <= "
                                 "active.target_distance "
                                 "   ORDER BY distance DESC, point_index DESC LIMIT 1) AS p1_index, "
                                 "  (SELECT point_index FROM geometry_points "
                                 "   WHERE geometry_id = active.geometry_id AND distance >= "
                                 "active.target_distance "
                                 "   ORDER BY distance ASC, point_index ASC LIMIT 1) AS p2_index "
                                 "  FROM active"
                                 ") "
                                 "SELECT bounds.vehicle, "
                                 "p1.lat, p1.lon, p2.lat, p2.lon, "
                                 "bounds.target_distance, p1.distance, p2.distance "
                                 "FROM bounds "
                                 "JOIN geometry_points p1 ON p1.geometry_id = bounds.geometry_id AND "
                                 "p1.point_index = bounds.p1_index "
                                 "JOIN geometry_points p2 ON p2.geometry_id = bounds.geometry_id AND "
                                 "p2.point_index = bounds.p2_index "
                                 "ORDER BY bounds.vehicle_numeric_id, bounds.vehicle";
    const char* legacy_active_start_sql = "WITH active AS ("
                                          "  SELECT rs.vehicle, rs.vehicle_numeric_id, rs.geometry_id, "
                                          "  CASE WHEN rs.duration <= 0 THEN g.total_distance "
                                          "  ELSE g.total_distance * max(0.0, min(1.0, (?1 - rs.time_start) / "
                                          "rs.duration)) END "
                                          "  AS target_distance "
                                          "  FROM route_segments AS rs INDEXED BY "
                                          "idx_route_segments_start_vehicle_numeric "
                                          "  JOIN geometries g ON g.geometry_id = rs.geometry_id "
                                          "  WHERE rs.time_start <= ?1 AND rs.time_end > ?1 "
                                          "  ORDER BY rs.vehicle_numeric_id, rs.vehicle "
                                          "  LIMIT ?2"
                                          "), bounds AS ("
                                          "  SELECT active.vehicle, active.vehicle_numeric_id, active.geometry_id, "
                                          "active.target_distance, "
                                          "  (SELECT point_index FROM geometry_points "
                                          "   WHERE geometry_id = active.geometry_id AND distance <= "
                                          "active.target_distance "
                                          "   ORDER BY distance DESC, point_index DESC LIMIT 1) AS p1_index, "
                                          "  (SELECT point_index FROM geometry_points "
                                          "   WHERE geometry_id = active.geometry_id AND distance >= "
                                          "active.target_distance "
                                          "   ORDER BY distance ASC, point_index ASC LIMIT 1) AS p2_index "
                                          "  FROM active"
                                          ") "
                                          "SELECT bounds.vehicle, "
                                          "p1.lat, p1.lon, p2.lat, p2.lon, "
                                          "bounds.target_distance, p1.distance, p2.distance "
                                          "FROM bounds "
                                          "JOIN geometry_points p1 ON p1.geometry_id = bounds.geometry_id AND "
                                          "p1.point_index = bounds.p1_index "
                                          "JOIN geometry_points p2 ON p2.geometry_id = bounds.geometry_id AND "
                                          "p2.point_index = bounds.p2_index "
                                          "ORDER BY bounds.vehicle_numeric_id, bounds.vehicle";
    const char* legacy_active_end_sql = "WITH active AS ("
                                        "  SELECT rs.vehicle, rs.vehicle_numeric_id, rs.geometry_id, "
                                        "  CASE WHEN rs.duration <= 0 THEN g.total_distance "
                                        "  ELSE g.total_distance * max(0.0, min(1.0, (?1 - rs.time_start) / "
                                        "rs.duration)) END "
                                        "  AS target_distance "
                                        "  FROM route_segments AS rs INDEXED BY "
                                        "idx_route_segments_end_vehicle_numeric "
                                        "  JOIN geometries g ON g.geometry_id = rs.geometry_id "
                                        "  WHERE rs.time_start <= ?1 AND rs.time_end > ?1 "
                                        "  ORDER BY rs.vehicle_numeric_id, rs.vehicle "
                                        "  LIMIT ?2"
                                        "), bounds AS ("
                                        "  SELECT active.vehicle, active.vehicle_numeric_id, active.geometry_id, "
                                        "active.target_distance, "
                                        "  (SELECT point_index FROM geometry_points "
                                        "   WHERE geometry_id = active.geometry_id AND distance <= "
                                        "active.target_distance "
                                        "   ORDER BY distance DESC, point_index DESC LIMIT 1) AS p1_index, "
                                        "  (SELECT point_index FROM geometry_points "
                                        "   WHERE geometry_id = active.geometry_id AND distance >= "
                                        "active.target_distance "
                                        "   ORDER BY distance ASC, point_index ASC LIMIT 1) AS p2_index "
                                        "  FROM active"
                                        ") "
                                        "SELECT bounds.vehicle, "
                                        "p1.lat, p1.lon, p2.lat, p2.lon, "
                                        "bounds.target_distance, p1.distance, p2.distance "
                                        "FROM bounds "
                                        "JOIN geometry_points p1 ON p1.geometry_id = bounds.geometry_id AND "
                                        "p1.point_index = bounds.p1_index "
                                        "JOIN geometry_points p2 ON p2.geometry_id = bounds.geometry_id AND "
                                        "p2.point_index = bounds.p2_index "
                                        "ORDER BY bounds.vehicle_numeric_id, bounds.vehicle";

    memset(playback_db, 0, sizeof(*playback_db));
    if (!resolve_project_path((db_path && db_path[0]) ? db_path : DEFAULT_DB_PATH, playback_db->path, sizeof(playback_db->path)))
    {
        snprintf(playback_db->status_line, sizeof(playback_db->status_line), "Could not resolve database path");
        return;
    }

    db_path = playback_db->path;

    if (!file_exists(db_path))
    {
        snprintf(playback_db->status_line, sizeof(playback_db->status_line), "SQLite playback DB not found: %s", db_path);
        return;
    }

    if (sqlite3_open_v2(db_path, &playback_db->db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK)
    {
        snprintf(playback_db->status_line, sizeof(playback_db->status_line), "Could not open %s.", db_path);
        return;
    }

    sqlite3_busy_timeout(playback_db->db, 5000);

    if (sqlite3_prepare_v2(playback_db->db, "SELECT MIN(time_start), MAX(time_end) FROM route_segments", -1, &range_stmt, NULL) != SQLITE_OK)
    {
        snprintf(playback_db->status_line, sizeof(playback_db->status_line), "Could not prepare playback time range query: %s", sqlite3_errmsg(playback_db->db));
        sqlite3_finalize(range_stmt);
        return;
    }

    {
        int step_result = sqlite3_step(range_stmt);

        if (step_result != SQLITE_ROW)
        {
            snprintf(playback_db->status_line, sizeof(playback_db->status_line), "Could not read playback time range: %s", sqlite3_errstr(step_result));
            sqlite3_finalize(range_stmt);
            return;
        }
    }

    playback_db->min_time = sqlite3_column_double(range_stmt, 0);
    playback_db->max_time = sqlite3_column_double(range_stmt, 1);
    playback_db->midpoint_time = playback_db->min_time + ((playback_db->max_time - playback_db->min_time) / 2.0);
    sqlite3_finalize(range_stmt);

    if (!prepare_sql_statement(playback_db->db, active_start_sql, &playback_db->active_start_stmt) || !prepare_sql_statement(playback_db->db, active_end_sql, &playback_db->active_end_stmt))
    {
        using_legacy_index_plan = true;

        if (!prepare_sql_statement(playback_db->db, legacy_active_start_sql, &playback_db->active_start_stmt) ||
            !prepare_sql_statement(playback_db->db, legacy_active_end_sql, &playback_db->active_end_stmt))
        {
            snprintf(playback_db->status_line, sizeof(playback_db->status_line), "Could not prepare detailed geometry playback query: %s", sqlite3_errmsg(playback_db->db));
            return;
        }
    }

    playback_db->ready = true;
    snprintf(playback_db->status_line, sizeof(playback_db->status_line),
             using_legacy_index_plan ? "SQLite OSM-geometry playback ready (legacy index plan): %s" : "SQLite OSM-geometry playback ready: %s", db_path);
}

static void
playback_db_shutdown(struct playback_db* playback_db)
{
    if (playback_db->active_start_stmt)
    {
        sqlite3_finalize(playback_db->active_start_stmt);
        playback_db->active_start_stmt = NULL;
    }

    if (playback_db->active_end_stmt)
    {
        sqlite3_finalize(playback_db->active_end_stmt);
        playback_db->active_end_stmt = NULL;
    }

    if (playback_db->db)
    {
        sqlite3_close(playback_db->db);
        playback_db->db = NULL;
    }

    playback_db->ready = false;
}

#ifdef __APPLE__
static void
reload_playback_db(struct playback_db* playback_db, struct vehicle_stream* stream, struct websocket_server* server, const char* db_path)
{
    char chosen_path[sizeof(playback_db->path)];

    snprintf(chosen_path, sizeof(chosen_path), "%s", (db_path && db_path[0]) ? db_path : DEFAULT_DB_PATH);

    if (stream)
    {
        stream_stop(stream);
        stream->next_frame_at_ms = 0.0;
        stream->last_tick_ms = 0;
        stream->last_sent_vehicle_count = 0;
        string_list_clear(&stream->visible_vehicle_ids);
    }

    playback_db_shutdown(playback_db);
    playback_db_init(playback_db, chosen_path);

    if (stream && playback_db->ready)
    {
        stream->simulated_seconds = playback_db->min_time;
    }

    if (server)
    {
        copy_status(server->status_line, sizeof(server->status_line), playback_db->status_line);
    }
}
#endif

static void
stream_free(struct vehicle_stream* stream)
{
    if (stream->last_snapshot_preview)
    {
        free(stream->last_snapshot_preview);
        stream->last_snapshot_preview = NULL;
    }

    stream->last_snapshot_preview_capacity = 0;
    string_list_free(&stream->visible_vehicle_ids);
    string_list_free(&stream->selected_vehicle_ids);
}

static bool
format_snapshot_preview(const char* snapshot, char** formatted_preview, size_t* formatted_length)
{
    yyjson_doc* doc = NULL;
    yyjson_val* root = NULL;
    yyjson_val* vehicle = NULL;
    char* preview = NULL;
    size_t preview_capacity = 0;
    size_t offset = 0;
    size_t index = 0;
    size_t max = 0;

    if (!snapshot || !formatted_preview || !formatted_length)
    {
        return false;
    }

    *formatted_preview = NULL;
    *formatted_length = 0;

    doc = yyjson_read(snapshot, strlen(snapshot), 0);
    if (!doc)
    {
        return false;
    }

    root = yyjson_doc_get_root(doc);
    if (!yyjson_is_arr(root))
    {
        yyjson_doc_free(doc);
        return false;
    }

    preview_capacity = strlen(snapshot) + (yyjson_arr_size(root) * 4) + 8;
    preview = (char*)malloc(preview_capacity);
    if (!preview)
    {
        yyjson_doc_free(doc);
        return false;
    }

    offset += (size_t)snprintf(preview + offset, preview_capacity - offset, "[\n");

    yyjson_arr_foreach(root, index, max, vehicle)
    {
        char* vehicle_json = NULL;
        size_t vehicle_length = 0;

        vehicle_json = yyjson_val_write(vehicle, 0, &vehicle_length);
        if (!vehicle_json)
        {
            free(preview);
            yyjson_doc_free(doc);
            return false;
        }

        if (offset + vehicle_length + 4 >= preview_capacity)
        {
            free(vehicle_json);
            free(preview);
            yyjson_doc_free(doc);
            return false;
        }

        if (index > 0)
        {
            preview[offset++] = ',';
            preview[offset++] = '\n';
        }

        memcpy(preview + offset, vehicle_json, vehicle_length);
        offset += vehicle_length;
        preview[offset] = '\0';
        free(vehicle_json);
    }

    if (offset + 3 >= preview_capacity)
    {
        free(preview);
        yyjson_doc_free(doc);
        return false;
    }

    preview[offset++] = '\n';
    preview[offset++] = ']';
    preview[offset] = '\0';

    yyjson_doc_free(doc);
    *formatted_preview = preview;
    *formatted_length = offset + 1;
    return true;
}

static bool
stream_store_snapshot_preview(struct vehicle_stream* stream, const char* snapshot)
{
    char* formatted_preview = NULL;
    size_t required_capacity = 0;
    size_t formatted_length = 0;
    const char* preview_source = snapshot;

    if (!snapshot)
    {
        return false;
    }

    if (format_snapshot_preview(snapshot, &formatted_preview, &formatted_length))
    {
        preview_source = formatted_preview;
        required_capacity = formatted_length;
    }
    else
    {
        required_capacity = strlen(snapshot) + 1;
    }

    if (required_capacity > stream->last_snapshot_preview_capacity)
    {
        char* resized_buffer = (char*)realloc(stream->last_snapshot_preview, required_capacity);

        if (!resized_buffer)
        {
            free(formatted_preview);
            return false;
        }

        stream->last_snapshot_preview = resized_buffer;
        stream->last_snapshot_preview_capacity = required_capacity;
    }

    memcpy(stream->last_snapshot_preview, preview_source, required_capacity);
    free(formatted_preview);
    return true;
}

static void
stream_start(struct vehicle_stream* stream)
{
    stream->streaming = true;
    stream->next_frame_at_ms = 0.0;
}

static void
stream_stop(struct vehicle_stream* stream)
{
    stream->streaming = false;
    stream->next_frame_at_ms = 0.0;
}

static void
stream_update_clock(struct vehicle_stream* stream, uint64_t now_ms)
{
    if (!stream->streaming)
    {
        stream->last_tick_ms = now_ms;
        return;
    }

    if (stream->last_tick_ms == 0)
    {
        stream->last_tick_ms = now_ms;
        return;
    }

    if (now_ms > stream->last_tick_ms)
    {
        stream->simulated_seconds += (double)(now_ms - stream->last_tick_ms) / 1000.0;
        stream->last_tick_ms = now_ms;
    }
}

static bool
append_snapshot_vehicle(yyjson_mut_doc* doc, yyjson_mut_val* snapshot_array, const char* vehicle_id, double lat, double lon)
{
    yyjson_mut_val* vehicle = NULL;

    if (!doc || !snapshot_array || !vehicle_id)
    {
        return false;
    }

    vehicle = yyjson_mut_obj(doc);
    if (!vehicle)
    {
        return false;
    }

    if (!yyjson_mut_arr_append(snapshot_array, vehicle))
    {
        return false;
    }

    if (!yyjson_mut_obj_add_strcpy(doc, vehicle, "id", vehicle_id))
    {
        return false;
    }

    if (!yyjson_mut_obj_add_real(doc, vehicle, "lat", lat) || !yyjson_mut_obj_add_real(doc, vehicle, "lon", lon))
    {
        return false;
    }

    return true;
}

static int
build_database_snapshot(struct vehicle_stream* stream, struct playback_db* playback_db, yyjson_mut_doc* doc, yyjson_mut_val* snapshot_array)
{
    int row_count = 0;
    int sent_row_count = 0;
    sqlite3_stmt* active_stmt = NULL;

    if (!playback_db->ready)
    {
        return -1;
    }

    active_stmt = stream->simulated_seconds <= playback_db->midpoint_time ? playback_db->active_start_stmt : playback_db->active_end_stmt;

    if (!active_stmt)
    {
        return -1;
    }

    sqlite3_reset(active_stmt);
    sqlite3_clear_bindings(active_stmt);
    sqlite3_bind_double(active_stmt, 1, stream->simulated_seconds);
    sqlite3_bind_int(active_stmt, 2, stream->vehicle_count);
    string_list_clear(&stream->visible_vehicle_ids);

    while (sqlite3_step(active_stmt) == SQLITE_ROW)
    {
        const unsigned char* vehicle_id = sqlite3_column_text(active_stmt, 0);
        const char* vehicle_id_text = vehicle_id ? (const char*)vehicle_id : "unknown";
        double lat1 = sqlite3_column_double(active_stmt, 1);
        double lon1 = sqlite3_column_double(active_stmt, 2);
        double lat2 = sqlite3_column_double(active_stmt, 3);
        double lon2 = sqlite3_column_double(active_stmt, 4);
        double target_distance = sqlite3_column_double(active_stmt, 5);
        double distance1 = sqlite3_column_double(active_stmt, 6);
        double distance2 = sqlite3_column_double(active_stmt, 7);
        double segment_progress = 0.0;
        double lat = lat1;
        double lon = lon1;

        if (!string_list_append_unique(&stream->visible_vehicle_ids, vehicle_id_text))
        {
            return -3;
        }

        if (distance2 > distance1)
        {
            segment_progress = (target_distance - distance1) / (distance2 - distance1);

            if (segment_progress < 0.0)
            {
                segment_progress = 0.0;
            }
            else if (segment_progress > 1.0)
            {
                segment_progress = 1.0;
            }

            lat = lat1 + (lat2 - lat1) * segment_progress;
            lon = lon1 + (lon2 - lon1) * segment_progress;
        }

        row_count += 1;

        if (stream->isolate_selected_vehicles && !string_list_contains(&stream->selected_vehicle_ids, vehicle_id_text))
        {
            continue;
        }

        if (!append_snapshot_vehicle(doc, snapshot_array, vehicle_id_text, lat, lon))
        {
            return -2;
        }

        sent_row_count += 1;
    }

    playback_db->last_active_count = row_count;
    stream->last_sent_vehicle_count = sent_row_count;
    return sent_row_count;
}

static bool
stream_send_current_snapshot(struct vehicle_stream* stream, struct websocket_server* server, struct playback_db* playback_db)
{
    yyjson_mut_doc* doc = NULL;
    yyjson_mut_val* snapshot_array = NULL;
    char* snapshot = NULL;
    size_t snapshot_length = 0;
    bool has_client = server_has_client(server);

    if (!playback_db->ready)
    {
        copy_status(server->status_line, sizeof(server->status_line), playback_db->status_line);
        return false;
    }

    if (stream->simulated_seconds > playback_db->max_time)
    {
        stream->simulated_seconds = playback_db->min_time;
    }

    doc = yyjson_mut_doc_new(NULL);
    if (!doc)
    {
        copy_status(server->status_line, sizeof(server->status_line), "Failed to allocate JSON snapshot document.");
        return false;
    }

    snapshot_array = yyjson_mut_arr(doc);
    if (!snapshot_array)
    {
        yyjson_mut_doc_free(doc);
        copy_status(server->status_line, sizeof(server->status_line), "Failed to allocate JSON snapshot array.");
        return false;
    }
    yyjson_mut_doc_set_root(doc, snapshot_array);

    {
        int database_rows = build_database_snapshot(stream, playback_db, doc, snapshot_array);

        if (database_rows < -1)
        {
            yyjson_mut_doc_free(doc);
            copy_status(server->status_line, sizeof(server->status_line), "Snapshot buffer was too small.");
            return false;
        }

        stream->packets_sent += (uint64_t)(database_rows > 0 ? database_rows : 0);
    }

    snapshot = yyjson_mut_write(doc, 0, &snapshot_length);
    yyjson_mut_doc_free(doc);
    if (!snapshot)
    {
        copy_status(server->status_line, sizeof(server->status_line), "Failed to serialize JSON snapshot.");
        return false;
    }

    if (!stream_store_snapshot_preview(stream, snapshot))
    {
        free(snapshot);
        copy_status(server->status_line, sizeof(server->status_line), "Failed to store snapshot preview.");
        return false;
    }

    if (!has_client)
    {
        free(snapshot);
        return false;
    }

    if (!server_send_text(server, snapshot))
    {
        free(snapshot);
        server_close_client(server);
        return false;
    }

    free(snapshot);
    stream->frames_sent += 1;
    return true;
}

static void
stream_send_frame(struct vehicle_stream* stream, struct websocket_server* server, struct playback_db* playback_db, uint64_t now_ms)
{
    if (!stream->streaming || stream->fps <= 0 || stream->vehicle_count <= 0)
    {
        return;
    }

    if (stream->next_frame_at_ms <= 0.0)
    {
        stream->next_frame_at_ms = (double)now_ms;
    }

    if ((double)now_ms + 0.001 < stream->next_frame_at_ms)
    {
        return;
    }

    if (!stream_send_current_snapshot(stream, server, playback_db))
    {
        return;
    }

    stream->next_frame_at_ms += 1000.0 / (double)stream->fps;

    if (stream->next_frame_at_ms < (double)now_ms - 1000.0)
    {
        stream->next_frame_at_ms = (double)now_ms;
    }
}

static void
draw_preview_lines(struct nk_context* ctx, const char* text)
{
    const char* cursor = text && text[0] ? text : "(empty)";
    float row_height = 22.0f;

    if (ctx && ctx->style.font)
    {
        row_height = ctx->style.font->height + 8.0f;
    }

    while (*cursor)
    {
        const char* line_end = strchr(cursor, '\n');
        size_t line_length = line_end ? (size_t)(line_end - cursor) : strlen(cursor);

        if (line_length == 0)
        {
            nk_layout_row_dynamic(ctx, row_height, 1);
            nk_label(ctx, " ", NK_TEXT_LEFT);
        }
        else
        {
            while (line_length > 0)
            {
                char line_buffer[512];
                size_t chunk_length = line_length < sizeof(line_buffer) - 1 ? line_length : sizeof(line_buffer) - 1;

                memcpy(line_buffer, cursor, chunk_length);
                line_buffer[chunk_length] = '\0';

                nk_layout_row_dynamic(ctx, row_height, 1);
                nk_label_wrap(ctx, line_buffer);

                cursor += chunk_length;
                line_length -= chunk_length;
            }
        }

        if (!line_end)
        {
            break;
        }

        cursor = line_end + 1;
    }
}

static float
ui_font_height(const struct nk_context* ctx)
{
    if (ctx && ctx->style.font)
    {
        return ctx->style.font->height;
    }

    return 14.0f;
}

static float
ui_text_row_height(const struct nk_context* ctx)
{
    return ui_font_height(ctx) + 8.0f;
}

static float
ui_input_row_height(const struct nk_context* ctx)
{
    return ui_font_height(ctx) + 14.0f;
}

static float
ui_button_row_height(const struct nk_context* ctx)
{
    return ui_font_height(ctx) + 14.0f;
}

static float
ui_small_spacer_height(const struct nk_context* ctx)
{
    float height = ui_font_height(ctx) * 0.5f;

    if (height < 8.0f)
    {
        height = 8.0f;
    }

    return height;
}

static void
apply_ui_style_overrides(struct nk_context* ctx, float ui_scale)
{
    float font_height = 0.0f;
    float slider_cursor = 0.0f;
    struct nk_color app_background = nk_rgb(APP_BG_R, APP_BG_G, APP_BG_B);

    if (!ctx || !ctx->style.font)
    {
        return;
    }

    nk_style_default(ctx);

    font_height = ctx->style.font->height;
    slider_cursor = font_height + 4.0f;

    if (slider_cursor < 18.0f)
    {
        slider_cursor = 18.0f;
    }

    ctx->style.window.fixed_background = nk_style_item_color(app_background);
    ctx->style.window.background = app_background;
    ctx->style.window.border_color = app_background;
    ctx->style.window.border = 0.0f;
    ctx->style.edit.normal = nk_style_item_color(nk_rgb(38, 38, 38));
    ctx->style.edit.hover = nk_style_item_color(nk_rgb(38, 38, 38));
    ctx->style.edit.active = nk_style_item_color(nk_rgb(38, 38, 38));
    ctx->style.property.normal = nk_style_item_color(nk_rgb(38, 38, 38));
    ctx->style.property.hover = nk_style_item_color(nk_rgb(38, 38, 38));
    ctx->style.property.active = nk_style_item_color(nk_rgb(38, 38, 38));
    ctx->style.combo.normal = nk_style_item_color(nk_rgb(38, 38, 38));
    ctx->style.combo.hover = nk_style_item_color(nk_rgb(38, 38, 38));
    ctx->style.combo.active = nk_style_item_color(nk_rgb(38, 38, 38));
    ctx->style.slider.cursor_size = nk_vec2(slider_cursor, slider_cursor);

    ctx->style.window.padding = nk_vec2(ctx->style.window.padding.x * ui_scale, ctx->style.window.padding.y * ui_scale);
    ctx->style.window.group_padding = nk_vec2(ctx->style.window.group_padding.x * ui_scale, ctx->style.window.group_padding.y * ui_scale);
    ctx->style.window.popup_padding = nk_vec2(ctx->style.window.popup_padding.x * ui_scale, ctx->style.window.popup_padding.y * ui_scale);
    ctx->style.window.spacing = nk_vec2(ctx->style.window.spacing.x * ui_scale, ctx->style.window.spacing.y * ui_scale);
    ctx->style.window.scrollbar_size = nk_vec2(ctx->style.window.scrollbar_size.x * ui_scale, ctx->style.window.scrollbar_size.y * ui_scale);

    ctx->style.button.padding = nk_vec2(ctx->style.button.padding.x * ui_scale, ctx->style.button.padding.y * ui_scale);
    ctx->style.button.image_padding = nk_vec2(ctx->style.button.image_padding.x * ui_scale, ctx->style.button.image_padding.y * ui_scale);
    ctx->style.button.touch_padding = nk_vec2(ctx->style.button.touch_padding.x * ui_scale, ctx->style.button.touch_padding.y * ui_scale);

    ctx->style.edit.padding = nk_vec2(ctx->style.edit.padding.x * ui_scale, ctx->style.edit.padding.y * ui_scale);
    ctx->style.property.padding = nk_vec2(ctx->style.property.padding.x * ui_scale, ctx->style.property.padding.y * ui_scale);
    ctx->style.combo.button_padding = nk_vec2(ctx->style.combo.button_padding.x * ui_scale, ctx->style.combo.button_padding.y * ui_scale);
    ctx->style.combo.content_padding = nk_vec2(ctx->style.combo.content_padding.x * ui_scale, ctx->style.combo.content_padding.y * ui_scale);

    ctx->style.selectable.padding = nk_vec2(ctx->style.selectable.padding.x * ui_scale, ctx->style.selectable.padding.y * ui_scale);
    ctx->style.selectable.touch_padding = nk_vec2(ctx->style.selectable.touch_padding.x * ui_scale, ctx->style.selectable.touch_padding.y * ui_scale);

    ctx->style.chart.padding = nk_vec2(ctx->style.chart.padding.x * ui_scale, ctx->style.chart.padding.y * ui_scale);
}

static void
draw_visible_vehicle_group(struct nk_context* ctx, struct vehicle_stream* stream, bool* timeline_changed, float panel_height)
{
    int i = 0;
    int matching_vehicle_count = 0;
    int total_visible_vehicle_count = stream->visible_vehicle_ids.count;
    int vehicle_list_height = 0;
    char selected_summary[512];
    float text_row_height = ui_text_row_height(ctx);
    float input_row_height = ui_input_row_height(ctx);
    float button_row_height = ui_button_row_height(ctx);
    float list_row_height = ui_font_height(ctx) + 12.0f;
    float used_height = 0.0f;

    if (!nk_group_begin(ctx, "Vehicles at Time", NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_NO_SCROLLBAR))
    {
        return;
    }

    build_selected_summary(&stream->selected_vehicle_ids, selected_summary, sizeof(selected_summary));

    nk_layout_row_dynamic(ctx, text_row_height, 1);
    nk_label(ctx, "Search vehicle ID:", NK_TEXT_LEFT);

    nk_layout_row_dynamic(ctx, input_row_height, 1);
    nk_edit_string_zero_terminated(ctx, (nk_flags)NK_EDIT_FIELD | (nk_flags)NK_EDIT_CLIPBOARD, stream->vehicle_search, sizeof(stream->vehicle_search), nk_filter_default);

    nk_layout_row_dynamic(ctx, text_row_height, 1);
    nk_labelf(ctx, NK_TEXT_LEFT, "Selected: %s", selected_summary);

    nk_layout_row_dynamic(ctx, button_row_height, 2);
    if (nk_button_label(ctx, stream->isolate_selected_vehicles ? "Isolate: ON" : "Isolate"))
    {
        if (stream->selected_vehicle_ids.count > 0)
        {
            stream->isolate_selected_vehicles = !stream->isolate_selected_vehicles;
            *timeline_changed = true;
        }
    }
    if (nk_button_label(ctx, "Clear"))
    {
        string_list_clear(&stream->selected_vehicle_ids);
        stream->isolate_selected_vehicles = false;
        *timeline_changed = true;
    }

    for (i = 0; i < total_visible_vehicle_count; ++i)
    {
        if (text_contains_ci(stream->visible_vehicle_ids.items[i], stream->vehicle_search))
        {
            matching_vehicle_count += 1;
        }
    }

    nk_layout_row_dynamic(ctx, text_row_height, 1);
    nk_labelf(ctx, NK_TEXT_LEFT, "Visible vehicles: %d / %d", matching_vehicle_count, total_visible_vehicle_count);

    used_height = text_row_height + input_row_height + text_row_height + button_row_height + text_row_height + (ui_small_spacer_height(ctx) * 2.0f) + 28.0f;

    vehicle_list_height = (int)(panel_height - used_height);
    if (vehicle_list_height < 160)
    {
        vehicle_list_height = 160;
    }
    nk_layout_row_dynamic(ctx, (float)vehicle_list_height, 1);

    if (nk_group_begin(ctx, "Vehicle List", NK_WINDOW_BORDER))
    {
        if (matching_vehicle_count == 0)
        {
            nk_layout_row_dynamic(ctx, text_row_height, 1);

            if (total_visible_vehicle_count == 0)
            {
                nk_label(ctx, "No active vehicles at this time", NK_TEXT_LEFT);
            }
            else
            {
                nk_label(ctx, "No vehicles match current filter", NK_TEXT_LEFT);
            }
        }
        else
        {
            for (i = 0; i < total_visible_vehicle_count; ++i)
            {
                const char* vehicle_id = stream->visible_vehicle_ids.items[i];
                char vehicle_button[128];
                bool is_selected = false;

                if (!text_contains_ci(vehicle_id, stream->vehicle_search))
                {
                    continue;
                }

                is_selected = string_list_contains(&stream->selected_vehicle_ids, vehicle_id);

                nk_layout_row_dynamic(ctx, list_row_height, 1);
                snprintf(vehicle_button, sizeof(vehicle_button), "%s%s", is_selected ? "* " : "", vehicle_id ? vehicle_id : "unknown");

                if (nk_button_label(ctx, vehicle_button))
                {
                    string_list_toggle(&stream->selected_vehicle_ids, vehicle_id);

                    if (stream->isolate_selected_vehicles && stream->selected_vehicle_ids.count == 0)
                    {
                        stream->isolate_selected_vehicles = false;
                    }

                    *timeline_changed = true;
                }
            }
        }

        nk_group_end(ctx);
    }

    nk_group_end(ctx);
}

static void
draw_ui(struct nk_context* ctx, struct websocket_server* server, struct vehicle_stream* stream, struct playback_db* playback_db, bool* timeline_changed, int window_width, int window_height)
{
    struct nk_rect bounds = nk_rect(0, 0, (float)window_width, (float)window_height);
    float title_row_height = ui_input_row_height(ctx);
    float text_row_height = ui_text_row_height(ctx);
    float button_row_height = ui_button_row_height(ctx);
    float slider_row_height = ui_input_row_height(ctx);
    float spacer_height = ui_small_spacer_height(ctx);
    float window_safety_margin = title_row_height + text_row_height + spacer_height + 24.0f;
    float top_reserved_height = (title_row_height * 2.0f) + (text_row_height * 8.0f) + (button_row_height * 3.0f) + (slider_row_height * 4.0f) + window_safety_margin;
    float preview_height = bounds.h - top_reserved_height;

    if (preview_height < 120.0f)
    {
        preview_height = 120.0f;
    }

    nk_window_set_bounds(ctx, "Vehicle Stream Server", bounds);

    if (nk_begin(ctx, "Vehicle Stream Server", bounds, NK_WINDOW_TITLE))
    {
        nk_layout_row_dynamic(ctx, title_row_height, 1);
        nk_label_wrap(ctx, server->status_line);

        nk_layout_row_dynamic(ctx, text_row_height, 1);
        nk_label(ctx, server_has_client(server) ? "WebSocket client: connected" : "WebSocket client: not connected", NK_TEXT_LEFT);
        nk_label(ctx, stream->streaming ? "Streaming: active" : "Streaming: stopped", NK_TEXT_LEFT);
        nk_label(ctx, playback_db->status_line, NK_TEXT_LEFT);

        nk_layout_row_dynamic(ctx, text_row_height, 1);
        nk_label(ctx, "Playback DB:", NK_TEXT_LEFT);

        nk_layout_row_begin(ctx, NK_DYNAMIC, slider_row_height, 2);
        nk_layout_row_push(ctx, 0.75f);
        nk_edit_string_zero_terminated(ctx, (nk_flags)NK_EDIT_FIELD | (nk_flags)NK_EDIT_CLIPBOARD, playback_db->path, sizeof(playback_db->path), nk_filter_default);
#ifdef __APPLE__
        nk_layout_row_push(ctx, 0.25f);
        if (nk_button_label(ctx, "Choose DB"))
        {
            char chosen_path[sizeof(playback_db->path)];

            if (pick_sqlite_db_file(chosen_path, sizeof(chosen_path)))
            {
                snprintf(playback_db->path, sizeof(playback_db->path), "%s", chosen_path);
                reload_playback_db(playback_db, stream, server, playback_db->path);
                *timeline_changed = playback_db->ready;
            }
        }
#else
        nk_layout_row_push(ctx, 0.25f);
        nk_label(ctx, "Choose DB unavailable", NK_TEXT_LEFT);
#endif
        nk_layout_row_end(ctx);

        nk_layout_row_dynamic(ctx, button_row_height, 2);
        if (nk_button_label(ctx, stream->streaming ? "Pause" : "Play"))
        {
            if (stream->streaming)
            {
                stream_stop(stream);
                copy_status(server->status_line, sizeof(server->status_line), "Playback paused.");
            }
            else
            {
                if (playback_db->ready)
                {
                    stream_start(stream);
                    copy_status(server->status_line, sizeof(server->status_line), "Playback started.");
                }
                else
                {
                    copy_status(server->status_line, sizeof(server->status_line), playback_db->status_line);
                }
            }
        }
        if (nk_button_label(ctx, "Reset Time"))
        {
            if (playback_db->ready)
            {
                stream->simulated_seconds = playback_db->min_time;
                stream->next_frame_at_ms = 0.0;
                *timeline_changed = true;
                copy_status(server->status_line, sizeof(server->status_line), "Playback time reset.");
            }
            else
            {
                copy_status(server->status_line, sizeof(server->status_line), playback_db->status_line);
            }
        }

        nk_layout_row_dynamic(ctx, slider_row_height, 1);
        nk_property_int(ctx, "FPS", 1, &stream->fps, 60, 1, 1);
        nk_property_int(ctx, "Vehicles", 1, &stream->vehicle_count, 10000, 1, 10);

        nk_layout_row_dynamic(ctx, text_row_height, 1);
        nk_labelf(ctx, NK_TEXT_LEFT, "Frames sent: %llu", (unsigned long long)stream->frames_sent);
        nk_labelf(ctx, NK_TEXT_LEFT, "Vehicle lines sent: %llu", (unsigned long long)stream->packets_sent);
        nk_labelf(ctx, NK_TEXT_LEFT, "Vehicle lines per second target: %d", stream->fps * stream->vehicle_count);
        if (playback_db->ready)
        {
            float timeline_value = (float)stream->simulated_seconds;
            float timeline_min = (float)playback_db->min_time;
            float timeline_max = (float)playback_db->max_time;

            nk_labelf(ctx, NK_TEXT_LEFT, "Simulation time: %.1f / %.1f, active vehicles: %d, sent: %d", stream->simulated_seconds, playback_db->max_time, playback_db->last_active_count,
                      stream->last_sent_vehicle_count);

            nk_layout_row_dynamic(ctx, slider_row_height, 1);
            nk_slider_float(ctx, timeline_min, &timeline_value, timeline_max, 1.0f);

            if ((double)timeline_value != stream->simulated_seconds)
            {
                stream->simulated_seconds = (double)timeline_value;
                stream->next_frame_at_ms = 0.0;
                *timeline_changed = true;
            }
        }

        nk_layout_row_dynamic(ctx, preview_height, 2);

        draw_visible_vehicle_group(ctx, stream, timeline_changed, preview_height);

        if (nk_group_begin(ctx, "Last Snapshot Sent", NK_WINDOW_BORDER | NK_WINDOW_TITLE))
        {
            draw_preview_lines(ctx, stream->last_snapshot_preview ? stream->last_snapshot_preview : server->last_sent);
            nk_layout_row_dynamic(ctx, spacer_height, 1);
            nk_label(ctx, " ", NK_TEXT_LEFT);
            nk_group_end(ctx);
        }
    }

    nk_end(ctx);
}

static void
glfw_error_callback(int code, const char* description)
{
    fprintf(stderr, "GLFW error %d: %s\n", code, description ? description : "(none)");
}

int
main(void)
{
    GLFWwindow* window = NULL;
    struct nk_glfw glfw_backend = {0};
    struct nk_context* ctx = NULL;
    struct nk_font_atlas* atlas = NULL;
    struct websocket_server server;
    struct vehicle_stream stream;
    struct playback_db playback_db;
    bool running = true;
    struct nk_font* custom_font = NULL;
    int window_width = WINDOW_WIDTH;
    int window_height = WINDOW_HEIGHT;
    float layout_scale = 1.0f;
    float font_scale = 1.0f;

    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
    {
        fprintf(stderr, "Failed to initialize GLFW\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
#ifdef __APPLE__
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#else
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
#endif
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);

    window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Simulator", NULL, NULL);
    if (!window)
    {
        fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return 1;
    }

    float window_content_scale_x = 1.0f;
    float window_content_scale_y = 1.0f;
    glfwGetWindowContentScale(window, &window_content_scale_x, &window_content_scale_y);
    float ui_scale = (window_content_scale_x + window_content_scale_y) * 0.5f;
    if (ui_scale < 1.0f)
    {
        ui_scale = 1.0f;
    }

#ifdef _WIN32
    layout_scale = ui_scale;
    font_scale = ui_scale;
#endif

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (gl3wInit() != GL3W_OK)
    {
        fprintf(stderr, "Failed to initialize gl3w\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    ctx = nk_glfw3_init(&glfw_backend, window, NK_GLFW3_INSTALL_CALLBACKS);
    nk_glfw3_font_stash_begin(&glfw_backend, &atlas);
    if (atlas)
    {
        char resolved_font_path[1024];
        float font_size = CUSTOM_FONT_SIZE * font_scale;
        if (resolve_project_path(CUSTOM_FONT_PATH, resolved_font_path, sizeof(resolved_font_path)) && file_exists(resolved_font_path))
        {
            custom_font = nk_font_atlas_add_from_file(atlas, resolved_font_path, font_size, NULL);
        }
        else
        {
            custom_font = nk_font_atlas_add_default(atlas, font_size, NULL);
        }
    }
    nk_glfw3_font_stash_end(&glfw_backend);
    if (custom_font)
    {
        nk_style_set_font(ctx, &custom_font->handle);
    }
    else if (atlas && atlas->default_font)
    {
        nk_style_set_font(ctx, &atlas->default_font->handle);
    }
    apply_ui_style_overrides(ctx, layout_scale);

    if (!server_init(&server, SERVER_PORT))
    {
        fprintf(stderr, "%s\n", server.status_line);
    }
    stream_init(&stream);
    playback_db_init(&playback_db, DEFAULT_DB_PATH);
    if (playback_db.ready)
    {
        stream.simulated_seconds = playback_db.min_time;
    }

    while (running && !glfwWindowShouldClose(window))
    {
        uint64_t now_ms = (uint64_t)llround(glfwGetTime() * 1000.0);
        bool timeline_changed = false;
        int framebuffer_width = 0;
        int framebuffer_height = 0;

        glfwPollEvents();
        if (glfwWindowShouldClose(window))
        {
            running = false;
        }
        nk_glfw3_new_frame(&glfw_backend);

        glfwGetWindowSize(window, &window_width, &window_height);
        if (window_width < 280)
        {
            window_width = 280;
        }
        if (window_height < 320)
        {
            window_height = 320;
        }

        stream_update_clock(&stream, now_ms);
        server_poll(&server);
        stream_send_frame(&stream, &server, &playback_db, now_ms);
        draw_ui(ctx, &server, &stream, &playback_db, &timeline_changed, window_width, window_height);

        if (timeline_changed)
        {
            stream_send_current_snapshot(&stream, &server, &playback_db);
        }

        glfwGetFramebufferSize(window, &framebuffer_width, &framebuffer_height);
        glViewport(0, 0, framebuffer_width, framebuffer_height);
        glClearColor((float)APP_BG_R / 255.0f, (float)APP_BG_G / 255.0f, (float)APP_BG_B / 255.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        nk_glfw3_render(&glfw_backend, NK_ANTI_ALIASING_ON, MAX_VERTEX_BUFFER, MAX_ELEMENT_BUFFER);
        glfwSwapBuffers(window);
    }

    server_shutdown(&server);
    playback_db_shutdown(&playback_db);
    stream_free(&stream);
    nk_glfw3_shutdown(&glfw_backend);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
