/*
    Copyright 2016-2023 melonDS team

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

#include "AboutDialog.h"

#include <QDesktopServices>
#include <QUrl>

#include "ui_AboutDialog.h"

#include "version.h"

AboutDialog::AboutDialog(QWidget *parent) :
    QDialog(parent), ui(new Ui::AboutDialog)
{
    ui->setupUi(this);

    ui->lblVersionInfo->setText("Version " MELONDS_VERSION);
#ifdef MELONDS_EMBED_BUILD_INFO
    {
        // build info refers to the melonRA repository this binary was built from
        const QString repo = MELONDS_GIT_REPO;
        const QString branch = MELONDS_GIT_BRANCH;
        const QString hash = MELONDS_GIT_HASH;
        const QString base = "https://github.com/" + repo;
        ui->lblBuildInfo->setTextFormat(Qt::RichText);
        ui->lblBuildInfo->setOpenExternalLinks(true);
        ui->lblBuildInfo->setText(QString(
            "Repository: <a href=\"%1\">%2</a><br>"
            "Branch: <a href=\"%1/tree/%3\">%3</a><br>"
            "Commit: <a href=\"%1/commit/%4\">%5</a><br>"
            "Built by: %6")
            .arg(base, repo.toHtmlEscaped(), branch.toHtmlEscaped(), hash,
                 hash.left(10), QString(MELONDS_BUILD_PROVIDER).toHtmlEscaped()));
    }
#else
    ui->lblBuildInfo->hide();
#endif
    adjustSize();
}

AboutDialog::~AboutDialog()
{
    delete ui;
}

void AboutDialog::openWebsite()
{
    QDesktopServices::openUrl(QUrl(MELONDS_URL));
}

void AboutDialog::openGitHub()
{
    QDesktopServices::openUrl(QUrl("https://github.com/melonDS-emu/melonDS"));;
}

void AboutDialog::openMelonRAGitHub()
{
    QDesktopServices::openUrl(QUrl("https://github.com/ppkkkkk/melonRA"));
}
