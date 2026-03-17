# Build stage
FROM ubuntu:24.04 AS builder
ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    python3 \
    git \
    curl \
    zip \
    unzip \
    tar \
    libvulkan1 \
    libgl1-mesa-dev \
    xorg-dev \
    libwayland-dev \
    libxkbcommon-dev \
    wayland-protocols \
    extra-cmake-modules && \
    rm -rf /var/lib/apt/lists/*

RUN git clone --depth 1 https://github.com/Microsoft/vcpkg.git /opt/vcpkg && \
    cd /opt/vcpkg && \
    ./bootstrap-vcpkg.sh

ENV VCPKG_ROOT=/opt/vcpkg
ENV PATH="${VCPKG_ROOT}:${PATH}"
ENV VCPKG_BINARY_SOURCES="clear;files,/opt/vcpkg-binary-cache,readwrite"

# Set working directory
WORKDIR /app

# Copy the dependency manifest inputs first so normal source edits do not
# invalidate the vcpkg cache priming layer.
COPY CMakeLists.txt /app
COPY CMakePresets.json /app
COPY vcpkg.json /app
COPY vcpkg-configuration.json /app
COPY triplets /app/triplets
COPY vcpkg-ports /app/vcpkg-ports

RUN --mount=type=cache,target=/opt/vcpkg/downloads \
    --mount=type=cache,target=/opt/vcpkg-binary-cache \
    vcpkg install \
    --triplet x64-linux-release \
    --x-install-root=/app/vcpkg_installed \
    --clean-after-build

# Copy project files into the build context after dependency cache priming.
COPY . /app

# Create build directory and configure
RUN --mount=type=cache,target=/opt/vcpkg/downloads \
    --mount=type=cache,target=/opt/vcpkg-binary-cache \
    cmake --preset release-linux -DVCPKG_INSTALLED_DIR=/app/vcpkg_installed

# Build the project
RUN cmake --build build/release

# Runtime stage
FROM ubuntu:24.04 AS runtime

RUN apt-get update && apt-get install -y \
    libvulkan1 && \
    rm -rf /var/lib/apt/lists/*

# Copy built application from builder
RUN mkdir -p /app && mkdir -p /app/data
COPY --from=builder /app/build/release/city /app
COPY --from=builder /app/data /app/data

# Set working directory
WORKDIR /app

# Run the application
CMD ["./city"]
