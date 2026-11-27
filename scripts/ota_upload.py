import csv
from pathlib import Path

Import("env")

protocol = env.subst("$UPLOAD_PROTOCOL")
if protocol == "esptool":
    # Arduino's default 0xe000 would overwrite the final sector of the existing NVS.
    partition_file = Path(env.subst("$PROJECT_DIR")) / env.GetProjectOption("board_build.partitions")
    with partition_file.open(encoding="utf-8") as partitions:
        ota_offset = next(
            (row[3].strip() for row in csv.reader(partitions)
             if row and row[0].strip() == "otadata"),
            None,
        )

    images = list(env["FLASH_EXTRA_IMAGES"])
    boot_image = next(
        ((index, image) for index, (_, image) in enumerate(images)
         if Path(env.subst(image)).name == "boot_app0.bin"),
        None,
    )
    if not ota_offset or boot_image is None:
        raise RuntimeError("Cannot locate the OTA partition and initial OTA data image.")

    index, image = boot_image
    old_offset = images[index][0]
    images[index] = (ota_offset, image)
    flags = list(env["UPLOADERFLAGS"])
    flag_index = next(
        (i for i in range(1, len(flags))
         if flags[i - 1] == old_offset and flags[i] == env.subst(image)),
        None,
    )
    if flag_index is None:
        raise RuntimeError("Cannot relocate boot_app0.bin in the USB upload command.")

    flags[flag_index - 1] = ota_offset
    env.Replace(FLASH_EXTRA_IMAGES=images, UPLOADERFLAGS=flags)
