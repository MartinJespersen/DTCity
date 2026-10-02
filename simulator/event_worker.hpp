#pragma once

struct SimulatorEventWorker
{
    Arena* arena;
    OS_Handle thread;
    OS_Handle mutex;
    OS_Handle work_cv;
    std::atomic<bool> stop;
    bool busy;
    bool pending;
    bool completed;
    city::ServerUpdate update;
    U64 request_id;
    U64 generation;
    char db_path[1024];
    String8 reply;
};

static void
simulator_event_worker_start(SimulatorEventWorker* worker);
static void
simulator_event_worker_stop(SimulatorEventWorker* worker);

static void
_simulator_event_worker_run(void* ptr);
static int
_simulator_event_worker_cancel(void* ptr);
