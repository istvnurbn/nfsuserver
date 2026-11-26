# The lobby server in a container. compose.yaml puts Caddy in front of it for
# the website; see "Docker" in README.md.

FROM docker.io/library/debian:bookworm-slim AS build
RUN apt-get update \
 && apt-get install -y --no-install-recommends g++ make libsqlite3-dev libcrypt-dev \
 && rm -rf /var/lib/apt/lists/*
COPY nfsuserver /src
# the dialect this code base is written in, as package.nix pins it
RUN make -C /src -j"$(nproc)" CC="g++ -std=c++98"

FROM docker.io/library/debian:bookworm-slim
RUN apt-get update \
 && apt-get install -y --no-install-recommends libsqlite3-0 libcrypt1 \
 && rm -rf /var/lib/apt/lists/* \
 && useradd --uid 10990 --no-create-home --home-dir /data nfsu \
 && mkdir /data && chown nfsu /data
COPY --from=build /src/nfsuserver /usr/local/bin/nfsuserver
# it reads nfsu.conf and news.txt from its working directory and keeps nfsu.db there
WORKDIR /data
USER nfsu
VOLUME /data
EXPOSE 10900 10901 10800 10800/udp 10980
ENTRYPOINT ["nfsuserver"]
