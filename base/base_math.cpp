Vec2F64
operator+(Vec2F64 l, Vec2F64 r)
{
    return {l.x + r.x, l.y + r.y};
}

Vec2F64
operator-(Vec2F64 l, Vec2F64 r)
{
    return {l.x - r.x, l.y - r.y};
}

Vec2F64
operator*(Vec2F64 v, F64 c)
{
    return {v.x * c, v.y * c};
}
