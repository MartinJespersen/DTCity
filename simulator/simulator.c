#include <initializer_list>
#include <atomic>
#include <memory>
#include <new>
#include <utility>

#define DEBUG_LOG(...)
#define ERROR_LOG(...)


#include "third_party/simdjson/simdjson.h"
#include "third_party/simdjson/simdjson.cpp"
using namespace simdjson;
#include "base/base_inc.hpp"
#include "os_core/os_core_inc.hpp"
#include "resource_paths.hpp"
#include "metadata.hpp"

#include "base/base_inc.cpp"
#include "resource_paths.cpp"
#define SY__MAIN 1
#include "city/simulator_shared_interface.hpp"
#include "city/simulator_shared_interface.cpp"

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

#include <math.h>
#include <sqlite3.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <commdlg.h>
#endif

#include <GL/gl3w.h>
#include <GLFW/glfw3.h>

#include <civetweb.h>

static void*
_server_thread_init(const struct mg_context* context, int thread_type);
static void
_server_thread_exit(const struct mg_context* context, int thread_type, void* thread_pointer);
#include <nuklear.h>
#include <nuklear_glfw_gl3.h>
#include <yyjson.h>
#include "event_snapshot.hpp"
#include "event_worker.hpp"
#include "event_snapshot.cpp"
#include "event_worker.cpp"
#include "metadata.cpp"

#define WINDOW_WIDTH 1024
#define WINDOW_HEIGHT 1024
#define MAX_VERTEX_BUFFER (4 * 1024 * 1024)
#define MAX_ELEMENT_BUFFER (1 * 1024 * 1024)
#define SERVER_PORT 8080
#define MAX_STATUS_TEXT 8192
#define MAX_PATH_TEXT 1024
#define DEFAULT_DB_PATH "simulator/database/eskiltuna_test.sqlite"
#define CUSTOM_FONT_PATH "simulator/fonts/segoeuithis.ttf"
#define CUSTOM_FONT_SIZE 18.0f
#define DEFAULT_WS_PATH "/ws"
#define APP_BG_R 45
#define APP_BG_G 45
#define APP_BG_B 45

struct websocket_server
{
    struct mg_context* context;
    struct mg_connection* client_connection;
    U64 stream_generation;
    F64 timestamp_start;
    F64 timestamp_end;
    char requested_name[1024];
    F64 requested_playback;
    F64 requested_period;
    U64 request_id;
    bool request_pending;
    OS_Handle mutex;
    char last_received[MAX_STATUS_TEXT];
    char last_sent[MAX_STATUS_TEXT];
    char status_line[256];
};

struct vehicle_stream
{
    U64 frames_sent;
    Arena* preview_arena;
    char* last_snapshot_preview;
};

struct playback_db
{
    Arena* arena;
    sqlite3* db;
    bool ready;
    double min_time;
    double max_time;
    String8 path;
    char path_input[MAX_PATH_TEXT];
    char status_line[256];
};

static void
_server_event_requests_process(struct vehicle_stream* stream, struct websocket_server* server,
                                struct playback_db* playback_db, SimulatorEventWorker* worker);

static void
_server_playback_range_update(struct websocket_server* server, struct playback_db* playback_db);

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

static String8
str_abs_path_from_relative(Arena* arena, String8 relative_path)
{
    String8 project_root = simulator_resource_root_get(arena);
    String8 result = str8_path_from_str8_list(arena, {project_root, relative_path});
    return result;
}

static bool
resolve_db_path(Arena* arena, String8 path, String8* out_path)
{
    String8 source_path = path;
    if (source_path.size == 0)
    {
        source_path = str8_c_string(DEFAULT_DB_PATH);
    }

    PathStyle path_style = path_style_from_str8(source_path);
    if (path_style == PathStyle_Relative)
    {
        *out_path = str_abs_path_from_relative(arena, source_path);
    }
    else
    {
        *out_path = push_str8_copy(arena, source_path);
    }

    B32 path_exists = os_file_path_exists(*out_path);
    return out_path->size > 0 && path_exists;
}

