
// copyright marazmista @ 12.05.2014

#include "rpdthread.h"
#include <QFile>
#include <QFileInfo>
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

    timer = new QTimer(this); // Initialize the timer with this class as parent
    connect(timer,SIGNAL(timeout()),this,SLOT(onTimer()));
}

void rpdThread::newConn() {
    qWarning() << "Connecting";
    signalReceiver = daemonServer->nextPendingConnection();
    connect(signalReceiver,SIGNAL(readyRead()),this,SLOT(decodeSignal()));
    connect(signalReceiver,SIGNAL(disconnected()),this,SLOT(disconnected()));

    sharedMem.setKey("radeon-profile");
    if (!sharedMem.isAttached()) {
        qDebug() << "Shared memory is not attached, trying to attach";
        if (!sharedMem.attach())
            qCritical() << "Unable to attach to shared memory:" << sharedMem.errorString();
    }
}

void rpdThread::disconnected() {
    qWarning() << "disconnect";
    timer->stop();
//    sharedMem.detach();
}


void rpdThread::decodeSignal() {
    char signal[256] = {0};

    qDebug() << "Received signal, reading it";
    signalReceiver->read(signal,signalReceiver->bytesAvailable());
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
    qDebug() << "Performing task: " << signal;
    if (signal.isEmpty())
        qWarning() << "Received empty signal";
    else{
        QString decodedSignal = QString(signal);

        switch (decodedSignal[0].toLatin1()) {
        case SIGNAL_CONFIG: {
            qDebug() << "Elaborating a CONFIG signal";
            QStringList s = decodedSignal.split(SEPARATOR,QString::SkipEmptyParts);
            int size=s.size();
            if(size > 1){
                QString filePath=s[1]; // The file path is after the '#'
                if(filePath.startsWith("/sys/kernel/debug/") /* || filePath.startsWith(safe directory)*/){ // Security check to prevent exploiters from reading root files
                    clocksDataPath=filePath;

                    if (QFile::exists(clocksDataPath)){
                        QString destination="/tmp/" + QFileInfo(clocksDataPath).fileName(); // Example: /sys/kernel/debug/dri/0/radeon_pm_info -> /tmp/radeon_pm_info
                        qDebug() << clocksDataPath << " will be copied into " << destination;
                        QFile::remove(destination); // QFile::copy() can't overwrite files, this call cleans the path
                        if( ! QFile::copy(clocksDataPath,destination) ) // If the copying process fails
                            qWarning() << "Failed copying " << clocksDataPath << " to " << destination;
                    }

                    // if timer interval also comes in, configure it
                    if(size > 2){
                        qDebug() << "Found multiple instructions";
                        if(size == 3)
                            performTask(s[2]);
                        else if(size == 4)
                            performTask(s[2]+s[3]);
                        else
                            qCritical() << "Too much instructions in one signal";
                    }
                }else{
                    qWarning() << "Invalid clocks data path: " << clocksDataPath;
                }
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
            QStringList s = decodedSignal.split(SEPARATOR,QString::SkipEmptyParts);
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
            QString input=decodedSignal.remove(0,1).remove(SEPARATOR); // Seconds string
            int inputMillis=input.toInt(); // Seconds integer
            if(inputMillis > 0){ // If seconds have been parsed correctly and the value is valid
                qDebug() << "Setting up timer with seconds interval: " << inputMillis;
                timer->start(inputMillis * 1000); // Config and start the timer
            } else
                qWarning() << "Invalid value TIMER_ON value: " << input;
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

    if (sharedMem.lock()) {
        qDebug() << "Writing into shared memory: " << data;
        char *to = (char*)sharedMem.data();
        char *text = data.toLatin1().data();
        memcpy(to, text, strlen(text)+1);
        sharedMem.unlock();
    } else
        qCritical() << "Can't lock shared memory" << sharedMem.errorString();
    }else
        qCritical() << "Shared memory is not attached: " << sharedMem.errorString();
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
