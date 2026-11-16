# PlatformIO pre-build script: reads the API keys from .env (gitignored) and passes them
# to the compiler as #defines, so no key ever has to appear in the source code.
#   FINNHUB_API_KEY      live quotes, company stats and names (required)
#   TWELVEDATA_API_KEY   price history backfill for 5D/1M charts and 1W/1M change (optional)
Import("env")
import os

KEYS = ["FINNHUB_API_KEY", "TWELVEDATA_API_KEY"]
PLACEHOLDER = "YOUR_KEY_HERE"   # the value in .env.example: counts as no key
values = {k: "" for k in KEYS}

# Lines look like KEY=value or KEY = value (quotes around the value are fine; # = comment).
path = os.path.join(env["PROJECT_DIR"], ".env")
if os.path.isfile(path):
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, value = line.split("=", 1)
            key, value = key.strip(), value.strip().strip('"').strip("'")
            if key in values and value != PLACEHOLDER:
                values[key] = value

if not values["FINNHUB_API_KEY"]:
    print("load_env.py: FINNHUB_API_KEY not found in .env; the tracker will show NO API KEY")
if not values["TWELVEDATA_API_KEY"]:
    print("load_env.py: TWELVEDATA_API_KEY not found in .env; 1W/1M and 5D/1M history disabled")

env.Append(CPPDEFINES=[(k, env.StringifyMacro(v)) for k, v in values.items()])