static void
server_mutex_init(struct websocket_server* server)
{
    server->mutex = OS_MutexAlloc();
}

static void
server_mutex_destroy(struct websocket_server* server)
{
    if (server && server->mutex.u64[0] != 0)
    {
        OS_MutexRelease(server->mutex);
        server->mutex = {};
    }
}

static void
server_lock(struct websocket_server* server)
{
    if (!server || server->mutex.u64[0] == 0)
    {
        return;
    }
    os_mutex_take(server->mutex);
}

static void
server_unlock(struct websocket_server* server)
{
    if (!server || server->mutex.u64[0] == 0)
    {
        return;
    }
    os_mutex_drop(server->mutex);
}

static void
_server_playback_range_update(struct websocket_server* server, struct playback_db* playback_db)
{
    // Websocket callbacks read this snapshot without accessing SQLite on their thread.
    server_lock(server);
    // A database reload invalidates replies computed from the previous file.
    ++server->stream_generation;
    server->timestamp_start = playback_db->ready ? playback_db->min_time : 0;
    server->timestamp_end = playback_db->ready ? playback_db->max_time : 0;
    server_unlock(server);
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
#elif defined(_WIN32) && !defined(ASAN_ENABLED)
static bool
pick_sqlite_db_file(char* out_path, size_t out_size)
{
    wchar_t selected_path[1024] = {};
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = L"SQLite databases (*.sqlite;*.sqlite3;*.db)\0*.sqlite;*.sqlite3;*.db\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = selected_path;
    dialog.nMaxFile = ArrayCount(selected_path);
    dialog.lpstrTitle = L"Select SQLite playback DB";
    dialog.lpstrDefExt = L"sqlite";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (!out_path || out_size == 0)
    {
        return false;
    }

    BOOL file_selected = GetOpenFileNameW(&dialog);
    if (!file_selected)
    {
        return false;
    }

    int utf8_size = WideCharToMultiByte(CP_UTF8, 0, selected_path, -1, NULL, 0, NULL, NULL);
    if (utf8_size <= 0 || (size_t)utf8_size > out_size)
    {
        return false;
    }

    int converted_size = WideCharToMultiByte(CP_UTF8, 0, selected_path, -1, out_path, utf8_size, NULL, NULL);
    return converted_size > 0;
}
#endif

static void
server_close_client(struct websocket_server* server)
{
    server_lock(server);
    server->client_connection = NULL;
    copy_status(server->status_line, sizeof(server->status_line),
                "Waiting for WebSocket client. Map viewer: http://127.0.0.1:8080");
    server_unlock(server);
}

static bool
server_send_text(struct websocket_server* server, const char* text, U64 stream_generation)
{
    struct mg_connection* connection = NULL;
    int sent = 0;

    if (!server || !text)
    {
        return false;
    }

    size_t text_length = strlen(text);
    server_lock(server);
    if (stream_generation != server->stream_generation)
    {
        server_unlock(server);
        return false;
    }
    connection = server->client_connection;
    if (connection)
    {
        mg_lock_connection(connection);
        sent = mg_websocket_write(connection, MG_WEBSOCKET_OPCODE_TEXT, text, text_length);
        mg_unlock_connection(connection);
    }
    if (sent > 0 && (size_t)sent == text_length)
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
        copy_status(server->status_line, sizeof(server->status_line),
                    user_agent && strstr(user_agent, "Mozilla") ? "Browser viewer connecting..."
                                                                : "External WebSocket client connecting...");
    }
    else
    {
        copy_status(server->status_line, sizeof(server->status_line),
                    "Rejected WebSocket client: another client is already connected.");
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
    ++server->stream_generation;
    server->request_pending = false;
    copy_status(server->status_line, sizeof(server->status_line),
                user_agent && strstr(user_agent, "Mozilla")
                    ? "Browser viewer connected. Map server: http://127.0.0.1:8080"
                    : "External WebSocket client connected on ws://127.0.0.1:8080/ws");
    server_unlock(server);
}

static int
server_websocket_data_handler(struct mg_connection* conn, int bits, char* data, size_t data_len, void* cbdata)
{
    ScratchScope scratch = ScratchScope(0, 0);
    struct websocket_server* server = (struct websocket_server*)cbdata;
    int opcode = bits & 0x0F;
    if (!server)
    {
        return 0;
    }

    if (opcode == MG_WEBSOCKET_OPCODE_CONNECTION_CLOSE)
    {
        return 0;
    }

    if (opcode == MG_WEBSOCKET_OPCODE_TEXT)
    {
        // The callback data has an explicit length and need not be null-terminated.
        size_t copy_len = Min(data_len, sizeof(server->last_received) - 1);
        server_lock(server);
        MemoryCopy(server->last_received, data, copy_len);
        server->last_received[copy_len] = '\0';
        server_unlock(server);

        ondemand::parser parser;
        ondemand::document doc;
        String8 msg = str8((U8*)data, data_len);
        error_code json_error = city::simulator_prepare_json_doc(scratch.arena, msg, parser, doc);
        if (json_error)
        {
            return 0;
        }

        U64 msg_id = {};
        String8 msg_id_name = SIMULATION_FIELD_NAME(MsgId);
        json_error = doc[(const char*)msg_id_name.str].get_uint64().get(msg_id);
        if (json_error)
        {
            return 0;
        }
        int sent = 0;
        switch ((city::SimulationMessageKind)msg_id)
        {
            case city::SimulationMessageKind::MetadataRequest:
            {
                String8 test_reply_msg[] = {S("1. Scenario")};
                F64 timestamp_start = 0;
                F64 timestamp_end = 0;
                server_lock(server);
                timestamp_start = server->timestamp_start;
                timestamp_end = server->timestamp_end;
                server_unlock(server);
                String8 json_str = simulator_metadata_reply(scratch.arena, test_reply_msg, ArrayCount(test_reply_msg),
                                                           timestamp_start, timestamp_end);
                if (json_str.size == 0)
                {
                    return 0;
                }
                sent = mg_websocket_write(conn, MG_WEBSOCKET_OPCODE_TEXT, (char*)json_str.str, json_str.size);

                return sent > 0 ? 1 : 0;
            };
            break;
            case city::SimulationMessageKind::ServerUpdate:
            {
                city::ServerUpdate update = {};
                U64 request_id = 0;
                json_error = city::simulator_server_update_from_json(doc, &update, &request_id);
                if (json_error || update.name.size() >= sizeof(server->requested_name))
                {
                    return 0;
                }
                // Transfer requests to the owner thread; callbacks never touch its SQLite connection.
                server_lock(server);
                // Polls in the same playback epoch must not invalidate an in-progress reply.
                if (request_id != server->request_id)
                {
                    ++server->stream_generation;
                }
                MemoryCopy(server->requested_name, update.name.data(), update.name.size());
                server->requested_name[update.name.size()] = 0;
                server->requested_playback = update.playback;
                server->requested_period = update.period;
                server->request_id = request_id;
                server->request_pending = true;
                server_unlock(server);
                return 1;
            }
            break;
            default: InvalidPath;
        }

        return sent > 0 ? 1 : 0;
    }

    return 0;
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
    copy_status(server->status_line, sizeof(server->status_line),
                "Waiting for WebSocket client. Map viewer: http://127.0.0.1:8080");
    server_unlock(server);
}

static bool
server_init(struct websocket_server* server, uint16_t port)
{
    const char* options[11];
    char port_option[64];
    struct mg_callbacks callbacks;
    struct mg_init_data init_data;
    struct mg_error_data error_data;
    char error_text[256];
    int option_index = 0;

    MemoryZeroStruct(server);
    server_mutex_init(server);

    ScratchScope scratch = ScratchScope(0, 0);
    String8 document_root = simulator_resource_root_get(scratch.arena);
    copy_status(server->status_line, sizeof(server->status_line), "Could not resolve project directory for CivetWeb.");

    snprintf(port_option, sizeof(port_option), "%u", (unsigned int)port);
    MemoryZeroStruct(&callbacks);
    callbacks.init_thread = _server_thread_init;
    callbacks.exit_thread = _server_thread_exit;
    MemoryZeroStruct(&init_data);
    MemoryZeroStruct(&error_data);
    error_data.text = error_text;
    error_data.text_buffer_size = sizeof(error_text);

    options[option_index++] = "document_root";
    options[option_index++] = (char*)document_root.str;
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
            snprintf(server->status_line, sizeof(server->status_line), "Could not start embedded CivetWeb server: %s",
                     error_text);
        }
        else
        {
            copy_status(server->status_line, sizeof(server->status_line),
                        "Could not start embedded CivetWeb server on 127.0.0.1:8080.");
        }
        return false;
    }

    mg_set_request_handler(server->context, "/health", server_health_handler, server);
    mg_set_websocket_handler(server->context, DEFAULT_WS_PATH, server_websocket_connect_handler,
                             server_websocket_ready_handler, server_websocket_data_handler,
                             server_websocket_close_handler, server);

    copy_status(server->status_line, sizeof(server->status_line),
                "Listening on http://127.0.0.1:8080 and ws://127.0.0.1:8080/ws");
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
    MemoryZeroStruct(stream);
    ArenaParams arena_params = {.reserve_size = MB(8), .commit_size = KB(64)};
    stream->preview_arena = arena_alloc(&arena_params);
}
static void
playback_db_init(struct playback_db* playback_db, String8 path)
{
    prof_scope_marker;
    sqlite3_stmt* range_stmt = NULL;


    Arena* arena = playback_db->arena;
    if (arena)
    {
        arena_clear(arena);
    }
    else
    {
        arena = arena_alloc();
    }

    MemoryZeroStruct(playback_db);
    playback_db->arena = arena;

    bool path_resolved = resolve_db_path(playback_db->arena, path, &playback_db->path);
    if (playback_db->path.size >= sizeof(playback_db->path_input))
    {
        snprintf(playback_db->status_line, sizeof(playback_db->status_line), "Database path is too long");
        return;
    }

    MemoryCopy(playback_db->path_input, playback_db->path.str, playback_db->path.size);
    playback_db->path_input[playback_db->path.size] = 0;

    if (!path_resolved)
    {
        snprintf(playback_db->status_line, sizeof(playback_db->status_line), "Database does not exist: %s",
                 playback_db->path_input);
        return;
    }

    if (sqlite3_open_v2((char*)playback_db->path.str, &playback_db->db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK)
    {
        snprintf(playback_db->status_line, sizeof(playback_db->status_line), "Could not open %s.",
                 (char*)playback_db->path.str);
        return;
    }

    sqlite3_busy_timeout(playback_db->db, 5000);

    {
        prof_scope_marker_named("sqlite3_prepare_v2");
        if (sqlite3_prepare_v2(playback_db->db,
                               "SELECT (SELECT MIN(time) FROM agent_events), (SELECT MAX(time) FROM agent_events)", -1,
                               &range_stmt, NULL) != SQLITE_OK)
        {
            snprintf(playback_db->status_line, sizeof(playback_db->status_line),
                     "Could not prepare playback time range query: %s", sqlite3_errmsg(playback_db->db));
            sqlite3_finalize(range_stmt);
            return;
        }
    }

    {
        prof_scope_marker_named("sqlite3_step");
        int step_result = sqlite3_step(range_stmt);

        if (step_result != SQLITE_ROW)
        {
            snprintf(playback_db->status_line, sizeof(playback_db->status_line),
                     "Could not read playback time range: %s", sqlite3_errstr(step_result));
            sqlite3_finalize(range_stmt);
            return;
        }
    }

    playback_db->min_time = sqlite3_column_double(range_stmt, 0);
    playback_db->max_time = sqlite3_column_double(range_stmt, 1);
    sqlite3_finalize(range_stmt);

    playback_db->ready = true;
    snprintf(playback_db->status_line, sizeof(playback_db->status_line), "SQLite agent playback ready: %s",
             (char*)playback_db->path.str);
}

static void
playback_db_shutdown(struct playback_db* playback_db)
{
    if (playback_db->db)
    {
        sqlite3_close(playback_db->db);
        playback_db->db = NULL;
    }

    playback_db->ready = false;
}

static void
reload_playback_db(struct playback_db* playback_db, struct websocket_server* server,
                   const char* db_path)
{
    char chosen_path[MAX_PATH_TEXT];

    snprintf(chosen_path, sizeof(chosen_path), "%s", (db_path && db_path[0]) ? db_path : DEFAULT_DB_PATH);
    String8 str_path = str8_c_string(chosen_path);

    playback_db_shutdown(playback_db);
    playback_db_init(playback_db, str_path);

    if (server)
    {
        _server_playback_range_update(server, playback_db);
        copy_status(server->status_line, sizeof(server->status_line), playback_db->status_line);
    }
}

static void
stream_free(struct vehicle_stream* stream)
{
    if (stream->preview_arena)
    {
        arena_release(stream->preview_arena);
    }

    stream->preview_arena = NULL;
    stream->last_snapshot_preview = NULL;
}

static bool
format_snapshot_preview(Arena* arena, const char* snapshot, char** formatted_preview, size_t* formatted_length)
{
    yyjson_doc* doc = NULL;
    yyjson_val* root = NULL;
    yyjson_val* vehicle = NULL;
    char* preview = NULL;
    size_t preview_capacity = 0;
    size_t offset = 0;
    size_t index = 0;
    size_t max = 0;

    if (!arena || !snapshot || !formatted_preview || !formatted_length)
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
    if (yyjson_is_obj(root))
    {
        size_t json_length = 0;
        char* json_text = yyjson_write(doc, YYJSON_WRITE_PRETTY, &json_length);
        yyjson_doc_free(doc);
        if (!json_text)
        {
            return false;
        }
        defer(free(json_text));
        String8 json_view = str8((U8*)json_text, json_length);
        String8 json_copy = push_str8_copy(arena, json_view);
        *formatted_preview = (char*)json_copy.str;
        *formatted_length = json_length + 1;
        return true;
    }
    if (!yyjson_is_arr(root))
    {
        yyjson_doc_free(doc);
        return false;
    }

    preview_capacity = strlen(snapshot) + (yyjson_arr_size(root) * 4) + 8;
    preview = PushArrayNoZero(arena, char, preview_capacity);
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
            yyjson_doc_free(doc);
            return false;
        }

        if (offset + vehicle_length + 4 >= preview_capacity)
        {
            free(vehicle_json);
            yyjson_doc_free(doc);
            return false;
        }

        if (index > 0)
        {
            preview[offset++] = ',';
            preview[offset++] = '\n';
        }

        MemoryCopy(preview + offset, vehicle_json, vehicle_length);
        offset += vehicle_length;
        preview[offset] = '\0';
        free(vehicle_json);
    }

    if (offset + 3 >= preview_capacity)
    {
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
    size_t formatted_length = 0;

    if (!stream->preview_arena || !snapshot)
    {
        return false;
    }

    arena_clear(stream->preview_arena);
    if (format_snapshot_preview(stream->preview_arena, snapshot, &formatted_preview, &formatted_length))
    {
        stream->last_snapshot_preview = formatted_preview;
    }
    else
    {
        arena_clear(stream->preview_arena);
        size_t snapshot_size = strlen(snapshot);
        stream->last_snapshot_preview = PushArrayNoZero(stream->preview_arena, char, snapshot_size + 1);
        MemoryCopy(stream->last_snapshot_preview, snapshot, snapshot_size);
        stream->last_snapshot_preview[snapshot_size] = 0;
    }

    return true;
}

