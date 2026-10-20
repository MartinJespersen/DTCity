#include "agent.hpp"
#include "city/tile_bvh.hpp"
#include "base/base_core.hpp"
#include <glm/ext/vector_double3.hpp>
#include <glm/geometric.hpp>
#include <cmath>

namespace city
{

B32
agent_height_result_apply(Agent* agent, const AgentHeightRequest& request,
                          const TileHeightResult& result, U64 epoch)
{
    if (request.request_id != result.request_id || request.epoch != epoch ||
        agent->handle.idx != request.agent_handle.idx || agent->handle.gen_id != request.agent_handle.gen_id ||
        agent->height_request_id != result.request_id)
        return false;
    agent->height_request_id = 0;
    glm::dvec3 displacement = agent->ecef_coord - request.sampled_ecef;
    if (!result.hit || glm::dot(displacement, displacement) > 25.0 ||
        !std::isfinite(result.hit_distance) || result.hit_distance < 0)
        return false;
    F32 height = (F32)(request.origin_height - result.hit_distance);
    agent->transform_dirty = agent->transform_dirty || agent->height != height;
    agent->height = height;
    return true;
}

} // namespace city
