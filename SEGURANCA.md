# Nodo - guia de seguranca (v3.0.0)

## Abertura do projeto pelo arquivo de configuracao

No app com janela, selecione a pasta do projeto ou uma pasta-pai. Se `nodo.conf` estiver em uma subpasta, o Nodo tenta localiza-lo e pede confirmacao antes de abrir o projeto. A chave e lida apenas localmente pelo aplicativo; nao e enviada para sites, nao e inserida no HTML e nao e exibida na tela. Pastas `.git`, `node_modules`, `nodo-dados`, `.venv` e `build` sao ignoradas durante a busca.

## Para publicar na internet (obrigatorio)
1. **Use HTTPS.** O Nodo fala HTTP. Coloque na frente um proxy HTTPS: o Tailscale Funnel (aba Publicar do app) ou o Caddy, que emite o certificado sozinho:
   ```
   seusite.com {
       reverse_proxy 127.0.0.1:7070
   }
   ```
2. No `nodo.conf`: `escutar=127.0.0.1` (o padrao desde a 1.5) e `proxy_confiavel=sim`. Assim so o proxy alcanca o Nodo.
3. Libere no firewall somente as portas 80 e 443. **Nunca** exponha 7070 nem 7071.
4. Nunca publique `nodo.conf` (contem a chave de administrador). O Nodo ja se recusa a servi-lo.
5. Troque a chave se ela vazar (app: Rede > Gerar nova chave, ou `nodo chave`).
6. Rode `nodo verificar` e corrija os alertas antes de publicar. O Nodo tambem confere a configuracao **ao ligar** e mostra os avisos no Registro.

## O que o Nodo faz por voce
- **Escuta so neste PC por padrao** (`escutar=127.0.0.1`). Abrir para a rede (`0.0.0.0`) e uma escolha sua, com aviso no Registro. Se `admin_remoto=sim` for combinado com uma escuta fora do PC, o Nodo **recusa ligar**.
- Chave de 192 bits gerada com aleatorio do sistema operacional; `nodo.conf` e `nodo-dados/` so legiveis pelo dono (Mac/Linux).
- **Login por token (1.5):** o painel `/admin` e o `Nodo.login()` trocam a chave por um token de 256 bits, valido por `sessao_horas` (8 por padrao) e **so para o IP que fez login**. O token nunca e guardado em disco (so o hash, em memoria) e e revogado no logout. A chave deixa de ficar no navegador.
- A chave (e o token) **so sao aceitos por HTTPS ou rede local**. De fora, por HTTP puro, sao recusados (`admin_remoto=nao`).
- 10 credenciais erradas em 15 minutos bloqueiam o IP por 15 minutos.
- **CORS por lista (1.5):** `origens=https://seusite.com`. Pedidos de outros sites sao **recusados no servidor** (403 `origem_negada`), inclusive escritas, nao so escondidos pelo navegador. Rotas internas (`/api/_*`, `/admin`) nunca usam `*`. Com `origens` vazio, qualquer site pode usar so as colecoes publicas - o `nodo verificar` avisa.
- **Escrita publica com campos obrigatorios (1.5):** colecao em `escrita_publica` sem `campos.<colecao>` e recusada (403). Cada campo tem limite de caracteres (`campos.mural=nome:40,texto:500`), sem `_id`/`_t` forjados, sem objetos aninhados, sem campo repetido. Limite de documentos por colecao: `max_docs.<colecao>` (conferido de forma atomica, mesmo sob 200 envios simultaneos).
- **JSON estrito (1.5):** so entra no banco JSON valido (RFC 8259). Antes, `{"a":tru}` passava e quebrava a listagem inteira para todos os visitantes.
- **Requisicoes malformadas (1.5):** ids e nomes fora do padrao, bytes de controle, `%00`, Content-Length repetido/negativo/nao numerico, Transfer-Encoding, metodos e versoes desconhecidos, corpo incompleto e cabecalho Origin invalido sao recusados com erro em JSON. O caminho da URL e decodificado uma unica vez (antes havia dupla decodificacao).
- Visitante: 30 envios/min por IP, 64 KB por documento. Leitura publica: 300/min por IP. Resposta 429 traz `Retry-After`.
- Arquivos do proprio Nodo, `.bak`, `.log`, `.env`, `.pem`, arquivos ocultos e links simbolicos que saem da pasta nunca sao servidos.
- Protecao contra conexoes lentas (slowloris): 10 s para enviar o cabecalho; no maximo 32 conexoes por IP e 256 no total.
- Busca automatica de PCs na rede local vem **desligada** (`descoberta=nao`) e, quando ligada, so aceita PCs de rede privada.
- Cabecalhos de seguranca (nosniff, frame, CSP no painel, HSTS sob HTTPS, Permissions-Policy), respostas da API sem cache.
- **Monitor de uso (1.5):** requisicoes, erros, chaves erradas, origens recusadas, bloqueios, latencia, CPU, memoria, disco e replicacao, no app, no `/admin` e em `nodo monitor`. Serve para notar ataques e abusos cedo. Os numeros ficam so em memoria.

