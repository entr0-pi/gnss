# syntax=docker/dockerfile:1.7
# ESP32-C3 Build Flag Tester
# Reproducible PlatformIO build environment for testing flag combinations.
#
# Build:  docker build -t gnss-build-tester -f utils/build-tester/Dockerfile .
# Run:    docker run --rm gnss-build-tester
# Args:   docker run --rm gnss-build-tester --tests 0,1,5
# List:   docker run --rm gnss-build-tester --list

FROM python:3.12-slim

WORKDIR /project

# Install PlatformIO with pip cache mount.
RUN --mount=type=cache,target=/root/.cache/pip \
    pip install --no-cache-dir platformio

# Copy dependency metadata first for better cache reuse.
COPY utils/build-tester/platformio.ini.test platformio.ini
COPY lib/ lib/

# Pre-install heavy PlatformIO packages and env dependencies with cache mount.
RUN --mount=type=cache,target=/root/.platformio \
    pio pkg install -g -p "espressif32" && \
    pio pkg install -g -t "toolchain-riscv32-esp" && \
    pio pkg install -e lolin_c3_mini || true

# Copy source and tester files after dependency layers.
COPY partitions.csv ./
COPY include/ include/
COPY src/ src/
COPY data/ data/

# Copy the build-tester tooling
COPY utils/build-tester/ utils/build-tester/

ENTRYPOINT ["python", "utils/build-tester/build_Tester.py"]
