# Container fuer gzh-rendezvous. Bauen im Wurzelverzeichnis des Repos:
#   docker build -f tools/rendezvous/Dockerfile -t gzh-rendezvous .
#   docker run -d --name rendezvous -p 8090:8090/udp --restart unless-stopped gzh-rendezvous
FROM debian:bookworm-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends g++ && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY Code/Core/EngineCore/Engine /src/Code/Core/EngineCore/Engine
COPY Code/Core/EngineCore/Engine /src/Code/Core/EngineCore/Engine
COPY Code/Core/Monocypher /src/Code/Core/Monocypher
COPY Code/Tools/Server/Rendezvous /src/Code/Tools/Server/Rendezvous
COPY tools/rendezvous /src/tools/rendezvous
RUN tools/rendezvous/build_linux.sh /src/gzh-rendezvous && /src/gzh-rendezvous --selftest

FROM debian:bookworm-slim
COPY --from=build /src/gzh-rendezvous /usr/local/bin/gzh-rendezvous
COPY tools/rendezvous/rendezvous.conf /etc/gzh-rendezvous.conf
USER nobody
EXPOSE 8090/udp
ENTRYPOINT ["/usr/local/bin/gzh-rendezvous", "--config", "/etc/gzh-rendezvous.conf"]
