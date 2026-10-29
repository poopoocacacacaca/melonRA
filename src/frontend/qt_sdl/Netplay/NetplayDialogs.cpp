/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#include <algorithm>

#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkInterface>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSpinBox>
#include <QTextCursor>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "NetplayDialogs.h"
#include "NetplayRunner.h"
#include "Config.h"
#include "Window.h"
#include "GameList/GameList.h"

namespace Netplay
{

static Config::Table netplayConfig()
{
    return Config::GetGlobalTable().GetTable("Netplay");
}

static QLabel* plainLabel(const QString& text, QWidget* parent)
{
    QLabel* l = new QLabel(parent);
    l->setTextFormat(Qt::PlainText);
    l->setText(text);
    l->setWordWrap(true);
    // wrapped text needs its height reserved, or the layout squeezes it
    l->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::MinimumExpanding);
    return l;
}

// a message box that never treats its text as HTML (it may hold names from
// other players)
static void plainMessage(QWidget* parent, QMessageBox::Icon icon, const QString& title, const QString& text)
{
    QMessageBox box(icon, title, QString(), QMessageBox::Ok, parent);
    box.setTextFormat(Qt::PlainText);
    box.setText(text);
    box.exec();
}

QString saveWarning()
{
    return QCoreApplication::translate("Netplay",
        "If the game is saving at that moment, the save can be left incomplete or damaged, the same "
        "as switching a DS off while it saves. Leave when the game isn't saving.");
}

// a wrapped explanation that keeps all of its lines at this width
static void reserveLines(QLabel* l, int width)
{
    l->setMinimumWidth(width);
    l->setMinimumHeight(l->heightForWidth(width));
}

// what netplay can and can't do for a game, shown before hosting or joining
static QWidget* gameNotice(QWidget* parent, int width)
{
    QGroupBox* box = new QGroupBox(QCoreApplication::translate("Netplay", "Which games work"), parent);
    QLabel* text = plainLabel(QCoreApplication::translate("Netplay",
        "Netplay works with games that have local wireless multiplayer, where every player has the game. "
        "It doesn't add multiplayer to games without it, and Download Play (single-card play) isn't "
        "supported.\n\n"
        "Once the game starts, connect inside the game as on two DS consoles side by side: one player "
        "creates a group in the game's wireless mode and the others join it. Who does which doesn't "
        "depend on who hosted the netplay room."), box);
    reserveLines(text, width - 24);
    QVBoxLayout* l = new QVBoxLayout(box);
    l->addWidget(text);
    return box;
}

static QString defaultName()
{
    QString name = netplayConfig().GetQString("Name");
    if (name.isEmpty())
        name = Config::GetLocalTable(0).GetQString("Firmware.Username");
    return name.left(kMaxName);
}

// --- hosting ---

