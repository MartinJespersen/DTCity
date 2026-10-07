#pragma once

static String8
simulator_metadata_reply_from_directory(Arena* arena, String8 directory, F64 timestamp_start, F64 timestamp_end);

static String8
simulator_metadata_reply(Arena* arena, String8* scenarios, U32 scenario_count, F64 timestamp_start, F64 timestamp_end);
