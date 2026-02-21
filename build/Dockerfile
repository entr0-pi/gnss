# syntax=docker/dockerfile:1.7
# ESP32-C3 Docker builder that exports flashable binaries
#
# Build: docker build -t gnss-bin-export -f build/Dockerfile .
# Run:   docker run --rm -v "$(pwd)/build/bin:/project/build/bin" gnss-bin-export

FROM python:3.12-slim

WORKDIR /project

# Install tooling with pip cache mount.
RUN --mount=type=cache,target=/root/.cache/pip \
    pip install --no-cache-dir platformio esptool

# Copy dependency metadata first, before source files, to maximize cache reuse.
COPY platformio.ini ./
COPY lib/ lib/

# Pre-install PlatformIO packages (no cache mount — packages persist in image
# so pio run at container start doesn't re-download them).
RUN pio pkg install -g -p "espressif32" && \
    pio pkg install -g -t "toolchain-riscv32-esp" && \
    pio pkg install -g -t "tool-mklittlefs" && \
    (pio pkg install -e full || true)

# Copy source files after dependencies so normal code edits do not invalidate
# dependency download/install layers.
COPY partitions.csv ./
COPY src/ src/
COPY include/ include/
COPY data/ data/
COPY build/scripts/docker-export-bins.sh /usr/local/bin/docker-export-bins.sh
COPY build/scripts/bin_utils/ /usr/local/bin/bin_utils/
RUN sed -i 's/\r$//' /usr/local/bin/docker-export-bins.sh && \
    find /usr/local/bin/bin_utils/ -type f -name "*.sh" -o -name "*.ps1" -o -name "*.cmd" | xargs sed -i 's/\r$//' && \
    chmod +x /usr/local/bin/bin_utils/*.sh && \
    chmod +x /usr/local/bin/docker-export-bins.sh

ENTRYPOINT ["/usr/local/bin/docker-export-bins.sh"]