HostDialog::HostDialog(MainWindow* parent) : QDialog(parent), Win(parent)
{
    setWindowTitle(tr("Host Netplay Game"));
    setAttribute(Qt::WA_DeleteOnClose, false);

    Config::Table cfg = netplayConfig();

    Name = new QLineEdit(defaultName(), this);
    Name->setMaxLength(kMaxName);

    Game = new QComboBox(this);
    QPushButton* browseBtn = new QPushButton(tr("Browse..."), this);
    connect(browseBtn, &QPushButton::clicked, this, &HostDialog::browse);

    std::vector<GameList::Game> games = GameList::scannedGames();
    std::sort(games.begin(), games.end(), [](const GameList::Game& a, const GameList::Game& b)
    {
        return a.title.compare(b.title, Qt::CaseInsensitive) < 0;
    });
    for (const GameList::Game& g : games)
    {
        if (g.notRom || g.platform == "DSi") continue;
        QString label = g.title.isEmpty() ? g.fileName : g.title;
        if (!g.gameCode.isEmpty()) label += QString(" (%1)").arg(g.gameCode);
        Game->addItem(label);
        Paths.append(g.path);
    }

    Port = new QSpinBox(this);
    Port->setRange(1024, 65535);
    Port->setValue(cfg.GetInt("Port") > 0 ? cfg.GetInt("Port") : kDefaultPort);

    Delay = new QSpinBox(this);
    Delay->setRange(1, kMaxInputDelay);
    Delay->setValue(cfg.GetInt("InputDelay") > 0 ? cfg.GetInt("InputDelay") : 3);
    Delay->setSuffix(tr(" frames"));
    Delay->setToolTip(tr("How far ahead inputs are sent. Raise it if the game stutters; each frame adds "
                         "about 17 ms of delay to every player's buttons."));

    QHBoxLayout* gameRow = new QHBoxLayout();
    gameRow->addWidget(Game, 1);
    gameRow->addWidget(browseBtn);

    QFormLayout* form = new QFormLayout();
    form->addRow(tr("Your name:"), Name);
    form->addRow(tr("Game:"), gameRow);
    form->addRow(tr("Port (UDP):"), Port);
    form->addRow(tr("Input delay:"), Delay);

    QLabel* help = plainLabel(tr(
        "Players join with your IP address and this port. For players outside your home network, "
        "forward this UDP port on your router to this computer. Your IP address is visible to "
        "everyone who joins.\n\n"
        "Everyone needs the same melonRA version and an identical copy of the game. Each player "
        "brings their own save; yours stays on this computer and is updated as you play."), this);
    help->setStyleSheet("color: palette(placeholder-text);");
    reserveLines(help, 460);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Host"));
    connect(buttons, &QDialogButtonBox::accepted, this, &HostDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &HostDialog::reject);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setSizeConstraint(QLayout::SetMinimumSize);
    layout->addWidget(gameNotice(this, 460));
    layout->addLayout(form);
    layout->addWidget(help);
    layout->addWidget(buttons);
    setMinimumWidth(480);
}

void HostDialog::browse()
{
    QString file = QFileDialog::getOpenFileName(this, tr("Choose a DS game"), QString(),
                                                tr("DS ROMs (*.nds *.srl *.zst);;All files (*)"));
    if (file.isEmpty()) return;

    Game->addItem(QFileInfo(file).fileName());
    Paths.append(file);
    Game->setCurrentIndex(Game->count() - 1);
}

void HostDialog::selectGame(const QString& path)
{
    int i = Paths.indexOf(path);
    if (i < 0)
    {
        Game->addItem(QFileInfo(path).fileName());
        Paths.append(path);
        i = Paths.size() - 1;
    }
    Game->setCurrentIndex(i);
}

QString HostDialog::name() const { return Name->text().trimmed(); }
u16 HostDialog::port() const { return (u16)Port->value(); }
int HostDialog::inputDelay() const { return Delay->value(); }

void HostDialog::accept()
{
    int i = Game->currentIndex();
    if (i < 0 || i >= Paths.size())
    {
        QMessageBox::warning(this, tr("Host Netplay Game"), tr("Choose a game to play."));
        return;
    }
    ROMPath = Win->splitArchivePath(Paths[i], true);

    Config::Table cfg = netplayConfig();
    cfg.SetQString("Name", name());
    cfg.SetInt("Port", port());
    cfg.SetInt("InputDelay", inputDelay());
    Config::Save();

    QDialog::accept();
}

// --- joining ---

