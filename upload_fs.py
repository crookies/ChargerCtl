Import("env", "projenv")

def after_upload(source, target, env):
    print("\n=== Uploading LittleFS Filesystem ===\n")
    env.Execute("pio run --target uploadfs")

env.AddPostAction("upload", after_upload)