static void
_server_event_requests_process(struct vehicle_stream* stream, struct websocket_server* server,
                                struct playback_db* playback_db, SimulatorEventWorker* worker)
{
    // Consume completed work without waiting for the query thread.
    os_mutex_take(worker->mutex);
    bool completed = worker->completed;
    bool busy = worker->busy;
    os_mutex_drop(worker->mutex);
    if (completed)
    {
        if (worker->reply.size)
        {
            bool sent = server_send_text(server, (const char*)worker->reply.str, worker->generation);
            if (sent)
            {
                stream_store_snapshot_preview(stream, (const char*)worker->reply.str);
                ++stream->frames_sent;
            }
        }
        else
        {
            copy_status(server->status_line, sizeof(server->status_line), "Failed to read requested event window.");
        }
        arena_clear(worker->arena);
        os_mutex_take(worker->mutex);
        worker->completed = false;
        worker->busy = false;
        os_mutex_drop(worker->mutex);
        busy = false;
    }
    if (busy) return;

    city::ServerUpdate update = {};
    U64 request_id = 0;
    U64 generation = 0;
    server_lock(server);
    bool pending = server->request_pending;
    if (pending)
    {
        update.name = server->requested_name;
        update.playback = server->requested_playback;
        update.period = server->requested_period;
        request_id = server->request_id;
        generation = server->stream_generation;
        server->request_pending = false;
    }
    server_unlock(server);
    if (!pending) return;