JoinDialog::JoinDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Join Netplay Game"));

    Config::Table cfg = netplayConfig();

    Name = new QLineEdit(defaultName(), this);
    Name->setMaxLength(kMaxName);

    Address = new QLineEdit(cfg.GetQString("Address"), this);
    Address->setMaxLength(253);
    Address->setPlaceholderText(tr("IP address or host name"));

    Port = new QSpinBox(this);
    Port->setRange(1024, 65535);
    Port->setValue(cfg.GetInt("JoinPort") > 0 ? cfg.GetInt("JoinPort") : kDefaultPort);

    QFormLayout* form = new QFormLayout();
    form->addRow(tr("Your name:"), Name);
    form->addRow(tr("Host address:"), Address);
    form->addRow(tr("Port (UDP):"), Port);

    QLabel* help = plainLabel(tr(
        "Ask the host for their IP address and port. You need the same melonRA version and an "
        "identical copy of the game; melonRA looks for it in your game list."), this);
    help->setStyleSheet("color: palette(placeholder-text);");
    reserveLines(help, 460);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Join"));
    connect(buttons, &QDialogButtonBox::accepted, this, &JoinDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &JoinDialog::reject);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setSizeConstraint(QLayout::SetMinimumSize);
    layout->addWidget(gameNotice(this, 460));
    layout->addLayout(form);
    layout->addWidget(help);
    layout->addWidget(buttons);
    setMinimumWidth(480);
}

QString JoinDialog::name() const { return Name->text().trimmed(); }
QString JoinDialog::address() const { return Address->text().trimmed(); }
u16 JoinDialog::port() const { return (u16)Port->value(); }

void JoinDialog::accept()
{
    if (address().isEmpty())
    {
        QMessageBox::warning(this, tr("Join Netplay Game"), tr("Enter the host's address."));
        return;
    }

    Config::Table cfg = netplayConfig();
    cfg.SetQString("Name", name());
    cfg.SetQString("Address", address());
    cfg.SetInt("JoinPort", port());
    Config::Save();

    QDialog::accept();
}

// --- the room ---

RoomDialog* RoomDialog::Current = nullptr;

RoomDialog* RoomDialog::open(MainWindow* win, Runner* runner)
{
    if (Current)
    {
        Current->showNormal();
        Current->raise();
        Current->activateWindow();
        return Current;
    }
    Current = new RoomDialog(win, runner);
    Current->show();
    return Current;
}

