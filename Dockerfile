# Use Ubuntu 22.04 as a stable, modern base
FROM ubuntu:22.04

# Avoid interactive prompts during apt install
ENV DEBIAN_FRONTEND=noninteractive

# Install modern C++ build toolchains, CMake, Git, and Python
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    python3 \
    gcc-12 \
    g++-12 \
    && rm -rf /var/lib/apt/lists/*

# Use modern gcc-12 which has robust C++23 support
ENV CC=gcc-12
ENV CXX=g++-12

WORKDIR /app

# Copy the entire workspace to the container
COPY . .

# Adjust the CMAKE required version to be compatible with Ubuntu's CMake (3.22)
# This avoids downloading hefty third-party cmake packages
RUN sed -i 's/VERSION 4.3/VERSION 3.20/g' CMakeLists.txt

# Create build directory, configure with Release optimization, and compile all tests and binaries
RUN mkdir build && \
    cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release .. && \
    make -j$(nproc)

# Put our freshly-compiled 'uwu' binary on the system PATH so it can be invoked globally
ENV PATH="/app/build:${PATH}"

# Define the entrypoint to run every test suite in one terminal pass
ENTRYPOINT ["bash", "test/run_all_tests.sh"]
