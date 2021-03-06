#include "mainwindow.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    //Setup the UI
    ui->fileBox->setDisabled(true);
    ui->sendBtn->setDisabled(true);
    ui->pauseBtn->setDisabled(true);
    ui->progressBar->setValue(0);
    ui->controlBox->setDisabled(true);
    ui->consoleGroup->setDisabled(true);
    ui->pauseBtn->setDisabled(true);
    ui->actionPrint_from_SD->setDisabled(true);
    ui->actionSet_SD_printing_mode->setDisabled(true);
    ui->actionEEPROM_editor->setDisabled(true);
    ui->extruderlcd->setPalette(Qt::red);
    ui->bedlcd->setPalette(Qt::red);
    ui->sendtext->installEventFilter(this);
    recentMenu = new QMenu(this);
    recentMenu->setTitle("Recent files");
    ui->menuFile->insertMenu(ui->actionSettings, recentMenu);
    ui->menuFile->insertSeparator(ui->actionSettings);

    //Init baudrate combobox
    ui->baudbox->addItem(QString::number(4800));
    ui->baudbox->addItem(QString::number(9600));
    ui->baudbox->addItem(QString::number(115200));
    ui->baudbox->addItem(QString::number(128000));
    ui->baudbox->addItem(QString::number(230400));
    ui->baudbox->addItem(QString::number(250000));
    ui->baudbox->addItem(QString::number(460800));
    ui->baudbox->addItem(QString::number(500000));

    //Restore settings
    firstrun = !settings.value("core/firstrun").toBool(); //firstrun is inverted!
    ui->baudbox->setCurrentIndex(settings.value("printer/baudrateindex", 2).toInt());
    checkingTemperature = settings.value("core/checktemperature", 0).toBool();
    ui->checktemp->setChecked(checkingTemperature);
    ui->etmpspin->setValue(settings.value("user/extrudertemp", 210).toInt());
    ui->btmpspin->setValue(settings.value("user/bedtemp", 60).toInt());
    echo = settings.value("core/echo", 0).toBool();
    autolock = settings.value("core/lockcontrols", 0).toBool();
    sendingChecksum = settings.value("core/checksums", 0).toBool();
    chekingSDStatus = settings.value("core/checksdstatus", 1).toBool();
    firmware = settings.value("printer/firmware", OtherFirmware).toInt();
    statusTimer.setInterval(settings.value("core/statusinterval", 3000).toInt());
    sendTimer.setInterval(settings.value("core/senderinterval", 2).toInt());
    int size = settings.beginReadArray("user/recentfiles");
    for(int i = 0; i < size; ++i)
    {
        settings.setArrayIndex(i);
        recentFiles.append(settings.value("user/file").toString());
    }
    settings.endArray();

    //Init values
    sending = false;
    paused = false;
    readingFiles = false;
    sdprinting = false;
    sdBytes = 0;
    currentLine = 0;
    readyRecieve = 1;
    lastRecieved = 0;
    userHistoryPos = 0;
    userHistory.append("");

    //Update serial ports
    serialupdate();

    //Internal signal-slots
    connect(&printer, SIGNAL(error(QSerialPort::SerialPortError)), this, SLOT(serialError(QSerialPort::SerialPortError)));
    connect(&printer, SIGNAL(readyRead()), this, SLOT(readSerial()));
    connect(&statusTimer, SIGNAL(timeout()), this, SLOT(checkStatus()));
    connect(&sendTimer, SIGNAL(timeout()), this, SLOT(sendNext()));
    connect(&progressSDTimer, SIGNAL(timeout()), this, SLOT(checkSDStatus()));
    connect(this, SIGNAL(eepromReady()), this, SLOT(openEEPROMeditor()));

    //Parser thread signal-slots and init
    qRegisterMetaType<TemperatureReadings>("TemperatureReadings");
    qRegisterMetaType<SDProgress>("SDProgress");
    parser = new Parser();
    parserThread = new QThread();
    parser->moveToThread(parserThread);
    connect(parserThread, &QThread::finished, parser, &QObject::deleteLater);
    connect(this, &MainWindow::recievedData, parser, &Parser::parse);
    connect(this, &MainWindow::startedReadingEEPROM, parser, &Parser::setEEPROMReadingMode);
    connect(parser, &Parser::recievedTemperature, this, &MainWindow::updateTemperature);
    connect(parser, &Parser::recievedOkNum, this, &MainWindow::recievedOkNum);
    connect(parser, &Parser::recievedOkWait, this, &MainWindow::recievedWait);
    connect(parser, &Parser::recievedSDFilesList, this, &MainWindow::initSDprinting);
    connect(parser, &Parser::recievedEEPROMLine, this, &MainWindow::EEPROMSettingRecieved);
    connect(parser, &Parser::recievingEEPROMDone, this, &MainWindow::openEEPROMeditor);
    connect(parser, &Parser::recievedError, this, &MainWindow::recievedError);
    connect(parser, &Parser::recievedSDDone, this, &MainWindow::recievedSDDone);
    connect(parser, &Parser::recievedResend, this, &MainWindow::recievedResend);
    connect(parser, &Parser::recievedSDUpdate, this, &MainWindow::updateSDStatus);
    parserThread->start();

    //Timers init
    statusTimer.start();
    sendTimer.start();
    progressSDTimer.setInterval(2500);
    if(chekingSDStatus) progressSDTimer.start();
    sinceLastTemp.start();
    sinceLastSDStatus.start();

    updateRecent();
}