// a window of its own (not kept above the main window), so it can sit
// minimized on the taskbar while the game runs
RoomDialog::RoomDialog(MainWindow* win, Runner* runner) : QDialog(nullptr), Win(win), Run(runner)
{
    setWindowTitle(tr("Netplay Room"));
    setWindowIcon(win->windowIcon());
    setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                   Qt::WindowMinimizeButtonHint | Qt::WindowCloseButtonHint);
    setAttribute(Qt::WA_DeleteOnClose);
    Session* s = runner->session();

    Game = plainLabel(QString(), this);
    QFont f = Game->font();
    f.setBold(true);
    f.setPointSizeF(f.pointSizeF() * 1.25);
    Game->setFont(f);
    Status = plainLabel(QString(), this);
    Note = plainLabel(QString(), this);
    Note->setStyleSheet("color: palette(placeholder-text);");
    Note->hide();
    GameHint = plainLabel(tr("In the game, open its wireless multiplayer mode: one player creates a group "
                             "and the others join it, whoever hosted here."), this);
    GameHint->setStyleSheet("color: palette(placeholder-text);");

    // players
    Players = new QTreeWidget(this);
    Players->setColumnCount(3);
    Players->setHeaderLabels({tr("Player"), tr("Status"), tr("Ping")});
    Players->setRootIsDecorated(false);
    Players->setSelectionMode(QAbstractItemView::SingleSelection);
    Players->setStyleSheet("QTreeWidget::item { padding: 3px 2px; }");
    // room for everyone a room can hold, and no more
    const int rowH = Players->fontMetrics().height() + 8;
    Players->setFixedHeight(Players->header()->sizeHint().height() + kMaxPlayers * rowH + 2 * Players->frameWidth() + 2);
    Players->header()->setStretchLastSection(false);
    Players->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    Players->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    Players->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);

    RemoveBtn = new QPushButton(tr("Remove Player"), this);
    RemoveBtn->setVisible(s->isHost());
    RemoveBtn->setEnabled(false);
    connect(Players, &QTreeWidget::itemSelectionChanged, this, &RoomDialog::refresh);
    connect(RemoveBtn, &QPushButton::clicked, this, [this]
    {
        if (!Run) return;
        QTreeWidgetItem* item = Players->currentItem();
        if (item) Run->session()->removePlayer(item->data(0, Qt::UserRole).toInt());
    });

    // input delay
    QHBoxLayout* delayRow = new QHBoxLayout();
    delayRow->addWidget(new QLabel(tr("Input delay:"), this));
    if (s->isHost())
    {
        DelaySpin = new QSpinBox(this);
        DelaySpin->setRange(1, kMaxInputDelay);
        DelaySpin->setSuffix(tr(" frames"));
        DelaySpin->setValue(s->inputDelay());
        connect(DelaySpin, &QSpinBox::valueChanged, this, [this](int v)
        {
            if (Run && v != Run->session()->inputDelay()) Run->session()->setInputDelay(v);
        });
        delayRow->addWidget(DelaySpin);
    }
    DelayInfo = plainLabel(QString(), this);
    delayRow->addWidget(DelayInfo, 1);
    if (s->isHost())
    {
        UseSuggested = new QPushButton(this);
        connect(UseSuggested, &QPushButton::clicked, this, [this]
        {
            if (Run) DelaySpin->setValue(Run->session()->suggestedDelay());
        });
        delayRow->addWidget(UseSuggested);
    }
    QWidget* delayHelp = plainLabel(tr("How many frames ahead everyone's buttons are sent. Higher is smoother on "
                                       "slow connections; lower reacts faster. It can be changed during the game."),
                                    this);
    delayHelp->setStyleSheet("color: palette(placeholder-text);");

    // chat
    ChatLog = new QPlainTextEdit(this);
    ChatLog->setReadOnly(true);
    ChatLog->setMaximumBlockCount(500);
    ChatLog->setMinimumHeight(90);
    ChatInput = new QLineEdit(this);
    ChatInput->setMaxLength(kMaxChat);
    ChatInput->setPlaceholderText(tr("Message everyone in the room"));
    SendBtn = new QPushButton(tr("Send"), this);
    connect(ChatInput, &QLineEdit::returnPressed, this, &RoomDialog::sendChat);
    connect(SendBtn, &QPushButton::clicked, this, &RoomDialog::sendChat);
    QHBoxLayout* chatRow = new QHBoxLayout();
    chatRow->addWidget(ChatInput, 1);
    chatRow->addWidget(SendBtn);

    StartBtn = new QPushButton(tr("Start Game"), this);
    LeaveBtn = new QPushButton(s->isHost() ? tr("Close Room") : tr("Leave"), this);
    StartBtn->setVisible(s->isHost());
    StartBtn->setDefault(false);
    StartBtn->setAutoDefault(false);
    connect(StartBtn, &QPushButton::clicked, this, [this]
    {
        if (Run) Run->startGame();
    });
    connect(LeaveBtn, &QPushButton::clicked, this, &RoomDialog::leaveSession);

    QHBoxLayout* buttons = new QHBoxLayout();
    buttons->addWidget(RemoveBtn);
    buttons->addStretch(1);
    buttons->addWidget(StartBtn);
    buttons->addWidget(LeaveBtn);

    PlayersBox = new QGroupBox(tr("Players"), this);
    QVBoxLayout* pl = new QVBoxLayout(PlayersBox);
    pl->addWidget(Players, 1);
    pl->addLayout(delayRow);
    pl->addWidget(delayHelp);

    QGroupBox* chatBox = new QGroupBox(tr("Chat"), this);
    QVBoxLayout* cl = new QVBoxLayout(chatBox);
    cl->addWidget(ChatLog, 1);
    cl->addLayout(chatRow);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(Game);
    layout->addWidget(Status);
    layout->addWidget(GameHint);
    AddressBox = makeAddressBox();
    layout->addWidget(AddressBox);
    layout->addWidget(PlayersBox);
    layout->addWidget(chatBox, 1);
    layout->addWidget(Note);
    layout->addLayout(buttons);
    setMinimumWidth(500);
    resize(540, 620);

    connect(s, &Session::rosterChanged, this, &RoomDialog::refresh);
    connect(s, &Session::pingsChanged, this, &RoomDialog::refresh);
    connect(s, &Session::delayChanged, this, &RoomDialog::refreshDelay);
    connect(s, &Session::notice, this, &RoomDialog::onNotice);
    connect(s, &Session::chat, this, &RoomDialog::onChat);
    connect(s, &Session::started, this, &RoomDialog::refresh);
    connect(s, &Session::go, this, &RoomDialog::onGo);
    connect(s, &Session::desync, this, &RoomDialog::onDesync);
    connect(runner, &Runner::needROM, this, &RoomDialog::onNeedROM);
    connect(runner, &Runner::finished, this, &RoomDialog::onFinished);

    refresh();
    refreshDelay();
}

