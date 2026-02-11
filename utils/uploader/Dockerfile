# ESP32-C3 Docker builder that exports flashable binaries
#
# Build: docker build -t gnss-bin-export -f utils/uploader/Dockerfile .
# Run:   docker run --rm -v "$(pwd)/build:/project/build" gnss-bin-export

FROM python:3.12-slim

RUN pip install --no-cache-dir platformio

# Pre-install platform/toolchains to improve repeat build time
RUN pio pkg install -g -p "espressif32" && \
    pio pkg install -g -t "toolchain-riscv32-esp"

WORKDIR /project

# Copy project files needed for a regular build
COPY platformio.ini ./
COPY partitions.csv ./
COPY src/ src/
COPY include/ include/
COPY lib/ lib/
COPY data/ data/
COPY utils/uploader/docker-export-bins.sh /usr/local/bin/docker-export-bins.sh

ENTRYPOINT ["/usr/local/bin/docker-export-bins.sh"]