MainWindow::~MainWindow()
{
    //Save settings
    if(firstrun) settings.setValue("core/firstrun", true); //firstrun is inverted!
    settings.setValue("printer/baudrateindex", ui->baudbox->currentIndex());
    settings.setValue("core/checktemperature", ui->checktemp->isChecked());
    settings.setValue("user/extrudertemp", ui->etmpspin->value());
    settings.setValue("user/bedtemp", ui->btmpspin->value());

    settings.beginWriteArray("user/recentfiles");
    for(int i = 0; i < recentFiles.size(); ++i)
    {
        settings.setArrayIndex(i);
        settings.setValue("user/file", recentFiles.at(i));
    }
    settings.endArray();

    //Cleanup what is left
    if(gfile.isOpen()) gfile.close();
    if(printer.isOpen()) printer.close();
    parserThread->quit();
    parserThread->wait();

    delete ui;
}

void MainWindow::open()
{
    sdprinting = false;
    QString filename;
    QDir home;
    filename = QFileDialog::getOpenFileName(this,
                                            "Open GCODE",
                                            home.home().absolutePath(),
                                            "GCODE (*.g *.gco *.gcode *.nc)");
    gfile.setFileName(filename);
    if(!recentFiles.contains(filename))
    {
        recentFiles.prepend(filename);
        if(recentFiles.size() >= 10) recentFiles.removeLast();
    }

    updateRecent();
    parseFile(filename);
}

void MainWindow::parseFile(QString filename)
{
    gfile.setFileName(filename);
    gcode.clear();
    if (gfile.open(QIODevice::ReadOnly))
    {
        QTextStream in(&gfile);
        int n = 0;
        while (!in.atEnd())
        {
            QString line = in.readLine();
            if(!line.startsWith(";"))
            {
                if(sendingChecksum)
                {
                    //Checksum algorithm from RepRap wiki
                    line = "N"+QString::number(n)+line+"*";
                    int cs = 0;
                    for(int i = 0; line.at(i) != '*'; i++) cs = cs ^ line.at(i).toLatin1();
                    cs &= 0xff;
                    line += QString::number(cs);
                    n++;
                }
                gcode.append(line);
            }

        }
        gfile.close();
        ui->fileBox->setEnabled(true);
        ui->progressBar->setEnabled(true);
        ui->sendBtn->setText("Send");
        ui->filename->setText(gfile.fileName().split(QDir::separator()).last());
        ui->filelines->setText(QString::number(gcode.size()) + QString("/0 lines"));
    }
}

bool MainWindow::sendLine(QString line)
{

    if(printer.isOpen())
    {
        if(printer.write(line.toUtf8()+'\n'))
        {
            if(echo) printMsg(line + '\n');
            return true;
        }
        else return false;
    }
    else return false;

}

void MainWindow::serialupdate()
{
    ui->serialBox->clear();
    QList<QSerialPortInfo> list = QSerialPortInfo::availablePorts();
    for(int i = 0; i < list.size(); i++)
        ui->serialBox->addItem(list.at(i).portName());
}

