
// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef BASE_THREADS_H
#define BASE_THREADS_H

////////////////////////////////
//~ rjf: Thread Types

typedef struct Barrier Barrier;
struct Barrier
{
    U64 u64[1];
};

typedef struct RWMutex RWMutex;
struct RWMutex
{
    U64 u64[1];
};

typedef struct CondVar CondVar;
struct CondVar
{
    U64 u64[1];
};

////////////////////////////////
//~ rjf: Table Stripes

typedef struct Stripe Stripe;
struct Stripe
{
    Arena* arena;
    RWMutex rw_mutex;
    CondVar cv;
    void* free;
};

typedef struct StripeArray StripeArray;
struct StripeArray
{
    Stripe* v;
    U64 count;
};

////////////////////////////////
//~ rjf: Table Stripe Functions

lib_internal StripeArray
stripe_array_alloc(Arena* arena);
lib_internal void
stripe_array_release(StripeArray* stripes);
lib_internal Stripe*
stripe_from_slot_idx(StripeArray* stripes, U64 slot_idx);

//- rjf: barriers
lib_internal Barrier
barrier_alloc(U64 count);
lib_internal void
barrier_release(Barrier barrier);
lib_internal void
barrier_wait(Barrier barrier);

////////////////////////////////
//~ rjf: Platform-Abstracted Synchronization Primitive Functions

//- rjf: slow barriers
lib_internal Barrier
slow_barrier_alloc(U64 count);
lib_internal void
slow_barrier_release(Barrier barrier);
lib_internal void
slow_barrier_wait(Barrier barrier);

lib_internal RWMutex
rw_mutex_alloc();
lib_internal void
rw_mutex_release(RWMutex mutex);
lib_internal void
rw_mutex_take(RWMutex mutex, B32 write_mode);
lib_internal void
rw_mutex_drop(RWMutex mutex, B32 write_mode);
lib_internal CondVar
cond_var_alloc();
lib_internal void
cond_var_release(CondVar cv);
lib_internal B32
cond_var_wait_rw(CondVar cv, RWMutex mutex, B32 write_mode, U64 endt_us);
lib_internal void
cond_var_signal(CondVar cv);

#endif // BASE_THREADS_H
