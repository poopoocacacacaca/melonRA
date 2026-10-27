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

#include "GameSettingsDialog.h"

#include <functional>

#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

#include "ARCodeFile.h"
#include "Config.h"
#include "EmuInstance.h"
#include "Screen.h"
#include "WfcServers.h"

namespace
{

enum Kind { OnOff, Choice };

struct Setting
{
    const char* key;    // full config path
    const char* label;
    Kind kind;
    std::vector<std::pair<QString, int>> choices; // for Choice
};

std::vector<std::pair<QString, int>> scaleChoices()
{
    std::vector<std::pair<QString, int>> out;
    for (int i = 1; i <= 8; i++)
        out.push_back({QString("%1x native (%2x%3)").arg(i).arg(256 * i).arg(192 * i), i});
    return out;
}

std::vector<std::pair<QString, int>> aspectChoices()
{
    std::vector<std::pair<QString, int>> out;
    for (int i = 0; i < AspectRatiosNum; i++)
        out.push_back({QString(aspectRatios[i].label).replace("window", "Fill window"), aspectRatios[i].id});
    return out;
}

// everything Game Settings offers, by index
const std::vector<Setting>& settings()
{
    static const std::vector<Setting> list = {
        // graphics (0-7)
        {"3D.Renderer", "3D renderer", Choice, {
            {"Software", renderer3D_Software},
#ifdef OGLRENDERER_ENABLED
            {"OpenGL", renderer3D_OpenGL},
            {"OpenGL (compute shaders)", renderer3D_OpenGLCompute},
#endif
        }},
        {"3D.GL.ScaleFactor", "Internal resolution", Choice, scaleChoices()},
        {"3D.Soft.Threaded", "Threaded software renderer", OnOff, {}},
        {"3D.GL.BetterPolygons", "Improved polygon splitting", OnOff, {}},
        {"3D.GL.HiresCoordinates", "High-resolution coordinates", OnOff, {}},
        {"Screen.UseGL", "OpenGL display", OnOff, {}},
        {"Screen.VSync", "VSync", OnOff, {}},
        {"Instance0.Window0.ScreenFilter", "Screen filtering", OnOff, {}},
        // screen layout (8-15)
        {"Instance0.Window0.ScreenLayout", "Layout", Choice,
            {{"Natural", 0}, {"Vertical", 1}, {"Horizontal", 2}, {"Hybrid", 3}}},
        {"Instance0.Window0.ScreenSizing", "Sizing", Choice,
            {{"Even", 0}, {"Emphasize Top", 1}, {"Emphasize Bottom", 2}, {"Auto", 3},
             {"Top Only", 4}, {"Bottom Only", 5}}},
        {"Instance0.Window0.ScreenRotation", "Rotation", Choice,
            {{"0°", 0}, {"90°", 1}, {"180°", 2}, {"270°", 3}}},
        {"Instance0.Window0.ScreenGap", "Screen gap", Choice,
            {{"0 px", 0}, {"1 px", 1}, {"8 px", 8}, {"64 px", 64}, {"90 px", 90}, {"128 px", 128}}},
        {"Instance0.Window0.ScreenSwap", "Swap screens", OnOff, {}},
        {"Instance0.Window0.IntegerScaling", "Force integer scaling", OnOff, {}},
        {"Instance0.Window0.ScreenAspectTop", "Top screen aspect ratio", Choice, aspectChoices()},
        {"Instance0.Window0.ScreenAspectBot", "Bottom screen aspect ratio", Choice, aspectChoices()},
        // emulation and audio (16-20)
        {"Emu.ConsoleType", "Console type", Choice, {{"DS", 0}, {"DSi", 1}}},
        {"Emu.DirectBoot", "Boot game directly", OnOff, {}},
        {"JIT.Enable", "JIT recompiler", OnOff, {}},
        {"Audio.Interpolation", "Audio interpolation", Choice,
            {{"None", 0}, {"Linear", 1}, {"Cosine", 2}, {"Cubic", 3}, {"Gaussian (SNES)", 4}}},
        {"Instance0.Audio.Volume", "Volume", Choice,
            {{"0%", 0}, {"25%", 64}, {"50%", 128}, {"75%", 192}, {"100%", 256}}},
    };
    return list;
}

// the usual (global) value of a setting, as the text shown in its "Global" entry
QString usualText(const Setting& s)
{
    Config::UsualSettings usual;
    // (a Config::Table is a reference to the data: never assign one to another)
    const std::string full = s.key;
    const bool local = full.rfind("Instance0.", 0) == 0;
    Config::Table table = local ? Config::GetLocalTable(0) : Config::GetGlobalTable();
    const std::string key = local ? full.substr(10) : full;

    if (s.kind == OnOff)
        return table.GetBool(key) ? "On" : "Off";

    const int value = table.GetInt(key);
    for (const auto& [label, v] : s.choices)
        if (v == value) return label;
    return QString::number(value);
}

}

