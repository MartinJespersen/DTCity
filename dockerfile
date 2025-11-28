# Build stage
FROM ubuntu:22.04 AS builder

# Install build dependencies
RUN apt-get update && apt-get install -y \
    python3 \
    build-essential \
    git \
    curl \
    zip \
	unzip \
    tar \
    cmake \
    libvulkan1 \
    libgl1-mesa-dev xorg-dev libwayland-dev libxkbcommon-dev wayland-protocols extra-cmake-modules

RUN git clone https://github.com/Microsoft/vcpkg.git /opt/vcpkg && \
    cd /opt/vcpkg && \
    ./bootstrap-vcpkg.sh

ENV VCPKG_ROOT=/opt/vcpkg
ENV PATH="${VCPKG_ROOT}:${PATH}"

# Set working directory
WORKDIR /app

# Copy project files
COPY --exclude=build . .

# Create build directory and configure
RUN cmake --preset=release-linux

# Build the project
RUN cmake --build build

# Runtime stage
FROM ubuntu:22.04 AS runtime

# Copy built application from builder
COPY --from=builder /app/build/city /app/build/city
COPY --from=builder /app/data /app/data

# Set working directory
WORKDIR /app/build

# Run the application
CMD ["/bin/bash"]