# ESP32-C3 Build Flag Tester
# Reproducible PlatformIO build environment for testing flag combinations.
#
# Build:  docker build -t gnss-build-tester -f utils/build-tester/Dockerfile .
# Run:    docker run --rm gnss-build-tester
# Args:   docker run --rm gnss-build-tester --tests 0,1,5
# List:   docker run --rm gnss-build-tester --list

FROM python:3.12-slim

# Install PlatformIO
RUN pip install --no-cache-dir platformio

# Pre-install ESP32 platform and RISC-V toolchain (heavy layer, cached)
RUN pio pkg install -g -p "espressif32" && \
    pio pkg install -g -t "toolchain-riscv32-esp"

WORKDIR /project

# Copy dependency-relevant files first (maximises layer caching)
COPY partitions.csv ./
COPY lib/ lib/
COPY include/ include/
COPY src/ src/
COPY data/ data/

# Use the test ini as the project's platformio.ini
COPY utils/build-tester/platformio.ini.test platformio.ini

# Pre-fetch library dependencies so they are cached in this layer
RUN pio pkg install -e lolin_c3_mini || true

# Copy the build-tester tooling
COPY utils/build-tester/ utils/build-tester/

ENTRYPOINT ["python", "utils/build-tester/build_Tester.py"]
