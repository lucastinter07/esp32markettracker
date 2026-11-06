# PlatformIO pre-build script: reads FINNHUB_API_KEY from .env (gitignored) and passes
# it to the compiler as a #define, so the key never has to appear in the source code.
Import("env")
import os

key = ""
path = os.path.join(env["PROJECT_DIR"], ".env")
if os.path.isfile(path):
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line.startswith("FINNHUB_API_KEY="):
                key = line.split("=", 1)[1].strip().strip('"').strip("'")

if not key:
    print("load_env.py: FINNHUB_API_KEY not found in .env; the tracker will show NO API KEY")

env.Append(CPPDEFINES=[("FINNHUB_API_KEY", env.StringifyMacro(key))])
