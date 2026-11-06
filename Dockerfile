# Nodo 3 em conteiner isolado (compila dentro do Docker; nao precisa de nada instalado no PC)
FROM ubuntu:24.04 AS build
RUN apt-get update && apt-get install -y --no-install-recommends g++ && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY nodo.cpp ./
COPY *.hpp ./
RUN g++ -std=c++17 -O2 -pthread -fstack-protector-strong -D_FORTIFY_SOURCE=2 nodo.cpp -o /nodo

FROM ubuntu:24.04
COPY --from=build /nodo /usr/local/bin/nodo
COPY docker-entrypoint.sh /usr/local/bin/docker-entrypoint.sh
RUN chmod 0555 /usr/local/bin/nodo /usr/local/bin/docker-entrypoint.sh
USER 1000:1000
WORKDIR /site
# Dentro do conteiner o Nodo escuta em todas as interfaces; quem limita o acesso e a porta publicada (127.0.0.1).
# Vale so em memoria: o nodo.conf da sua pasta nao e alterado.
ENV NODO_ESCUTAR=0.0.0.0
ENTRYPOINT ["/usr/local/bin/docker-entrypoint.sh"]