// where the others connect (host), or where this player is connected
QWidget* RoomDialog::makeAddressBox()
{
    Session* s = Run->session();
    QGroupBox* box = new QGroupBox(s->isHost() ? tr("How players join") : tr("Connection"), this);
    QGridLayout* g = new QGridLayout(box);

    if (!s->isHost())
    {
        g->addWidget(plainLabel(tr("Connected to %1, port %2.").arg(s->address()).arg(s->port()), box), 0, 0);
        return box;
    }

    // the first private IPv4 address is usually the one on the home network
    QString local;
    for (const QHostAddress& a : QNetworkInterface::allAddresses())
    {
        if (a.protocol() != QAbstractSocket::IPv4Protocol || a.isLoopback()) continue;
        quint32 ip = a.toIPv4Address();
        bool priv = (ip >> 24) == 10 || (ip >> 20) == (172 << 4 | 1) || (ip >> 16) == (192 << 8 | 168);
        if (priv) { local = a.toString(); break; }
        if (local.isEmpty()) local = a.toString();
    }

    int row = 0;
    if (!local.isEmpty())
    {
        QString addr = QString("%1:%2").arg(local).arg(s->port());
        g->addWidget(new QLabel(tr("Same network:"), box), row, 0);
        QLineEdit* field = new QLineEdit(addr, box);
        field->setReadOnly(true);
        g->addWidget(field, row, 1);
        QPushButton* copy = new QPushButton(tr("Copy"), box);
        copy->setAutoDefault(false);
        connect(copy, &QPushButton::clicked, this, [addr] { QGuiApplication::clipboard()->setText(addr); });
        g->addWidget(copy, row, 2);
        row++;
    }
    QLabel* internet = plainLabel(tr("Over the internet: players use your public IP address and port %1. "
                                     "Forward UDP port %1 on your router to this computer first.").arg(s->port()), box);
    internet->setStyleSheet("color: palette(placeholder-text);");
    g->addWidget(internet, row, 0, 1, 3);
    g->setColumnStretch(1, 1);
    return box;
}

RoomDialog::~RoomDialog()
{
    if (Current == this) Current = nullptr;
}