GameSettingsDialog::GameSettingsDialog(QWidget* parent, const std::string& id, const QString& title,
                                       const QIcon& icon, const QString& rom, bool isRunning)
    : QDialog(parent), gameId(id), romPath(rom), running(isRunning)
{
    setWindowTitle(QString("Game Settings - %1").arg(title));
    setAttribute(Qt::WA_DeleteOnClose);

    auto* layout = new QVBoxLayout(this);

    // which game
    auto* header = new QHBoxLayout();
    if (!icon.isNull())
    {
        auto* pic = new QLabel();
        pic->setPixmap(icon.pixmap(32, 32));
        header->addWidget(pic);
    }
    auto* name = new QLabel(QString("<b>%1</b><br>Settings here replace the usual ones while this game runs. "
                                    "\"Global\" follows the emulator's settings.").arg(title.toHtmlEscaped()));
    name->setWordWrap(true);
    header->addWidget(name, 1);
    layout->addLayout(header);

    auto* tabs = new QTabWidget();
    addGroup(tabs, "Graphics", {0, 1, 2, 3, 4, 5, 6, 7});
    addGroup(tabs, "Screen Layout", {8, 9, 10, 11, 12, 13, 14, 15});

    // Wi-Fi: global, off, or a server
    {
        auto* page = new QWidget();
        auto* form = new QFormLayout(page);
        wifiServer = new QComboBox();
        QString usual;
        {
            Config::UsualSettings u;
            const QString sel = WfcServers::selected();
            usual = sel.isEmpty() ? "firmware settings" : sel == "custom" ? "custom DNS" : sel;
            for (const WfcServers::Server& s : WfcServers::servers())
                if (sel == s.id) usual = s.name;
        }
        wifiServer->addItem(QString("Global (%1)").arg(usual), QVariant());
        wifiServer->addItem("Off (no online play)", QString("off"));
        for (const WfcServers::Server& s : WfcServers::servers())
            wifiServer->addItem(QString("%1 (%2)").arg(s.name, s.dns), QString(s.id));
        wifiServer->addItem("Custom DNS server", QString("custom"));
        form->addRow("Online play:", wifiServer);

        wifiCustom = new QLineEdit();
        wifiCustom->setPlaceholderText("DNS server address, e.g. 1.2.3.4");
        form->addRow("", wifiCustom);

        auto* note = new QLabel("Off keeps this game from going online: it reports that it can't connect.");
        note->setWordWrap(true);
        form->addRow(note);

        const QVariantMap current = Config::GetGameSettings(gameId);
        const int idx = wifiServer->findData(current.value("LAN.WFCServer"));
        wifiServer->setCurrentIndex(current.contains("LAN.WFCServer") && idx >= 0 ? idx : 0);
        wifiCustom->setText(current.value("LAN.WFCCustomDNS").toString());
        connect(wifiServer, &QComboBox::currentIndexChanged, this, [this]() { updateWifiControls(); });
        connect(wifiCustom, &QLineEdit::textChanged, this, [this]() { updateWifiControls(); });
        tabs->addTab(page, "Wi-Fi");
    }

    std::vector<int> emu = {16, 17};
#ifdef JIT_ENABLED
    emu.push_back(18);
#endif
    emu.push_back(19);
    emu.push_back(20);
    addGroup(tabs, "Emulation && Audio", emu);
    addCheatsTab(tabs);
    layout->addWidget(tabs);

    auto* note = new QLabel("Graphics, layout and volume change at once if the game is running. "
                            "Console type, JIT, boot, audio interpolation and Wi-Fi apply the next time "
                            "the game starts.");
    note->setWordWrap(true);
    note->setEnabled(false);
    layout->addWidget(note);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    QPushButton* reset = buttons->addButton("Reset All to Global", QDialogButtonBox::ResetRole);
    connect(reset, &QPushButton::clicked, this, &GameSettingsDialog::resetAll);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() { save(); accept(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    updateWifiControls();
    resize(520, sizeHint().height());
}

void GameSettingsDialog::addGroup(QTabWidget* tabs, const QString& name, const std::vector<int>& which)
{
    const QVariantMap current = Config::GetGameSettings(gameId);
    auto* page = new QWidget();
    auto* form = new QFormLayout(page);
    for (int i : which)
    {
        const Setting& s = settings()[i];
        auto* combo = new QComboBox();
        combo->addItem(QString("Global (%1)").arg(usualText(s)), QVariant());
        if (s.kind == OnOff)
        {
            combo->addItem("On", true);
            combo->addItem("Off", false);
        }
        else
        {
            for (const auto& [label, value] : s.choices)
                combo->addItem(label, value);
        }

        if (current.contains(s.key))
        {
            const QVariant v = current.value(s.key);
            int idx = s.kind == OnOff ? combo->findData(v.toBool()) : combo->findData(v.toInt());
            if (idx >= 0) combo->setCurrentIndex(idx);
        }
        form->addRow(QString("%1:").arg(s.label), combo);
        rows.push_back({s.key, combo});
    }
    tabs->addTab(page, name);
}

GameSettingsDialog::~GameSettingsDialog() = default;

// The game's cheat file (.mch) in the cheats folder from Path Settings, or
// next to the ROM, like the emulator looks for it
void GameSettingsDialog::addCheatsTab(QTabWidget* tabs)
{
    auto* page = new QWidget();
    auto* box = new QVBoxLayout(page);

    // cheats on or off for this game
    auto* form = new QFormLayout();
    auto* combo = new QComboBox();
    {
        Config::UsualSettings usual;
        combo->addItem(QString("Global (%1)").arg(Config::GetLocalTable(0).GetBool("EnableCheats") ? "On" : "Off"),
                       QVariant());
    }
    combo->addItem("On", true);
    combo->addItem("Off", false);
    const QVariantMap current = Config::GetGameSettings(gameId);
    if (current.contains("Instance0.EnableCheats"))
        combo->setCurrentIndex(current.value("Instance0.EnableCheats").toBool() ? 1 : 2);
    form->addRow("Cheats:", combo);
    rows.push_back({"Instance0.EnableCheats", combo});
    box->addLayout(form);

    QString folder = QString::fromStdString(Config::GetLocalTable(0).GetString("CheatFilePath"));
    if (folder.isEmpty()) folder = QFileInfo(romPath).absolutePath();
    const QString file = QDir(folder).filePath(QFileInfo(romPath).completeBaseName() + ".mch");

    auto* where = new QLabel(QString("Cheat file: %1").arg(QDir::toNativeSeparators(file)));
    where->setWordWrap(true);
    where->setTextInteractionFlags(Qt::TextSelectableByMouse);
    box->addWidget(where);

    cheatTree = new QTreeWidget();
    cheatTree->setHeaderHidden(true);
    box->addWidget(cheatTree, 1);

    auto* note = new QLabel();
    note->setWordWrap(true);
    box->addWidget(note);

    if (!QFileInfo::exists(file))
    {
        cheatTree->hide();
        note->setText("This game has no cheats yet. Add them with System → Set Up Cheat Codes while it runs, or put a "
                      "cheat file with this name in the cheats folder (Config → Path Settings).");
        tabs->addTab(page, "Cheats");
        return;
    }

    cheats = std::make_unique<melonDS::ARCodeFile>(QDir::toNativeSeparators(file).toStdString());

    // the codes and their categories, each with its checkbox
    std::function<void(QTreeWidgetItem*, melonDS::ARCodeCat&)> fill = [&](QTreeWidgetItem* parent, melonDS::ARCodeCat& cat)
    {
        for (melonDS::ARCodeItem& child : cat.Children)
        {
            if (auto* code = std::get_if<melonDS::ARCode>(&child))
            {
                auto* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(cheatTree);
                item->setText(0, QString::fromStdString(code->Name));
                item->setToolTip(0, QString::fromStdString(code->Description));
                item->setCheckState(0, code->Enabled ? Qt::Checked : Qt::Unchecked);
                item->setData(0, Qt::UserRole, QVariant::fromValue((quintptr)code));
            }
            else if (auto* sub = std::get_if<melonDS::ARCodeCat>(&child))
            {
                auto* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(cheatTree);
                item->setText(0, QString::fromStdString(sub->Name));
                item->setToolTip(0, QString::fromStdString(sub->Description));
                item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);
                item->setData(0, Qt::UserRole + 1, sub->OnlyOneCodeEnabled);
                fill(item, *sub);
                item->setExpanded(true);
            }
        }
    };
    fill(nullptr, cheats->RootCat);

    if (running)
    {
        cheatTree->setEnabled(false);
        note->setText("This game is running: change its cheats in System → Set Up Cheat Codes.");
    }
    else
        note->setText("Checked cheats are active when cheats are on. They're always off in RetroAchievements "
                      "hardcore mode.");

    connect(cheatTree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item)
    {
        cheatsEdited = true;
        // categories that allow only one code at a time
        QTreeWidgetItem* parent = item->parent();
        if (item->checkState(0) == Qt::Checked && parent && parent->data(0, Qt::UserRole + 1).toBool())
        {
            QSignalBlocker block(cheatTree);
            for (int i = 0; i < parent->childCount(); i++)
                if (parent->child(i) != item) parent->child(i)->setCheckState(0, Qt::Unchecked);
        }
    });

    tabs->addTab(page, "Cheats");
}

