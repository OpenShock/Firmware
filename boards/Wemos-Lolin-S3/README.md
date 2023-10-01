
# Wemos Lolin S3

## Flashing
```bash
python -m esptool \
    --chip esp32-s3 \
    merge_bin -o OpenShock.S3.bin \
    --flash_mode dio \
    --flash_freq 40m \
    --flash_size 4MB \
    0x0 ./bootloader.bin \
    0x8000 ./partitions.bin \
    0x10000 ./firmware.bin \
    0x310000 ./littlefs.bin
```