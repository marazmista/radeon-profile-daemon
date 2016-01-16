
// copyright marazmista @ 12.05.2014

#include "rpdthread.h"
#include <QFile>
#include <QDebug>
#include <QStringList>
#include <QProcess>

rpdThread::rpdThread() : QThread() {
    qDebug() << "Starting in debug mode";

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
    connect(signalReceiver,SIGNAL(disconnected()),this,SLOT(disconnected()));

    sharedMem.setKey("radeon-profile");
    if (!sharedMem.isAttached()) {
        if (!sharedMem.attach()) {
            qCritical() << sharedMem.errorString();
            return;
        }
        qDebug() << "connection";
    }
}

void rpdThread::disconnected() {
    qWarning() << "disconnect";
//    sharedMem.detach();
}


void rpdThread::decodeSignal() {
    char signal[256] = {0};

    signalReceiver->read(signal,signalReceiver->bytesAvailable());
    qDebug() << "Received signal: " << signal;
    performTask(QString(signal));
}

void rpdThread::onTimer() {
    qDebug() << "Tick";
    if (signalReceiver->state() == QLocalSocket::ConnectedState) {
        readData();
   }
}

// the signal comes in and:
// 0 - config (card index)
// 1 - give me the clocks
// 2 - set value to specified file
//    signal type + new value + # + file path
//
// 4 - start timer with given interval
// 5 - stop timer
void rpdThread::performTask(const QString &signal) {
    if (signal.isEmpty())
        qWarning() << "Received empty signal";
    else{
        QString decodedSignal = QString(signal);

        switch (decodedSignal[0].toLatin1()) {
        case SIGNAL_CONFIG: {
            qDebug() << "Elaborating a CONFIG signal";
            QStringList s = decodedSignal.split("#",QString::SkipEmptyParts);
            int size=s.size();
            if(size > 1){
                clocksDataPath = s[1];

                qDebug() << "Copying " << clocksDataPath;
                if (QFile::exists(clocksDataPath))
                    system(QString("cp "+ clocksDataPath + " /tmp/").toStdString().c_str());

                // if timer interval also comes in, configure it
                if(size == 3)
                    performTask(s[2]);
                else if(size == 4)
                    performTask(s[2]+s[3]);
                else
                    qCritical() << "Too much instructions in one signal";
            }else{
                qWarning() << "Received a malformed signal: " << signal;
            }
        }
        break;
        case SIGNAL_READ_CLOCKS:{
            qDebug() << "Elaborating a READ_CLOCKS signal";
            readData();
        }
        break;
        case SIGNAL_SET_VALUE: {
            qDebug() << "Elaborating a SET_VALUE signal";
            // when two singlans have been sent one after another instantly, they comes as one singal to daemon
            // so this is handling such things. # and later split, and later dealing with it in while...
            QStringList s = decodedSignal.split("#",QString::SkipEmptyParts);
            int sIdx =0, // singal index
                pIdx = 1; // path index
            while (pIdx < s.count()) {
                setNewValue(s[pIdx],s[sIdx].remove(0,1));
                sIdx = sIdx + 2, pIdx = pIdx + 2;
            };
        }
        break;
        case SIGNAL_TIMER_ON:{            
            qDebug() << "Elaborating a TIMER_ON signal";
            timer->setInterval(decodedSignal.remove(0,1).toInt() * 1000);
            timer->start();
        }
        break;
        case SIGNAL_TIMER_OFF:{            
            qDebug() << "Elaborating a TIMER_OFF signal";
            timer->stop();
        }
        break;
        default:
            qWarning() << "Unknown signal received";
        }
    }
    readData();
}

void rpdThread::readData() {
    if (sharedMem.isAttached()){

    QFile f(clocksDataPath);
    QString data;
    qDebug() << "Opening file: " << clocksDataPath;
    if (f.open(QIODevice::ReadOnly)) {
        data = f.readAll();
        f.close();
    } else{
        data = "null";
        qCritical() << "Unable to read file " << clocksDataPath;
    }
    qDebug() << "Writing into shared memory: " << data;

    if (sharedMem.lock()) {
        char *to = (char*)sharedMem.data();
        char *text = data.toLatin1().data();
        memcpy(to, text, strlen(text)+1);
        sharedMem.unlock();
        qDebug() << "Written into shared memory: " << to;
    } else
        qCritical() << sharedMem.errorString();
    }else
        qCritical() << sharedMem.errorString();
}

void rpdThread::setNewValue(const QString &filePath, const QString &newValue) {
    // limit potetntial vulnerability of writing files as root system wide
    if (!filePath.contains("/sys/class/drm/"))
        return;

    QFile file(filePath);
    file.open(QIODevice::WriteOnly | QIODevice::Text);
    QTextStream stream(&file);
    stream << newValue + "\n";
    file.flush();
    file.close();
}

void rpdThread::figureOutGpuDataPaths(const QString &gpuIndex) {
    clocksDataPath = "/sys/kernel/debug/dri/"+gpuIndex+"/radeon_pm_info";
}
