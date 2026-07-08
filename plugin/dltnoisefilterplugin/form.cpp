/**
 * @licence app begin@
 * Copyright (C) 2026  COVESA
 *
 * This file is part of COVESA Project Dlt Viewer.
 *
 * Contributions are licensed to the COVESA Alliance under one or more
 * Contribution License Agreements.
 *
 * \copyright
 * This Source Code Form is subject to the terms of the
 * Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with
 * this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * \file form.cpp
 * For further information see http://www.covesa.global/.
 * @licence end@
 */

#include <QTableWidgetItem>
#include <QHeaderView>

#include "form.h"
#include "ui_form.h"
#include "dltnoisefilterplugin.h"

using namespace DltNoiseFilterPluginNs;

Form::Form(DltNoiseFilterPlugin *plugin, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::Form),
    plugin(plugin)
{
    ui->setupUi(this);
    ui->tableSignals->horizontalHeader()->setStretchLastSection(true);

    connect(ui->buttonAdd,    &QPushButton::clicked, this, &Form::on_buttonAdd_clicked);
    connect(ui->buttonRemove, &QPushButton::clicked, this, &Form::on_buttonRemove_clicked);
    connect(ui->buttonApply,  &QPushButton::clicked, this, &Form::on_buttonApply_clicked);

    reloadTable();
}

Form::~Form()
{
    delete ui;
}

void Form::reloadTable()
{
    if (!plugin)
        return;

    ui->lineMarker->setText(plugin->markerToken());
    ui->lineDefaultThreshold->setText(QString::number(plugin->globalDefaultThreshold()));

    const QList<NoiseSignalConfig> configs = plugin->signalConfigs();
    ui->tableSignals->setRowCount(configs.size());
    for (int row = 0; row < configs.size(); ++row) {
        const NoiseSignalConfig &cfg = configs.at(row);
        ui->tableSignals->setItem(row, 0, new QTableWidgetItem(cfg.apid));
        ui->tableSignals->setItem(row, 1, new QTableWidgetItem(cfg.ctid));
        ui->tableSignals->setItem(row, 2, new QTableWidgetItem(cfg.name));
        ui->tableSignals->setItem(row, 3, new QTableWidgetItem(QString::number(cfg.threshold)));
    }

    updateStatus();
}

void Form::updateStatus()
{
    if (!plugin)
        return;
    ui->labelStatus->setText(QString("Suppressed messages: %1").arg(plugin->suppressedCount()));
}

void Form::on_buttonAdd_clicked()
{
    const int row = ui->tableSignals->rowCount();
    ui->tableSignals->insertRow(row);
    ui->tableSignals->setItem(row, 0, new QTableWidgetItem(QString()));
    ui->tableSignals->setItem(row, 1, new QTableWidgetItem(QString()));
    ui->tableSignals->setItem(row, 2, new QTableWidgetItem(QString()));
    ui->tableSignals->setItem(row, 3, new QTableWidgetItem(QString("1.0")));
}

void Form::on_buttonRemove_clicked()
{
    const int row = ui->tableSignals->currentRow();
    if (row >= 0)
        ui->tableSignals->removeRow(row);
}

void Form::on_buttonApply_clicked()
{
    if (!plugin)
        return;

    QList<NoiseSignalConfig> configs;
    for (int row = 0; row < ui->tableSignals->rowCount(); ++row) {
        const QTableWidgetItem *nameItem = ui->tableSignals->item(row, 2);
        if (!nameItem || nameItem->text().trimmed().isEmpty())
            continue;

        NoiseSignalConfig cfg;
        cfg.apid = ui->tableSignals->item(row, 0) ? ui->tableSignals->item(row, 0)->text().trimmed() : QString();
        cfg.ctid = ui->tableSignals->item(row, 1) ? ui->tableSignals->item(row, 1)->text().trimmed() : QString();
        cfg.name = nameItem->text().trimmed();
        bool ok = false;
        const QTableWidgetItem *thrItem = ui->tableSignals->item(row, 3);
        cfg.threshold = thrItem ? thrItem->text().toDouble(&ok) : 0.0;
        if (!ok)
            cfg.threshold = 0.0;
        configs.append(cfg);
    }

    bool defOk = false;
    double defaultThreshold = ui->lineDefaultThreshold->text().trimmed().toDouble(&defOk);
    if (!defOk)
        defaultThreshold = 0.0;

    plugin->setConfiguration(configs, ui->lineMarker->text(), defaultThreshold);
    plugin->reanalyze();
    reloadTable();
}