void MainWindow::serialconnect()
{
    userCommands.clear();

    if(!printer.isOpen())
    {
        foreach (const QSerialPortInfo &info, QSerialPortInfo::availablePorts())
        {
            if(info.portName() == ui->serialBox->currentText())
            {
                printerinfo = info;
                break;
            }
        }

        printer.setPort(printerinfo);

        if(printer.open(QIODevice::ReadWrite))
        {

            //Moved here to be compatible with Qt 5.2.1
            switch(ui->baudbox->currentText().toInt())
            {
                case 4800:
                printer.setBaudRate(QSerialPort::Baud4800);
                break;

                case 9600:
                printer.setBaudRate(QSerialPort::Baud9600);
                break;

                case 115200:
                printer.setBaudRate(QSerialPort::Baud115200);
                break;

                default:
                printer.setBaudRate(ui->baudbox->currentText().toInt());
                break;
            }

            ui->connectBtn->setText("Disconnect");
            ui->sendBtn->setDisabled(false);
            //ui->pauseBtn->setDisabled(false);
            ui->progressBar->setValue(0);
            ui->controlBox->setDisabled(false);
            ui->consoleGroup->setDisabled(false);
            ui->actionPrint_from_SD->setEnabled(true);
            ui->actionSet_SD_printing_mode->setEnabled(true);
            if(firmware == Repetier) ui->actionEEPROM_editor->setDisabled(false);
        }
    }

    else if(printer.isOpen())
    {
        printer.close();

        ui->connectBtn->setText("Connect");
        ui->sendBtn->setDisabled(true);
        ui->pauseBtn->setDisabled(true);
        ui->progressBar->setValue(0);
        ui->controlBox->setDisabled(true);
        ui->consoleGroup->setDisabled(true);
        ui->actionPrint_from_SD->setDisabled(true);
        ui->actionSet_SD_printing_mode->setDisabled(true);
        ui->actionEEPROM_editor->setDisabled(false);
     }
}

/////////////////
//Buttons start//
/////////////////
void MainWindow::xplus()
{
    QString command = "G91\nG1 X" + ui->stepspin->text() + "\nG90";
    injectCommand(command);
}

void MainWindow::xminus()
{
    QString command = "G91\nG1 X-" + ui->stepspin->text() + "\nG90";
    injectCommand(command);
}

void MainWindow::xhome()
{
    injectCommand("G28 X0");
}

void MainWindow::yplus()
{
    QString command = "G91\nG1 Y" + ui->stepspin->text() + "\nG90";
    injectCommand(command);
}

void MainWindow::yminus()
{
    QString command = "G91\nG1 Y-" + ui->stepspin->text() + "\nG90";
    injectCommand(command);
}

void MainWindow::yhome()
{
    injectCommand("G28 Y0");
}

void MainWindow::zplus()
{
    QString command = "G91\nG1 Z" + ui->stepspin->text() + "\nG90";
    injectCommand(command);
}

void MainWindow::zminus()
{
    QString command = "G91\nG1 Z-" + ui->stepspin->text() + "\nG90";
    injectCommand(command);
}

void MainWindow::zhome()
{
    injectCommand("G28 Z0");
}

void MainWindow::eplus()
{
    QString command = "G91\nG1 E" + ui->estepspin->text() + "\nG90";
    injectCommand(command);
}

void MainWindow::eminus()
{
    QString command = "G91\nG1 E-" + ui->estepspin->text() + "\nG90";
    injectCommand(command);
}

void MainWindow::ezero()
{
    injectCommand("G92 E0");
}

void MainWindow::homeall()
{
    injectCommand("G28");
}

void MainWindow::on_sendbtn_clicked()
{
    injectCommand(ui->sendtext->text());
    userHistory.append(ui->sendtext->text());
    userHistoryPos = 0;
}

void MainWindow::on_fanonbtn_clicked()
{
    injectCommand("M106");
}

void MainWindow::on_fanoffbtn_clicked()
{
    injectCommand("M107");
}

void MainWindow::on_atxonbtn_clicked()
{
    injectCommand("M80");
}

void MainWindow::on_atxoffbtn_clicked()
{
    injectCommand("M81");
}

void MainWindow::on_etmpset_clicked()
{
    QString command = "M80\nM104 S" + ui->etmpspin->text();
    injectCommand(command);
}

void MainWindow::on_etmpoff_clicked()
{
    injectCommand("M104 S0");
}

void MainWindow::on_btmpset_clicked()
{
    QString command = "M80\nM140 S" + ui->btmpspin->text();
    injectCommand(command);
}

void MainWindow::on_btmpoff_clicked()
{
    injectCommand("M140 S0");
}

void MainWindow::bedcenter()
{
    int x, y;

    x = settings.value("printer/bedx", 200).toInt();
    y = settings.value("printer/bedy", 200).toInt();

    QString command = "G1 X" + QString::number(x/2) + "Y" + QString::number(y/2);
    injectCommand(command);
}

