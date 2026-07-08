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
 * \file form.h
 * For further information see http://www.covesa.global/.
 * @licence end@
 */

#ifndef DLTNOISEFILTER_FORM_H
#define DLTNOISEFILTER_FORM_H

#include <QWidget>

class DltNoiseFilterPlugin;

namespace DltNoiseFilterPluginNs {

namespace Ui {
    class Form;
}

class Form : public QWidget
{
    Q_OBJECT

public:
    explicit Form(DltNoiseFilterPlugin *plugin, QWidget *parent = nullptr);
    ~Form();

    //! Refresh the suppressed-messages counter shown in the UI.
    void updateStatus();

private slots:
    void on_buttonAdd_clicked();
    void on_buttonRemove_clicked();
    void on_buttonApply_clicked();

private:
    void reloadTable();

    Ui::Form *ui;
    DltNoiseFilterPlugin *plugin;
};

} // namespace DltNoiseFilterPluginNs

#endif // DLTNOISEFILTER_FORM_H
