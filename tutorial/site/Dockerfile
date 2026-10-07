FROM debian:12-slim AS build

ARG CONAN_VERSION=2.28.1
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install --no-install-recommends --yes \
        build-essential ca-certificates cmake libssl-dev pkg-config \
        python3 python3-pip \
    && rm -rf /var/lib/apt/lists/*
RUN python3 -m pip install --break-system-packages --no-cache-dir \
    "conan==${CONAN_VERSION}"

COPY site /src/site
COPY content /src/content
COPY labs /src/labs
WORKDIR /src/site
RUN conan profile detect --force \
    && conan install . --lockfile=conan.lock --build=missing \
        --output-folder=build -s build_type=Release \
        -o 'crowcpp-crow/*:with_ssl=False' \
    && cmake -S . -B build \
        -DCMAKE_TOOLCHAIN_FILE=build/build/Release/generators/conan_toolchain.cmake \
        -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF \
    && cmake --build build --config Release --parallel

FROM debian:12-slim

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
    && apt-get install --no-install-recommends --yes \
        ca-certificates libgcc-s1 libssl3 libstdc++6 \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --create-home --uid 10001 tutorial

COPY --from=build /src/site/build/freeswitch-tutorial-site \
    /opt/freeswitch-tutorial/site/freeswitch-tutorial-site
COPY --from=build /src/site/config /opt/freeswitch-tutorial/site/config
COPY --from=build /src/site/web /opt/freeswitch-tutorial/site/web
COPY --from=build /src/site/THIRD_PARTY_NOTICES.md \
    /opt/freeswitch-tutorial/site/THIRD_PARTY_NOTICES.md
COPY --from=build /src/site/test /opt/freeswitch-tutorial/site/test
COPY --from=build /src/site/CMakeLists.txt \
    /opt/freeswitch-tutorial/site/CMakeLists.txt
COPY --from=build /src/content /opt/freeswitch-tutorial/content
COPY --from=build /src/labs /opt/freeswitch-tutorial/labs

USER tutorial
WORKDIR /opt/freeswitch-tutorial/site
EXPOSE 7009
ENTRYPOINT ["/opt/freeswitch-tutorial/site/freeswitch-tutorial-site"]
CMD ["--config", "/opt/freeswitch-tutorial/site/config/config.yaml"]
