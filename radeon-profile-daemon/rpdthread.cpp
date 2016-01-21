
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
        QStringList instructions = signal.split(SEPARATOR, QString::SkipEmptyParts);
        int size = instructions.size();
        for(int index = 0; index < size; index++){ // Cycle through instructions
            switch (instructions[index][0].toLatin1()) { // Check the first char (instruction type)

            case SIGNAL_CONFIG: { // SIGNAL_CONFIG + SEPARATOR + CLOCKS_PATH + SEPARATOR
                qDebug() << "Elaborating a CONFIG signal";
                if(index < (size - 1)){ // Another instruction (the new clocks file path) is available
                    QString filePath = instructions[++index]; // The file path is after the separator
                    if(filePath.startsWith("/sys/kernel/debug/dri/")){ // Security check to prevent exploiters from reading root files
                        QFileInfo clocksFileInfo(filePath);
                        if (clocksFileInfo.exists()){ // The indicated clocks file path exists
                            if(clocksFileInfo.isFile()){ // The path is a file, not a directory

                                qDebug() << "The new clocks data path is " << filePath;
                                clocksDataPath=filePath; // Remember the path for the next readings

                                QString destination="/tmp/" + clocksFileInfo.fileName(); // Example: /sys/kernel/debug/dri/0/radeon_pm_info -> /tmp/radeon_pm_info
                                qDebug() << clocksDataPath << " will be copied into " << destination;
                                QFile::remove(destination); // QFile::copy() can't overwrite files, this call cleans the path
                                if( ! QFile::copy(clocksDataPath,destination) ) // If the copying process fails
                                    qCritical() << "Failed copying " << filePath;

                            } else // If the path is a directory
                                qCritical() << "Indicated clocks data path is not a valid file: " << filePath;
                        } else // If the path does not exist
                            qCritical() << "Indicated clocks data path does not exist: " << filePath;
                    } else // If the path is not in whitelisted directories
                        qCritical() << "Invalid clocks data path: " << filePath;
                } else // If the clocks file path is not present
                    qCritical() << "Received a CONFIG signal with no path: " << signal;
            } break;


            case SIGNAL_READ_CLOCKS:{ // SIGNAL_READ_CLOCKS + SEPARATOR
                qDebug() << "Elaborating a READ_CLOCKS signal";
                readData();
            } break;


            case SIGNAL_SET_VALUE: { // SIGNAL_SET_VALUE + SEPARATOR + VALUE + SEPARATOR + PATH + SEPARATOR
                qDebug() << "Elaborating a SET_VALUE signal";
                if(index < (size - 2)){
                    const QString value=instructions[++index], path=instructions[++index];
                    setNewValue(path, value);
                } else // Value and/or path are not indicated
                    qWarning() << "Received a SET_VALUE signal with no path: " << signal;
            } break;


            case SIGNAL_TIMER_ON:{ // SIGNAL_TIMER_ON + SEPARATOR + INTERVAL + SEPARATOR
                qDebug() << "Elaborating a TIMER_ON signal";
                if(index < (size - 1)){
                    QString input=instructions[++index]; // Seconds string
                    int inputMillis=input.toInt(); // Seconds integer
                    if(inputMillis > 0){ // If seconds have been parsed correctly and the value is valid
                        qDebug() << "Setting up timer with seconds interval: " << inputMillis;
                        timer->start(inputMillis * 1000); // Config and start the timer
                    } else
                        qCritical() << "Invalid value TIMER_ON value: " << input;
                } else
                    qCritical() << "Received TIMER_ON signal with no interval: " << signal;
            } break;


            case SIGNAL_TIMER_OFF:{ // SIGNAL_TIMER_OFF + SEPARATOR
                qDebug() << "Elaborating a TIMER_OFF signal";
                timer->stop();
            } break;


            default:
                qWarning() << "Unknown signal received: " << signal;
            }
        }
    }
}

void rpdThread::readData() {
    if (sharedMem.isAttached() || sharedMem.attach()){

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
            if(to != NULL)
                memcpy(sharedMem.data(), data.constData(), SHARED_MEM_SIZE);
            else
                qWarning() << "Shared memory data pointer is invalid: " << sharedMem.errorString();
            sharedMem.unlock();
        } else
            qWarning() << "Shared memory can't be locked, can't write data: " << sharedMem.errorString();
    } else
        qWarning() << "Shared memory is not attached, can't write data: " << sharedMem.errorString();
}

void rpdThread::setNewValue(const QString &filePath, const QString &newValue) {
    // limit potetntial vulnerability of writing files as root system wide
    if (filePath.startsWith("/sys/class/drm/")){
        QFileInfo fileInfo(filePath);
        if (fileInfo.exists()){ // The indicated path exists
            if(fileInfo.isFile()){ // The path is a file, not a directory
                QFile file(filePath);
                if(file.open(QIODevice::WriteOnly | QIODevice::Text)){ // If the file opens successfully

                    qDebug() << newValue << " will be written into " << filePath;
                    QTextStream stream(&file);
                    stream << newValue + "\n";
                    if(!file.flush()) // If writing down the changes fails
                        qWarning() << "Failed writing in " << filePath;
                    file.close();

                } else // Failed to open the file
                    qWarning() << "Failed to open " << filePath;
            } else // The path is a directory
                qWarning() << "Indicated path to be set is not a valid file: " << filePath;
        } else // The path does not exist
            qWarning() << "Indicated path to be set does not exist: " << filePath;
    } else // The path is not in the whitelisted directories
        qWarning() << "Invalid path to be set: " << filePath;
}

void rpdThread::figureOutGpuDataPaths(const QString &gpuIndex) {
    clocksDataPath = "/sys/kernel/debug/dri/"+gpuIndex+"/radeon_pm_info";
}
