# Pebble SDK build environment for Windows (the SDK only runs on Linux/macOS).
# Used by build.ps1; works with Podman or Docker.
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates curl git build-essential python3 python3-venv python3-pip \
        nodejs npm libsdl2-2.0-0 libglib2.0-0 libpixman-1-0 zlib1g libsndio7.0 \
        libfreetype6 xvfb xauth \
    && rm -rf /var/lib/apt/lists/*

RUN python3 -m venv /opt/pebble \
    && /opt/pebble/bin/pip install --no-cache-dir pebble-tool
# `pebble sdk install` builds the SDK's own venv with uv.
RUN /opt/pebble/bin/pip install --no-cache-dir uv
ENV PATH="/opt/pebble/bin:${PATH}"

# The .pbw is named after this folder: build/PrintPulse.pbw
WORKDIR /PrintPulse
CMD ["bash"]