void RoomDialog::refresh()
{
    if (!Run) return;
    Session* s = Run->session();

    QString title = QString::fromStdString(s->room().Title);
    if (!s->room().GameCode.empty())
        title += QString(" (%1)").arg(QString::fromStdString(s->room().GameCode));
    Game->setText(title.isEmpty() || s->room().Title.empty() ? tr("Netplay") : title);

    // keep the selection across refreshes
    int selected = Players->currentItem() ? Players->currentItem()->data(0, Qt::UserRole).toInt() : -1;
    {
        QSignalBlocker block(Players);
        Players->clear();
        for (const PlayerView& p : s->roster())
        {
            QString name = p.Name;
            if (p.Slot == s->mySlot()) name += tr(" (you)");
            QString status = p.Slot == 0 ? tr("Host") : p.Ready ? tr("Ready") : tr("Sending their save...");
            QString ping = p.Ping >= 0 ? tr("%1 ms").arg(p.Ping) : QString::fromUtf8("\xE2\x80\x94");

            auto* item = new QTreeWidgetItem(Players, {name, status, ping});
            item->setData(0, Qt::UserRole, p.Slot);
            item->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
            if (p.Slot == selected) Players->setCurrentItem(item);
        }
        if (selected < 0)
        {
            Players->setCurrentItem(nullptr);
            Players->clearSelection();
        }
    }

    const bool lobby = s->state() == Session::State::Lobby;
    switch (s->state())
    {
    case Session::State::Connecting:
        Status->setText(tr("Connecting to the host..."));
        break;
    case Session::State::Lobby:
        if (s->isHost())
        {
            if (s->players() < 2)
                Status->setText(tr("Step 1 of 2: give the other players your address (below), then wait for them to join."));
            else if (!s->canStart())
                Status->setText(tr("Waiting for everyone's save to arrive..."));
            else
                Status->setText(tr("Step 2 of 2: when everyone is here, press Start Game."));
        }
        else
            Status->setText(s->mySlot() < 0 ? tr("Joining the room...")
                                             : tr("You're in. The game starts when the host presses Start Game."));
        break;
    case Session::State::Loading:
        Status->setText(tr("Starting the game..."));
        break;
    case Session::State::Playing:
        Status->setText(tr("Playing. Closing this window only hides it: it's under Experimental → "
                           "Show Netplay Room, and chat also shows on the game screen."));
        break;
    case Session::State::Ended:
        Status->setText(tr("The session has ended."));
        break;
    default:
        break;
    }

    const bool inGame = s->state() == Session::State::Loading || s->state() == Session::State::Playing;
    LeaveBtn->setText(inGame ? tr("Leave Game") : s->isHost() ? tr("Close Room") : tr("Leave"));

    QTreeWidgetItem* cur = Players->currentItem();
    RemoveBtn->setEnabled(s->isHost() && lobby && cur && cur->data(0, Qt::UserRole).toInt() != 0);
    StartBtn->setEnabled(s->isHost() && s->canStart());
    if (StartBtn->isEnabled()) StartBtn->setDefault(true);
    refreshDelay();
}

void RoomDialog::refreshDelay()
{
    if (!Run) return;
    Session* s = Run->session();
    int d = s->inputDelay();

    if (DelaySpin && DelaySpin->value() != d)
    {
        QSignalBlocker block(DelaySpin);
        DelaySpin->setValue(d);
    }

    QString ms = tr("about %1 ms").arg(qRound(d * 1000.0 / 59.8261));
    if (s->isHost())
    {
        DelayInfo->setText(ms);
        int sug = s->suggestedDelay();
        UseSuggested->setText(tr("Use suggested (%1)").arg(sug));
        UseSuggested->setEnabled(sug != d && s->players() >= 2);
        UseSuggested->setToolTip(tr("Worked out from the slowest players' ping."));
    }
    else
        DelayInfo->setText(tr("%1 frames, %2 (set by the host)").arg(d).arg(ms));
}

void RoomDialog::addLine(const QString& text, bool dim)
{
    // plain text only: names and messages come from other players
    QTextCharFormat fmt;
    if (dim) fmt.setForeground(palette().color(QPalette::PlaceholderText));
    QTextCursor c(ChatLog->document());
    c.movePosition(QTextCursor::End);
    if (!ChatLog->document()->isEmpty()) c.insertBlock();
    c.insertText(text, fmt);
    ChatLog->verticalScrollBar()->setValue(ChatLog->verticalScrollBar()->maximum());
}

