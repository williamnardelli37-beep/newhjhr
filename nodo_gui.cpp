// Nodo - app com janela. Liga/desliga o servidor, gerencia o banco de dados, conecta PCs e configura tudo.
// Mesmo nucleo do comando "nodo" (nodo_core.hpp), sem precisar de terminal.
#include <QApplication>
#include <QCheckBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QGridLayout>
#include <QLocale>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QProgressDialog>
#include <QProcess>
#include <QTemporaryDir>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QSysInfo>
#include <QSplashScreen>
#include <QSplitter>
#include <QStackedWidget>
#include <QSystemTrayIcon>
#include <QTableWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <QGuiApplication>
#include <QMap>
#include <QSignalBlocker>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QStyleHints>
#include <QStyleFactory>
#include <thread>
#include <functional>

#include "nodo_publicar.hpp"
#include "nodo_projetos.hpp"

static QString Q(const string& s) { return QString::fromUtf8(s.c_str(), (int)s.size()); }
static string S(const QString& q) { return q.toUtf8().toStdString(); }
static QString T(const char* s) { return QString::fromUtf8(s); }

static QIcon appIcon(bool on) {
  QPixmap p(128, 128);
  p.fill(Qt::transparent);
  QPainter g(&p);
  g.setRenderHint(QPainter::Antialiasing);
  g.setBrush(QColor("#171717"));
  g.setPen(QPen(QColor("#171717"), 2));
  g.drawRoundedRect(8, 8, 112, 112, 24, 24);
  g.setPen(Qt::white);
  QFont f;
  f.setBold(true);
  f.setPixelSize(72);
  g.setFont(f);
  g.drawText(QRect(0, 4, 128, 128), Qt::AlignCenter, "N");
  return QIcon(p);
}

// JSON em varias linhas para editar com conforto
static string pretty(const string& j) {
  string o;
  int d = 0;
  bool in = false;
  auto ind = [&] { return string(max(d, 0) * 2, ' '); };
  for (size_t i = 0; i < j.size(); i++) {
    char c = j[i];
    if (in) {
      o += c;
      if (c == '\\' && i + 1 < j.size()) o += j[++i];
      else if (c == '"') in = false;
      continue;
    }
    switch (c) {
      case '"': in = true; o += c; break;
      case '{': case '[':
        if (i + 1 < j.size() && (j[i + 1] == '}' || j[i + 1] == ']')) { o += c; o += j[++i]; }
        else { o += c; d++; o += "\n" + ind(); }
        break;
      case '}': case ']': d--; o += "\n" + ind() + c; break;
      case ',': o += ",\n" + ind(); break;
      case ':': o += ": "; break;
      default: o += c;
    }
  }
  return o;
}
static string compacto(const string& doc) {  // mostra o documento sem o _id
  size_t c = doc.find(',');
  return c == string::npos ? "{}" : "{" + doc.substr(c + 1);
}
static bool editarJson(QWidget* pai, const QString& titulo, string& texto) {
  QDialog dlg(pai);
  dlg.setWindowTitle(titulo);
  dlg.resize(560, 420);
  auto* v = new QVBoxLayout(&dlg);
  v->addWidget(new QLabel(T("Escreva o documento em JSON (um objeto com campos):")));
  auto* ed = new QPlainTextEdit(Q(texto));
  QFont mono("Menlo");
  mono.setStyleHint(QFont::Monospace);
  ed->setFont(mono);
  v->addWidget(ed);
  auto* box = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
  box->button(QDialogButtonBox::Save)->setText(T("Salvar"));
  box->button(QDialogButtonBox::Cancel)->setText(T("Cancelar"));
  v->addWidget(box);
  QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
  QObject::connect(box, &QDialogButtonBox::accepted, &dlg, [&] {
    vector<pair<string, string>> p;
    if (!jpairs(minify(S(ed->toPlainText())), p)) {
      QMessageBox::warning(&dlg, T("JSON inválido"), T("O texto precisa ser um objeto JSON válido, por exemplo:\n{\"nome\": \"Maria\", \"idade\": 30}"));
      return;
    }
    dlg.accept();
  });
  if (dlg.exec() != QDialog::Accepted) return false;
  texto = S(ed->toPlainText());
  return true;
}

static bool temaEscuro() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
  return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
#else
  return QApplication::palette().color(QPalette::Window).lightness() < 128;
