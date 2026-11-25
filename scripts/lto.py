# Link-time optimisation for this project's own code (src/ and lib/), measured
# at about -110 KB of flash (spec 030). The framework, ESP-IDF's prebuilt
# archives and the lib_deps libraries are compiled as before. None of our code
# is placed in IRAM: spec 029's crash hooks (the wrapped panic handler's
# callback and the task watchdog's user handler) run from flash with or without
# LTO, and their RTC_NOINIT memory keeps its section (checked on the .elf with
# nm and objdump for spec 030), so LTO can't put flash code where the cache may
# be off. The link line carries -fno-lto
# from the framework; this replaces it so the linker plugin optimises across
# our objects.
import os

Import("env")  # noqa: F821 (SCons)

PROJECT = env.subst("$PROJECT_DIR")  # noqa: F821
OWN = tuple(os.path.join(PROJECT, d) + os.sep for d in ("src", "lib"))


def own_code_with_lto(build_env, node):
    if node.srcnode().get_abspath().startswith(OWN):
        return build_env.Object(node, CCFLAGS=build_env["CCFLAGS"] + ["-flto=auto"])
    return node


env.AddBuildMiddleware(own_code_with_lto)  # noqa: F821


def link_with_lto(*_args, **_kwargs):
    # The framework's flags are in place only once its builder has run, so the
    # link line is edited just before the link.
    env.Replace(LINKFLAGS=[f for f in env["LINKFLAGS"] if f != "-fno-lto"] + ["-flto=auto", "-Os"])  # noqa: F821


env.AddPreAction("$BUILD_DIR/${PROGNAME}.elf", link_with_lto)  # noqa: F821