    bool scenario_available = update.name == "1. Scenario";
    os_mutex_take(worker->mutex);
    worker->update = std::move(update);
    worker->request_id = request_id;
    worker->generation = generation;
    worker->db_path[0] = 0;
    if (playback_db->ready && scenario_available)
    {
        snprintf(worker->db_path, sizeof(worker->db_path), "%s", playback_db->path_input);
    }
    worker->busy = true;
    worker->pending = true;
    os_condition_variable_signal(worker->work_cv);
    os_mutex_drop(worker->mutex);
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

                MemoryCopy(line_buffer, cursor, chunk_length);
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
    ctx->style.window.group_padding =
        nk_vec2(ctx->style.window.group_padding.x * ui_scale, ctx->style.window.group_padding.y * ui_scale);
    ctx->style.window.popup_padding =
        nk_vec2(ctx->style.window.popup_padding.x * ui_scale, ctx->style.window.popup_padding.y * ui_scale);
    ctx->style.window.spacing = nk_vec2(ctx->style.window.spacing.x * ui_scale, ctx->style.window.spacing.y * ui_scale);
    ctx->style.window.scrollbar_size =
        nk_vec2(ctx->style.window.scrollbar_size.x * ui_scale, ctx->style.window.scrollbar_size.y * ui_scale);

