TEST_CASE("Rng3F32 subtraction translates both bounds")
{
    Vec3F32 min = v3f32(-2.0f, 4.0f, 1.0f);
    Vec3F32 max = v3f32(8.0f, 10.0f, 7.0f);
    Rng3F32 bounds = r3f32(min, max);
    Vec3F32 offset = v3f32(3.0f, 4.0f, -2.0f);

    Rng3F32 translated_bounds = sub_rng3f32(bounds, offset);

    CHECK(translated_bounds.min.x == -5.0f);
    CHECK(translated_bounds.min.y == 0.0f);
    CHECK(translated_bounds.min.z == 3.0f);
    CHECK(translated_bounds.max.x == 5.0f);
    CHECK(translated_bounds.max.y == 6.0f);
    CHECK(translated_bounds.max.z == 9.0f);
    CHECK(bounds.min.x == -2.0f);
    CHECK(bounds.max.x == 8.0f);
}
