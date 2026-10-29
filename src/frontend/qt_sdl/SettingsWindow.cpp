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
#include <QHBoxLayout>
#include <QImageReader>
#include <QKeyEvent>
#include <QListWidget>
#include <QPainter>
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
    const char* Icon;   // SVG shapes on a 24x24 grid, drawn with lines
};

const PageInfo kPages[SettingsWindow::Page_Count] = {
    {"Emulation",
     "<rect x='6' y='2.5' width='12' height='8.5' rx='1.5'/><rect x='6' y='13' width='12' height='8.5' rx='1.5'/>"},
    {"Input and Hotkeys",
     "<path d='M7 8h10a4 4 0 0 1 3.9 3.1l1 4.6a2.5 2.5 0 0 1-4.3 2.2L15.5 16h-7l-2.1 1.9a2.5 2.5 0 0 1-4.3-2.2l1-4.6A4 4 0 0 1 7 8z'/>"
     "<path d='M8 10.5v3M6.5 12h3'/><circle cx='15.5' cy='11' r='0.6'/><circle cx='17.5' cy='13' r='0.6'/>"},
    {"Video",
     "<rect x='2.5' y='4' width='19' height='13' rx='1.5'/><path d='M8 21h8M12 17v4'/>"},
    {"Camera",
     "<path d='M3 8a2 2 0 0 1 2-2h2.5l1.5-2h6l1.5 2H19a2 2 0 0 1 2 2v9a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z'/><circle cx='12' cy='12.5' r='3.5'/>"},
    {"Audio",
     "<path d='M4 9.5h3.5L12 5.5v13l-4.5-4H4z'/><path d='M15.5 9a4 4 0 0 1 0 6M18 6.5a7.5 7.5 0 0 1 0 11'/>"},
    {"Multiplayer",
     "<circle cx='9' cy='8' r='3'/><path d='M3.5 19a5.5 5.5 0 0 1 11 0'/><circle cx='17' cy='9' r='2.5'/><path d='M15.5 14.2A4.5 4.5 0 0 1 21 18.5'/>"},
    {"Wi-Fi",
     "<path d='M2.5 9a14 14 0 0 1 19 0M5.5 12.5a9.5 9.5 0 0 1 13 0M8.5 16a5 5 0 0 1 7 0'/><circle cx='12' cy='19' r='0.8'/>"},
    {"Firmware",
     "<rect x='6' y='6' width='12' height='12' rx='1.5'/><rect x='9.5' y='9.5' width='5' height='5'/>"
     "<path d='M9 2.5V6M15 2.5V6M9 18v3.5M15 18v3.5M2.5 9H6M2.5 15H6M18 9h3.5M18 15h3.5'/>"},
    {"Interface",
     "<path d='M4 6h9M17 6h3M4 12h3M11 12h9M4 18h11M19 18h1'/><circle cx='15' cy='6' r='2'/><circle cx='9' cy='12' r='2'/><circle cx='17' cy='18' r='2'/>"},
    {"Paths",
     "<path d='M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z'/>"},
    {"Updates",
     "<path d='M12 3.5v11M7.5 10l4.5 4.5 4.5-4.5M4 16.5v2a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2v-2'/>"},
    {"RetroAchievements",
     "<path d='M8 4h8v5a4 4 0 0 1-8 0z'/><path d='M8 6H5a3 3 0 0 0 3 4M16 6h3a3 3 0 0 1-3 4M12 13v4M8.5 20.5h7M9.5 17h5'/>"},
};

void addLineIcon(QIcon& icon, const char* shapes, const QColor& color, qreal dpr, QIcon::Mode mode)
{
    QByteArray svg = QByteArray("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='")
                   + color.name().toLatin1()
                   + "' stroke-width='1.7' stroke-linecap='round' stroke-linejoin='round'>"
                   + shapes + "</svg>";

    for (int size : {20, 24, 32})
    {
        QBuffer buf(&svg);
        buf.open(QIODevice::ReadOnly);
        QImageReader reader(&buf, "svg");
        int px = qRound(size * dpr);
        reader.setScaledSize(QSize(px, px));
        QImage img = reader.read();
        if (img.isNull()) continue;
        QPixmap pm = QPixmap::fromImage(img);
        pm.setDevicePixelRatio(dpr);
        icon.addPixmap(pm, mode);
    }
}

// drawn in the text color, and in the highlighted text color when selected
QIcon lineIcon(const char* shapes, const QPalette& pal, qreal dpr)
{
    QIcon icon;
    addLineIcon(icon, shapes, pal.color(QPalette::WindowText), dpr, QIcon::Normal);
    addLineIcon(icon, shapes, pal.color(QPalette::HighlightedText), dpr, QIcon::Selected);
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
    List->setIconSize(QSize(20, 20));
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
    qreal dpr = devicePixelRatioF();
    for (int i = 0; i < Page_Count; i++)
        if (QListWidgetItem* item = List->item(i))
            item->setIcon(lineIcon(kPages[i].Icon, palette(), dpr));
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
