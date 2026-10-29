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

#include <QBuffer>
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QFile>
#include <QHBoxLayout>
#include <QImageReader>
#include <QKeyEvent>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "SettingsWindow.h"
#include "Window.h"
#include "RetroAchievements/RAClient.h"

SettingsWindow* SettingsWindow::Current = nullptr;

namespace
{

struct PageInfo
{
    const char* Name;
    const char* Icon;   // in resources/settings (Lucide icons)
};

const PageInfo kPages[SettingsWindow::Page_Count] = {
    {"Emulation", "emulation"},
    {"Input and Hotkeys", "input"},
    {"Video", "video"},
    {"Camera", "camera"},
    {"Audio", "audio"},
    {"Multiplayer", "multiplayer"},
    {"Wi-Fi", "wifi"},
    {"Firmware", "firmware"},
    {"Interface", "interface"},
    {"Paths", "paths"},
    {"Updates", "updates"},
    {"RetroAchievements", "achievements"},
};

constexpr int kIconSize = 20;

// a line icon drawn in the text color, at one size; the same picture whether
// the page is selected or not
QIcon pageIcon(const char* name, const QColor& color, qreal dpr)
{
    QFile file(QString(":/settings/%1.svg").arg(name));
    if (!file.open(QIODevice::ReadOnly)) return QIcon();
    QByteArray svg = file.readAll();
    svg.replace("currentColor", color.name().toLatin1());

    QBuffer buf(&svg);
    buf.open(QIODevice::ReadOnly);
    QImageReader reader(&buf, "svg");
    const int px = qRound(kIconSize * dpr);
    reader.setScaledSize(QSize(px, px));
    QImage img = reader.read();
    if (img.isNull()) return QIcon();

    QPixmap pm = QPixmap::fromImage(img);
    pm.setDevicePixelRatio(dpr);
    QIcon icon;
    icon.addPixmap(pm, QIcon::Normal);
    icon.addPixmap(pm, QIcon::Selected);
    icon.addPixmap(pm, QIcon::Active);
    return icon;
}

}

void SettingsWindow::open(MainWindow* win, Page page)
{
    if (!Current)
    {
        Current = new SettingsWindow(win);
        Current->show();
    }
    Current->showPage(page);
    Current->raise();
    Current->activateWindow();
}

SettingsWindow::SettingsWindow(MainWindow* win) : QDialog(win), Win(win)
{
    setWindowTitle("Settings - melonRA");
    setAttribute(Qt::WA_DeleteOnClose);

    List = new QListWidget(this);
    List->setFixedWidth(200);
    List->setIconSize(QSize(kIconSize, kIconSize));
    List->setSpacing(1);
    List->setFrameShape(QFrame::NoFrame);
    List->setStyleSheet("QListWidget { background: transparent; } QListWidget::item { padding: 5px 4px; }");

    Stack = new QStackedWidget(this);

    Pages[Page_Achievements].Available = RA::Manager::get() != nullptr;

    for (int i = 0; i < Page_Count; i++)
    {
        auto* item = new QListWidgetItem(kPages[i].Name, List);
        item->setData(Qt::UserRole, i);
        item->setHidden(!Pages[i].Available);

        auto* holder = new QScrollArea(Stack);
        holder->setWidgetResizable(true);
        holder->setFrameShape(QFrame::NoFrame);
        Pages[i].Holder = holder;
        Stack->addWidget(holder);
    }
    updateIcons();

    connect(List, &QListWidget::currentRowChanged, this, [this](int row)
    {
        if (row >= 0 && row < Page_Count) showPage((Page)row);
    });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsWindow::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &SettingsWindow::reject);

    auto* top = new QHBoxLayout();
    top->addWidget(List);
    top->addWidget(Stack, 1);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(top, 1);
    layout->addWidget(buttons);

    resize(940, 640);
}

SettingsWindow::~SettingsWindow()
{
    if (Current == this) Current = nullptr;
}

