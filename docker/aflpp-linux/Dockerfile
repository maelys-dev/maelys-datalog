FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y \
    build-essential \
    clang \
    llvm \
    lld \
    make \
    git \
    ca-certificates \
    afl++ \
    libclang-rt-18-dev \
    && rm -rf /var/lib/apt/lists/*

RUN command -v afl-clang-fast && \
    command -v afl-fuzz && \
    afl-clang-fast --version

WORKDIR /work
