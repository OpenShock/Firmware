import os
import shutil

Import("env")

def build_frontend(source, target, env):
    # Change working directory to frontend.
    os.chdir("WebUI")

    print("Building frontend...")
    os.system("npm i")
    os.system("npm run build")
    print("Frontend build complete.")

    # Change working directory back to root.
    os.chdir("..")

    # Delete the data/www directory if it exists.
    if os.path.exists("data/www"):
        shutil.rmtree("data/www")

    # Copy the frontend build to the data/www directory.
    shutil.copytree("WebUI/build", "data/www")

env.AddPreAction("$BUILD_DIR/littlefs.bin", build_frontend)
