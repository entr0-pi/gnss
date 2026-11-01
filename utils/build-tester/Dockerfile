# syntax=docker/dockerfile:1.7
# ESP32-C3 Build Flag Tester
# Reproducible PlatformIO build environment for testing flag combinations.
# Requires the gnss-base image (see build/README.md).
#
# Base:   docker build -t gnss-base -f build/Dockerfile.base .
# Build:  docker build -t gnss-build-tester -f utils/build-tester/Dockerfile .
# Run:    docker run --rm gnss-build-tester
# Args:   docker run --rm gnss-build-tester --tests 0,1,5
# List:   docker run --rm gnss-build-tester --list
# Log:    docker run --rm -v "${PWD}/results:/results" gnss-build-tester

# Toolchains, framework and libraries are already baked into gnss-base
FROM gnss-base

WORKDIR /project

# Copy dependency metadata first for better cache reuse.
COPY utils/build-tester/platformio.ini.test platformio.ini

# Resolve env dependencies (no-op when gnss-base already matches).
# Only the download cache is mounted, so installed packages stay in the image.
RUN --mount=type=cache,target=/root/.platformio/.cache \
    pio pkg install -e lolin_c3_mini

# Copy source and tester files after dependency layers.
COPY partitions.csv ./
COPY lib/ lib/
COPY include/ include/
COPY src/ src/
COPY data/ data/

# Copy the build-tester tooling
COPY utils/build-tester/ utils/build-tester/

# Write the log outside the tooling dir so it can be bind-mounted.
ENV BUILD_TESTER_LOG=/results/build_test_results.log
RUN mkdir -p /results

ENTRYPOINT ["python", "utils/build-tester/build_Tester.py"]
