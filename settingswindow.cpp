#include "settingswindow.h"
#include "ui_settingswindow.h"

SettingsWindow::SettingsWindow(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::SettingsWindow)
{
    ui->setupUi(this);

    if(!settings.value("core/firstrun").toBool()) ui->senderbox->setValue(4);
    else ui->senderbox->setValue(settings.value("core/senderinterval").toFloat());

    if(!settings.value("core/firstrun").toBool()) ui->echobox->setChecked(false);
    else ui->echobox->setChecked(settings.value("core/echo").toBool());

    if(settings.value("core/statusinterval").toInt()) ui->statusbox->setValue(settings.value("core/statusinterval").toInt());
    else ui->statusbox->setValue(1500);

    if(settings.value("printer/bedx").toInt()) ui->bedxbox->setValue(settings.value("printer/bedx").toInt());
    else ui->bedxbox->setValue(200);

    if(settings.value("printer/bedy").toInt()) ui->bedybox->setValue(settings.value("printer/bedy").toInt());
    else ui->bedybox->setValue(200);

    ui->lockbox->setChecked(settings.value("core/lockcontrols").toBool());

}

SettingsWindow::~SettingsWindow()
{
    delete ui;
}

void SettingsWindow::on_buttonBox_accepted()
{
    settings.setValue("core/senderinterval", ui->senderbox->value());
    settings.setValue("core/statusinterval", ui->statusbox->value());
    settings.setValue("printer/bedy", ui->bedybox->value());
    settings.setValue("printer/bedx", ui->bedxbox->value());
    settings.setValue("core/echo", ui->echobox->isChecked());
    settings.setValue("core/lockcontrols", ui->lockbox->isChecked());
}