    ctx->style.button.padding = nk_vec2(ctx->style.button.padding.x * ui_scale, ctx->style.button.padding.y * ui_scale);
    ctx->style.button.image_padding =
        nk_vec2(ctx->style.button.image_padding.x * ui_scale, ctx->style.button.image_padding.y * ui_scale);
    ctx->style.button.touch_padding =
        nk_vec2(ctx->style.button.touch_padding.x * ui_scale, ctx->style.button.touch_padding.y * ui_scale);

    ctx->style.edit.padding = nk_vec2(ctx->style.edit.padding.x * ui_scale, ctx->style.edit.padding.y * ui_scale);
    ctx->style.property.padding =
        nk_vec2(ctx->style.property.padding.x * ui_scale, ctx->style.property.padding.y * ui_scale);
    ctx->style.combo.button_padding =
        nk_vec2(ctx->style.combo.button_padding.x * ui_scale, ctx->style.combo.button_padding.y * ui_scale);
    ctx->style.combo.content_padding =
        nk_vec2(ctx->style.combo.content_padding.x * ui_scale, ctx->style.combo.content_padding.y * ui_scale);

    ctx->style.selectable.padding =
        nk_vec2(ctx->style.selectable.padding.x * ui_scale, ctx->style.selectable.padding.y * ui_scale);
    ctx->style.selectable.touch_padding =
        nk_vec2(ctx->style.selectable.touch_padding.x * ui_scale, ctx->style.selectable.touch_padding.y * ui_scale);