void MainWindow::on_speedslider_valueChanged(int value)
{
    ui->speededit->setText(QString::number(value));
}

void MainWindow::on_speededit_textChanged(const QString &arg1)
{
    if(arg1.toInt()) ui->speedslider->setValue(arg1.toInt());
    else ui->speededit->setText(QString::number(ui->speedslider->value()));
}

void MainWindow::on_speedsetbtn_clicked()
{
    QString command = "M220 S" + QString::number(ui->speedslider->value());
    injectCommand(command);
}

void MainWindow::on_flowedit_textChanged(const QString &arg1)
{
    if(arg1.toInt()) ui->flowslider->setValue(arg1.toInt());
    else ui->flowedit->setText(QString::number(ui->flowslider->value()));
}

void MainWindow::on_flowslider_valueChanged(int value)
{
    ui->flowedit->setText(QString::number(value));
}

void MainWindow::on_flowbutton_clicked()
{
    QString command = "M221 S" + QString::number(ui->flowslider->value());
    injectCommand(command);
}

void MainWindow::on_haltbtn_clicked()
{
    if(sending && !paused)ui->pauseBtn->click();
    userCommands.clear();
    injectCommand("M112");
}

void MainWindow::on_actionPrint_from_SD_triggered()
{
    injectCommand("M20");
}

void MainWindow::on_sendBtn_clicked()
{
    userCommands.clear();
    if(sending && !sdprinting)
    {
        sending = false;
        ui->sendBtn->setText("Send");
        ui->pauseBtn->setText("Pause");
        ui->pauseBtn->setDisabled(true);
        if(autolock) ui->controlBox->setChecked(true);
        paused = false;
    }
    else if(!sending && !sdprinting)
    {
        sending=true;
        ui->sendBtn->setText("Stop");
        ui->pauseBtn->setText("Pause");
        ui->pauseBtn->setEnabled(true);
        if(autolock) ui->controlBox->setChecked(false);
        paused = false;
    }
    else if(sdprinting)
    {
        sending = false;
        injectCommand("M24");
        injectCommand("M27");
        ui->sendBtn->setText("Start");
        ui->pauseBtn->setText("Pause");
        ui->pauseBtn->setEnabled(true);
        if(autolock) ui->controlBox->setChecked(true);
        paused = false;
    }

    ui->progressBar->setValue(0);
    currentLine = 0;
}

void MainWindow::on_pauseBtn_clicked()
{
    if(paused && !sdprinting)
    {
        paused = false;
        if(autolock) ui->controlBox->setChecked(false);
        ui->pauseBtn->setText("Pause");
    }
    else if(!paused && !sdprinting)
    {
        paused = true;
        if(autolock) ui->controlBox->setChecked(true);
        ui->pauseBtn->setText("Resume");
    }
    else if(sdprinting)
    {
        injectCommand("M25");
    }
}

void MainWindow::on_releasebtn_clicked()
{
    injectCommand("M84");
}
/////////////////
//Buttons end  //
/////////////////

void MainWindow::readSerial()
{
    if(printer.canReadLine()) //Check if full line in buffer
    {
        QByteArray data = printer.readLine(); //Read the line

        emit recievedData(data); //Send data to parser thread

        if(data.startsWith("ok")) readyRecieve++;
        else if(data.startsWith("wa")) readyRecieve=1;

        printMsg(QString(data)); //echo
    }
}

void MainWindow::printMsg(const char* text)
{
    QTextCursor cursor = ui->terminal->textCursor();
    cursor.movePosition(QTextCursor::End);

    cursor.insertText(text);

    ui->terminal->setTextCursor(cursor);
;
}

void MainWindow::printMsg(QString text)
{
    QTextCursor cursor = ui->terminal->textCursor();
    cursor.movePosition(QTextCursor::End);

    cursor.insertText(text);

    ui->terminal->setTextCursor(cursor);
}



