#pragma once

struct SimulatorScenario
{
    String8 name;
    String8 path;
};

static Buffer<SimulatorScenario>
simulator_scenarios_find(Arena* arena, String8 directory);
static String8
simulator_scenario_path_find(Arena* arena, String8 directory, String8 name);
