Import("env")

import shutil
import os

def after_build(source, target, env):
    build_dir = env.subst("$BUILD_DIR")
    src = os.path.join(build_dir, "firmware.bin")
    dst = os.path.join(build_dir, "UM980-BLE.bin")

    if os.path.exists(src):
        shutil.copy(src, dst)
        print("✔ Single BIN generated:", dst)

env.AddPostAction("buildprog", after_build)