void RoomDialog::onChat(const QString& name, const QString& text)
{
    addLine(QString("%1: %2").arg(name, text), false);
}

void RoomDialog::sendChat()
{
    QString t = ChatInput->text().trimmed();
    if (t.isEmpty() || !Run) return;
    Run->session()->sendChat(t);
    ChatInput->clear();
}

void RoomDialog::onNotice(const QString& text)
{
    addLine(text, true);
    refresh();
}

void RoomDialog::onNeedROM(const RoomInfo& room)
{
    if (!Run) return;
    refresh();

    QStringList path = Run->findROM(room);
    QString err;
    if (!path.isEmpty() && Run->useROM(path, err))
        return;

    QString title = QString::fromStdString(room.Title);
    plainMessage(this, QMessageBox::Information, tr("Netplay"),
        tr("The host is playing %1%2. It isn't in your game list, so choose your copy of it.")
            .arg(title, room.GameCode.empty() ? QString()
                 : QString(" (%1)").arg(QString::fromStdString(room.GameCode))));

    for (;;)
    {
        QString file = QFileDialog::getOpenFileName(this, tr("Choose your copy of the game"), QString(),
                                                    tr("DS ROMs (*.nds *.srl *.zst);;All files (*)"));
        if (file.isEmpty() || !Run)
        {
            if (Run) Run->leave();
            return;
        }
        if (Run->useROM(Win->splitArchivePath(file, true), err))
            return;
        plainMessage(this, QMessageBox::Warning, tr("Netplay"), err);
    }
}

void RoomDialog::onGo()
{
    refresh();
    addLine(tr("The game started."), true);

    // out of the way, a click away on the taskbar
    showMinimized();
    Win->raise();
    Win->activateWindow();
}

void RoomDialog::onDesync(int frame, int console)
{
    if (!Run) return;
    QString who = Run->session()->nameOf(console);
    Note->setText(tr("The consoles stopped matching (%1's console, about %2 seconds in). From here the "
                     "game may play out differently on each computer; restarting the session is best.")
                      .arg(who).arg(frame / 60));
    Note->show();
    showNormal();
    raise();
}

void RoomDialog::onFinished(const QString& reason)
{
    // the session is over and its consoles are gone
    if (Run) Run->deleteLater();
    Run = nullptr;
    Closing = true;

    if (reason.isEmpty())
    {
        close();
        return;
    }

    // it ended without this player choosing to: say why, here in the room
    Status->setText(tr("The session has ended."));
    Note->setText(reason + " " + tr("The game was stopped."));
    Note->setStyleSheet(QString());
    Note->show();
    addLine(reason, true);

    StartBtn->hide();
    RemoveBtn->hide();
    GameHint->hide();
    AddressBox->hide();
    PlayersBox->hide();
    ChatInput->setEnabled(false);
    ChatInput->setPlaceholderText(QString());
    SendBtn->setEnabled(false);
    LeaveBtn->setText(tr("Close"));
    LeaveBtn->setDefault(true);

    showNormal();
    raise();
    activateWindow();
}

void RoomDialog::leaveSession()
{
    if (Closing || !Run)
    {
        close();
        return;
    }

    if (Run->playing())
    {
        auto res = QMessageBox::question(this, tr("Netplay"),
            tr("Leave the game? It ends for every player.\n\n%1").arg(saveWarning()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (res != QMessageBox::Yes)
            return;
    }

    Closing = true;
    Runner* r = Run;
    disconnect(r, nullptr, this, nullptr);
    r->leave();
    r->deleteLater();
    close();
}

void RoomDialog::closeEvent(QCloseEvent* event)
{
    if (Closing || !Run)
    {
        event->accept();
        return;
    }

    // during the game, closing only puts the room away
    if (Run->playing())
    {
        event->ignore();
        hide();
        return;
    }

    // before the game, the room is all there is: closing it leaves
    event->ignore();
    leaveSession();
}

}
