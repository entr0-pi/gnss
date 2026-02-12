# ESP32-C3 Docker builder that exports flashable binaries
#
# Build: docker build -t gnss-bin-export -f build/Dockerfile .
# Run:   docker run --rm -v "$(pwd)/build:/project/build" gnss-bin-export

FROM python:3.12-slim

RUN pip install --no-cache-dir platformio

# Pre-install platform/toolchains to improve repeat build time
RUN pio pkg install -g -p "espressif32" && \
    pio pkg install -g -t "toolchain-riscv32-esp"

WORKDIR /project

# Copy dependency metadata first to maximize layer caching.
# This avoids re-downloading packages when only source files change.
COPY platformio.ini ./

# Pre-fetch library dependencies so they are cached in this layer
RUN pio pkg install -e full || true

# Copy the rest of project files needed for firmware build/export
COPY partitions.csv ./
COPY src/ src/
COPY include/ include/
COPY lib/ lib/
COPY data/ data/
COPY build/docker-export-bins.sh /usr/local/bin/docker-export-bins.sh
RUN sed -i 's/\r$//' /usr/local/bin/docker-export-bins.sh && \
    chmod +x /usr/local/bin/docker-export-bins.sh

ENTRYPOINT ["/usr/local/bin/docker-export-bins.sh"]
