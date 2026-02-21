# build/Dockerfile
# Use the custom base image instead of python:3.12-slim
FROM gnss-base:latest

WORKDIR /project

# Copy dependency metadata
COPY platformio.ini ./

# Install Project Specific Libraries (Fast)
RUN --mount=type=cache,target=/root/.platformio/.cache \
    pio pkg install && \
    (pio pkg install -e full || true)

# Copy Source Files
COPY partitions.csv ./
COPY lib/ lib/
COPY src/ src/
COPY include/ include/
COPY data/ data/
COPY build/scripts/docker-export-bins.sh /usr/local/bin/docker-export-bins.sh
COPY build/scripts/bin_utils/ /usr/local/bin/bin_utils/

# Prepare Scripts
RUN sed -i 's/\r$//' /usr/local/bin/docker-export-bins.sh && \
    find /usr/local/bin/bin_utils/ -type f \( -name "*.sh" -o -name "*.ps1" -o -name "*.cmd" \) -exec sed -i 's/\r$//' {} + && \
    chmod +x /usr/local/bin/bin_utils/*.sh && \
    chmod +x /usr/local/bin/docker-export-bins.sh

ENTRYPOINT ["/usr/local/bin/docker-export-bins.sh"]