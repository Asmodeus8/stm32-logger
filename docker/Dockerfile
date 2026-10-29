FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
      gcc-arm-none-eabi libnewlib-arm-none-eabi cmake ninja-build git ca-certificates gcc libc6-dev make python3 \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
