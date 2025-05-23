// hash
internal U128
HashFromStr8(String8 string)
{
    U128 hash = {0};
    {
        meow_u128 meow_hash = MeowHash(MeowDefaultSeed, string.size, string.str);
        MemoryCopy(&hash, &meow_hash, Min(sizeof(meow_hash), sizeof(hash)));
    }
    return hash;
}