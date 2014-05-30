
// copyright marazmista @ 12.05.2014

#include "rpdthread.h"
#include <QFile>
#include <QDebug>

rpdThread::rpdThread() : QThread() {
    QLocalServer::removeServer(serverName);
    daemonServer = new QLocalServer(this);
    signalReceiver = new QLocalSocket(this);

    daemonServer->listen(serverName);
    connect(daemonServer,SIGNAL(newConnection()),this,SLOT(newConn()));
    qDebug() << "ok";
    QFile::setPermissions("/tmp/"+serverName,QFile("/tmp/"+serverName).permissions() | QFile::WriteOther | QFile::ReadOther);

    timer = new QTimer();
    connect(timer,SIGNAL(timeout()),this,SLOT(onTimer()));
}

void rpdThread::newConn() {
    signalReceiver = daemonServer->nextPendingConnection();
    connect(signalReceiver,SIGNAL(readyRead()),this,SLOT(decodeSignal()));

    sharedMem.setKey("radeon-profile");
    if (!sharedMem.isAttached()) {
        sharedMem.attach();
    }
}

void rpdThread::decodeSignal() {
    char signal[16] = {0};

    signalReceiver->read(signal,signalReceiver->bytesAvailable());
//    qDebug() << signal;
    performTask(QString(signal));
}

void rpdThread::onTimer() {
    if (signalReceiver->state() == QLocalSocket::ConnectedState) {
        readData();
    }
}

// the signal comes in and:
// 0 - config (card index)
// 1 - give me the clocks
// 2 - set dmp profile. Example: "20battery" means:
//      2 - signal type
//      0 - card index
//      battery - profile
// 3 - force power level. Example: "30auto" means:
//      3 - signal type
//      0 - card index
//      auto - power level
// 4 - start timer with given interval
// 5 - stop timer
void rpdThread::performTask(const QString &signal) {
    if (signal.isEmpty())
        return;

    if (signal[0] == '0') {
        QString decodedSignal = QString(signal);
        figureOutGpuDataPaths(decodedSignal.at(1));
        if (decodedSignal.length() > 2 && decodedSignal.at(2) == '4') {
            decodedSignal.remove(0,3);
            timer->setInterval(decodedSignal.toInt() * 1000);
            timer->start();
        }
    } else if (signal[0] == '1') {
        readData();
    } else if (signal[0] == '2') {
        QString decodedSignal = QString(signal);
        decodedSignal.remove(0,1);
        setNewValue(gpuDataPaths.powerProfilePath,decodedSignal);
    } else if (signal[0] == '3') {
        QString decodedSignal = QString(signal);
        decodedSignal.remove(0,1);
        setNewValue(gpuDataPaths.powerLevelPath,decodedSignal);
    } else if (signal[0] == '4') {
        QString decodedSignal = QString(signal);
        timer->setInterval(decodedSignal.remove(0,1).toInt() * 1000);
        timer->start();
    } else if (signal[0] == '5') {
        timer->stop();
    }
    readData();
}

void rpdThread::readData() {
    if (!sharedMem.isAttached())
        return;

    QFile f(gpuDataPaths.clocksDataPath);
    QString data;
    if (f.open(QIODevice::ReadOnly)) {
        data = f.readAll();
        f.close();
    } else
        data = "null";

    sharedMem.lock();
    char *to = (char*)sharedMem.data();
    const char *text = data.toStdString().c_str();
    memcpy(to, text, strlen(text)+1);
    sharedMem.unlock();
}

void rpdThread::setNewValue(const QString &filePath, const QString &newValue) {
    QFile file(filePath);
    file.open(QIODevice::WriteOnly | QIODevice::Text);
    QTextStream stream(&file);
    stream << newValue + "\n";
    file.flush();
    file.close();
}

void rpdThread::figureOutGpuDataPaths(const QString &gpuIndex) {
    gpuDataPaths.clocksDataPath = "/sys/kernel/debug/dri/"+gpuIndex+"/radeon_pm_info";
    gpuDataPaths.powerLevelPath = "/sys/class/drm/card"+gpuIndex+"/device/power_dpm_force_performance_level";
    gpuDataPaths.powerProfilePath = "/sys/class/drm/card"+gpuIndex+"/device/power_dpm_state";
}