void MainWindow::sendNext()
{
    if(!userCommands.isEmpty() && printer.isWritable() && readyRecieve > 0) //Inject user command
    {
        sendLine(userCommands.dequeue());
        readyRecieve--;
        return;
    }
    else if(sending && !paused && readyRecieve > 0 && !sdprinting && printer.isWritable()) //Send line of gcode
    {
        if(currentLine >= gcode.size()) //check if we are at the end of array
        {
            sending = false;
            currentLine = 0;
            ui->sendBtn->setText("Send");
            ui->pauseBtn->setDisabled(true);
            ui->filelines->setText(QString::number(gcode.size())
                                   + QString("/")
                                   + QString::number(currentLine)
                                   + QString(" Lines"));
            if(sendingChecksum) injectCommand("M110 N0");
            return;
        }
        sendLine(gcode.at(currentLine));
        currentLine++;
        readyRecieve--;

        ui->filelines->setText(QString::number(gcode.size())
                               + QString("/")
                               + QString::number(currentLine)
                               + QString(" Lines"));
        ui->progressBar->setValue(((float)currentLine/gcode.size()) * 100);
    }
}

void MainWindow::checkStatus()
{
    if(checkingTemperature
            &&(sinceLastTemp.elapsed() > statusTimer.interval())) injectCommand("M105");
}

void MainWindow::on_checktemp_stateChanged(int arg1)
{
    if(arg1) checkingTemperature = true;
    else checkingTemperature = false;
}

void MainWindow::on_actionSettings_triggered()
{
    SettingsWindow settingswindow(this);

    settingswindow.exec();
}

void MainWindow::on_actionAbout_triggered()
{
    AboutWindow aboutwindow(this);

    aboutwindow.exec();
}

void MainWindow::injectCommand(QString command)
{
    if(!userCommands.contains(command)) userCommands.enqueue(command);
}

void MainWindow::updateRecent()
{
    if(!recentFiles.isEmpty()) //If array is empty we should not be bothered
    {
        recentMenu->clear(); //Clear menu from previos actions
        foreach (QString str, recentFiles)
        {
            if (!str.isEmpty()) //Empty strings are not needed
            {
                QAction *action = new QAction(this);
                action->setText(str); //Set filepath as a title
                action->setObjectName(str); //Also set name to the path so we can get it later
                recentMenu->addAction(action); //Add action to the menu
                connect(action, SIGNAL(triggered()), this, SLOT(recentClicked()));
            }
        }
    }
}

void MainWindow::serialError(QSerialPort::SerialPortError error)
{
    if(error == QSerialPort::NoError) return;
    if(error == QSerialPort::NotOpenError) return; //this error is internal

    if(printer.isOpen()) printer.close();

    if(sending) paused = true;

    userCommands.clear();

    ui->connectBtn->setText("Connect");
    ui->sendBtn->setDisabled(true);
    ui->pauseBtn->setDisabled(true);
    ui->controlBox->setDisabled(true);
    ui->consoleGroup->setDisabled(true);
    ui->actionPrint_from_SD->setDisabled(true);
    ui->actionSet_SD_printing_mode->setDisabled(true);
    ui->actionEEPROM_editor->setDisabled(true);

    qDebug() << error;

    QString errorMsg;
    switch(error)
    {
    case QSerialPort::DeviceNotFoundError:
        errorMsg = "Device not found";
        break;

    case QSerialPort::PermissionError:
        errorMsg = "Insufficient permissions\nAlready opened?";
        break;

    case QSerialPort::OpenError:
        errorMsg = "Cant open port\nAlready opened?";
        break;

    case QSerialPort::TimeoutError:
        errorMsg = "Serial connection timed out";
        break;

    case QSerialPort::WriteError:
    case QSerialPort::ReadError:
        errorMsg = "I/O Error";
        break;

    case QSerialPort::ResourceError:
        errorMsg = "Disconnected";
        break;

    default:
        errorMsg = "Unknown error\nSomething went wrong";
        break;
    }

    ErrorWindow errorwindow(this, errorMsg);
    errorwindow.exec();
}

void MainWindow::updateTemperature(TemperatureReadings r)
{
    ui->extruderlcd->display(r.e);
    ui->bedlcd->display(r.b);
    ui->tempLine->setText(r.raw);
    sinceLastTemp.restart();
}

void MainWindow::on_actionAbout_Qt_triggered()
{
    qApp->aboutQt();
}

void MainWindow::initSDprinting(QStringList sdFiles)
{
    SDWindow sdwindow(sdFiles, this); //Made it to 666 lines!

    connect(&sdwindow, SIGNAL(fileSelected(QString)), this, SLOT(selectSDfile(QString)));

    sdwindow.exec();
}


