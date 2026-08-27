
// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ rjf: Table Stripe Functions

lib_internal StripeArray
stripe_array_alloc(Arena* arena)
{
    StripeArray array = {0};
    array.count = OS_GetSystemInfo()->logical_processor_count;
    array.v = PushArray(arena, Stripe, array.count);
  for
      EachIndex(idx, array.count)
      {
          array.v[idx].arena = arena_alloc();
          array.v[idx].rw_mutex = rw_mutex_alloc();
          array.v[idx].cv = cond_var_alloc();
      }
  return array;
}

lib_internal void
stripe_array_release(StripeArray* stripes)
{
  for
      EachIndex(idx, stripes->count)
      {
          arena_release(stripes->v[idx].arena);
          rw_mutex_release(stripes->v[idx].rw_mutex);
          cond_var_release(stripes->v[idx].cv);
      }
}

lib_internal Stripe*
stripe_from_slot_idx(StripeArray* stripes, U64 slot_idx)
{
    Stripe* stripe = &stripes->v[slot_idx % stripes->count];
    return stripe;
}

////////////////////////////////
//~ rjf: Platform-Abstracted Synchronization Primitive Functions

//- rjf: slow barriers

typedef struct BarrierNode BarrierNode;
struct BarrierNode
{
    BarrierNode* next;
    U64 count;
    U64 threads_left_to_enter;
    U64 threads_left_to_leave;
    RWMutex rw_mutex;
    CondVar cv;
};

typedef struct BarrierTCTX BarrierTCTX;
struct BarrierTCTX
{
    Arena* arena;
    BarrierNode* free_barrier_node;
};

thread_static BarrierTCTX* barrier_tctx = 0;

lib_internal Barrier
slow_barrier_alloc(U64 count)
{
    if (barrier_tctx == 0)
    {
        Arena* arena = arena_alloc();
        barrier_tctx = PushArray(arena, BarrierTCTX, 1);
        barrier_tctx->arena = arena;
    }
    BarrierNode* n = barrier_tctx->free_barrier_node;
    if (n != 0)
    {
        SLLStackPop(barrier_tctx->free_barrier_node);
    }
    else
    {
        n = PushArrayNoZero(barrier_tctx->arena, BarrierNode, 1);
    }
    MemoryZeroStruct(n);
    n->count = count;
    n->threads_left_to_enter = count;
    n->rw_mutex = rw_mutex_alloc();
    n->cv = cond_var_alloc();
    Barrier result = {(U64)n};
    return result;
}

lib_internal void
slow_barrier_release(Barrier barrier)
{
    if (barrier_tctx == 0)
    {
        Arena* arena = arena_alloc();
        barrier_tctx = PushArray(arena, BarrierTCTX, 1);
        barrier_tctx->arena = arena;
    }
    BarrierNode* n = (BarrierNode*)barrier.u64[0];
    rw_mutex_release(n->rw_mutex);
    cond_var_release(n->cv);
    SLLStackPush(barrier_tctx->free_barrier_node, n);
}

lib_internal void
slow_barrier_wait(Barrier barrier)
{
    prof_scope_marker;
    BarrierNode* n = (BarrierNode*)barrier.u64[0];
    U64 threads_left_to_enter = ins_atomic_u64_dec_eval(&n->threads_left_to_enter);

    //- rjf: threads left to enter > 0 => wait
    if (threads_left_to_enter > 0)
    {
        // rjf: first try a spin loop
        B32 done_waiting = 0;
        for (U64 spin_count = 0; spin_count < 10000; spin_count += 1)
        {
            if (ins_atomic_u64_eval(&n->threads_left_to_leave) != 0)
            {
                prof_scope_marker_named("spin loop wait");
                done_waiting = 1;
                break;
            }
        }

        // rjf: not done waiting -> need to do slow wait on condition variable
        if (!done_waiting)
        {
            prof_scope_marker_named("spin slow wait");
            DeferLoop(rw_mutex_take(n->rw_mutex, 0), rw_mutex_drop(n->rw_mutex, 0)) for (;;)
            {
                if (ins_atomic_u64_eval(&n->threads_left_to_leave) != 0)
                {
                    break;
                }
                cond_var_wait_rw(n->cv, n->rw_mutex, 0, max_U64);
            }
        }

        // rjf: decrement leave counter
        if (ins_atomic_u64_dec_eval(&n->threads_left_to_leave) > 0)
        {
            prof_scope_marker_named("signal");
            cond_var_signal(n->cv);
        }
    }

    //- rjf: threads left to enter == 0 -> last thread, wakeup
    else
    {
        ins_atomic_u64_eval_assign(&n->threads_left_to_enter, n->count);
        DeferLoop(rw_mutex_take(n->rw_mutex, 1), rw_mutex_drop(n->rw_mutex, 1))
        {
            prof_scope_marker_named("wake up");
            ins_atomic_u64_eval_assign(&n->threads_left_to_leave, n->count - 1);
        }
        {
            prof_scope_marker_named("signal");
            cond_var_signal(n->cv);
        }
    }

    //- rjf: wait for threads left to leave == 0
    {
        prof_scope_marker_named("wait for threads to leave");
        for (U64 spin_count = 0;; spin_count += 1)
        {
            if (ins_atomic_u64_eval(&n->threads_left_to_leave) == 0)
            {
                break;
            }
        }
    }
}

lib_internal Barrier
barrier_alloc(U64 count)
{
    Barrier barrier = slow_barrier_alloc(count);
    return barrier;
}

lib_internal void
barrier_release(Barrier barrier)
{
    slow_barrier_release(barrier);
}

lib_internal void
barrier_wait(Barrier barrier)
{
    slow_barrier_wait(barrier);
}
