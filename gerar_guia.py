#!/usr/bin/env python3
# Gera guia.html e guia_embutido.hpp (a pagina /guia que o proprio Nodo serve) a partir do texto abaixo.
import html
E = html.escape
def code(t): return '<pre><code>' + E(t.strip('\n')) + '</code></pre>'
def p(t): return '<p>' + t + '</p>'
def step(n, titulo, *corpo): return '<section class="passo"><h3><span>%s</span>%s</h3>%s</section>' % (n, titulo, ''.join(corpo))

S = []
S.append('<h1>Guia de integração do Nodo</h1>' + p('Como ligar o seu site ao servidor e ao banco de dados do Nodo, passo a passo. Esta página só abre no próprio computador ou na sua rede local.'))
S.append('<h2>Antes de tudo: o essencial</h2>' + step('1', 'Prepare a pasta do site',
  p('No app: <b>Início &gt; Escolher pasta do projeto</b> e escolha a pasta que tem o <code>index.html</code>. No terminal:'),
  code('nodo integrar /caminho/do/meu-site'),
  p('Isso cria na pasta o <code>nodo.conf</code> (com a sua chave de administrador), o <code>nodo-dados/</code> (o banco) e o <code>nodo.js</code>, e já coloca <code>&lt;script src="nodo.js"&gt;</code> no seu <code>index.html</code> (a cópia original fica em <code>index.html.bak</code>).')) +
  step('2', 'Ligue o servidor', p('No app, clique em <b>Ligar</b>. No terminal:'), code('cd /caminho/do/meu-site\nnodo iniciar .'), p('Abra <code>http://localhost:7070</code>. O painel para ver e editar os dados fica em <code>/admin</code> (peça a chave em <code>nodo.conf</code>).')) +
  step('3', 'Decida quem pode fazer o quê',
  p('Por padrão <b>visitantes não podem nada</b>. Você libera coleção por coleção no <code>nodo.conf</code> (ou em Configurações, no app):'),
  code('# visitantes podem LER estas coleções\nleitura_publica=produtos,mural\n# visitantes podem só CRIAR documentos nestas\nescrita_publica=contatos,mural\n# e só com estes campos (qualquer outro é recusado)\ncampos.contatos=nome:80,email:120,mensagem:1000\ncampos.mural=nome:40,texto:500\n# sites que podem usar o servidor pelo navegador (CORS); vazio = qualquer site\norigens=https://meusite.com'),
  p('Apagar e editar <b>sempre</b> exigem a chave. Reinicie o Nodo depois de mudar o arquivo.')))

S.append('<h2>Receitas prontas</h2>')
S.append(step('A', 'Formulário de contato (visitante envia, você lê no painel)',
  code('escrita_publica=contatos\ncampos.contatos=nome:80,email:120,mensagem:1000\nmax_docs.contatos=5000'),
  p('No seu HTML:'),
  code('''<form id="f">
  <input name="nome" placeholder="Nome" required maxlength="80">
  <input name="email" type="email" placeholder="E-mail" required maxlength="120">
  <textarea name="mensagem" placeholder="Mensagem" required maxlength="1000"></textarea>
  <button>Enviar</button>
</form>
<p id="r"></p>

<script src="nodo.js"></script>
<script>
document.getElementById('f').addEventListener('submit', async function (e) {
  e.preventDefault();
  var dados = Object.fromEntries(new FormData(e.target));
  try {
    await Nodo.colecao('contatos').criar(dados);
    document.getElementById('r').textContent = 'Enviado! Obrigado.';
    e.target.reset();
  } catch (err) {
    document.getElementById('r').textContent = 'Não foi possível enviar: ' + err.message;
  }
});
</script>'''),
  p('As mensagens aparecem no painel <code>/admin</code>, na coleção <code>contatos</code>.')))