void MainWindow::selectSDfile(QString file)
{
    QStringList split = file.split(' ');
    QString bytes = split.at(split.size()-1);
    QString filename = file.remove(" "+bytes);

    ui->filename->setText(filename);
    if(chekingSDStatus)
    {
        ui->filelines->setText(bytes + QString("/0 bytes"));
        ui->progressBar->setEnabled(true);
        ui->progressBar->setValue(0);
    }
    else ui->progressBar->setDisabled(true);
    ui->sendBtn->setText("Start");
    sdBytes = bytes.toDouble();

    userCommands.clear();
    injectCommand("M23 " + filename);
    sdprinting = true;
    ui->fileBox->setDisabled(false);
}

void MainWindow::updateSDStatus(SDProgress p)
{
    ui->filelines->setText(QString::number(p.progress)
                           + QString("/")
                           + QString::number(p.total)
                           + QString(" bytes"));
    if(p.progress != 0) ui->progressBar->setValue(((double)p.progress/p.total) * 100);
    else ui->progressBar->setValue(0);
    if(p.total == p.progress) sdprinting = false;
    sinceLastSDStatus.restart();
}

void MainWindow::checkSDStatus()
{
    if(sdprinting && chekingSDStatus && sinceLastSDStatus.elapsed() > progressSDTimer.interval())
        injectCommand("M27");
}

void MainWindow::on_stepspin_valueChanged(const QString &arg1)
{
    if(arg1.toFloat() < 1) ui->stepspin->setSingleStep(0.1);
    else if(arg1.toFloat() >=10) ui->stepspin->setSingleStep(10);
    else if(arg1.toFloat() >= 1) ui->stepspin->setSingleStep(1);
}

void MainWindow::on_estepspin_valueChanged(const QString &arg1)
{
    if(arg1.toFloat() < 1) ui->estepspin->setSingleStep(0.1);
    else if(arg1.toFloat() >=10) ui->estepspin->setSingleStep(10);
    else if(arg1.toFloat() >= 1) ui->estepspin->setSingleStep(1);
}

void MainWindow::on_actionSet_SD_printing_mode_triggered()
{
    sdprinting = true;
    ui->fileBox->setDisabled(false);
}

void MainWindow::requestEEPROMSettings()
{
    userCommands.clear();
    EEPROMSettings.clear();

    switch(firmware)
    {
    case Marlin:
        injectCommand("M503");
        break;
    case Repetier:
        injectCommand("M205");
        break;
    case OtherFirmware:
        return;
    }

   emit startedReadingEEPROM();
}

void MainWindow::on_actionEEPROM_editor_triggered()
{
    requestEEPROMSettings();
}

void MainWindow::openEEPROMeditor()
{
    EEPROMWindow eepromwindow(EEPROMSettings, this);

    eepromwindow.setWindowModality(Qt::NonModal);
    connect(&eepromwindow, SIGNAL(changesComplete(QStringList)), this, SLOT(sendEEPROMsettings(QStringList)));

    eepromwindow.exec();
}

void MainWindow::sendEEPROMsettings(QStringList changes)
{
    userCommands.clear();
    foreach (QString str, changes)
    {
        injectCommand(str);
    }
}

void MainWindow::recievedOkNum(int num)
{
    readyRecieve++;
    lastRecieved = num;
}

void MainWindow::recievedWait()
{
    readyRecieve = 1;
}

void MainWindow::EEPROMSettingRecieved(QString esetting)
{
    EEPROMSettings.append(esetting);
}

void MainWindow::recievedError()
{
    ErrorWindow errorwindow(this,"Hardware failure");
    errorwindow.exec();
}

void MainWindow::recievedSDDone()
{
    sdprinting=false;
    ui->progressBar->setValue(0);
    ui->filename->setText("");
    ui->fileBox->setDisabled(true);
}

void MainWindow::recievedResend(int num)
{
    if(!sendingChecksum) currentLine--;
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    if(obj == ui->sendtext && !userHistory.isEmpty())
    {
        if(event->type() == QEvent::KeyPress)
        {
            QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);

            if (keyEvent->key() == Qt::Key_Up)
            {
                if(++userHistoryPos <= userHistory.size()-1)
                    ui->sendtext->setText(userHistory.at(userHistoryPos));
                else userHistoryPos--;
                return true;
            }
            else if(keyEvent->key() == Qt::Key_Down)
            {
                if(--userHistoryPos >= 0)
                    ui->sendtext->setText(userHistory.at(userHistoryPos));
                else userHistoryPos++;
                return true;
            }
        }
        return false;
    }
    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::recentClicked()
{
    parseFile(sender()->objectName());
}