void SettingsWindow::updateIcons()
{
    const QColor color = palette().color(QPalette::WindowText);
    const qreal dpr = devicePixelRatioF();
    for (int i = 0; i < Page_Count; i++)
        if (QListWidgetItem* item = List->item(i))
            item->setIcon(pageIcon(kPages[i].Icon, color, dpr));
}

void SettingsWindow::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange)
        updateIcons();
    QDialog::changeEvent(event);
}

void SettingsWindow::showPage(Page page)
{
    if (page < 0 || page >= Page_Count || !Pages[page].Available) return;
    PageSlot& slot = Pages[page];

    if (!slot.Dialog)
    {
        QDialog* dlg = Win->createSettingsPage(page);
        if (!dlg) return;

        // shown as part of this window: its own buttons give way to OK and
        // Cancel here. It takes the window's width and its own height, at
        // the top.
        auto* body = new QWidget(slot.Holder);
        auto* bodyLayout = new QVBoxLayout(body);
        bodyLayout->setContentsMargins(0, 0, 0, 0);
        dlg->setParent(body, Qt::Widget);
        bodyLayout->addWidget(dlg);
        bodyLayout->addStretch(1);
        for (QDialogButtonBox* box : dlg->findChildren<QDialogButtonBox*>())
        {
            if (box->button(QDialogButtonBox::Ok) || box->button(QDialogButtonBox::Cancel) ||
                box->button(QDialogButtonBox::Close))
                box->hide();
        }
        dlg->installEventFilter(this);

        // pages made as fixed-size dialogs use the room the window has
        if (QLayout* l = dlg->layout())
            l->setSizeConstraint(QLayout::SetDefaultConstraint);
        dlg->setMinimumSize(0, 0);
        dlg->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        dlg->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);

        slot.Dialog = dlg;
        slot.Finished = false;
        connect(dlg, &QDialog::finished, this, [this, page](int)
        {
            PageSlot& s = Pages[page];
            s.Finished = true;
            if (Closing) return;

            // the page closed by itself: open a fresh one if it's showing
            if (s.Dialog)
            {
                s.Dialog->deleteLater();
                s.Dialog = nullptr;
            }
            if (Stack->currentIndex() == page)
                QTimer::singleShot(0, this, [this, page] { showPage(page); });
        });

        slot.Holder->setWidget(body);
        dlg->show();
    }

    Stack->setCurrentIndex(page);
    if (List->currentRow() != page)
    {
        QSignalBlocker block(List);
        List->setCurrentRow(page);
    }
}

void SettingsWindow::accept()
{
    // each page saves its settings; one that refuses (asking first, or a
    // setting it can't take) keeps the window open on it
    Closing = true;
    for (int i = 0; i < Page_Count; i++)
    {
        PageSlot& slot = Pages[i];
        if (!slot.Dialog || slot.Finished) continue;

        slot.Dialog->accept();
        if (slot.Dialog && !slot.Finished)
        {
            Closing = false;
            showPage((Page)i);
            return;
        }
    }
    QDialog::accept();
}

void SettingsWindow::reject()
{
    Closing = true;
    for (int i = 0; i < Page_Count; i++)
    {
        PageSlot& slot = Pages[i];
        if (slot.Dialog && !slot.Finished)
            slot.Dialog->reject();
    }
    QDialog::reject();
}

void SettingsWindow::closeEvent(QCloseEvent* event)
{
    if (!Closing)
    {
        reject();
        event->accept();
        return;
    }
    QDialog::closeEvent(event);
}

bool SettingsWindow::eventFilter(QObject* obj, QEvent* event)
{
    // Escape and Enter belong to this window, not to the page inside it
    if (event->type() == QEvent::KeyPress)
    {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape)
        {
            reject();
            return true;
        }
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)
            return true;
    }
    return QDialog::eventFilter(obj, event);
}
