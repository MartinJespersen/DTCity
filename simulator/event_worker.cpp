static void
simulator_event_worker_start(SimulatorEventWorker* worker)
{
    worker->arena = arena_alloc();
    worker->mutex = OS_MutexAlloc();
    worker->work_cv = os_condition_variable_alloc();
    worker->thread = OS_ThreadLaunch(_simulator_event_worker_run, worker, NULL);
}

static void
simulator_event_worker_stop(SimulatorEventWorker* worker)
{
    os_mutex_take(worker->mutex);
    worker->stop.store(true);
    os_condition_variable_signal(worker->work_cv);
    os_mutex_drop(worker->mutex);
    // Joining is restricted to shutdown; frame processing only polls completed work.
    OS_ThreadJoin(worker->thread, max_U64);
    os_condition_variable_release(worker->work_cv);
    OS_MutexRelease(worker->mutex);
    arena_release(worker->arena);
}

static void
_simulator_event_worker_run(void* ptr)
{
    SimulatorEventWorker* worker = (SimulatorEventWorker*)ptr;
    for (;;)
    {
        os_mutex_take(worker->mutex);
        while (!worker->pending && !worker->stop.load())
        {
            os_condition_variable_wait(worker->work_cv, worker->mutex, max_U64);
        }
        bool stop = worker->stop.load();
        worker->pending = false;
        os_mutex_drop(worker->mutex);
        if (stop) break;

        // The worker owns this connection. UI reloads cannot close it during a query.
        sqlite3* db = NULL;
        defer(sqlite3_close(db));
        bool ready = true;
        if (worker->db_path[0])
        {
            int result = sqlite3_open_v2(worker->db_path, &db, SQLITE_OPEN_READONLY, NULL);
            ready = result == SQLITE_OK;
            if (ready)
            {
                sqlite3_progress_handler(db, 1000, _simulator_event_worker_cancel, worker);
            }
        }
        worker->reply = {};
        if (ready)
        {
            worker->reply = simulator_event_window_reply(worker->arena, db, worker->update, worker->request_id);
        }
        os_mutex_take(worker->mutex);
        worker->completed = true;
        os_mutex_drop(worker->mutex);
    }
}

static int
_simulator_event_worker_cancel(void* ptr)
{
    SimulatorEventWorker* worker = (SimulatorEventWorker*)ptr;
    return worker->stop.load() ? 1 : 0;
}
