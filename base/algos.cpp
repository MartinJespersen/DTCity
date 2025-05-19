

// fixed array

template <typename T>
root_function Array<T>
ArrayAlloc(Arena* arena, u64 capacity)
{
    Array<T>* arr = PushStruct(arena, Array<T>);
    arr->capacity = capacity;
    arr->data = PushArray(arena, T, arr->capacity);
    return *arr;
}

// hash
root_function u128
HashFromStr8(String8 string)
{
    u128 hash = {0};
    {
        meow_u128 meow_hash = MeowHash(MeowDefaultSeed, string.size, string.str);
        MemoryCopy(&hash, &meow_hash, Min(sizeof(meow_hash), sizeof(hash)));
    }
    return hash;
}