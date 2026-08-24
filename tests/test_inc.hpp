// third party header
#define DOCTEST_CONFIG_IMPLEMENT
#include "third_party/doctest/doctest.h"
#include <CDT.h>
#include "glm/glm.hpp"
#include <limits>

// user header
#include "diagnostics.hpp"
#include "base/base_inc.hpp"
#include "async/segment_buffer.hpp"
#include "async/async_heap.hpp"
#include "async/thread_pool.hpp"
#include "render/render.hpp"
#include "misc/geometry.hpp"