    ctx->style.chart.padding = nk_vec2(ctx->style.chart.padding.x * ui_scale, ctx->style.chart.padding.y * ui_scale);
}

static void
draw_ui(struct nk_context* ctx, struct websocket_server* server, struct vehicle_stream* stream,
        struct playback_db* playback_db, int window_width, int window_height)
{
    prof_scope_marker;
    struct nk_rect bounds = nk_rect(0, 0, (float)window_width, (float)window_height);
    float title_row_height = ui_input_row_height(ctx);
    float text_row_height = ui_text_row_height(ctx);
    float slider_row_height = ui_input_row_height(ctx);
    float spacer_height = ui_small_spacer_height(ctx);
    float received_message_height = text_row_height * 4.0f;
    nk_window_set_bounds(ctx, "Vehicle Stream Server", bounds);

    if (nk_begin(ctx, "Vehicle Stream Server", bounds, NK_WINDOW_TITLE))
    {
        nk_layout_row_dynamic(ctx, title_row_height, 1);
        nk_label_wrap(ctx, server->status_line);

        nk_layout_row_dynamic(ctx, text_row_height, 1);
        nk_label(ctx, server_has_client(server) ? "WebSocket client: connected" : "WebSocket client: not connected",
                 NK_TEXT_LEFT);
        nk_label(ctx, playback_db->status_line, NK_TEXT_LEFT);

        // Capture the latest client update and its message under the same short lock.
        char last_received[sizeof(server->last_received)];
        char scenario_name[sizeof(server->requested_name)];
        F64 playback = 0;
        F64 period = 0;
        server_lock(server);
        MemoryCopy(last_received, server->last_received, sizeof(last_received));
        MemoryCopy(scenario_name, server->requested_name, sizeof(scenario_name));
        playback = server->requested_playback;
        period = server->requested_period;
        server_unlock(server);

        nk_layout_row_dynamic(ctx, text_row_height, 1);
        nk_labelf(ctx, NK_TEXT_LEFT, "Scenario: %s", scenario_name[0] ? scenario_name : "None");
        nk_labelf(ctx, NK_TEXT_LEFT, "Playback: %.3f s, fetch period: %.3f s", playback, period);
        if (playback_db->ready && playback_db->max_time > playback_db->min_time)
        {
            // Playback is controlled by the client; the slider displays its latest request.
            float timeline_min = (float)playback_db->min_time;
            float timeline_max = (float)playback_db->max_time;
            float timeline_value = (float)Clamp(playback_db->min_time, playback, playback_db->max_time);
            nk_layout_row_dynamic(ctx, slider_row_height, 1);
            nk_widget_disable_begin(ctx);
            nk_bool slider_changed = nk_slider_float(ctx, timeline_min, &timeline_value, timeline_max, 1.0f);
            (void)slider_changed;
            nk_widget_disable_end(ctx);
        }

        nk_layout_row_dynamic(ctx, text_row_height, 1);
        nk_label(ctx, "Last message from client:", NK_TEXT_LEFT);
        nk_layout_row_dynamic(ctx, received_message_height, 1);
        if (nk_group_begin(ctx, "Received client message", NK_WINDOW_BORDER))
        {
            draw_preview_lines(ctx, last_received);
            nk_group_end(ctx);
        }

        nk_layout_row_dynamic(ctx, text_row_height, 1);
        nk_label(ctx, "Playback DB:", NK_TEXT_LEFT);

        nk_layout_row_begin(ctx, NK_DYNAMIC, slider_row_height, 3);
        nk_layout_row_push(ctx, 0.60f);
        nk_edit_string_zero_terminated(ctx, (nk_flags)NK_EDIT_FIELD | (nk_flags)NK_EDIT_CLIPBOARD,
                                       playback_db->path_input, sizeof(playback_db->path_input), nk_filter_default);
        nk_layout_row_push(ctx, 0.18f);
        if (nk_button_label(ctx, "Load DB"))
        {
            reload_playback_db(playback_db, server, playback_db->path_input);
        }
#if defined(__APPLE__) || (defined(_WIN32) && !defined(ASAN_ENABLED))
        nk_layout_row_push(ctx, 0.22f);
        if (nk_button_label(ctx, "Choose DB"))
        {
            char chosen_path[MAX_PATH_TEXT];
            if (pick_sqlite_db_file(chosen_path, sizeof(chosen_path)))
            {
                reload_playback_db(playback_db, server, chosen_path);
            }
        }
#else
        nk_layout_row_push(ctx, 0.22f);
#if defined(_WIN32) && defined(ASAN_ENABLED)
        nk_label(ctx, "Picker disabled with ASan", NK_TEXT_LEFT);
#else
        nk_label(ctx, "Picker unavailable", NK_TEXT_LEFT);
#endif
#endif
        nk_layout_row_end(ctx);

        nk_layout_row_dynamic(ctx, text_row_height, 1);
        nk_labelf(ctx, NK_TEXT_LEFT, "Replies sent: %llu", (unsigned long long)stream->frames_sent);
        nk_layout_row_dynamic(ctx, Max(120.0f, bounds.h * 0.4f), 1);
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
App(int argc, char** argv)
{
    prof_scope_marker;
    (void)argc;
    (void)argv;
    GLFWwindow* window = NULL;
    struct nk_glfw glfw_backend = {0};
    struct nk_context* ctx = NULL;
    struct nk_font_atlas* atlas = NULL;
    struct websocket_server server;
    struct vehicle_stream stream;
    struct playback_db playback_db = {};
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
        ScratchScope scratch = ScratchScope(0, 0);
        float font_size = CUSTOM_FONT_SIZE * font_scale;
        String8 abs_font_path = str_abs_path_from_relative(scratch.arena, str8_c_string(CUSTOM_FONT_PATH));
        B32 file_exists = os_file_path_exists(abs_font_path);
        if (abs_font_path.size > 0 && file_exists)
        {
            custom_font = nk_font_atlas_add_from_file(atlas, (char*)abs_font_path.str, font_size, NULL);
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
    playback_db_init(&playback_db, str8_c_string(DEFAULT_DB_PATH));
    _server_playback_range_update(&server, &playback_db);
    Arena* frame_arena = arena_alloc();
    SimulatorEventWorker event_worker = {};
    simulator_event_worker_start(&event_worker);

    while (running && !glfwWindowShouldClose(window))
    {
        arena_clear(frame_arena);
        prof_frame_marker;
        prof_scope_marker_named("simulator_frame");
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

        server_poll(&server);
        _server_event_requests_process(&stream, &server, &playback_db, &event_worker);
        draw_ui(ctx, &server, &stream, &playback_db, window_width, window_height);

        glfwGetFramebufferSize(window, &framebuffer_width, &framebuffer_height);
        glViewport(0, 0, framebuffer_width, framebuffer_height);
        glClearColor((float)APP_BG_R / 255.0f, (float)APP_BG_G / 255.0f, (float)APP_BG_B / 255.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        nk_glfw3_render(&glfw_backend, NK_ANTI_ALIASING_ON, MAX_VERTEX_BUFFER, MAX_ELEMENT_BUFFER);
        glfwSwapBuffers(window);
    }

    simulator_event_worker_stop(&event_worker);
    server_shutdown(&server);
    playback_db_shutdown(&playback_db);
    arena_release(playback_db.arena);
    stream_free(&stream);
    nk_glfw3_shutdown(&glfw_backend);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

// CivetWeb creates its own threads, bypassing the os_core thread entry point.
static void*
_server_thread_init(const struct mg_context* context, int thread_type)
{
    (void)context;
    (void)thread_type;
    static thread_static TCTX thread_context;
    TCTX_InitAndEquip(&thread_context);
    return &thread_context;
}

static void
_server_thread_exit(const struct mg_context* context, int thread_type, void* thread_pointer)
{
    (void)context;
    (void)thread_type;
    TCTX* thread_context = TCTX_Get();
    Assert(thread_context == thread_pointer);
    TCTX_Release();
}