S.append(step('B', 'Lista pública (produtos, notícias, cardápio)',
  code('leitura_publica=produtos'),
  p('Cadastre os itens pelo painel <code>/admin</code> (botão "Adicionar documento") ou pelo terminal, do seu computador:'),
  code('curl -X POST http://localhost:7070/api/produtos \\\n  -H "X-Nodo-Key: SUA_CHAVE" \\\n  -d \'{"nome":"Camiseta","preco":59.9}\''),
  p('Mostre na página <b>sempre com <code>textContent</code></b> (ou <code>Nodo.esc()</code>), nunca com <code>innerHTML</code> puro:'),
  code('''<ul id="lista"></ul>
<script src="nodo.js"></script>
<script>
Nodo.colecao('produtos').listar({ limite: 50 }).then(function (itens) {
  var ul = document.getElementById('lista');
  itens.forEach(function (p) {
    var li = document.createElement('li');
    li.textContent = p.nome + ' - R$ ' + p.preco;
    ul.append(li);
  });
});
</script>''')))
S.append(step('C', 'Mural / comentários (todos leem e escrevem)',
  code('leitura_publica=mural\nescrita_publica=mural\ncampos.mural=nome:40,texto:500'),
  code('''Nodo.colecao('mural').criar({ nome: nome, texto: texto });      // enviar
Nodo.colecao('mural').listar({ limite: 100 });                  // ler
// Se precisar montar HTML com texto de visitantes, escape antes:
el.innerHTML = '<b>' + Nodo.esc(msg.nome) + '</b> ' + Nodo.esc(msg.texto);''')))
S.append(step('D', 'Site em outra hospedagem (Netlify, WordPress, outro domínio)',
  p('Deixe o Nodo no seu servidor, com HTTPS (veja "Publicar com segurança"), e no site externo aponte o script para ele:'),
  code('<script src="https://api.seusite.com/nodo.js"></script>\n<script>\n  // o endereço do servidor é deduzido do src acima; se precisar, force:\n  // Nodo.configurar({ servidor: "https://api.seusite.com" });\n</script>'),
  p('No WordPress use um bloco "HTML personalizado". Em React/Vue, carregue o <code>nodo.js</code> no <code>index.html</code> e use <code>window.Nodo</code>, ou chame a API com <code>fetch</code> (tabela abaixo).')))
S.append(step('E', 'Filtros, paginação e atualização',
  code('''Nodo.colecao('produtos').listar({ categoria: 'roupas', limite: 20, pular: 40 });
Nodo.colecao('produtos').obter('ID');
// Com a chave (somente em ferramentas SUAS, nunca no site):
Nodo.configurar({ chave: 'SUA_CHAVE' });
Nodo.colecao('produtos').atualizar('ID', { nome: 'Novo', preco: 10 });
Nodo.colecao('produtos').remover('ID');''')))

S.append('<h2>Publicar o site: Tailscale + Git + Vercel (aba Publicar do app)</h2>' +
  p('Como funciona: o <b>site</b> (HTML, CSS, JS) fica na <b>Vercel</b>, com o seu domínio .com. O <b>servidor e o banco</b> continuam no seu computador, no Nodo, e ficam acessíveis por um endereço HTTPS do <b>Tailscale</b>. O <b>Git</b> liga as duas pontas: cada push publica o site e atualiza o servidor. O computador com o Nodo precisa ficar ligado e conectado.') +
  step('1', 'Tailscale: endereço HTTPS para o servidor',
    p('Aba <b>Publicar &gt; 1</b>: <b>Baixar Tailscale</b>, instale, volte e clique em <b>Entrar</b> (abre o login no navegador). Depois clique em <b>Publicar na internet (HTTPS)</b>.'),
    p('Na primeira vez o Tailscale pode pedir, no painel dele (login.tailscale.com), para habilitar <b>HTTPS</b> (em DNS) e o <b>Funnel</b> (em Access controls). O app mostra o endereço, algo como <code>https://seu-pc.tailnet.ts.net</code>. Para uso só seu, escolha <b>Só nos meus dispositivos</b>.'),
    p('O Nodo é reiniciado sozinho com <code>proxy_confiavel=sim</code>. Pelo Funnel a chave de administrador funciona (a conexão é HTTPS). Terminal: <code>nodo tailscale publicar</code>.')) +
  step('2', 'Git: vincular o repositório',
    p('Crie um repositório vazio no GitHub (ou GitLab). Em <b>Publicar &gt; 2</b> cole o endereço (<code>https://github.com/voce/meu-site.git</code>) e clique <b>Vincular repositório</b>, depois <b>Enviar (commit + push)</b>. O app cria o <code>.gitignore</code>, o <code>.vercelignore</code> e o <code>README.md</code>, e <b>nunca envia</b> o <code>nodo.conf</code> (chave) nem o <code>nodo-dados</code>.'),
    p('O app usa o login do Git que já existe no computador (GitHub Desktop, <code>gh auth login</code> ou chave SSH). Se pedir login, entre por uma dessas formas e tente de novo. Terminal: <code>nodo git vincular URL</code> e <code>nodo git enviar "mensagem"</code>.')) +
  step('3', 'Vercel: publicar o site e comprar o domínio .com',
    p('Crie um token em vercel.com/account/tokens (botão <b>Criar token</b>) e cole em <b>Publicar &gt; 3</b>. O token fica só na memória: some quando você fecha o app.'),
    p('<b>Criar projeto ligado ao Git</b> faz a Vercel publicar o site a cada push (se pedir, instale o app da Vercel no seu GitHub em vercel.com/new). Depois, em <b>Ligar o site ao servidor</b>, clique <b>Apontar o site para o servidor (Tailscale)</b> e envie para o Git: assim o site na Vercel usa o seu servidor.'),
    p('<b>Domínio:</b> digite <code>meusite.com</code>, clique <b>Buscar domínio</b> (mostra disponibilidade e preço), <b>Comprar domínio…</b> (pede confirmação; é <b>cobrado na hora</b> no cartão da sua conta Vercel) e <b>Vincular domínio ao projeto</b>. Se a compra por aqui falhar, use <b>Abrir Vercel (domínios)</b> e compre pelo site.')) +
  step('4', 'Deploy automático do servidor (opcional)',
    p('Duas formas de manter o servidor atualizado a cada push: marcar <b>Atualizar sozinho a cada 5 minutos</b> no app, ou chamar o webhook do Nodo a partir do GitHub Actions (guarde a chave em um <i>Secret</i> chamado <code>NODO_KEY</code>, nunca no código):'),
    code('''# .github/workflows/deploy.yml
name: Deploy no Nodo
on: { push: { branches: [main] } }
jobs:
  deploy:
    runs-on: ubuntu-latest
    steps:
      - run: curl -fsS -X POST -H "X-Nodo-Key: ${{ secrets.NODO_KEY }}" https://seu-pc.tailnet.ts.net/api/_deploy''')))

