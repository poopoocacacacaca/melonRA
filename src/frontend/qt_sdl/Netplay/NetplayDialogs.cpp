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

#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
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

// a wrapped explanation that keeps all of its lines at this width
static void reserveLines(QLabel* l, int width)
{
    l->setMinimumWidth(width);
    l->setMinimumHeight(l->heightForWidth(width));
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
    reserveLines(help, 400);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Join"));
    connect(buttons, &QDialogButtonBox::accepted, this, &JoinDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &JoinDialog::reject);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setSizeConstraint(QLayout::SetMinimumSize);
    layout->addLayout(form);
    layout->addWidget(help);
    layout->addWidget(buttons);
    setMinimumWidth(420);
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
    if (Current) { Current->show(); Current->raise(); return Current; }
    Current = new RoomDialog(win, runner);
    Current->show();
    return Current;
}

RoomDialog::RoomDialog(MainWindow* win, Runner* runner) : QDialog(win), Win(win), Run(runner)
{
    setWindowTitle(tr("Netplay Room"));
    setAttribute(Qt::WA_DeleteOnClose);

    Game = plainLabel(QString(), this);
    QFont f = Game->font();
    f.setBold(true);
    Game->setFont(f);
    Status = plainLabel(QString(), this);
    Note = plainLabel(QString(), this);
    Note->setStyleSheet("color: palette(placeholder-text);");

    Players = new QListWidget(this);
    Players->setSelectionMode(QAbstractItemView::NoSelection);
    Players->setMinimumHeight(110);

    StartBtn = new QPushButton(tr("Start"), this);
    LeaveBtn = new QPushButton(tr("Leave"), this);
    StartBtn->setVisible(runner->session()->isHost());
    connect(StartBtn, &QPushButton::clicked, this, [this]
    {
        if (Run) Run->startGame();
    });
    connect(LeaveBtn, &QPushButton::clicked, this, &RoomDialog::close);

    QHBoxLayout* buttons = new QHBoxLayout();
    buttons->addStretch(1);
    buttons->addWidget(StartBtn);
    buttons->addWidget(LeaveBtn);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(Game);
    layout->addWidget(Status);
    layout->addWidget(Players, 1);
    layout->addWidget(Note);
    layout->addLayout(buttons);
    setMinimumWidth(380);

    Session* s = runner->session();
    connect(s, &Session::rosterChanged, this, &RoomDialog::refresh);
    connect(s, &Session::notice, this, &RoomDialog::onNotice);
    connect(s, &Session::started, this, &RoomDialog::refresh);
    connect(s, &Session::go, this, &RoomDialog::onGo);
    connect(s, &Session::desync, this, &RoomDialog::onDesync);
    connect(runner, &Runner::needROM, this, &RoomDialog::onNeedROM);
    connect(runner, &Runner::finished, this, &RoomDialog::onFinished);

    refresh();
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
    Game->setText(title.isEmpty() ? tr("Netplay") : title);

    Players->clear();
    for (const PlayerView& p : s->roster())
    {
        QStringList tags;
        if (p.Slot == 0) tags << tr("host");
        if (p.Slot == s->mySlot()) tags << tr("you");
        QString line = p.Name;
        if (!tags.isEmpty()) line += QString(" (%1)").arg(tags.join(", "));
        if (!p.Ready) line += tr(" - sending their save...");
        Players->addItem(line);
    }

    switch (s->state())
    {
    case Session::State::Connecting:
        Status->setText(tr("Connecting..."));
        break;
    case Session::State::Lobby:
        if (s->isHost())
            Status->setText(s->players() < 2 ? tr("Waiting for players to join...")
                                             : tr("Start when everyone is here."));
        else
            Status->setText(s->mySlot() < 0 ? tr("Joining...") : tr("Waiting for the host to start..."));
        break;
    case Session::State::Loading:
        Status->setText(tr("Starting..."));
        break;
    case Session::State::Playing:
        Status->setText(tr("Playing."));
        break;
    case Session::State::Ended:
        Status->setText(tr("The session has ended."));
        break;
    default:
        break;
    }

    StartBtn->setEnabled(s->isHost() && s->canStart());
}

void RoomDialog::onNotice(const QString& text)
{
    Note->setText(text);
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
    Note->setText(QString());
    hide();
}

void RoomDialog::onDesync(int frame, int console)
{
    if (!Run) return;
    QString who = Run->session()->nameOf(console);
    Note->setText(tr("The consoles stopped matching (%1's console, about %2 seconds in). From here the "
                     "game may play out differently on each computer; restarting the session is best.")
                      .arg(who).arg(frame / 60));
    show();
    raise();
}

void RoomDialog::onFinished(const QString& reason)
{
    if (!reason.isEmpty())
        plainMessage(Win, QMessageBox::Information, tr("Netplay"), reason);

    Closing = true;
    if (Run) Run->deleteLater();
    close();
}

void RoomDialog::closeEvent(QCloseEvent* event)
{
    if (Closing || !Run)
    {
        event->accept();
        return;
    }

    if (Run->playing())
    {
        auto res = QMessageBox::question(this, tr("Netplay"),
            tr("Leave the game? It ends for every player."),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (res != QMessageBox::Yes)
        {
            event->ignore();
            return;
        }
    }

    Closing = true;
    Runner* r = Run;
    disconnect(r, nullptr, this, nullptr);
    r->leave();
    r->deleteLater();
    event->accept();
}

}
