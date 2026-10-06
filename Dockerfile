FROM debian:bookworm-slim AS build

RUN apt-get update && \
    apt-get install -y --no-install-recommends g++ cmake make && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY CMakeLists.txt .
COPY main.cpp Server.cpp Poller.cpp HttpUtil.cpp Server.hpp Poller.hpp HttpUtil.hpp ./
COPY tests ./tests

RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build

FROM debian:bookworm-slim AS runtime

RUN useradd --system --no-create-home atlas

WORKDIR /app
COPY --from=build /src/build/Atlas ./atlas
COPY public ./public

USER atlas

EXPOSE 8080

CMD ["./atlas", "8080", "10", "./public"]
