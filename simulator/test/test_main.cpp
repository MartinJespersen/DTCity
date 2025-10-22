#include "base/base_inc.hpp"
#include "os_core/os_core_inc.hpp"
#include "simulator/simulator_inc.hpp"

#include "base/base_inc.cpp"
#include "os_core/os_core_inc.cpp"
#include "simulator/simulator_inc.cpp"

void
App()
{
    SIM_Init();
    ScratchScope scratch = ScratchScope(0, 0);

    String8 file_path = S("C:\\matsim\\scenarios\\locations.csv");
    SIM_Read(file_path);

    // if (!parsed_result.success)
    // {
    //     for (SIM_ParseErrorNode* error_node = parsed_result.errors.first; error_node != NULL;
    //          error_node = error_node->next)
    //     {
    //         printf("%s\n", error_node->message.str);
    //     }
    //     exit(1);
    // }

    SIM_AgentSnapshotBucket* snapshot_bucket = &g_sim_ctx->bucket;
    for (U32 i = 0; i < 10; ++i)
    {
        printf("%llu, ", snapshot_bucket->snapshots[i].timestamp);
        printf("%s, ", (char*)snapshot_bucket->snapshots[i].id.str);
        printf("%f, ", snapshot_bucket->snapshots[i].x);
        printf("%f\n", snapshot_bucket->snapshots[i].y);
    }
    printf("...\n");
    for (U32 i = snapshot_bucket->total_snapshots - 10; i < snapshot_bucket->total_snapshots; ++i)
    {
        printf("%llu, ", snapshot_bucket->snapshots[i].timestamp);
        printf("%s, ", (char*)snapshot_bucket->snapshots[i].id.str);
        printf("%f, ", snapshot_bucket->snapshots[i].x);
        printf("%f\n", snapshot_bucket->snapshots[i].y);
    }

    SIM_Del();
}
