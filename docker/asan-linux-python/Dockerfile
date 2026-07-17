FROM ubuntu:24.04

RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      bash \
      ca-certificates \
      clang \
      gcc \
      libclang-rt-18-dev \
      make \
      binutils \
      libc6-dev \
      cmake \
      python3 \
      python3-dev \
      python3-pip \
      python3-venv \
      tar \
      jq \
    && rm -rf /var/lib/apt/lists/*

# This disposable validation image installs the Python test/build dependencies
# system-wide. No binding-side requirements file currently pins cffi/pytest.
RUN pip3 install --break-system-packages --no-cache-dir cffi pytest

WORKDIR /work
