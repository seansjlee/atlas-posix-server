# ---- Build stage ----
FROM debian:bookworm-slim AS build

RUN apt-get update && \
    apt-get install -y --no-install-recommends g++ cmake make && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY CMakeLists.txt .
COPY main.cpp Server.cpp Server.hpp ./

RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build

# ---- Runtime stage ----
FROM debian:bookworm-slim AS runtime

# Run as a non-root user
RUN useradd --system --no-create-home atlas

WORKDIR /app
COPY --from=build /src/build/Atlas ./atlas
COPY public ./public

USER atlas

EXPOSE 8080

# port, workers, doc_root — matches main.cpp's positional args
CMD ["./atlas", "8080", "10", "./public"]