#endif
}
// Liquid glass: superficies translucidas com borda clara, sobre um fundo de luzes suaves (veja Vidro).
static QString qss(bool) {
  return QString::fromUtf8(R"QSS(
QWidget { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", "Ubuntu", sans-serif; font-size: 13px; color: #171717; }
QMainWindow, QWidget#vidro { background: #ffffff; }
QDialog, QMessageBox, QInputDialog, QProgressDialog, QMenu { background: #ffffff; color: #171717; }
QToolTip { background: #171717; color: #ffffff; border: 1px solid #171717; padding: 4px 8px; }
QWidget#side { background: #fafafa; border-right: 1px solid #e5e5e5; }
QListWidget#nav { background: transparent; border: 0; font-size: 14px; outline: 0; padding: 8px 6px; }
QListWidget#nav::item { padding: 11px 14px; border-radius: 8px; margin: 2px 6px; border: 1px solid transparent; color: #404040; }
QListWidget#nav::item:hover { background: #f5f5f5; }
QListWidget#nav::item:selected { background: #eeeeee; border: 1px solid #e5e5e5; color: #111111; font-weight: 600; }
QFrame#card { background: #ffffff; border: 1px solid #e5e5e5; border-radius: 12px; }
QPushButton#primary { background: #171717; color: #ffffff; border: 1px solid #171717; border-radius: 8px; padding: 10px 20px; font-weight: 600; }
QPushButton#primary:hover { background: #333333; }
QPushButton#primary:pressed { background: #000000; }
QPushButton#primary:disabled { background: #a3a3a3; border-color: transparent; }
QPushButton#danger { background: #404040; color: white; border: 1px solid #404040; border-radius: 8px; padding: 10px 20px; }
QPushButton#flat { padding: 8px 14px; border: 1px solid #d4d4d4; border-radius: 8px; background: #ffffff; }
QPushButton#flat:hover { background: #f5f5f5; }
QPushButton#flat:disabled { color: #a3a3a3; background: #fafafa; border-color: #e5e5e5; }
QLineEdit, QPlainTextEdit, QComboBox, QSpinBox, QTableWidget, QListWidget { border: 1px solid #d4d4d4; border-radius: 8px; padding: 5px; background: #ffffff; selection-background-color: #e5e5e5; selection-color: #171717; }
QListWidget::item { border-radius: 6px; padding: 4px; }
QListWidget::item:selected { background: #eeeeee; color: #171717; }
QTableWidget { gridline-color: #e5e5e5; }
QHeaderView::section { background: #fafafa; border: 0; border-bottom: 1px solid #e5e5e5; padding: 6px; color: #737373; }
QComboBox QAbstractItemView { background: #ffffff; color: #171717; border: 1px solid #d4d4d4; }
QMenu { border: 1px solid #e5e5e5; border-radius: 8px; padding: 4px; }
QMenu::item { padding: 6px 18px; border-radius: 6px; }
QMenu::item:selected { background: #eeeeee; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: #d4d4d4; border-radius: 4px; min-height: 28px; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:horizontal { background: #d4d4d4; border-radius: 4px; min-width: 28px; }
QLabel#versionBadge { color: #737373; font-size: 11px; padding: 4px 10px; background: #fafafa; border: 1px solid #e5e5e5; border-radius: 8px; }
)QSS");
}
// Fundo da janela: gradiente e tres luzes suaves. Os cartoes translucidos por cima ganham o aspecto de vidro fosco.
struct Vidro : QWidget {
  bool escuro = false;
  explicit Vidro(QWidget* pai = nullptr) : QWidget(pai) { setObjectName("vidro"); }
  void paintEvent(QPaintEvent*) override { QPainter g(this); g.fillRect(rect(), QColor("#ffffff")); }
};
// Programa de linha de comando que cuida dos outros projetos (fica ao lado do app; no Mac, dentro do .app)
static string cliPathApp() {
  QString d = QCoreApplication::applicationDirPath();
  for (const QString& n : {QString("nodo-cli"),
#ifdef _WIN32
       QString("nodo.exe")
#else
       QString("nodo")
#endif
  }) {
    QString c = d + "/" + n;
    if (QFileInfo(c).isFile()) return S(QDir::toNativeSeparators(c));
  }
  return S(QStandardPaths::findExecutable("nodo"));
}

// Grafico de barras simples: requisicoes por minuto na ultima hora (parte vermelha = erros)
struct Grafico : QWidget {
  vector<unsigned> req, err;
  Grafico() { setMinimumHeight(130); setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed); }
  void defineDados(const vector<unsigned>& r, const vector<unsigned>& e) { req = r; err = e; update(); }
  void paintEvent(QPaintEvent*) override {
    QPainter g(this);
    g.setRenderHint(QPainter::Antialiasing);
    g.fillRect(rect(), palette().base());
    g.setPen(palette().color(QPalette::Mid));
    g.drawRect(rect().adjusted(0, 0, -1, -1));
    if (req.empty()) return;
    unsigned mx = 1;
    for (auto v : req) mx = max(mx, v);
    double w = (double)(width() - 20) / req.size(), alt = height() - 28;
    for (size_t i = 0; i < req.size(); i++) {
      double h = req[i] * alt / mx, x = 10 + i * w;
      g.fillRect(QRectF(x + 1, height() - 14 - h, max(1.0, w - 2), h), QColor("#737373"));
      if (i < err.size() && err[i]) { double he = err[i] * alt / mx; g.fillRect(QRectF(x + 1, height() - 14 - he, max(1.0, w - 2), he), QColor("#404040")); }
    }
    g.setPen(palette().color(QPalette::Text));
    QFont f = g.font();
    f.setPointSize(max(6, f.pointSize() - 2));
    g.setFont(f);
    g.drawText(QRect(10, 1, width() - 20, 14), Qt::AlignRight, QString("pico: %1 req/min").arg(mx));
    g.drawText(QRect(10, height() - 14, 200, 14), Qt::AlignLeft, QString("-60 min"));
    g.drawText(QRect(width() - 210, height() - 14, 200, 14), Qt::AlignRight, QString("agora"));
  }
};

struct Janela : QMainWindow {
  Vidro* vidro = nullptr;
  QListWidget* pjLista = nullptr;
  QLabel* pjInfo = nullptr;
  QPushButton *pjBLigar = nullptr, *pjBDesl = nullptr, *pjBSite = nullptr, *pjBPainel = nullptr, *pjBUsar = nullptr, *pjBPorta = nullptr, *pjBRem = nullptr;
  vector<proj::Item> pjItens;
  QString pjSig;
  bool pjOcupado = false;
  QListWidget* nav;
  QStackedWidget* pages;
  QLabel* navStatus;
  // inicio
  QLabel *dot, *stTitulo, *stSub;
  QPushButton *btnLigar, *btnPasta, *btnAbrirSite, *btnAbrirPainel, *btnCopiarEnd, *btnCopiarCod;
  QListWidget* enderecos;
  QPlainTextEdit* snippet;
  QCheckBox* chkAuto;
  string sigEnd;
  // banco
  QListWidget* colList;
  QTableWidget* tbl;
  QLineEdit* busca;
  QPushButton *bNovoDoc, *bEditar, *bApagar, *bNovaCol, *bExp, *bImp;
  string colAtual, sigBanco;
  set<string> pend;
  // rede
  QLabel *redeInfo, *redeCount;
  QListWidget* peersW;
  QLineEdit* chaveEd;
  QPushButton* btnMostrar;
  // config
  QSpinBox* spPorta;
  QCheckBox* chkRede;
  QCheckBox *chkFecharSegundoPlano = nullptr, *chkConfirmarSaida = nullptr;
  QLineEdit *edLeit, *edEsc, *edOrigens;
  QPlainTextEdit* edCampos;
  // monitor
  vector<QLabel*> monVals;
  Grafico* monGraf = nullptr;
  QTableWidget *monCols = nullptr, *monIps = nullptr;
  QLabel* monMeta = nullptr;
  QLabel *cfgPasta, *cfgOk;
  // registro
  QPlainTextEdit* logW;
  string sigLog;
  // estado
  QString projeto;
  bool projetoOk = false, sair = false;
  QSystemTrayIcon* tray = nullptr;
  QAction* actLigar = nullptr;

  // ---- pequenos construtores de widgets
  QFrame* card() { auto* f = new QFrame; f->setObjectName("card"); return f; }
  QLabel* h1(const QString& t) {
    auto* l = new QLabel(t);
    QFont f = l->font();
    f.setPointSize(f.pointSize() + 9);
    f.setBold(true);
    l->setFont(f);
    return l;
  }
  QLabel* bold(const QString& t) { auto* l = new QLabel(t); QFont f = l->font(); f.setBold(true); l->setFont(f); return l; }
  QLabel* muted(const QString& t) { auto* l = new QLabel(t); l->setWordWrap(true); l->setStyleSheet("color: gray;"); return l; }
  QPushButton* primary(const QString& t) { auto* b = new QPushButton(t); b->setObjectName("primary"); b->setCursor(Qt::PointingHandCursor); return b; }
  QPushButton* flat(const QString& t) { auto* b = new QPushButton(t); b->setObjectName("flat"); b->setCursor(Qt::PointingHandCursor); return b; }
  QWidget* pagina(QVBoxLayout*& v) {
    auto* w = new QWidget;
    v = new QVBoxLayout(w);
    v->setContentsMargins(28, 24, 28, 24);
    v->setSpacing(14);
    return w;
  }
  void copiar(const QString& s) { QApplication::clipboard()->setText(s); }

  // ================================================================ construcao da janela
  Janela() {
    setWindowTitle("NODO");
    resize(1120, 740);
    setMinimumSize(900, 620);
    vidro = new Vidro;
    vidro->escuro = false;
    auto* central = vidro;
    auto* hl = new QHBoxLayout(central);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(0);
    auto* side = new QWidget;
    side->setObjectName("side");
    side->setFixedWidth(220);
    auto* sv = new QVBoxLayout(side);
    sv->setContentsMargins(0, 18, 0, 14);
    auto* logo = new QLabel(T("N\nNODO"));
    QFont lf = logo->font();
    lf.setPointSize(lf.pointSize() + 9);
    lf.setBold(true);
    logo->setFont(lf);
    logo->setContentsMargins(18, 8, 0, 14);
    sv->addWidget(logo);
    auto* vers = new QLabel(T("  DESKTOP")); vers->setObjectName("versionBadge"); vers->setContentsMargins(18, 3, 8, 3); sv->addWidget(vers);
    nav = new QListWidget;
    nav->setObjectName("nav");
    for (auto n : {"Início", "Projetos", "Banco de dados", "Rede", "Publicar", "Configurações", "Registro", "Monitor de uso", "Diagnóstico e suporte"}) nav->addItem(T(n));
    sv->addWidget(nav, 1);
    navStatus = new QLabel;
    navStatus->setContentsMargins(16, 0, 0, 0);
    sv->addWidget(navStatus);
    hl->addWidget(side);
    auto* sep = new QFrame;
    sep->setFrameShape(QFrame::VLine);
    sep->setFrameShadow(QFrame::Sunken);
    hl->addWidget(sep);
    pages = new QStackedWidget;
    pages->addWidget(paginaInicio());
    pages->addWidget(paginaProjetos());
    pages->addWidget(paginaBanco());
    pages->addWidget(paginaRede());
    pages->addWidget(paginaPublicar());
    pages->addWidget(paginaConfig());
    pages->addWidget(paginaLog());
    pages->addWidget(paginaMonitor());
    pages->addWidget(paginaDiagnostico());
    hl->addWidget(pages, 1);
    setCentralWidget(central);
    connect(nav, &QListWidget::currentRowChanged, this, [this](int i) {
      pages->setCurrentIndex(i);
      if (i == 1) atualizarProjetos();
      if (i == 2) atualizarBanco(true);
      if (i == 4) carregarPublicar();
      if (i == 7) atualizarMonitor();
      atualizarTudo();
    });
    nav->setCurrentRow(0);

    if (QSystemTrayIcon::isSystemTrayAvailable()) {
      tray = new QSystemTrayIcon(appIcon(false), this);
      auto* m = new QMenu(this);
      auto* abrir = m->addAction(T("Abrir Nodo"));
      connect(abrir, &QAction::triggered, this, [this] { mostrar(); });
      actLigar = m->addAction(T("Ligar servidor"));
      connect(actLigar, &QAction::triggered, this, [this] { alternar(); });
      m->addSeparator();
      auto* sai = m->addAction(T("Sair (desliga o servidor)"));
      connect(sai, &QAction::triggered, this, [this] {
        if (QSettings().value("preferencias/confirmarSaida", false).toBool() &&
            QMessageBox::question(this, T("Confirmar saída"), T("Deseja sair do Nodo e desligar o servidor?"),
              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
        sair = true; close();
      });
      tray->setContextMenu(m);
      connect(tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
        if (r == QSystemTrayIcon::Trigger || r == QSystemTrayIcon::DoubleClick) mostrar();
      });
      tray->show();
    }
    auto* t = new QTimer(this);
    connect(t, &QTimer::timeout, this, [this] { atualizarTudo(); });
    t->start(1000);
    atualizarTudo();
  }
  void mostrar() { show(); raise(); activateWindow(); }
  void closeEvent(QCloseEvent* e) override {
    QSettings prefs;
    const bool manterSegundoPlano = prefs.value("preferencias/fecharSegundoPlano", false).toBool();
    const bool confirmarSaida = prefs.value("preferencias/confirmarSaida", false).toBool();
    if (!sair && manterSegundoPlano && tray && tray->isVisible()) {
      hide();
      if (rodando && !avisouBandeja) {
        avisouBandeja = true;
        tray->showMessage("Nodo", T("O servidor continua ligado em segundo plano. Use o ícone da barra de menus para abrir ou sair."), QSystemTrayIcon::Information, 5000);
      }
      e->ignore();
      return;
    }
    if (!sair && confirmarSaida) {
      auto escolha = QMessageBox::question(this, T("Confirmar saída"),
        T("Deseja realmente sair do Nodo e desligar o servidor?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
      if (escolha != QMessageBox::Yes) { e->ignore(); return; }
    }
    stopServer();
    e->accept();
    qApp->quit();
  }
  bool avisouBandeja = false;

  // ================================================================ pagina: Publicar (Tailscale, Git, Vercel)
  QPlainTextEdit* pubLog = nullptr;
  QLabel *tsEstadoL = nullptr, *tsUrlL = nullptr, *gitEstadoL = nullptr;
  QLineEdit *edRepo = nullptr, *edBranch = nullptr, *edToken = nullptr, *edTeam = nullptr, *edProj = nullptr, *edDominio = nullptr;
  QCheckBox* chkAutoPull = nullptr;
  QTimer* autoPullT = nullptr;
  bool ocupado = false;
  string ultDominio, ultPreco;  // ultimo dominio consultado e preco (a compra so vale para ele)
  string ultTsUrl;

  void plog(const QString& t) { if (pubLog) pubLog->appendPlainText(t); }
  // roda fn em outra thread (um por vez) e mostra o resultado no quadro "Resultado"
  void tarefa(const QString& titulo, std::function<string()> fn, std::function<void(const string&)> depois = nullptr) {
    if (!exigeProjeto()) return;
    if (ocupado) { plog(T("Aguarde: ainda há uma tarefa em andamento.")); return; }
    ocupado = true;
    plog(T("▶ ") + titulo);
    std::thread([this, fn, depois] {
      string r = fn();
      QMetaObject::invokeMethod(this, [this, r, depois] {
        ocupado = false;
        plog(Q(r) + "\n");
        if (depois) depois(r);
        atualizarPublicar();
      }, Qt::QueuedConnection);
    }).detach();
  }
  void reiniciarServidor() {
    if (!rodando) return;
    stopServer();
    string e = startServer();
    plog(e.empty() ? T("Servidor reiniciado com a nova configuração.") : Q(e));
  }
  QFrame* secao(const QString& titulo, const QString& desc, QVBoxLayout*& lay) {
    auto* c = card();
    lay = new QVBoxLayout(c);
    lay->setContentsMargins(20, 16, 20, 16);
    lay->setSpacing(8);
    lay->addWidget(bold(titulo));
    lay->addWidget(muted(desc));
    return c;
  }
  QHBoxLayout* linha(std::initializer_list<QWidget*> ws) {
    auto* h = new QHBoxLayout;
    for (auto* w : ws) h->addWidget(w);
    h->addStretch();
    return h;
  }
  void carregarPublicar() {
    if (!projetoOk) return;
    edRepo->setText(Q(cfg.gitRemoto));
    edBranch->setText(Q(cfg.gitBranch));
    edTeam->setText(Q(cfg.vercelTeam));
    edDominio->setText(Q(cfg.dominio));
    string nome = pub::nomePasta();
    for (auto& ch : nome) ch = (char)tolower((unsigned char)ch);
    edProj->setText(Q(cfg.vercelProjeto.empty() ? nome : cfg.vercelProjeto));
    atualizarPublicar();
  }
  void atualizarPublicar() {
    if (!projetoOk || !gitEstadoL) return;
    gitEstadoL->setText(cfg.gitRemoto.empty() ? T("Nenhum repositório vinculado ainda.") : T("Vinculado a ") + Q(cfg.gitRemoto) + T(" (branch ") + Q(cfg.gitBranch) + ")");
    std::thread([this] {
      auto e = pub::tsEstado();
      QMetaObject::invokeMethod(this, [this, e] {
        if (!e.instalado) { tsEstadoL->setText(T("O Tailscale não está instalado neste computador.")); tsUrlL->setText(""); ultTsUrl.clear(); }
        else if (!e.logado) { tsEstadoL->setText(T("Tailscale instalado, mas sem login (estado: ") + Q(e.estado.empty() ? string("desconhecido") : e.estado) + ")"); tsUrlL->setText(""); ultTsUrl.clear(); }
        else {
          tsEstadoL->setText(e.funnel ? T("Conectado. Seu servidor está PÚBLICO na internet (HTTPS).") : T("Conectado. Ainda não publicado na internet."));
          tsUrlL->setText(Q(e.url));
          ultTsUrl = e.url;
        }
      }, Qt::QueuedConnection);
    }).detach();
  }
  QWidget* paginaPublicar() {
    QVBoxLayout *v, *l;
    auto* w = pagina(v);
    v->addWidget(h1(T("Publicar")));
    v->addWidget(muted(T("Coloque o seu site no ar em três etapas: servidor com HTTPS (Tailscale), código no Git e site na Vercel com domínio .com. Os botões fazem a configuração por você.")));

    // ---- 1. Tailscale
    v->addWidget(secao(T("1 · Tailscale: HTTPS e acesso de qualquer lugar"), T("Dá ao seu Nodo um endereço seguro (https://…ts.net) sem abrir portas no roteador. É o que deixa o servidor acessível pelo site na internet."), l));
    tsEstadoL = new QLabel(T("Verificando…"));
    tsUrlL = new QLabel;
    tsUrlL->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->addWidget(tsEstadoL);
    l->addWidget(tsUrlL);
    auto* bBaixar = flat(T("Baixar Tailscale"));
    auto* bEntrar = flat(T("Entrar"));
    auto* bPub = primary(T("Publicar na internet (HTTPS)"));
    auto* bPriv = flat(T("Só nos meus dispositivos"));
    auto* bDesl = flat(T("Desligar"));
    auto* bCopiaUrl = flat(T("Copiar endereço"));
    l->addLayout(linha({bBaixar, bEntrar, bPub}));
    l->addLayout(linha({bPriv, bDesl, bCopiaUrl}));
    connect(bBaixar, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl("https://tailscale.com/download")); });
    connect(bEntrar, &QPushButton::clicked, this, [this] {
      if (!exigeProjeto()) return;
      std::thread([this] {
        string ts = pub::tsExe();
        if (ts.empty()) { QMetaObject::invokeMethod(this, [this] { plog(T("Tailscale não encontrado. Use o botão Baixar Tailscale.")); }, Qt::QueuedConnection); return; }
        pub::rodarLinhas(ts + " up", [this](const string& ln) {
          QMetaObject::invokeMethod(this, [this, ln] {
            plog(Q(ln));
            size_t p = ln.find("https://");
            if (p != string::npos) { string u = ln.substr(p); u = u.substr(0, u.find_first_of(" \t\r\n")); QDesktopServices::openUrl(QUrl(Q(u))); }
          }, Qt::QueuedConnection);
        });
        QMetaObject::invokeMethod(this, [this] { atualizarPublicar(); }, Qt::QueuedConnection);
      }).detach();
      plog(T("▶ Abrindo o login do Tailscale no navegador…"));
    });
    connect(bPub, &QPushButton::clicked, this, [this] { tarefa(T("Publicando o servidor na internet com HTTPS…"), [] { return pub::tsPublicar(true); }, [this](const string&) { reiniciarServidor(); }); });
    connect(bPriv, &QPushButton::clicked, this, [this] { tarefa(T("Compartilhando só com os seus dispositivos…"), [] { return pub::tsPublicar(false); }, [this](const string&) { reiniciarServidor(); }); });
    connect(bDesl, &QPushButton::clicked, this, [this] { tarefa(T("Desligando a publicação…"), [] { return pub::tsDesligar(); }); });
    connect(bCopiaUrl, &QPushButton::clicked, this, [this] { if (!ultTsUrl.empty()) copiar(Q(ultTsUrl)); });

    // ---- 2. Git
    QVBoxLayout* g;
    v->addWidget(secao(T("2 · Git: código e deploy"), T("Vincule a pasta ao seu repositório (GitHub, GitLab…). O app cria o .gitignore e o README, e nunca envia a chave nem o banco de dados. Usa o login do Git que já está no seu computador."), g));
    edRepo = new QLineEdit;
    edRepo->setPlaceholderText("https://github.com/usuario/meu-site.git");
    edBranch = new QLineEdit("main");
    edBranch->setMaximumWidth(120);
    gitEstadoL = new QLabel;
    g->addWidget(edRepo);
    g->addLayout(linha({new QLabel(T("Branch:")), edBranch}));
    g->addWidget(gitEstadoL);
    auto* bVinc = primary(T("Vincular repositório"));
    auto* bEnv = flat(T("Enviar (commit + push)"));
    auto* bAtu = flat(T("Atualizar (pull)"));
    auto* bCopDep = flat(T("Copiar comando de deploy remoto"));
    chkAutoPull = new QCheckBox(T("Atualizar o site sozinho a cada 5 minutos (deploy automático)"));
    g->addLayout(linha({bVinc, bEnv, bAtu}));
    g->addWidget(chkAutoPull);
    g->addLayout(linha({bCopDep}));
    connect(bVinc, &QPushButton::clicked, this, [this] {
      string url = S(edRepo->text().trimmed()), br = S(edBranch->text().trimmed());
      tarefa(T("Vinculando ao repositório…"), [url, br] { return pub::gitVincular(url, br); });
    });
    connect(bEnv, &QPushButton::clicked, this, [this] {
      bool ok = false;
      QString m = QInputDialog::getText(this, T("Enviar para o Git"), T("Descreva a mudança:"), QLineEdit::Normal, T("Atualização pelo Nodo"), &ok);
      if (!ok) return;
      string msg = S(m);
      tarefa(T("Enviando para o repositório…"), [msg] { return pub::gitEnviar(msg); });
    });
    connect(bAtu, &QPushButton::clicked, this, [this] { tarefa(T("Atualizando a partir do Git…"), [] { return pub::gitAtualizar(); }); });
    connect(bCopDep, &QPushButton::clicked, this, [this] {
      QString base = ultTsUrl.empty() ? T("https://SEU-SERVIDOR") : Q(ultTsUrl);
      copiar("curl -X POST -H \"X-Nodo-Key: $NODO_KEY\" " + base + "/api/_deploy");
      plog(T("Copiado. Use em um passo do GitHub Actions guardando a chave em um Secret (nunca no código): atualiza o servidor a cada push."));
    });
    autoPullT = new QTimer(this);
    autoPullT->setInterval(5 * 60 * 1000);
    connect(autoPullT, &QTimer::timeout, this, [this] {
      if (!projetoOk || ocupado) return;
      std::thread([this] {
        string r = pub::gitAtualizar();
        if (r.find("Already up to date") == string::npos && r.find("up-to-date") == string::npos)
          QMetaObject::invokeMethod(this, [this, r] { plog(T("▶ Deploy automático: ") + Q(r) + "\n"); }, Qt::QueuedConnection);
      }).detach();
    });
    connect(chkAutoPull, &QCheckBox::toggled, this, [this](bool on) { if (on) autoPullT->start(); else autoPullT->stop(); });

    // ---- 3. Vercel
    QVBoxLayout* vl;
    v->addWidget(secao(T("3 · Vercel: site e domínio .com"), T("A Vercel publica o site a cada push e vende o domínio. O token fica só na memória e some quando você fecha o app (crie em vercel.com/account/tokens). A compra é cobrada na hora no cartão da sua conta Vercel."), vl));
    edToken = new QLineEdit;
    edToken->setEchoMode(QLineEdit::Password);
    edToken->setPlaceholderText(T("Token da Vercel"));
    edTeam = new QLineEdit;
    edTeam->setPlaceholderText(T("Team ID (só se usar equipe; senão deixe vazio)"));
    edProj = new QLineEdit;
    edProj->setPlaceholderText(T("nome-do-projeto"));
    edDominio = new QLineEdit;
    edDominio->setPlaceholderText("meusite.com");
    vl->addWidget(edToken);
    vl->addWidget(edTeam);
    vl->addWidget(edProj);
    vl->addWidget(edDominio);
    auto* bTok = flat(T("Criar token"));
    auto* bProj = primary(T("Criar projeto ligado ao Git"));
    auto* bBusca = flat(T("Buscar domínio"));
    auto* bComp = flat(T("Comprar domínio…"));
    auto* bLiga = flat(T("Vincular domínio ao projeto"));
    auto* bWeb = flat(T("Abrir Vercel (domínios)"));
    vl->addLayout(linha({bTok, bProj}));
    vl->addLayout(linha({bBusca, bComp, bLiga, bWeb}));
    auto prepara = [this]() -> string {  // le o token e o team na thread da interface
      string t = S(edToken->text().trimmed()), tm = S(edTeam->text().trimmed());
      cfg.vercelTeam = pub::charsOk(tm, 100, "_-") ? tm : "";
      return t;
    };
    connect(bTok, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl("https://vercel.com/account/tokens")); });
    connect(bWeb, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl("https://vercel.com/domains")); });
    connect(bProj, &QPushButton::clicked, this, [this, prepara] {
      string tk = prepara(), nome = S(edProj->text().trimmed());
      tarefa(T("Criando o projeto na Vercel…"), [tk, nome] { return pub::vercelCriarProjeto(tk, nome); });
    });
    connect(bBusca, &QPushButton::clicked, this, [this, prepara] {
      string tk = prepara(), dom = S(edDominio->text().trimmed());
      tarefa(T("Consultando o domínio…"), [this, tk, dom] {
        auto d = pub::dominioBuscar(tk, dom);
        if (!d.ok) return d.msg;
        if (!d.disponivel) return string(dom + " não está disponível.");
        string preco = d.preco;
        QMetaObject::invokeMethod(this, [this, dom, preco] { ultDominio = dom; ultPreco = preco; }, Qt::QueuedConnection);
        string o = dom + " está DISPONÍVEL";
        if (!preco.empty()) o += " por US$ " + preco + " (1 ano).";
        if (!d.msg.empty()) o += "\n" + d.msg;
        return o;
      });
    });
    connect(bComp, &QPushButton::clicked, this, [this, prepara] {
      string tk = prepara(), dom = S(edDominio->text().trimmed());
      if (ultDominio != dom || ultPreco.empty()) { plog(T("Busque o domínio primeiro: o app só compra pelo preço que a Vercel acabou de informar.")); return; }
      string preco = ultPreco;
      auto r = QMessageBox::question(this, T("Confirmar compra"), T("Comprar ") + Q(dom) + T(" por US$ ") + Q(preco) + T(" (1 ano, com renovação automática)?\n\nO valor será cobrado agora no cartão da sua conta Vercel e não há desistência pelo app."), QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
      if (r != QMessageBox::Yes) return;
      tarefa(T("Comprando o domínio…"), [tk, dom, preco] { return pub::dominioComprar(tk, dom, preco); });
    });
    connect(bLiga, &QPushButton::clicked, this, [this, prepara] {
      string tk = prepara(), proj = S(edProj->text().trimmed()), dom = S(edDominio->text().trimmed());
      tarefa(T("Vinculando o domínio ao projeto…"), [tk, proj, dom] { return pub::vercelVincularDominio(tk, proj, dom); });
    });

    // ---- ferramentas
    QVBoxLayout* fl;
    v->addWidget(secao(T("Ligar o site ao servidor e conferir tudo"), T("Com o site na Vercel e o Nodo no seu computador, o site precisa saber o endereço do servidor. Depois confira a segurança."), fl));
    auto* bApont = primary(T("Apontar o site para o servidor (Tailscale)"));
    auto* bLocal = flat(T("Voltar ao local"));
    auto* bRead = flat(T("Gerar README e .gitignore"));
    auto* bVer = flat(T("Verificar segurança"));
    fl->addLayout(linha({bApont, bLocal}));
    fl->addLayout(linha({bRead, bVer}));
    connect(bApont, &QPushButton::clicked, this, [this] {
      string u = ultTsUrl.empty() ? cfg.tsUrl : ultTsUrl;
      if (u.empty()) { plog(T("Publique o servidor com o Tailscale primeiro (etapa 1).")); return; }
      tarefa(T("Apontando os arquivos HTML para o servidor…"), [u] { return pub::apontarSite(u) + "\nEnvie para o Git (etapa 2) para a Vercel publicar a mudança."; });
    });
    connect(bLocal, &QPushButton::clicked, this, [this] { tarefa(T("Voltando ao nodo.js local…"), [] { return pub::apontarSite(""); }); });
    connect(bRead, &QPushButton::clicked, this, [this] { tarefa(T("Gerando arquivos do projeto…"), [] { return pub::prepararArquivos() + "\n" + pub::gerarReadme(true); }); });
    connect(bVer, &QPushButton::clicked, this, [this] {
      tarefa(T("Verificando a segurança…"), [] {
        string o;
        for (auto& f : auditoria()) o += string(f.nivel == 'O' ? "[ok]     " : f.nivel == 'A' ? "[aviso]  " : "[ALERTA] ") + f.msg + "\n";
        return o;
      });
    });

    v->addWidget(bold(T("Resultado")));
    pubLog = new QPlainTextEdit;
    pubLog->setReadOnly(true);
    pubLog->setMinimumHeight(150);
    v->addWidget(pubLog);
    v->addStretch();
    auto* sc = new QScrollArea;
    sc->setWidgetResizable(true);
    sc->setFrameShape(QFrame::NoFrame);
    sc->setWidget(w);
    return sc;
  }

  // ================================================================ pagina: Inicio
  QWidget* paginaInicio() {
    QVBoxLayout* v;
    auto* w = pagina(v);
    v->addWidget(h1(T("Início")));
    auto* c = card();
    auto* cl = new QHBoxLayout(c);
    cl->setContentsMargins(20, 18, 20, 18);
    cl->setSpacing(14);
    dot = new QLabel(T("●"));
    dot->setStyleSheet("font-size: 34px; color: #9ca3af;");
    auto* tv = new QVBoxLayout;
    stTitulo = bold("");
    QFont tf = stTitulo->font();
    tf.setPointSize(tf.pointSize() + 4);
    stTitulo->setFont(tf);
    stSub = muted("");
    tv->addWidget(stTitulo);
    tv->addWidget(stSub);
    cl->addWidget(dot);
    cl->addLayout(tv, 1);
    btnPasta = flat(T("Escolher pasta do projeto…"));
    btnLigar = primary(T("Ligar"));
    cl->addWidget(btnPasta);
    cl->addWidget(btnLigar);
    v->addWidget(c);
    connect(btnPasta, &QPushButton::clicked, this, [this] { escolherPasta(); });
    connect(btnLigar, &QPushButton::clicked, this, [this] { alternar(); });

    auto* c2 = card();
    auto* l2 = new QVBoxLayout(c2);
    l2->setContentsMargins(20, 16, 20, 16);
    l2->addWidget(bold(T("Endereços do servidor")));
    enderecos = new QListWidget;
    enderecos->setFixedHeight(96);
    l2->addWidget(enderecos);
    auto* r2 = new QHBoxLayout;
    btnAbrirSite = flat(T("Abrir site"));
    btnAbrirPainel = flat(T("Abrir painel web"));
    btnCopiarEnd = flat(T("Copiar endereço"));
    r2->addWidget(btnAbrirSite);
    r2->addWidget(btnAbrirPainel);
    r2->addWidget(btnCopiarEnd);
    auto* btnGuia = flat(T("Guia de integração"));
    r2->addWidget(btnGuia);
    connect(btnGuia, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl(QString("http://localhost:%1/guia").arg(cfg.porta))); });
    r2->addStretch();
    l2->addLayout(r2);
    v->addWidget(c2);
    connect(btnAbrirSite, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(QUrl(QString("http://localhost:%1/").arg(cfg.porta))); });
    connect(btnAbrirPainel, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(QUrl(QString("http://localhost:%1/admin").arg(cfg.porta))); });
    connect(btnCopiarEnd, &QPushButton::clicked, this, [this] {
      auto* it = enderecos->currentItem() ? enderecos->currentItem() : enderecos->item(0);
      if (it) copiar(it->data(Qt::UserRole).toString());
    });
    connect(enderecos, &QListWidget::itemDoubleClicked, this, [](QListWidgetItem* it) { QDesktopServices::openUrl(QUrl(it->data(Qt::UserRole).toString())); });

    auto* c3 = card();
    auto* l3 = new QVBoxLayout(c3);
    l3->setContentsMargins(20, 16, 20, 16);
    l3->addWidget(bold(T("Usar no seu site")));
    l3->addWidget(muted(T("Cole no HTML de qualquer site. Para visitantes enviarem dados sem chave (ex.: formulário), libere a coleção em Configurações.")));
    snippet = new QPlainTextEdit;
    snippet->setReadOnly(true);
    snippet->setFixedHeight(128);
    QFont mono("Menlo");
    mono.setStyleHint(QFont::Monospace);
    snippet->setFont(mono);
    l3->addWidget(snippet);
    btnCopiarCod = flat(T("Copiar código"));
    auto* r3 = new QHBoxLayout;
    r3->addWidget(btnCopiarCod);
    r3->addStretch();
    l3->addLayout(r3);
    v->addWidget(c3);
    connect(btnCopiarCod, &QPushButton::clicked, this, [this] { copiar(snippet->toPlainText()); });

    chkAuto = new QCheckBox(T("Ligar o servidor automaticamente ao abrir o app"));
    chkAuto->setChecked(QSettings().value("auto", false).toBool());
    connect(chkAuto, &QCheckBox::toggled, this, [](bool b) { QSettings().setValue("auto", b); });
    v->addWidget(chkAuto);
    v->addStretch();
    return w;
  }

  // ================================================================ pagina: Banco de dados
  QWidget* paginaBanco() {
    QVBoxLayout* v;
    auto* w = pagina(v);
    v->addWidget(h1(T("Banco de dados")));
    auto* sp = new QSplitter;
    auto* left = new QWidget;
    auto* ll = new QVBoxLayout(left);
    ll->setContentsMargins(0, 0, 8, 0);
    ll->addWidget(bold(T("Coleções")));
    colList = new QListWidget;
    ll->addWidget(colList, 1);
    bNovaCol = flat(T("+ Nova coleção"));
    ll->addWidget(bNovaCol);
    auto* right = new QWidget;
    auto* rl = new QVBoxLayout(right);
    rl->setContentsMargins(8, 0, 0, 0);
    busca = new QLineEdit;
    busca->setPlaceholderText(T("Buscar nos documentos…"));
    busca->setClearButtonEnabled(true);
    rl->addWidget(busca);
    tbl = new QTableWidget(0, 2);
    tbl->setHorizontalHeaderLabels({"ID", T("Conteúdo")});
    tbl->horizontalHeader()->setStretchLastSection(true);
    tbl->verticalHeader()->hide();
    tbl->setColumnWidth(0, 170);
    tbl->setSelectionBehavior(QAbstractItemView::SelectRows);
    tbl->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tbl->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tbl->setAlternatingRowColors(true);
    rl->addWidget(tbl, 1);
    auto* br = new QHBoxLayout;
    bNovoDoc = primary(T("Novo documento"));
    bEditar = flat(T("Editar"));
    bApagar = flat(T("Apagar"));
    bExp = flat(T("Exportar backup…"));
    bImp = flat(T("Importar…"));
    br->addWidget(bNovoDoc);
    br->addWidget(bEditar);
    br->addWidget(bApagar);
    br->addStretch();
    br->addWidget(bExp);
    br->addWidget(bImp);
    rl->addLayout(br);
    sp->addWidget(left);
    sp->addWidget(right);
    sp->setStretchFactor(1, 1);
    sp->setSizes({210, 700});
    v->addWidget(sp, 1);

    connect(colList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* it, QListWidgetItem*) {
      colAtual = it ? S(it->data(Qt::UserRole).toString()) : "";
      atualizarBanco(true);
    });
    connect(busca, &QLineEdit::textChanged, this, [this] { atualizarBanco(true); });
    connect(bNovaCol, &QPushButton::clicked, this, [this] {
      if (!exigeProjeto()) return;
      bool ok;
      QString n = QInputDialog::getText(this, T("Nova coleção"), T("Nome (letras, números, _ e -). Ex.: produtos, clientes, contatos"), QLineEdit::Normal, "", &ok).trimmed();
      if (!ok || n.isEmpty()) return;
      if (!colOk(S(n))) { QMessageBox::warning(this, "Nodo", T("Nome inválido. Use só letras, números, _ e -, e não comece com _.")); return; }
      pend.insert(S(n));
      colAtual = S(n);
      atualizarBanco(true);
    });
    connect(bNovoDoc, &QPushButton::clicked, this, [this] {
      if (!exigeProjeto()) return;
      if (colAtual.empty()) { QMessageBox::information(this, "Nodo", T("Escolha ou crie uma coleção primeiro.")); return; }
      string txt = "{\n  \n}", doc;
      if (!editarJson(this, T("Novo documento em ") + Q(colAtual), txt)) return;
      string id = newId();
      if (makeDoc(txt, id, doc)) { storePut(colAtual, id, doc, true); pend.erase(colAtual); }
      atualizarBanco(true);
    });
    auto editar = [this] {
      auto ids = idsSelecionados();
      if (ids.empty()) return;
      string old;
      if (!storeGet(colAtual, ids[0], old)) return;
      string txt = pretty(old), doc;
      if (!editarJson(this, T("Editar documento"), txt)) return;
      if (makeDoc(txt, ids[0], doc)) storePut(colAtual, ids[0], doc, true);
      atualizarBanco(true);
    };
    connect(bEditar, &QPushButton::clicked, this, editar);
    connect(tbl, &QTableWidget::cellDoubleClicked, this, editar);
    connect(bApagar, &QPushButton::clicked, this, [this] {
      auto ids = idsSelecionados();
      if (ids.empty()) return;
      if (QMessageBox::question(this, T("Apagar"), QString(T("Apagar %1 documento(s)? Isso não pode ser desfeito.")).arg(ids.size())) != QMessageBox::Yes) return;
      for (auto& id : ids) storeDel(colAtual, id, true);
      atualizarBanco(true);
    });
    connect(bExp, &QPushButton::clicked, this, [this] {
      if (!exigeProjeto()) return;
      QString f = QFileDialog::getSaveFileName(this, T("Salvar backup"), QDir::home().filePath("backup-nodo.json"), "JSON (*.json)");
      if (f.isEmpty()) return;
      if (storeExport(S(f))) QMessageBox::information(this, "Nodo", T("Backup salvo."));
      else QMessageBox::warning(this, "Nodo", T("Não consegui salvar o arquivo."));
    });
    connect(bImp, &QPushButton::clicked, this, [this] {
      if (!exigeProjeto()) return;
      QString f = QFileDialog::getOpenFileName(this, T("Importar backup"), QDir::homePath(), "JSON (*.json)");
      if (f.isEmpty()) return;
      string txt;
      if (!readFile(S(f), txt)) { QMessageBox::warning(this, "Nodo", T("Não consegui ler o arquivo.")); return; }
      int n = storeImport(txt);
      if (n < 0) QMessageBox::warning(this, "Nodo", T("Arquivo inválido. O formato esperado é {\"colecao\": [ {...}, {...} ]}."));
      else QMessageBox::information(this, "Nodo", QString(T("%1 documento(s) importado(s).")).arg(n));
      atualizarBanco(true);
    });
    return w;
  }
  vector<string> idsSelecionados() {
    vector<string> o;
    set<int> rows;
    for (auto* it : tbl->selectedItems()) rows.insert(it->row());
    for (int r : rows) o.push_back(S(tbl->item(r, 0)->text()));
    return o;
  }
  bool exigeProjeto() {
    if (projetoOk) return true;
    QMessageBox::information(this, "Nodo", T("Escolha a pasta do seu projeto na aba Início primeiro."));
    return false;
  }
  void atualizarBanco(bool force) {
    auto cs = storeCols();
    string sig = S(busca ? busca->text() : QString()) + "|" + colAtual + "|";
    for (auto& c : cs) sig += c.first + ":" + to_string(c.second) + ",";
    for (auto& p : pend) sig += "+" + p;
    auto docs = storeDocs(colAtual);
    size_t tam = 0;
    for (auto& d : docs) tam += d.second.size();
    sig += "#" + to_string(tam);
    if (!force && sig == sigBanco) return;
    sigBanco = sig;
    // lista de colecoes
    set<string> nomes;
    map<string, size_t> cnt;
    for (auto& c : cs) { nomes.insert(c.first); cnt[c.first] = c.second; }
    for (auto& p : pend) if (!nomes.count(p)) { nomes.insert(p); cnt[p] = 0; }
    if (colAtual.empty() && !nomes.empty()) {  // abre ja com a primeira colecao selecionada
      colAtual = *nomes.begin();
      docs = storeDocs(colAtual);
    }
    colList->blockSignals(true);
    colList->clear();
    int sel = -1, k = 0;
    for (auto& n : nomes) {
      auto* it = new QListWidgetItem(Q(n) + "  (" + QString::number(cnt[n]) + ")");
      it->setData(Qt::UserRole, Q(n));
      colList->addItem(it);
      if (n == colAtual) sel = k;
      k++;
    }
    if (sel >= 0) colList->setCurrentRow(sel);
    colList->blockSignals(false);
    // documentos
    string q = ::lower(S(busca->text()));
    tbl->setRowCount(0);
    for (auto& d : docs) {
      string txt = compacto(d.second);
      if (!q.empty() && ::lower(d.second).find(q) == string::npos) continue;
      int r = tbl->rowCount();
      tbl->insertRow(r);
      tbl->setItem(r, 0, new QTableWidgetItem(Q(d.first)));
      tbl->setItem(r, 1, new QTableWidgetItem(Q(txt.substr(0, 400))));
    }
    bool tem = !colAtual.empty();
    bNovoDoc->setEnabled(tem);
    bEditar->setEnabled(tem);
    bApagar->setEnabled(tem);
  }

  // ================================================================ pagina: Rede
  QWidget* paginaRede() {
    QVBoxLayout* v;
    auto* w = pagina(v);
    v->addWidget(h1(T("Rede")));
    auto* c = card();
    auto* l = new QVBoxLayout(c);
    l->setContentsMargins(20, 16, 20, 16);
    l->addWidget(bold(T("Este PC")));
    redeInfo = new QLabel;
    redeInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->addWidget(redeInfo);
    v->addWidget(c);

    auto* c2 = card();
    auto* l2 = new QVBoxLayout(c2);
    l2->setContentsMargins(20, 16, 20, 16);
    redeCount = bold("");
    l2->addWidget(redeCount);
    l2->addWidget(muted(T("PCs na mesma rede Wi-Fi/cabo com a mesma chave se encontram sozinhos em até 5 segundos. Cada documento salvo em um PC é copiado para os outros.")));
    peersW = new QListWidget;
    peersW->setFixedHeight(100);
    l2->addWidget(peersW);
    auto* r2 = new QHBoxLayout;
    auto* bCon = flat(T("Conectar PC pelo endereço…"));
    r2->addWidget(bCon);
    r2->addStretch();
    l2->addLayout(r2);
    v->addWidget(c2);
    connect(bCon, &QPushButton::clicked, this, [this] {
      if (!exigeProjeto()) return;
      if (!rodando) { QMessageBox::information(this, "Nodo", T("Ligue o servidor primeiro (aba Início).")); return; }
      bool ok;
      QString e = QInputDialog::getText(this, T("Conectar PC"), T("Endereço do outro PC (ex.: 192.168.0.15:7070):"), QLineEdit::Normal, "", &ok).trimmed();
      if (!ok || e.isEmpty()) return;
      string hp = S(e);
      if (hp.find(':') == string::npos) hp += ":7070";
      QApplication::setOverrideCursor(Qt::WaitCursor);
      bool alcancou = http_do(hp, "GET", "/api/_status", "", nullptr);
      QApplication::restoreOverrideCursor();
      if (!alcancou) { QMessageBox::warning(this, "Nodo", T("Não consegui conectar. Confira o IP, a porta, se o outro Nodo está ligado e se a chave é a mesma nos dois PCs.")); return; }
      addPeer(hp, true);
      atualizarTudo();
    });

    auto* c3 = card();
    auto* l3 = new QVBoxLayout(c3);
    l3->setContentsMargins(20, 16, 20, 16);
    l3->addWidget(bold(T("Chave da rede")));
    l3->addWidget(muted(T("Para conectar outro PC: instale o Nodo nele, abra um projeto e use \"Usar outra chave…\" colando esta mesma chave. Ela também é a senha de administrador.")));
    chaveEd = new QLineEdit;
    chaveEd->setReadOnly(true);
    chaveEd->setEchoMode(QLineEdit::Password);
    l3->addWidget(chaveEd);
    auto* r3 = new QHBoxLayout;
    btnMostrar = flat(T("Mostrar"));
    auto* bCop = flat(T("Copiar chave"));
    auto* bUsar = flat(T("Usar outra chave…"));
    auto* bNova = flat(T("Gerar nova chave"));
    r3->addWidget(btnMostrar);
    r3->addWidget(bCop);
    r3->addWidget(bUsar);
    r3->addWidget(bNova);
    r3->addStretch();
    l3->addLayout(r3);
    v->addWidget(c3);
    v->addStretch();
    connect(btnMostrar, &QPushButton::clicked, this, [this] {
      bool oculto = chaveEd->echoMode() == QLineEdit::Password;
      chaveEd->setEchoMode(oculto ? QLineEdit::Normal : QLineEdit::Password);
      btnMostrar->setText(oculto ? T("Ocultar") : T("Mostrar"));
    });
    connect(bCop, &QPushButton::clicked, this, [this] { if (projetoOk) copiar(Q(cfg.chave)); });
    connect(bUsar, &QPushButton::clicked, this, [this] {
      if (!exigeProjeto()) return;
      bool ok;
      QString k = QInputDialog::getText(this, T("Usar outra chave"), T("Cole a chave do outro PC (16 a 128 letras/números):"), QLineEdit::Normal, "", &ok).trimmed();
      if (!ok || k.isEmpty()) return;
      mudarChave(S(k));
    });
    connect(bNova, &QPushButton::clicked, this, [this] {
      if (!exigeProjeto()) return;
      if (QMessageBox::question(this, T("Gerar nova chave"), T("Os outros PCs conectados e qualquer sistema que use a chave atual precisarão da nova chave. Continuar?")) != QMessageBox::Yes) return;
      mudarChave(hexrand(48));
    });
    return w;
  }
  void mudarChave(const string& k) {
    bool ok = k.size() >= 16 && k.size() <= 128;
    for (char c : k) if (!isalnum((unsigned char)c)) ok = false;
    if (!ok) { QMessageBox::warning(this, "Nodo", T("Chave inválida: use de 16 a 128 letras e números, sem espaços.")); return; }
    bool was = rodando;
    if (was) stopServer();
    cfg.chave = k;
    saveCfg();
    if (was) { string e = startServer(); if (!e.empty()) QMessageBox::warning(this, "Nodo", Q(e)); }
    atualizarTudo();
  }

  // ================================================================ pagina: Configuracoes
  // ================================================================ pagina: Monitor de uso
  QWidget* paginaMonitor() {
    QVBoxLayout* v;
    auto* w = pagina(v);
    v->addWidget(h1(T("Monitor de uso")));
    v->addWidget(muted(T("O que o servidor está fazendo agora. Os números ficam só na memória e zeram quando o servidor reinicia.")));
    auto* grade = new QGridLayout;
    grade->setSpacing(10);
    const char* titulos[] = {"Requisições (total)", "Por minuto (últimos 5 min)", "Leituras / escritas", "Erros 4xx / 5xx", "Limite excedido (429)", "Chaves erradas", "Origens recusadas",
                             "Conexões agora / pico", "Latência média / p95", "CPU", "Memória", "Banco em disco", "Documentos", "Tráfego enviado / recebido", "PCs online / total", "Fila de replicação"};
    int n = 0;
    for (auto t : titulos) {
      auto* c = card();
      auto* l = new QVBoxLayout(c);
      l->setContentsMargins(12, 8, 12, 8);
      l->setSpacing(2);
      l->addWidget(muted(T(t)));
      auto* val = bold(T("-"));
      QFont f = val->font();
      f.setPointSize(f.pointSize() + 3);
      val->setFont(f);
      l->addWidget(val);
      monVals.push_back(val);
      grade->addWidget(c, n / 4, n % 4);
      n++;
    }
    v->addLayout(grade);
    v->addWidget(bold(T("Requisições por minuto (última hora) — vermelho = erros")));
    monGraf = new Grafico;
    v->addWidget(monGraf);
    auto* tabs = new QHBoxLayout;
    auto mk = [&](QTableWidget*& t, const QStringList& cab, const QString& titulo) {
      auto* col = new QVBoxLayout;
      col->addWidget(bold(titulo));
      t = new QTableWidget(0, cab.size());
      t->setHorizontalHeaderLabels(cab);
      t->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
      t->setEditTriggers(QAbstractItemView::NoEditTriggers);
      t->setSelectionMode(QAbstractItemView::NoSelection);
      t->verticalHeader()->setVisible(false);
      col->addWidget(t);
      tabs->addLayout(col, 1);
    };
    mk(monCols, QStringList() << T("Coleção") << T("Documentos") << T("Leituras") << T("Escritas"), T("Por coleção"));
    mk(monIps, QStringList() << T("IP") << T("Requisições"), T("Quem mais pediu"));
    v->addLayout(tabs, 1);
    monMeta = muted(QString());
    v->addWidget(monMeta);
    return w;
  }
  static QString numBr(ull n) { return QLocale(QLocale::Portuguese, QLocale::Brazil).toString((qulonglong)n); }
  void atualizarMonitor() {
    if (!monGraf) return;
    MetSnap m = metricasSnap();
    unsigned ult5 = 0;
    for (size_t i = m.serieReq.size() >= 5 ? m.serieReq.size() - 5 : 0; i < m.serieReq.size(); i++) ult5 += m.serieReq[i];
    QStringList val;
    val << numBr(m.reqs) << QString::number(ult5 / 5.0, 'f', 1) << numBr(m.leituras) + " / " + numBr(m.escritas) << numBr(m.e4) + " / " + numBr(m.e5) << numBr(m.limitados)
        << numBr(m.authFalhas) << numBr(m.origemNegada) << QString("%1 / %2").arg(m.conexoes).arg(m.pico)
        << QString("%1 / %2 ms").arg(m.latMed, 0, 'f', 1).arg(m.latP95, 0, 'f', 1)
        << (m.cpu < 0 ? T("n/d") : QString("%1 %").arg(m.cpu, 0, 'f', 1)) << (m.rssMb < 0 ? T("n/d") : QString("%1 MB").arg(m.rssMb, 0, 'f', 1))
        << Q(humano(m.disco)) << QString("%1 em %2 coleções").arg(m.nDocs).arg(m.nCols) << Q(humano(m.bytesOut)) + " / " + Q(humano(m.bytesIn))
        << QString("%1 / %2").arg(m.peersOnline).arg(m.peersTotal) << QString::number(m.fila);
    for (int i = 0; i < val.size() && i < (int)monVals.size(); i++) monVals[i]->setText(val[i]);
    monVals[3]->setStyleSheet(m.e5 ? "color:#dc2626;" : "");
    monVals[5]->setStyleSheet(m.authFalhas ? "color:#dc2626;" : "");
    monGraf->defineDados(m.serieReq, m.serieErr);
    monCols->setRowCount((int)m.colunas.size());
    for (size_t i = 0; i < m.colunas.size(); i++) {
      monCols->setItem((int)i, 0, new QTableWidgetItem(Q(m.colunas[i].first)));
      for (int k = 0; k < 3; k++) monCols->setItem((int)i, k + 1, new QTableWidgetItem(numBr(m.colunas[i].second[k])));
    }
    monIps->setRowCount((int)m.ips.size());
    for (size_t i = 0; i < m.ips.size(); i++) {
      monIps->setItem((int)i, 0, new QTableWidgetItem(Q(m.ips[i].first)));
      monIps->setItem((int)i, 1, new QTableWidgetItem(numBr(m.ips[i].second)));
    }
    long up = m.uptime;
    monMeta->setText(rodando ? QString(T("Online há %1 h %2 min · sessões do painel web: %3 · replicação: %4 enviados, %5 falhas, %6 descartados"))
                                   .arg(up / 3600).arg((up % 3600) / 60).arg(m.sessoes).arg(m.replEnv).arg(m.replFalh).arg(m.replDesc)
                             : T("Servidor desligado."));
  }

  QWidget* paginaConfig() {
    QVBoxLayout* v;
    auto* w = pagina(v);
    v->addWidget(h1(T("Configurações")));
    auto* c = card();
    auto* l = new QVBoxLayout(c);
    l->setContentsMargins(20, 16, 20, 16);
    l->setSpacing(10);
    l->addWidget(bold(T("Projeto")));
    cfgPasta = new QLabel;
    cfgPasta->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->addWidget(cfgPasta);
    auto* pr = new QHBoxLayout;
    auto* bPrep = flat(T("Preparar / reinstalar arquivos na pasta"));
    auto* bFinder = flat(T("Abrir pasta"));
    pr->addWidget(bPrep);
    pr->addWidget(bFinder);
    pr->addStretch();
    l->addLayout(pr);
    v->addWidget(c);
    connect(bPrep, &QPushButton::clicked, this, [this] {
      if (!exigeProjeto()) return;
      auto r = integrarPasta(S(projeto), "", false);
      QMessageBox::information(this, "Nodo", r.ok ? T("Pasta atualizada:\n") + Q(r.msg) : Q(r.msg));
    });
    connect(bFinder, &QPushButton::clicked, this, [this] { if (projetoOk) QDesktopServices::openUrl(QUrl::fromLocalFile(projeto)); });

    auto* c2 = card();
    auto* l2 = new QVBoxLayout(c2);
    l2->setContentsMargins(20, 16, 20, 16);
    l2->setSpacing(10);
    l2->addWidget(bold(T("Servidor")));
    auto* form = new QFormLayout;
    spPorta = new QSpinBox;
    spPorta->setRange(1024, 65535);
    spPorta->setValue(7070);
    chkRede = new QCheckBox(T("Permitir acesso de outros PCs e celulares da rede"));
    edLeit = new QLineEdit;
    edLeit->setPlaceholderText(T("ex.: produtos, posts (separe por vírgula)"));
    edEsc = new QLineEdit;
    edEsc->setPlaceholderText(T("ex.: contatos, pedidos"));
    form->addRow(T("Porta:"), spPorta);
    form->addRow("", chkRede);
    form->addRow(T("Coleções que qualquer visitante pode LER:"), edLeit);
    form->addRow(T("Coleções onde visitantes só podem ENVIAR:"), edEsc);
    edOrigens = new QLineEdit;
    edOrigens->setPlaceholderText(T("ex.: https://meusite.com, https://www.meusite.com (vazio = qualquer site)"));
    form->addRow(T("Sites autorizados (CORS):"), edOrigens);
    edCampos = new QPlainTextEdit;
    edCampos->setMaximumHeight(90);
    edCampos->setPlaceholderText(T("Uma coleção por linha, com os campos e o máximo de caracteres de cada um:\ncontatos = nome:80, email:120, mensagem:1000"));
    form->addRow(T("Campos que visitantes podem enviar:"), edCampos);
    l2->addLayout(form);
    l2->addWidget(muted(T("Tudo que não estiver liberado aqui só funciona com a chave de administrador. Editar e apagar sempre exigem a chave.")));
    auto* sr = new QHBoxLayout;
    auto* bSalvar = primary(T("Salvar"));
    cfgOk = new QLabel;
    sr->addWidget(bSalvar);
    sr->addWidget(cfgOk);
    sr->addStretch();
    l2->addLayout(sr);
    v->addWidget(c2);

    v->addStretch();
    connect(bSalvar, &QPushButton::clicked, this, [this] { salvarConfig(); });
    return w;
  }
  void preencherConfig() {
    spPorta->setValue(cfg.porta);
    chkRede->setChecked(cfg.escutar != "127.0.0.1");
    edLeit->setText(Q(joinSet(cfg.leitura)));
    edEsc->setText(Q(joinSet(cfg.escrita)));
    edOrigens->setText(Q(joinSet(cfg.origens)));
    {
      string t;
      for (auto& kv : cfg.campos) {
        string l;
        for (auto& cp : kv.second) l += (l.empty() ? "" : ", ") + cp.first + ":" + to_string(cp.second);
        t += kv.first + " = " + l + "\n";
      }
      edCampos->setPlainText(Q(t));
    }
    cfgPasta->setText(projeto);
    cfgOk->clear();
  }
  bool lerLista(QLineEdit* ed, set<string>& out) {
    out.clear();
    for (auto& x : split(S(ed->text()), ',')) {
      string n = trim(x);
      if (n.empty()) continue;
      if (!colOk(n)) { QMessageBox::warning(this, "Nodo", T("Nome de coleção inválido: ") + Q(n)); return false; }
      out.insert(n);
    }
    return true;
  }
  void salvarConfig() {
    if (!exigeProjeto()) return;
    set<string> a, b;
    if (!lerLista(edLeit, a) || !lerLista(edEsc, b)) return;
    set<string> orig;
    for (auto& x : split(S(edOrigens->text()), ',')) {
      string o = trim(x);
      while (o.size() > 1 && o.back() == '/') o.pop_back();
      if (o.empty()) continue;
      if (!origemOk(o)) { QMessageBox::warning(this, "Nodo", T("Site inválido em \"Sites autorizados\": ") + Q(o) + T("\nUse o formato https://meusite.com")); return; }
      orig.insert(o);
    }
    map<string, map<string, int>> cm;
    for (auto& ln : split(S(edCampos->toPlainText()), '\n')) {
      string l = trim(ln);
      if (l.empty()) continue;
      size_t eq = l.find('=');
      string col = eq == string::npos ? "" : trim(l.substr(0, eq));
      if (!colOk(col)) { QMessageBox::warning(this, "Nodo", T("Linha inválida em \"Campos que visitantes podem enviar\": ") + Q(l) + T("\nUse: coleção = campo:limite, campo:limite")); return; }
      for (auto& x : split(l.substr(eq + 1), ',')) {
        string it = trim(x);
        if (it.empty()) continue;
        int lim = cfg.maxCampoPadrao;
        size_t d = it.find(':');
        if (d != string::npos) { lim = atoi(it.substr(d + 1).c_str()); it = trim(it.substr(0, d)); }
        if (!campoNomeOk(it) || it[0] == '_' || lim < 1) { QMessageBox::warning(this, "Nodo", T("Campo inválido: ") + Q(it)); return; }
        cm[col][it] = min(lim, 60000);
      }
    }
    bool was = rodando;
    if (was) stopServer();
    cfg.porta = spPorta->value();
    cfg.escutar = chkRede->isChecked() ? "0.0.0.0" : "127.0.0.1";
    cfg.escutarArquivo = cfg.escutar;
    cfg.leitura = a;
    cfg.escrita = b;
    cfg.origens = orig;
    cfg.campos = cm;
    bool ok = saveCfg();
    if (was) { string e = startServer(); if (!e.empty()) QMessageBox::warning(this, "Nodo", Q(e)); }
    cfgOk->setText(ok ? T("✓ Salvo") + (was ? T(" (servidor reiniciado)") : QString()) : T("Não consegui salvar nodo.conf"));
    atualizarTudo();
  }

  QWidget* paginaDiagnostico() {
    QVBoxLayout* v;
    auto* w = pagina(v);
    v->addWidget(h1(T("Diagnóstico e suporte · v3.0.0")));
    v->addWidget(muted(T("Gere um relatório técnico para identificar problemas de inicialização, rede e versão. O relatório não inclui chaves de acesso nem o conteúdo do banco de dados.")));
    auto* c = card();
    auto* l = new QVBoxLayout(c);
    l->setContentsMargins(20, 16, 20, 16);
    l->setSpacing(12);
    l->addWidget(bold(T("Informações do ambiente")));
    auto* preview = new QPlainTextEdit;
    preview->setReadOnly(true);
    preview->setMinimumHeight(230);
    auto gerarRelatorio = [this]() {
      QString estado = T("desligado");
      if (rodando) estado = T("ligado");
      QString r;
      r += T("Nodo 3 — relatório de diagnóstico\n");
      r += T("Versão: 3.0.0\n");
      r += T("Data: ") + QDateTime::currentDateTime().toString(Qt::ISODate) + "\n";
      r += T("Sistema operacional: ") + QSysInfo::prettyProductName() + "\n";
      r += T("Arquitetura: ") + QSysInfo::currentCpuArchitecture() + "\n";
      r += T("Versão do Qt: ") + QString::fromLatin1(QT_VERSION_STR) + "\n";
      r += T("Executável: ") + QCoreApplication::applicationFilePath() + "\n";
      r += T("Servidor Nodo: ") + estado + "\n";
      r += T("Projeto aberto: ") + (projetoOk ? T("sim") : T("não")) + "\n";
      r += T("Observação: relatório sem chaves, senhas ou dados das coleções.\n");
      return r;
    };
    preview->setPlainText(gerarRelatorio());
    l->addWidget(preview, 1);
    auto* linha = new QHBoxLayout;
    auto* copiarRel = primary(T("Copiar relatório"));
    auto* salvarRel = flat(T("Salvar relatório .txt"));
    auto* atualizarRel = flat(T("Atualizar informações"));
    linha->addWidget(copiarRel);
    linha->addWidget(salvarRel);
    linha->addWidget(atualizarRel);
    linha->addStretch();
    l->addLayout(linha);
    v->addWidget(c, 1);
    connect(copiarRel, &QPushButton::clicked, this, [this, preview, gerarRelatorio] {
      preview->setPlainText(gerarRelatorio());
      copiar(preview->toPlainText());
      QMessageBox::information(this, T("Diagnóstico"), T("Relatório copiado. Confira o texto antes de compartilhá-lo."));
    });
    connect(salvarRel, &QPushButton::clicked, this, [this, preview, gerarRelatorio] {
      preview->setPlainText(gerarRelatorio());
      QString caminho = QFileDialog::getSaveFileName(this, T("Salvar relatório de diagnóstico"), QDir::homePath() + "/Nodo-diagnostico.txt", T("Arquivo de texto (*.txt)"));
      if (caminho.isEmpty()) return;
      QFile f(caminho);
      if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate) || f.write(preview->toPlainText().toUtf8()) < 0) {
        QMessageBox::critical(this, T("Diagnóstico"), T("Não foi possível salvar o relatório."));
        return;
      }
      QMessageBox::information(this, T("Diagnóstico"), T("Relatório salvo com sucesso."));
    });
    connect(atualizarRel, &QPushButton::clicked, this, [preview, gerarRelatorio] { preview->setPlainText(gerarRelatorio()); });
    return w;
  }

  // ================================================================ pagina: Registro
  QWidget* paginaLog() {
    QVBoxLayout* v;
    auto* w = pagina(v);
    v->addWidget(h1(T("Registro")));
    v->addWidget(muted(T("O que o servidor está fazendo: conexões de outros PCs e escritas no banco.")));
    logW = new QPlainTextEdit;
    logW->setReadOnly(true);
    QFont mono("Menlo");
    mono.setStyleHint(QFont::Monospace);
    logW->setFont(mono);
    v->addWidget(logW, 1);
    return w;
  }

  // ================================================================ acoes
  void escolherPasta() {
    QString d = QFileDialog::getExistingDirectory(this, T("Escolha a pasta do seu projeto (onde está o index.html do site)"), projeto.isEmpty() ? QDir::homePath() : projeto);
    if (!d.isEmpty()) abrirProjeto(d);
  }
  void abrirProjeto(const QString& d) {
    // Aceita uma pasta de projeto ou uma pasta-pai: localiza nodo.conf automaticamente,
    // sem pedir ao usuario para copiar chave/codigo. Nunca procura dentro de pastas de dados,
    // dependencias ou controle de versao.
    QString pastaEscolhida = QDir(d).absolutePath();
    if (!QFile::exists(pastaEscolhida + "/nodo.conf")) {
      QString encontrado;
      QDirIterator it(pastaEscolhida, QStringList() << "nodo.conf", QDir::Files,
                      QDirIterator::Subdirectories);
      while (it.hasNext()) {
        QString f = it.next();
        QString rel = QDir(pastaEscolhida).relativeFilePath(f);
        QStringList partes = rel.split('/');
        bool ignorar = false;
        for (const QString& parte : partes) {
          if (parte == ".git" || parte == "node_modules" || parte == "nodo-dados" || parte == ".venv" || parte == "build") { ignorar = true; break; }
        }
        if (ignorar) continue;
        if (encontrado.isEmpty() || f.count('/') < encontrado.count('/')) encontrado = f;
      }
      if (!encontrado.isEmpty()) {
        QString pastaConf = QFileInfo(encontrado).absolutePath();
        auto resposta = QMessageBox::question(this, T("Configuração encontrada"),
          T("Encontrei o arquivo nodo.conf em:\n") + pastaConf +
          T("\n\nDeseja abrir esse projeto? O Nodo vai ler a configuração localmente e usar a chave do arquivo sem pedir para digitá-la."));
        if (resposta == QMessageBox::Yes) pastaEscolhida = pastaConf;
      }
    }
    const QString& pastaAbrir = pastaEscolhida;
    bool was = rodando;
    if (was) stopServer();
    auto restaurar = [&] {
      if (projetoOk && openProject(S(projeto)).empty()) { loadStore(); if (was) startServer(); }
    };
    string e = openProject(S(pastaAbrir));
    if (e == "NOCONF") {
      auto b = QMessageBox::question(this, T("Preparar pasta"),
        T("Esta pasta ainda não foi preparada pelo Nodo.\n\nVou instalar nela:\n• nodo.js (biblioteca para o site usar o banco)\n• nodo.conf (chave de segurança)\n• nodo-dados (o banco de dados)\n• e ligar o nodo.js ao seu index.html (com cópia de segurança)\n\nContinuar?"));
      if (b != QMessageBox::Yes) { restaurar(); return; }
      auto r = integrarPasta(S(pastaAbrir), "", false);
      if (!r.ok) { QMessageBox::critical(this, "Nodo", Q(r.msg)); restaurar(); return; }
      e = openProject(S(pastaAbrir));
      if (e.empty()) QMessageBox::information(this, T("Pasta pronta"), T("Instalado: ") + Q(r.msg) + T("\n\nAgora é só clicar em Ligar."));
    }
    if (!e.empty()) { QMessageBox::warning(this, "Nodo", e == "NOCONF" ? T("Não consegui preparar a pasta.") : Q(e)); restaurar(); return; }
    projeto = pastaAbrir;
    projetoOk = true;
    loadStore();
    QSettings().setValue("projeto", pastaAbrir);
    { auto rg = proj::adicionar(S(pastaAbrir)); if (rg.ok) { openProject(S(pastaAbrir)); loadStore(); } }  // garante o projeto na lista de projetos, com porta que nao colide
    colAtual.clear();
    pend.clear();
    preencherConfig();
    logln("[app] projeto aberto: " + S(pastaAbrir) + " (nodo.conf detectado localmente; chave carregada sem entrada manual)");
    if (was) { string er = startServer(); if (!er.empty()) QMessageBox::warning(this, "Nodo", Q(er)); }
    atualizarBanco(true);
    atualizarTudo();
  }
  void alternar() {
    if (!projetoOk) { escolherPasta(); return; }
    if (rodando) stopServer();
    else { string e = startServer(); if (!e.empty()) QMessageBox::critical(this, "Nodo", Q(e)); }
    atualizarTudo();
  }

  // ================================================================ projetos (varios sites ao mesmo tempo)
  bool pjAtivo(const proj::Item& it) { return projetoOk && proj::canon(S(projeto)) == it.pasta; }
  bool pjLigado(const proj::Item& it) { return pjAtivo(it) ? (bool)rodando : it.rodando; }
  const proj::Item* pjSel() {
    int r = pjLista ? pjLista->currentRow() : -1;
    return (r >= 0 && r < (int)pjItens.size()) ? &pjItens[r] : nullptr;
  }
  void pjFundo(function<proj::Res()> f) {  // liga/desliga em outra thread para a janela nao travar
    if (pjOcupado) return;
    pjOcupado = true;
    pjInfo->setText(T("Trabalhando…"));
    atualizarBotoesProjetos();
    std::thread([this, f] {
      proj::Res r = f();
      QMetaObject::invokeMethod(this, [this, r] {
        pjOcupado = false;
        pjInfo->setText(Q(r.msg));
        pjSig.clear();
        atualizarProjetos();
        atualizarTudo();
      }, Qt::QueuedConnection);
    }).detach();
  }
  void atualizarBotoesProjetos() {
    const proj::Item* it = pjSel();
    bool tem = it != nullptr, on = tem && pjLigado(*it);
    if (pjBLigar) pjBLigar->setEnabled(tem && !on && !pjOcupado && it->confOk);
    if (pjBDesl) pjBDesl->setEnabled(tem && on && !pjOcupado);
    if (pjBSite) pjBSite->setEnabled(tem && on);
    if (pjBPainel) pjBPainel->setEnabled(tem && on);
    if (pjBUsar) pjBUsar->setEnabled(tem && !pjAtivo(*it) && !pjOcupado);
    if (pjBPorta) pjBPorta->setEnabled(tem && !on && !pjOcupado);
    if (pjBRem) pjBRem->setEnabled(tem && !on && !pjOcupado);
  }
  void atualizarProjetos() {
    if (!pjLista) return;
    auto itens = proj::carregar();
    QString sig;
    for (auto& it : itens) sig += Q(it.nome) + "|" + QString::number(it.porta) + "|" + (pjLigado(it) ? "1" : "0") + "|" + (it.confOk ? "1" : "0") + "|" + (pjAtivo(it) ? "1" : "0") + ";";
    pjItens = itens;
    if (sig != pjSig) {
      pjSig = sig;
      QString sel = pjLista->currentItem() ? pjLista->currentItem()->data(Qt::UserRole).toString() : QString();
      QSignalBlocker bloq(pjLista);
      pjLista->clear();
      int linhaSel = -1, n = 0;
      for (size_t i = 0; i < pjItens.size(); i++) {
        auto& it = pjItens[i];
        bool on = pjLigado(it);
        if (on) n++;
        QString estado = !it.confOk ? T("sem nodo.conf") : on ? T("ligado") : T("desligado");
        QString extra = pjAtivo(it) ? T("  ·  aberto no app") : QString();
        QString txt = T(on ? "● " : "○ ") + Q(it.nome) + "   " + T("porta ") + QString::number(it.porta) + "   " + estado + extra + "\n     " + Q(it.pasta);
        auto* li = new QListWidgetItem(txt);
        li->setData(Qt::UserRole, Q(it.nome));
        li->setSizeHint(QSize(0, 56));
        if (on) li->setForeground(QColor("#12945a"));
        pjLista->addItem(li);
        if (Q(it.nome) == sel) linhaSel = (int)i;
      }
      if (linhaSel < 0 && !pjItens.empty()) linhaSel = 0;
      if (linhaSel >= 0) pjLista->setCurrentRow(linhaSel);
      if (!pjOcupado) pjInfo->setText(pjItens.empty() ? T("Nenhum projeto ainda. Clique em “Adicionar pasta…” e escolha a pasta de um site.")
                                                     : QString::number(n) + T(" ligado(s) de ") + QString::number(pjItens.size()) + T("."));
    }
    atualizarBotoesProjetos();
  }
  void pjAdicionar() {
    QString d = QFileDialog::getExistingDirectory(this, T("Escolha a pasta do site"), QDir::homePath());
    if (d.isEmpty()) return;
    auto r = proj::adicionar(S(d));
    QString m = Q(r.msg);
    if (!r.chave.empty()) m += T("\n\nChave de administrador deste projeto (guarde):\n") + Q(r.chave) + T("\n\nEla fica só no arquivo nodo.conf da pasta; não envie para o Git nem coloque no site.");
    if (r.ok) QMessageBox::information(this, T("Projeto adicionado"), m); else QMessageBox::warning(this, T("Projeto"), m);
    pjSig.clear();
    atualizarProjetos();
  }
  void pjLigar() {
    const proj::Item* it = pjSel();
    if (!it) return;
    if (pjAtivo(*it)) { if (!rodando) alternar(); pjSig.clear(); atualizarProjetos(); return; }
    string nome = it->nome, cli = cliPathApp();
    pjFundo([nome, cli] { return proj::iniciar(nome, cli); });
  }
  void pjDesligar() {
    const proj::Item* it = pjSel();
    if (!it) return;
    if (pjAtivo(*it)) { if (rodando) alternar(); pjSig.clear(); atualizarProjetos(); return; }
    string nome = it->nome, cli = cliPathApp();
    pjFundo([nome, cli] { return proj::parar(nome, cli); });
  }
  void pjAbrirNoNavegador(bool painel) {
    const proj::Item* it = pjSel();
    if (it) QDesktopServices::openUrl(QUrl(QString("http://localhost:%1").arg(it->porta) + (painel ? QString("/admin") : QString("/"))));
  }
  // "Usar no app": as outras abas (banco, rede, monitor...) mostram UM projeto por vez. O que estava aberto continua ligado em processo proprio.
  void pjUsar() {
    const proj::Item* sel = pjSel();
    if (!sel || pjAtivo(*sel) || pjOcupado) return;
    proj::Item novo = *sel;
    string cli = cliPathApp();
    QString velho = projeto;
    bool velhoLigado = projetoOk && rodando, novoLigado = novo.rodando;
    if (velhoLigado) stopServer();
    if (novoLigado) {
      auto r = proj::parar(novo.nome, cli);
      if (!r.ok) { if (velhoLigado) startServer(); QMessageBox::warning(this, "Nodo", Q(r.msg)); return; }
    }
    abrirProjeto(Q(novo.pasta));
    bool abriu = projetoOk && proj::canon(S(projeto)) == novo.pasta;
    function<proj::Res()> depois;
    if (velhoLigado && !velho.isEmpty()) { string v = S(velho); depois = [v, cli] { return proj::iniciar(v, cli); }; }
    if (abriu) { if (novoLigado) alternar(); }
    else if (novoLigado) { string n = novo.nome; auto antes = depois; depois = [n, cli, antes] { if (antes) antes(); return proj::iniciar(n, cli); }; }
    if (depois) pjFundo(depois);
    pjSig.clear();
    atualizarProjetos();
  }
  void pjTrocarPorta() {
    const proj::Item* it = pjSel();
    if (!it || pjLigado(*it)) { QMessageBox::information(this, "Nodo", T("Desligue o projeto antes de trocar a porta.")); return; }
    string nome = it->nome;
    bool ativo = pjAtivo(*it);
    bool ok = false;
    int p = QInputDialog::getInt(this, T("Trocar porta"), T("Nova porta para ") + Q(nome) + ":", it->porta, 1024, 65535, 1, &ok);
    if (!ok) return;
    auto r = proj::definirPorta(nome, p);
    if (r.ok && ativo) { openProject(S(projeto)); preencherConfig(); }
    pjInfo->setText(Q(r.msg));
    pjSig.clear();
    atualizarProjetos();
    pjInfo->setText(Q(r.msg));
  }
  void pjRemover() {
    const proj::Item* it = pjSel();
    if (!it) return;
    if (pjLigado(*it)) { QMessageBox::information(this, "Nodo", T("Desligue o projeto antes de tirá-lo da lista.")); return; }
    string nome = it->nome;
    if (QMessageBox::question(this, T("Tirar da lista"), T("Tirar “") + Q(nome) + T("” da lista? Os arquivos da pasta continuam no disco.")) != QMessageBox::Yes) return;
    auto r = proj::remover(nome);
    pjSig.clear();
    atualizarProjetos();
    pjInfo->setText(Q(r.msg));
  }
  void pjTodos(bool ligar) {
    string cli = cliPathApp();
    vector<string> nomes;
    for (auto& it : pjItens) {
      if (pjAtivo(it)) { if (ligar != (bool)rodando) alternar(); }
      else if (it.rodando != ligar && it.confOk) nomes.push_back(it.nome);
    }
    if (nomes.empty()) { pjSig.clear(); atualizarProjetos(); return; }
    pjFundo([nomes, cli, ligar] {
      proj::Res tot;
      tot.ok = true;
      int ruins = 0;
      for (auto& n : nomes) { auto r = ligar ? proj::iniciar(n, cli) : proj::parar(n, cli); if (!r.ok) { ruins++; tot.msg += r.msg + "\n"; } }
      if (!ruins) tot.msg = ligar ? "Projetos ligados." : "Projetos desligados.";
      return tot;
    });
  }
  QWidget* paginaProjetos() {
    QVBoxLayout* v;
    auto* w = pagina(v);
    v->addWidget(h1(T("Projetos")));
    v->addWidget(muted(T("Cada site roda no seu próprio processo, com porta e chave próprias, então dá para ligar vários ao mesmo tempo. Os sites ligados continuam funcionando quando você fecha este app.")));
    auto* c = card();
    auto* cv = new QVBoxLayout(c);
    cv->setContentsMargins(14, 14, 14, 14);
    pjLista = new QListWidget;
    pjLista->setSpacing(3);
    cv->addWidget(pjLista, 1);
    v->addWidget(c, 1);
    pjInfo = muted(QString());
    v->addWidget(pjInfo);
    auto* l1 = new QHBoxLayout;
    auto* bAdd = primary(T("Adicionar pasta…"));
    pjBLigar = flat(T("Ligar")); pjBDesl = flat(T("Desligar")); pjBSite = flat(T("Abrir site")); pjBPainel = flat(T("Abrir painel"));
    for (QPushButton* b : {bAdd, pjBLigar, pjBDesl, pjBSite, pjBPainel}) l1->addWidget(b);
    l1->addStretch();
    v->addLayout(l1);
    auto* l2 = new QHBoxLayout;
    pjBUsar = flat(T("Usar no app")); pjBPorta = flat(T("Trocar porta…")); pjBRem = flat(T("Tirar da lista"));
    auto* bTodos = flat(T("Ligar todos")); auto* bNenhum = flat(T("Desligar todos"));
    for (QPushButton* b : {pjBUsar, pjBPorta, pjBRem, bTodos, bNenhum}) l2->addWidget(b);
    l2->addStretch();
    v->addLayout(l2);
    auto* chk = new QCheckBox(T("Desligar todos os sites ao fechar o app"));
    chk->setChecked(QSettings().value("preferencias/desligarTodosAoSair", false).toBool());
    connect(chk, &QCheckBox::toggled, this, [](bool b) { QSettings().setValue("preferencias/desligarTodosAoSair", b); });
    v->addWidget(chk);
    connect(bAdd, &QPushButton::clicked, this, [this] { pjAdicionar(); });
    connect(pjBLigar, &QPushButton::clicked, this, [this] { pjLigar(); });
    connect(pjBDesl, &QPushButton::clicked, this, [this] { pjDesligar(); });
    connect(pjBSite, &QPushButton::clicked, this, [this] { pjAbrirNoNavegador(false); });
    connect(pjBPainel, &QPushButton::clicked, this, [this] { pjAbrirNoNavegador(true); });
    connect(pjBUsar, &QPushButton::clicked, this, [this] { pjUsar(); });
    connect(pjBPorta, &QPushButton::clicked, this, [this] { pjTrocarPorta(); });
    connect(pjBRem, &QPushButton::clicked, this, [this] { pjRemover(); });
    connect(bTodos, &QPushButton::clicked, this, [this] { pjTodos(true); });
    connect(bNenhum, &QPushButton::clicked, this, [this] { pjTodos(false); });
    connect(pjLista, &QListWidget::currentRowChanged, this, [this](int) { atualizarBotoesProjetos(); });
    connect(pjLista, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) { pjAbrirNoNavegador(true); });
    atualizarBotoesProjetos();
    return w;
  }

  // ================================================================ atualizacao periodica
  void atualizarTudo() {
    atualizarInicio();
    atualizarRede();
    atualizarLog();
    if (pages->currentIndex() == 1) atualizarProjetos();
    if (pages->currentIndex() == 2) atualizarBanco(false);
    if (pages->currentIndex() == 7) atualizarMonitor();
  }
  void atualizarInicio() {
    bool on = rodando;
    if (!projetoOk) {
      dot->setStyleSheet("font-size: 34px; color: #9ca3af;");
      stTitulo->setText(T("Nenhum projeto escolhido"));
      stSub->setText(T("Escolha a pasta do seu site para começar. O Nodo prepara tudo nela."));
      btnLigar->setText(T("Ligar"));
      btnLigar->setEnabled(false);
    } else {
      dot->setStyleSheet(QString("font-size: 34px; color: %1;").arg(on ? "#16a34a" : "#9ca3af"));
      stTitulo->setText(on ? T("Servidor ligado") : T("Servidor desligado"));
      stSub->setText(T("Projeto: ") + projeto + (on ? QString(T(" · porta %1")).arg(cfg.porta) : QString()));
      btnLigar->setEnabled(true);
      btnLigar->setText(on ? T("Desligar") : T("Ligar"));
    }
    btnLigar->setObjectName(on ? "danger" : "primary");
    btnLigar->setStyleSheet(on ? "background:#dc2626;color:white;border:0;border-radius:8px;padding:9px 20px;font-weight:600;"
                               : (projetoOk ? "background:#4f46e5;color:white;border:0;border-radius:8px;padding:9px 20px;font-weight:600;"
                                            : "background:#9ca3af;color:white;border:0;border-radius:8px;padding:9px 20px;font-weight:600;"));
    btnAbrirSite->setEnabled(on);
    btnAbrirPainel->setEnabled(on);
    btnCopiarEnd->setEnabled(on);
    navStatus->setText(on ? T("<span style='color:#16a34a'>●</span> Servidor ligado") : T("<span style='color:#9ca3af'>●</span> Desligado"));
    if (tray) { tray->setIcon(appIcon(on)); tray->setToolTip(on ? "Nodo: ligado" : "Nodo: desligado"); }
    if (actLigar) actLigar->setText(on ? T("Desligar servidor") : T("Ligar servidor"));
    // enderecos e codigo de exemplo
    auto ips = localIps();
    string sig = (on ? "1" : "0") + to_string(cfg.porta) + cfg.escutar;
    for (auto& ip : ips) sig += ip;
    if (sig != sigEnd) {
      sigEnd = sig;
      enderecos->clear();
      if (on) {
        auto add = [&](const QString& rot, const QString& url) {
          auto* it = new QListWidgetItem(rot + "   " + url);
          it->setData(Qt::UserRole, url);
          enderecos->addItem(it);
        };
        add(T("Neste PC:"), QString("http://localhost:%1").arg(cfg.porta));
        if (cfg.escutar != "127.0.0.1") for (auto& ip : ips) add(T("Na rede:"), QString("http://%1:%2").arg(Q(ip)).arg(cfg.porta));
        else enderecos->addItem(T("Acesso pela rede desativado (veja Configurações)"));
      } else enderecos->addItem(T("Ligue o servidor para ver os endereços."));
      QString base = (!ips.empty() && cfg.escutar != "127.0.0.1") ? Q(ips[0]) : "localhost";
      snippet->setPlainText(QString(
        "<script src=\"http://%1:%2/nodo.js\"></script>\n<script>\n  // guardar\n  Nodo.colecao(\"contatos\").criar({ nome: \"Maria\", mensagem: \"Ol\xc3\xa1!\" });\n"
        "  // ler\n  Nodo.colecao(\"produtos\").listar().then(console.log);\n</script>").arg(base).arg(cfg.porta));
    }
  }
  void atualizarRede() {
    auto ips = localIps();
    QString info = T("Código deste PC: ") + Q(meuId.empty() ? "-" : meuId) + "\n";
    if (rodando) { for (auto& ip : ips) info += QString("http://%1:%2\n").arg(Q(ip)).arg(cfg.porta); }
    else info += T("Servidor desligado.");
    redeInfo->setText(info.trimmed());
    auto ps = peersList();
    redeCount->setText(ps.empty() ? T("Nenhum outro PC conectado") : QString(T("%1 PC(s) conectado(s)")).arg(ps.size()));
    if ((size_t)peersW->count() != ps.size() || (!ps.empty() && peersW->item(0)->text() != Q(ps[0]))) {
      peersW->clear();
      for (auto& p : ps) peersW->addItem(Q(p));
    }
    if (projetoOk && chaveEd->text() != Q(cfg.chave)) chaveEd->setText(Q(cfg.chave));
  }
  void atualizarLog() {
    auto l = logCopy();
    string sig = to_string(l.size()) + (l.empty() ? "" : l.back());
    if (sig == sigLog) return;
    sigLog = sig;
    string tudo;
    for (auto& x : l) tudo += x + "\n";
    logW->setPlainText(Q(tudo));
    logW->verticalScrollBar()->setValue(logW->verticalScrollBar()->maximum());
  }
};

