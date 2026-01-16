Import("env")

import os
import shutil

def after_build(source, target, env):
    build_dir = env.subst("$BUILD_DIR")

    src = os.path.join(build_dir, "firmware.bin")
    dst = os.path.join(build_dir, "UM980_BLE.bin")

    if not os.path.exists(src):
        print("✖ firmware.bin not found")
        return

    # Copy / rename
    shutil.copy(src, dst)
    print("✔ Single BIN generated:", dst)

    # Remove original firmware.bin
    try:
        os.remove(src)
        print("✔ firmware.bin removed")
    except Exception as e:
        print("✖ failed to remove firmware.bin:", e)

env.AddPostAction("buildprog", after_build)