S.append('<h2>Publicar com segurança</h2>')
S.append('<ol class="chk">' +
  '<li><b>Nunca coloque a chave no código do site.</b> Qualquer visitante vê o código. A chave é só sua (painel, terminal, scripts no seu PC).</li>' +
  '<li><b>Use HTTPS.</b> Instale o Caddy e use <code>reverse_proxy 127.0.0.1:7070</code>; no <code>nodo.conf</code> ponha <code>escutar=127.0.0.1</code> e <code>proxy_confiavel=sim</code>.</li>' +
  '<li><b>Libere só as portas 80 e 443</b> no firewall. Nunca a 7070 nem a 7071.</li>' +
  '<li><b>Limite os campos</b> de escrita pública com <code>campos.&lt;coleção&gt;=nome:80,...</code> (obrigatório: sem isso o Nodo recusa os envios) e o tamanho da coleção com <code>max_docs.&lt;coleção&gt;</code>.</li>' +
  '<li><b>Diga quais sites podem usar o servidor</b> com <code>origens=https://seusite.com</code> (CORS). Um site que não está na lista é barrado no servidor, não só no navegador.</li>' +
  '<li><b>Entre no painel sem expor a chave:</b> o <code>/admin</code> troca a chave por um acesso temporário (<code>sessao_horas</code>); em scripts use <code>Nodo.login(chave)</code>.</li>' +
  '<li><b>Escape tudo</b> que veio de visitantes ao mostrar na página (<code>textContent</code> ou <code>Nodo.esc</code>).</li>' +
  '<li><b>Rode a auditoria</b> antes de publicar: <code>nodo verificar</code>. Ela aponta o que ainda está inseguro.</li>' +
  '<li><b>Backups:</b> o Nodo salva um por dia em <code>nodo-dados/backups</code> (guarda 7). Faça um agora com <code>nodo backup</code>; restaure com <code>nodo restaurar arquivo.json</code>. Copie para fora do servidor de vez em quando.</li>' +
  '<li><b>Trocou de ideia sobre a chave?</b> <code>nodo chave</code> gera outra (reinicie o Nodo depois).</li></ol>')

