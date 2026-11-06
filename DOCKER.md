# Nodo 3 no Docker (isolado)

    sh INICIAR-DOCKER.sh ~/Downloads/site      # liga   |   docker compose down   # desliga

**O que o conteiner trava:** roda sem root, sistema de arquivos somente leitura, sem nenhuma capability do Linux, sem
subir privilegios, no maximo 256 processos / 512 MB / 1 CPU, enxerga so a pasta do site, e a porta fica so em
127.0.0.1 (nenhum outro aparelho acessa). A chave nunca entra na imagem: fica no nodo.conf da sua pasta.

**O que ele NAO faz (para nao ter falsa seguranca):** nao criptografa o banco no disco (use FileVault/BitLocker),
nao bloqueia a internet de saida do conteiner, nao e HTTPS (para publicar use Tailscale ou Caddy; veja SEGURANCA.md)
e nao substitui backup (copie nodo-dados/backups). Conteiner compartilha o kernel do PC; nao e uma maquina virtual.
Nunca coloque o nodo.conf no Git. Testado so o codigo; o build do Docker em si nao foi rodado aqui.

**Varios sites:** rode `sh INICIAR-DOCKER.sh ~/loja` e depois `sh INICIAR-DOCKER.sh ~/blog 7072` (cada pasta vira um conteiner
chamado nodo-<pasta>, com a porta do nodo.conf ou a informada). Desligar: `sh INICIAR-DOCKER.sh --parar ~/loja`.

**Proxy no PC (Caddy) na frente do conteiner:** o Nodo enxerga o gateway do Docker, nao 127.0.0.1. No nodo.conf do site:
`proxy_confiavel=sim` e `proxies_confiaveis=172.16.0.0/12`. Sem isso o IP real do visitante nao e usado.