void GameSettingsDialog::saveCheats()
{
    if (!cheats || !cheatsEdited || running) return;

    QTreeWidgetItemIterator it(cheatTree);
    for (; *it; ++it)
    {
        const quintptr ptr = (*it)->data(0, Qt::UserRole).value<quintptr>();
        if (ptr) reinterpret_cast<melonDS::ARCode*>(ptr)->Enabled = (*it)->checkState(0) == Qt::Checked;
    }
    cheats->Save();
}

void GameSettingsDialog::updateWifiControls()
{
    const bool custom = wifiServer->currentData().toString() == "custom";
    wifiCustom->setVisible(custom);
    if (auto* box = findChild<QDialogButtonBox*>())
        box->button(QDialogButtonBox::Save)->setEnabled(!custom || WfcServers::isValidAddress(wifiCustom->text()));
}

void GameSettingsDialog::resetAll()
{
    for (const Row& r : rows) r.combo->setCurrentIndex(0);
    wifiServer->setCurrentIndex(0);
}

void GameSettingsDialog::save()
{
    QVariantMap values;
    for (const Row& r : rows)
    {
        const QVariant v = r.combo->currentData();
        if (v.isValid()) values[QString::fromStdString(r.key)] = v;
    }
    const QVariant server = wifiServer->currentData();
    if (server.isValid())
    {
        values["LAN.WFCServer"] = server.toString();
        if (server.toString() == "custom")
            values["LAN.WFCCustomDNS"] = wifiCustom->text().trimmed();
    }

    Config::SetGameSettings(gameId, values);
    Config::Save();
    saveCheats();
    emit settingsChanged(QString::fromStdString(gameId));
}
