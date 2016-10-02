
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
    qWarning() << "Connecting to the client";
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
    qWarning() << "Disconnecting from the client";
    timer->stop();
    //    sharedMem.detach();
}

void rpdThread::decodeSignal() {
    char signal[256] = {0};

    signalReceiver->read(signal,signalReceiver->bytesAvailable());
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
// 2 - set value to specified file
//    signal type + new value + # + file path
//
// 4 - start timer with given interval
// 5 - stop timer
void rpdThread::performTask(const QString &signal) {
    qDebug() << "Performing task: " << signal;

    if (signal.isEmpty()) {
        qWarning() << "Received empty signal";
        return;
    }


    QStringList instructions = signal.split(SEPARATOR, QString::SkipEmptyParts);
    int size = instructions.size();

    // Cycle through instructions
    for (int index = 0; index < size; index++) {
        switch (instructions[index][0].toLatin1()) { // Check the first char (instruction type)

            case SIGNAL_CONFIG: { // SIGNAL_CONFIG + SEPARATOR + CLOCKS_PATH + SEPARATOR
                qDebug() << "Elaborating a CONFIG signal";
                if (index + 1 >= size) {
                    // No other instruction is available
                    // The clocks file path is not present
                    qCritical() << "Received a CONFIG signal with no path: " << signal;
                    break;
                }

                QString filePath = instructions[++index]; // The file path is after the separator
                if (!filePath.startsWith("/sys/kernel/debug/dri/")) {
                    // The file path is not in whitelisted directories
                    // This is a security check to prevent exploiters from reading root files
                    qCritical() << "Illegal clocks data path: " << filePath;
                    break;
                }

                QFileInfo clocksFileInfo(filePath);
                if (!clocksFileInfo.exists()) {
                    // The indicated clocks file path does not exist
                    qCritical() << "Indicated clocks data path does not exist: " << filePath;
                    break;
                }

                if (!clocksFileInfo.isFile()) {
                    // The path is a directory, not a file
                    qCritical() << "Indicated clocks data path is not a valid file: " << filePath;
                    break;
                }

                qDebug() << "The new clocks data path is " << filePath;
                clocksDataPath = filePath; // Remember the path for the next readings

                // The clocks data file will be copied in /tmp/
                // Let's calculate the destination file path
                QString destination="/tmp/" + clocksFileInfo.fileName();
                // Example: /sys/kernel/debug/dri/0/radeon_pm_info -> /tmp/radeon_pm_info
                qDebug() << clocksDataPath << " will be copied into " << destination;

                system(QString("cp " + clocksDataPath + " " + destination).toStdString().c_str());
            }
                break;


            case SIGNAL_READ_CLOCKS: { // SIGNAL_READ_CLOCKS + SEPARATOR
                qDebug() << "Elaborating a READ_CLOCKS signal";
                readData();
            }
                break;


            case SIGNAL_SET_VALUE: { // SIGNAL_SET_VALUE + SEPARATOR + VALUE + SEPARATOR + PATH + SEPARATOR
                qDebug() << "Elaborating a SET_VALUE signal";
                if (index < (size - 2)) {
                    const QString value=instructions[++index], path=instructions[++index];
                    setNewValue(path, value);
                } else // Value and/or path are not indicated
                    qWarning() << "Received a SET_VALUE signal with no path: " << signal;
            }
                break;


            case SIGNAL_TIMER_ON: { // SIGNAL_TIMER_ON + SEPARATOR + INTERVAL + SEPARATOR
                qDebug() << "Elaborating a TIMER_ON signal";
                if (index < (size - 1)) {
                    QString input = instructions[++index]; // Seconds string
                    int inputMillis = input.toInt(); // Seconds integer

                    if (inputMillis > 0) { // If seconds have been parsed correctly and the value is valid
                        qDebug() << "Setting up timer with seconds interval: " << inputMillis;
                        timer->start(inputMillis * 1000); // Config and start the timer
                    } else
                        qCritical() << "Invalid value TIMER_ON value: " << input;
                } else
                    qCritical() << "Received TIMER_ON signal with no interval: " << signal;
            }
                break;


            case SIGNAL_TIMER_OFF:{ // SIGNAL_TIMER_OFF + SEPARATOR
                qDebug() << "Elaborating a TIMER_OFF signal";
                timer->stop();
            }
                break;


            default:
                qWarning() << "Unknown signal received: " << signal;
                break;
        }
    }

}

void rpdThread::readData() {
    if (sharedMem.isAttached() || sharedMem.attach()) {

        QFile f(clocksDataPath);
        QByteArray data;
        if (f.open(QIODevice::ReadOnly)) {
            qDebug() << "Reading file: " << clocksDataPath;
            data = f.readAll();
            f.close();
            if(data.isEmpty())
                qWarning() << "Unable to read file " << clocksDataPath;
        } else
            qWarning() << "Unable to open file " << clocksDataPath;

        if (sharedMem.lock()) {
            char *to = (char*)sharedMem.data();
            if (to != NULL)
                memcpy(sharedMem.data(), data.constData(), sharedMem.size());
            else
                qWarning() << "Shared memory data pointer is invalid: " << sharedMem.errorString();
            sharedMem.unlock();
        } else
            qWarning() << "Shared memory can't be locked, can't write data: " << sharedMem.errorString();
    } else
        qWarning() << "Shared memory is not attached, can't write data: " << sharedMem.errorString();
}

void rpdThread::setNewValue(const QString &filePath, const QString &newValue) {
    if (filePath.isEmpty()) {
        // The specified file path is invalid
        qWarning() << "The file path indicated to be set is empty. Lost value: " << newValue;
        return;
    }

    if (!filePath.startsWith("/sys/class/drm/")) {
        // The file path is not in whitelisted directories
        // This is a security check to prevent exploiters from writing files as root system wide
        qWarning() << "Illegal path to be set: " << filePath;
        return;
    }

    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists()) {
        // The indicated path does not exist
        qWarning() << "The ndicated path to be set does not exist: " << filePath;
        return;
    }

    if (!fileInfo.isFile()) {
        // The path is a directory, not a file
        qWarning() << "Indicated path to be set is not a valid file: " << filePath;
        return;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        // If the file does not open successfully
        qWarning() << "Failed to open the file to be set: " << filePath;
        return;
    }

    qDebug() << newValue << " will be written into " << filePath;
    QTextStream stream(&file);
    stream << newValue + "\n";

    if (!file.flush())
        // If writing down the changes fails
        qWarning() << "Failed writing in " << filePath;

    file.close();
}

void rpdThread::figureOutGpuDataPaths(const QString &gpuIndex) {
    clocksDataPath = "/sys/kernel/debug/dri/"+gpuIndex+"/radeon_pm_info";
}
