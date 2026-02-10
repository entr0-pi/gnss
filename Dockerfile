# syntax=docker/dockerfile:1.7

FROM python:3.11-slim

ARG PIO_VERSION=6.1.18
ARG USERNAME=builder
ARG UID=1000
ARG GID=1000

ENV DEBIAN_FRONTEND=noninteractive \
    PIO_HOME_DIR=/home/${USERNAME}/.platformio \
    PLATFORMIO_CORE_DIR=/home/${USERNAME}/.platformio \
    PATH=/home/${USERNAME}/.local/bin:${PATH}

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        git \
        build-essential \
        curl \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd --gid ${GID} ${USERNAME} \
    && useradd --uid ${UID} --gid ${GID} --create-home --shell /bin/bash ${USERNAME}

USER ${USERNAME}
WORKDIR /workspace

RUN --mount=type=cache,target=/home/${USERNAME}/.cache/pip \
    pip install --no-cache-dir --user platformio==${PIO_VERSION}

# Prime dependency/toolchain cache for standard full build and the build tester environment.
COPY --chown=${USERNAME}:${USERNAME} platformio.ini ./platformio.ini
COPY --chown=${USERNAME}:${USERNAME} partitions.csv ./partitions.csv
COPY --chown=${USERNAME}:${USERNAME} utils/build-tester/platformio.ini.test ./utils/build-tester/platformio.ini.test
RUN --mount=type=cache,target=/home/${USERNAME}/.platformio \
    pio pkg install -e full \
    && pio pkg install --project-conf utils/build-tester/platformio.ini.test -e lolin_c3_mini

# Copy source last so code changes do not invalidate toolchain/package cache layers.
COPY --chown=${USERNAME}:${USERNAME} . .

CMD ["pio", "run", "-e", "full"]
