#pragma once

// TODO: Create a array/list that is a mix of a fixed array and a linked list to improve cache
// utility for some workloads.

// Fixed array

template <typename T> struct Array
{
    T* data;
    u64 capacity;

    T&
    operator[](u64 index)
    {
        if (index >= capacity)
        {
            exitWithError("Indexing is out of bounds");
        }
        return data[index];
    }
};

template <typename T>
root_function Array<T>
ArrayAlloc(Arena* arena, u64 capacity);

// hashing
root_function u128
HashFromStr8(String8 string);