## Replicacao entre PCs (1.5)
- Conflitos: cada documento tem `_t` (ms da ultima alteracao); **vence a alteracao mais recente** em todos os PCs. Relogios diferentes importam: mantenha os PCs com hora automatica (um `_t` mais de 1 dia no futuro e limitado).
- Documentos apagados deixam um registro por 30 dias, para uma copia antiga nao os ressuscitar.
- Uma fila unica envia as alteracoes; se um PC cai, o Nodo para de tentar nele e **ressincroniza sozinho** (a cada `sync_segundos`, 30 por padrao) quando ele volta. Os PCs conectados sao lembrados entre reinicios (`nodo-dados/peers.txt`).
- Linhas recebidas de outro PC sao revalidadas (id, colecao, JSON estrito), como qualquer outra escrita.
- **Limite importante:** entre os PCs o trafego e HTTP e leva a chave. Conecte so pela rede local ou pelo Tailscale (criptografado) e so maquinas suas.

## Publicacao pelo app
- **Tailscale:** o Funnel da HTTPS publico sem abrir portas no roteador; o Nodo passa a confiar nos cabecalhos do proxy local (`proxy_confiavel=sim`). Redes do Tailscale (100.64.0.0/10) sao criptografadas e contam como rede segura.
- **Git:** `nodo.conf` e `nodo-dados/` sao colocados no `.gitignore` e no `.vercelignore` automaticamente; se algum deles ja estiver versionado, o envio o remove do Git. Se a chave chegou a ser enviada em algum commit antigo, **troque-a** (`nodo chave`).
- **Deploy remoto:** `POST /api/_deploy` roda `git pull --ff-only` e exige a chave. Guarde-a como Secret (GitHub Actions), nunca no codigo.
- **Vercel:** o token fica so na memoria do app. A compra de dominio mostra o preco real, pede confirmacao e so compra o dominio que acabou de ser consultado.
- Tudo que vai para comandos (endereco do repositorio, branch, dominio, token) e validado por lista de caracteres permitidos.
- O computador com o Nodo precisa ficar ligado para o site na Vercel enxergar o servidor.

## Testes automaticos
`python3 testes/rodar.py build/nodo` (123 verificacoes: ids malformados, JSON invalido, campos e limites, token, CORS, requisicoes HTTP defeituosas, concorrencia, persistencia, monitor, prompt, replicacao com conflitos e queda) e `node testes/sdk.mjs build/nodo` (44 verificacoes do `nodo.js`: rede fora, timeout, HTML no lugar de JSON, 503 passageiro...).

## O que ainda NAO esta coberto (leia antes de publicar)
- **Nao houve auditoria independente.** Os testes acima sao do proprio projeto e cobrem o que foi pensado; nao substituem uma revisao externa ou um teste de intrusao.
- O Nodo **nao faz HTTPS sozinho**. Sem proxy HTTPS na frente, qualquer pessoa no caminho da rede le o trafego.
- Sem HTTPS entre os PCs da replicacao (veja acima) e sem contas de usuarios finais: so existe o administrador (chave) e o visitante anonimo.
- Os limites por IP dependem de enxergar o IP real: so use `proxy_confiavel=sim` atras de um proxy seu, com `escutar=127.0.0.1`.
- No Windows nao ha `chmod`: proteja a pasta do projeto com as permissoes normais do seu usuario.
- O site que usa o Nodo deve **escapar** o texto que mostra (`textContent` em vez de `innerHTML`, ou `Nodo.esc`), senao HTML enviado por visitantes executa na pagina de outros.
- A interface com janela (Qt) e o monitor no Windows/macOS foram escritos mas **nao compilados no ambiente de desenvolvimento**: o CI (Actions > "Build Nodo") compila os tres sistemas; confira a primeira build.

## nodo.regras (v1.8.0)
O nodo.regras so aceita regras publicas e nunca e servido pelo site. Chave, porta, escutar e proxy_confiavel so valem no nodo.conf. Mantenha o nodo.conf fora do Git.

## v3.0.0: varios projetos, Host e proxies
- Cada projeto e um processo separado, com chave e porta proprias. Uma chave nao abre outro projeto (testado).
- Acesso direto exige Host confiavel (localhost, IP, nomes de uma palavra, *.local, *.ts.net, origens, dominio, hosts_permitidos).
- Atras de Caddy/nginx: proxy_confiavel=sim. Se o proxy nao for o loopback (Docker), liste-o em proxies_confiaveis.
- Cabecalhos repetidos e conflitantes, LF solto e byte nulo sao recusados com 400.
- Descoberta automatica na LAN continua com a fraqueza descrita em ATUALIZACAO-v3.0.0.txt: deixe desligada.
