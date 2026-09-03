FROM ubuntu:24.04

RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      bash \
      ca-certificates \
      clang \
      libclang-rt-18-dev \
      make \
      binutils \
      libc6-dev \
      tar \
      jq \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work
