# Spec 029 FR-022: a crash record names its build by the first 16 hex digits
# of the .elf's SHA-256, which esptool writes into the image's app descriptor
# (--elf-sha256-offset 0xb0, passed by pioarduino's hybrid compile). This
# checks it after every build, so a builder change can't silently break the
# match, and keeps the last few .elf files gzipped under .pio/elf/ so a board
# flashed from a local build can still be decoded (tools/decode_crash.py).
import gzip
import hashlib
import os
import shutil

Import("env")  # noqa: F821 (SCons)

DESCRIPTOR_OFFSET = 0x20  # image header 0x18 + first segment header 0x8
DESCRIPTOR_MAGIC = bytes.fromhex("3254cdab")  # ESP_APP_DESC_MAGIC_WORD, little-endian
ELF_SHA_OFFSET = 0xB0  # app_elf_sha256 at 0x90 in esp_app_desc_t
KEEP = 5


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.digest()


def keep(elf_path, build_id, project_dir):
    folder = os.path.join(project_dir, ".pio", "elf")
    os.makedirs(folder, exist_ok=True)
    target = os.path.join(folder, build_id + ".elf.gz")
    if not os.path.exists(target):
        with open(elf_path, "rb") as source, gzip.open(target, "wb", compresslevel=6) as out:
            shutil.copyfileobj(source, out)
    kept = sorted(
        (os.path.join(folder, name) for name in os.listdir(folder) if name.endswith(".elf.gz")),
        key=os.path.getmtime,
        reverse=True,
    )
    for old in kept[KEEP:]:
        os.remove(old)
    print(f"keep_elf: {os.path.relpath(target, project_dir)}")


def check_and_keep(source, target, env):  # noqa: ARG001 (SCons action signature)
    bin_path = str(target[0])
    elf_path = os.path.splitext(bin_path)[0] + ".elf"
    with open(bin_path, "rb") as handle:
        image = handle.read(ELF_SHA_OFFSET + 32)
    elf_sha = sha256_of(elf_path)
    if image[DESCRIPTOR_OFFSET : DESCRIPTOR_OFFSET + 4] != DESCRIPTOR_MAGIC:
        raise SystemExit("keep_elf: the app descriptor isn't at 0x20 of the image")
    if image[ELF_SHA_OFFSET : ELF_SHA_OFFSET + 32] != elf_sha:
        raise SystemExit("keep_elf: the image doesn't carry its .elf's SHA-256 (esptool --elf-sha256-offset 0xb0)")
    build_id = elf_sha.hex()[:16]
    print(f"keep_elf: build {build_id}")
    if not os.environ.get("CI"):
        keep(elf_path, build_id, env["PROJECT_DIR"])


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", check_and_keep)  # noqa: F821