S.append('<h2>Referência da API</h2><table><tr><th>Ação</th><th>Requisição</th><th>Quem pode</th></tr>' +
  '<tr><td>Listar</td><td><code>GET /api/colecao?campo=valor&amp;limite=50&amp;pular=0</code></td><td>chave, ou público se em <code>leitura_publica</code></td></tr>' +
  '<tr><td>Obter um</td><td><code>GET /api/colecao/ID</code></td><td>idem</td></tr>' +
  '<tr><td>Criar</td><td><code>POST /api/colecao</code> (corpo JSON)</td><td>chave, ou público se em <code>escrita_publica</code></td></tr>' +
  '<tr><td>Atualizar</td><td><code>PUT /api/colecao/ID</code></td><td>só chave</td></tr>' +
  '<tr><td>Apagar</td><td><code>DELETE /api/colecao/ID</code></td><td>só chave</td></tr>' +
  '<tr><td>Login</td><td><code>POST /api/_login</code> com <code>{"chave":"..."}</code> devolve um token; use <code>Authorization: Bearer TOKEN</code></td><td>chave (o token vale só para o IP que fez login)</td></tr>' +
  '<tr><td>Uso do servidor</td><td><code>GET /api/_metricas</code></td><td>só chave</td></tr>' +
  '<tr><td>Saúde</td><td><code>GET /api/_saude</code></td><td>público</td></tr></table>' +
  p('A chave vai no cabeçalho <code>X-Nodo-Key</code>. Nomes de coleção: letras, números, <code>_</code> e <code>-</code>. Limite de 1 MB por requisição; visitantes: 64 KB por documento. Todo documento tem <code>_id</code> e <code>_t</code> (instante da última alteração, em ms), criados pelo servidor. Todo erro é JSON: <code>{"erro":"texto","codigo":"campo_grande","status":400}</code>.'))

S.append('<h2>Erros no site: o NodoErro</h2>' + p('Toda falha do <code>nodo.js</code> chega como <code>NodoErro</code>, com <code>.message</code> (texto para o visitante), <code>.status</code> (HTTP; 0 = não houve resposta), <code>.codigo</code> (estável, para o seu código testar) e <code>.rede</code> (true = não chegou ao servidor). Nomes de coleção e ids malformados são recusados no próprio navegador, sem ir à rede; leituras tentam de novo sozinhas em falha de rede; respostas que não são JSON (portal de WiFi, página de erro do proxy) viram erro e nunca dado.') + code('''Nodo.colecao('mural').criar(dados).catch(function (e) {
  if (e.rede) msg.textContent = 'Servidor fora do ar. Tente de novo em instantes.';
  else if (e.codigo === 'muitas_requisicoes') msg.textContent = 'Muitos envios. Aguarde um minuto.';
  else msg.textContent = e.message;   // ex.: o campo nome passou do limite de 40 caracteres
});''') + '<table><tr><th>codigo</th><th>Quando</th></tr><tr><td>rede / tempo_esgotado</td><td>não chegou ao servidor / demorou mais que <code>Nodo.timeout</code></td></tr><tr><td>resposta_invalida</td><td>o servidor (ou algo no caminho) respondeu algo que não é JSON válido</td></tr><tr><td>colecao_invalida / id_invalido / filtro_invalido / dados_invalidos</td><td>valor malformado, detectado antes de enviar</td></tr><tr><td>campo_nao_permitido / campo_grande / json_invalido</td><td>o envio não obedece a <code>campos.&lt;coleção&gt;</code></td></tr><tr><td>campos_obrigatorios</td><td>coleção com escrita pública sem <code>campos.&lt;coleção&gt;</code> no nodo.conf</td></tr><tr><td>origem_negada</td><td>o site não está em <code>origens</code></td></tr><tr><td>muitas_requisicoes / colecao_cheia / documento_grande</td><td>limites de uso (429 / 413)</td></tr></table>')

S.append('<h2>Monitor de uso e prompt para IA</h2>' + p('No app: abas <b>Monitor de uso</b> (requisições por minuto, erros, chaves erradas, origens recusadas, conexões, latência, CPU, memória, disco, replicação, coleções mais usadas e IPs que mais pedem) e <b>Prompt para IA</b> (você descreve o site e o Nodo monta um prompt com as regras e limites deste servidor para colar em qualquer IA). O painel <code>/admin</code> tem as mesmas abas. No terminal:') + code('nodo monitor --seguir\nnodo prompt "site de uma barbearia com agenda" --stack html'))

