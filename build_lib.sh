#!/bin/bash
set -e

source ./build_config.sh



g++ ${cflags} -shared -fPIC -o ${temp_lib_full_path} ${entrypoint_file_name} ${entrypoint_ldflags} $@
