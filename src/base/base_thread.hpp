#pragma once


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

StripeArray
stripe_array_alloc(Arena* arena);
void
stripe_array_release(StripeArray* stripes);
Stripe*
stripe_from_slot_idx(StripeArray* stripes, U64 slot_idx);

//- rjf: barriers
Barrier
barrier_alloc(U64 count);
void
barrier_release(Barrier barrier);
void
barrier_wait(Barrier barrier);

////////////////////////////////
//~ rjf: Platform-Abstracted Synchronization Primitive Functions

//- rjf: slow barriers
Barrier
slow_barrier_alloc(U64 count);
void
slow_barrier_release(Barrier barrier);
void
slow_barrier_wait(Barrier barrier);

RWMutex
rw_mutex_alloc();
void
rw_mutex_release(RWMutex mutex);
void
rw_mutex_take(RWMutex mutex, B32 write_mode);
void
rw_mutex_drop(RWMutex mutex, B32 write_mode);
CondVar
cond_var_alloc();
void
cond_var_release(CondVar cv);
B32
cond_var_wait_rw(CondVar cv, RWMutex mutex, B32 write_mode, U64 endt_us);
void
cond_var_signal(CondVar cv);

#endif // BASE_THREADS_H