S.append('<h2>Se algo der errado</h2><table><tr><th>Sintoma</th><th>Causa e solução</th></tr>' +
  '<tr><td>401 "leitura exige chave"</td><td>A coleção não está em <code>leitura_publica</code>. Edite o <code>nodo.conf</code> e reinicie.</td></tr>' +
  '<tr><td>401 "chave só é aceita por HTTPS"</td><td>Você usou a chave por HTTP vindo da internet. Use HTTPS (Caddy) ou acesse pela rede local.</td></tr>' +
  '<tr><td>403 "campos_obrigatorios"</td><td>A coleção está em <code>escrita_publica</code> mas falta <code>campos.&lt;coleção&gt;</code> no <code>nodo.conf</code>.</td></tr>' +
  '<tr><td>403 "origem_negada"</td><td>O site que fez o pedido não está em <code>origens</code>. Acrescente o endereço exato (com https://).</td></tr>' +
  '<tr><td>400 "campo não permitido"</td><td>O formulário enviou um campo que não está em <code>campos.&lt;coleção&gt;</code>.</td></tr>' +
  '<tr><td>413</td><td>Documento maior que 64 KB (visitante) ou coleção cheia.</td></tr>' +
  '<tr><td>429</td><td>Muitos envios do mesmo IP. Aguarde um minuto (ajuste <code>limite_escrita</code>).</td></tr>' +
  '<tr><td>Aviso amarelo "Abra o app Nodo"</td><td>O servidor está desligado. Clique em Ligar.</td></tr>' +
  '<tr><td>Git pede login / "Authentication failed"</td><td>Entre no Git pelo computador (GitHub Desktop, <code>gh auth login</code> ou chave SSH) e tente de novo.</td></tr>' +
  '<tr><td>Funnel não liga</td><td>No painel do Tailscale habilite HTTPS (DNS) e o Funnel (Access controls).</td></tr>' +
  '<tr><td>Site na Vercel sem dados</td><td>Rode "Apontar o site para o servidor", envie ao Git e confirme que o computador com o Nodo está ligado.</td></tr>' +
  '<tr><td>Não abre a porta</td><td>Outra coisa usa a 7070. Troque <code>porta=</code> no <code>nodo.conf</code>.</td></tr></table>')

CSS = ':root{color-scheme:light dark;--bg:#fff;--fg:#14121c;--mut:#5b5870;--ac:#5C16EA;--cd:#f3f0ff;--bd:#d9d2f5}@media(prefers-color-scheme:dark){:root{--bg:#120d22;--fg:#f1eeff;--mut:#b9b2d6;--ac:#b79bff;--cd:#1d1636;--bd:#3a2f63}}*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:16px/1.6 system-ui,sans-serif}main{max-width:860px;margin:0 auto;padding:32px 20px 80px}h1{font-size:34px;letter-spacing:-1px;color:var(--ac)}h2{margin-top:44px;border-bottom:2px solid var(--bd);padding-bottom:6px}h3{display:flex;gap:12px;align-items:center;margin:0 0 10px}h3 span{background:var(--ac);color:var(--bg);border-radius:50%;width:30px;height:30px;display:inline-flex;align-items:center;justify-content:center;font-size:15px;flex:none}.passo{background:var(--cd);border:1px solid var(--bd);border-radius:14px;padding:18px 20px;margin:16px 0}pre{background:var(--bg);border:1px solid var(--bd);border-radius:10px;padding:12px 14px;overflow-x:auto;font-size:13.5px}code{font-family:ui-monospace,Menlo,Consolas,monospace;font-size:.92em}p code,li code,td code{background:var(--cd);padding:1px 5px;border-radius:5px}table{border-collapse:collapse;width:100%;font-size:15px;display:block;overflow-x:auto}td,th{border:1px solid var(--bd);padding:8px 10px;text-align:left;vertical-align:top}th{background:var(--cd)}.chk li{margin:8px 0}'
doc = '<!doctype html><html lang="pt-BR"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Guia de integração - Nodo</title><style>' + CSS + '</style></head><body><main>' + ''.join(S) + '</main></body></html>'
open('guia.html', 'w').write(doc)
assert ')HTML"' not in doc
open('guia_embutido.hpp', 'w').write('#pragma once\n// Gerado por gerar_guia.py - nao edite a mao.\nstatic const char* GUIA_HTML = R"HTML(' + doc + ')HTML";\n')
print(len(doc), 'bytes')
