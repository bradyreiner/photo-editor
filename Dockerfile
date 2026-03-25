FROM ubuntu:24.04 AS builder

ARG DEBIAN_FRONTEND=noninteractive
ARG BUILD_DESKTOP=OFF
ARG CMAKE_BUILD_TYPE=RelWithDebInfo
ARG RUSTUP_HOME=/opt/rustup
ARG CARGO_HOME=/opt/cargo

ENV RUSTUP_HOME=${RUSTUP_HOME}
ENV CARGO_HOME=${CARGO_HOME}
ENV PATH=${CARGO_HOME}/bin:${PATH}

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    clang \
    cmake \
    curl \
    git \
    libsqlite3-dev \
    ninja-build \
    pkg-config \
    python3 \
    && if [ "${BUILD_DESKTOP}" = "ON" ]; then apt-get install -y --no-install-recommends \
      qt6-base-dev \
      qt6-declarative-dev \
      qt6-tools-dev-tools; \
    fi \
    && rm -rf /var/lib/apt/lists/*

RUN curl https://sh.rustup.rs -sSf | sh -s -- -y --profile minimal --default-toolchain stable

WORKDIR /workspace

COPY . .

RUN cargo check --manifest-path rust/Cargo.toml --workspace

RUN cmake -S . -B build/docker -G Ninja \
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} \
    -DPHOTO_EDITOR_BUILD_DESKTOP=${BUILD_DESKTOP} \
    -DPHOTO_EDITOR_BUILD_TESTS=ON \
    -DPHOTO_EDITOR_ENABLE_RUST=ON

RUN cmake --build build/docker

RUN ctest --test-dir build/docker --output-on-failure

CMD ["/bin/bash"]
