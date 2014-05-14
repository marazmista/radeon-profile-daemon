
// copyright marazmista @ 12.05.2014

#include "rpdthread.h"
#include <QFile>
#include <QDebug>

rpdThread::rpdThread() : QThread() {
    QLocalServer::removeServer(serverName);
    daemonServer = new QLocalServer(this);
    signalReceiver = new QLocalSocket(this);
    dataSender = new QLocalSocket(this);

    daemonServer->listen(serverName);
    connect(daemonServer,SIGNAL(newConnection()),this,SLOT(newConn()));
    qDebug() << "ok";
    QFile::setPermissions("/tmp/"+serverName,QFile("/tmp/"+serverName).permissions() | QFile::WriteOther | QFile::ReadOther);
}

void rpdThread::newConn() {
    qDebug() << "conn";
    signalReceiver = daemonServer->nextPendingConnection();
    connect(signalReceiver,SIGNAL(readyRead()),this,SLOT(decodeSignal()));

    dataSender->connectToServer(serverGuiName,QLocalSocket::ReadWrite);
}

void rpdThread::decodeSignal() {
    char signal[16] = {0};
    signalReceiver->read(signal,signalReceiver->bytesAvailable());

    qDebug() <<signal;

    performTask(QString(signal));
}

// the signal comes in and:
// 1 - give me the clocks
// 2 - set dmp profile. Example: "20battery" means:
//      2 - signal type
//      0 - card index
//      battery - profile
// 3 - force power level. Example: "30auto" means:
//      3 - signal type
//      0 - card index
//      auto - power level
void rpdThread::performTask(const QString &signal) {
    if (signal[0] == '1') {
        if (dataSender->state() == QLocalSocket::ConnectedState) {
            QFile f("/sys/kernel/debug/dri/"+QString(signal[1])+"/radeon_pm_info");
            qDebug() << "Sending";
            QString data = "CD\n";
            if (f.open(QIODevice::ReadOnly)) {
                data += f.readAll();
                qDebug() << data;
                f.close();
            } else
                data = "Daemon can't read data.";

            dataSender->write(data.toAscii(),data.length());
        }
    }  else if (signal[0] == '2') {
        QString filePath = "/sys/class/drm/card"+QString(signal[1])+"/device/power_dpm_state";
        QString decodedSignal = QString(signal);
        decodedSignal.remove(0,2);
        setNewValue(filePath,decodedSignal);
    } else if (signal[0] == '3') {
        QString filePath = "/sys/class/drm/card"+QString(signal[1])+"/device/power_dpm_force_performance_level";
        QString decodedSignal = QString(signal);
        decodedSignal.remove(0,2);
        setNewValue(filePath,decodedSignal);
    }
}

void rpdThread::setNewValue(const QString &filePath, const QString &newValue) {
    QFile file(filePath);
    file.open(QIODevice::WriteOnly | QIODevice::Text);
    QTextStream stream(&file);
    stream << newValue + "\n";
    file.flush();
    file.close();
}