// Clique no icone do Dock (macOS) reabre a janela escondida
struct Reabrir : QObject {
  Janela* j;
  explicit Reabrir(Janela* w) : j(w) {}
  bool eventFilter(QObject*, QEvent* e) override {
    if (e->type() == QEvent::ApplicationActivate && !j->isVisible()) j->mostrar();
    return false;
  }
};

int main(int argc, char** argv) {
#ifndef _WIN32
  signal(SIGPIPE, SIG_IGN);
#endif
  QApplication app(argc, argv);
  app.setApplicationName("Nodo");
  app.setOrganizationName("Loop Life in Server");
  app.setApplicationVersion("3.0.0");
  app.setStyle(QStyleFactory::create("Fusion"));
  app.setStyleSheet(qss(false));
  app.setQuitOnLastWindowClosed(false);
  app.setWindowIcon(appIcon(true));
  deployHook = [] { return pub::deploy(); };
  QPixmap splashPix(520, 300); splashPix.fill(QColor("#f5f5f7"));
  { QPainter p(&splashPix); p.setRenderHint(QPainter::Antialiasing); p.setBrush(QColor("#1d6bf0")); p.setPen(Qt::NoPen); p.drawRoundedRect(208, 38, 104, 104, 28, 28); p.setPen(Qt::white); QFont f; f.setBold(true); f.setPixelSize(66); p.setFont(f); p.drawText(QRect(208, 38, 104, 104), Qt::AlignCenter, "N"); p.setPen(QColor("#1d1d1f")); f.setPixelSize(25); p.setFont(f); p.drawText(QRect(20, 160, 480, 38), Qt::AlignCenter, "Nodo"); p.setPen(QColor("#66666b")); f.setPixelSize(13); f.setBold(false); p.setFont(f); p.drawText(QRect(20, 198, 480, 28), Qt::AlignCenter, "by Loop Life in Server"); p.drawText(QRect(20, 238, 480, 24), Qt::AlignCenter, "Iniciando serviços · Sistema v3.0.0"); }
  QSplashScreen splash(splashPix); splash.show(); app.processEvents();
  Janela w;
  Reabrir ro(&w);
  app.installEventFilter(&ro);
  QObject::connect(&app, &QApplication::aboutToQuit, [] {
    stopServer();  // o site aberto neste app; os outros projetos continuam em processos proprios
    if (QSettings().value("preferencias/desligarTodosAoSair", false).toBool()) {
      string cli = cliPathApp();
      for (auto& it : proj::carregar()) if (it.rodando) proj::parar(it.nome, cli);
    }
  });
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
  QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, &app, [&app, &w] {
    w.vidro->escuro = false;
    app.setStyleSheet(qss(false));
    w.vidro->update();
  });
#endif

  QString proj = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QSettings().value("projeto").toString();
  w.show(); splash.finish(&w);
  if (!proj.isEmpty() && QDir(proj).exists() && (argc > 1 || QFile::exists(proj + "/nodo.conf"))) {
    w.abrirProjeto(QDir(proj).absolutePath());
    if (w.projetoOk && (argc > 1 || QSettings().value("auto", false).toBool())) w.alternar();
  }
  if (const char* d = getenv("NODO_SHOT")) {  // modo de teste: salva uma imagem de cada aba e sai
    QTimer::singleShot(1500, [&w, d] {
      for (int i = 0; i < w.nav->count(); i++) {
        w.nav->setCurrentRow(i);
        qApp->processEvents();
        w.grab().save(QString("%1/aba%2.png").arg(d).arg(i));
      }
      w.sair = true;
      w.close();
    });
  }
  return app.exec();
}
