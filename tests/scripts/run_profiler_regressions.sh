#/bin/bash

set -eo pipefail

if [[ -z "$TT_METAL_HOME" ]]; then
  echo "Must provide TT_METAL_HOME in environment" 1>&2
  exit 1
fi

cd $TT_METAL_HOME

source build/python_env/bin/activate
export PYTHONPATH=$TT_METAL_HOME

if [[ $1 == "FW" ]]; then
    pytest $TT_METAL_HOME/tests/tt_metal/tools/profiler/test_device_profiler.py -vvv
else
    pytest $TT_METAL_HOME/tests/tt_metal/tools/profiler/test_device_logs.py -vvv
    pytest $TT_METAL_HOME/tests/tt_metal/tools/profiler/test_device_profiler.py -vvv
fi
