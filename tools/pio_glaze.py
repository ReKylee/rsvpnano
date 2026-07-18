Import("env")

from pathlib import Path


# PlatformIO otherwise builds Glaze's repository demo as library source.
include = Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env.subst("$PIOENV") / "glaze" / "include"
env.AppendUnique(CPPPATH=[str(include)])
