
// copyright marazmista @ 12.05.2014

#include "rpdthread.h"
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QStringList>
#include <QProcess>
#include <QDataStream>

rpdThread::rpdThread() : QThread(),
    signalReceiver(nullptr),
    connectionConfirmed(false)
{
    qInfo() << "radeon-profile-daemon (v. " + appVersion + ")";
    qDebug() << "Starting in debug mode";

    createServer();
    connect(&daemonServer,SIGNAL(newConnection()),this,SLOT(newConnection()));
    connect(&timer,SIGNAL(timeout()),this,SLOT(onTimer()));

    connectionCheckTimer.setInterval(20000);
    connect(&connectionCheckTimer, SIGNAL(timeout()), this, SLOT(checkConnection()));

}

void rpdThread::newConnection() {
    qInfo() << "Connecting to the client";
    signalReceiver = daemonServer.nextPendingConnection();
    connect(signalReceiver,SIGNAL(readyRead()),this,SLOT(readSignalAndPerformTask()));
    connect(signalReceiver,SIGNAL(disconnected()),this,SLOT(disconnected()));

    connectionConfirmed = true;
    connectionCheckTimer.start();

    // close server, to avoid multiple connections
    daemonServer.close();
}

void rpdThread::createServer()
{
    if (daemonServer.isListening())
        return;

    qInfo() << "Awaiting connections...";

    QLocalServer::removeServer(serverName);
    daemonServer.listen(serverName);
    QFile::setPermissions("/tmp/" + serverName, QFile("/tmp/" + serverName).permissions() | QFile::WriteOther | QFile::ReadOther);
}

void rpdThread::closeConnection()
{
    timer.stop();

    if (sharedMem.isAttached())
        sharedMem.detach();

    signalReceiver->close();
    signalReceiver->deleteLater();

    connectionCheckTimer.stop();
    connectionConfirmed = false;

    // create server for new connections
    createServer();
}

void rpdThread::disconnected() {
    qInfo() << "Client disconnected";

    closeConnection();
}

void rpdThread::sendMessage(const QString &msg)
{
    QByteArray feedback;
    QDataStream out(&feedback, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_5_7);

    out << msg;

    if (signalReceiver != nullptr)
        signalReceiver->write(feedback);
}

void rpdThread::checkConnection() {
    if (!connectionConfirmed) {
        qDebug() << "Confirmation not received, closing connection";

        resetSystemDefaults();
        closeConnection();
        return;
    }

    qDebug() << "Asking connection confirmation...";
    connectionConfirmed = false;

    sendMessage("7#1#");
}

void rpdThread::onTimer() {
    if (signalReceiver->state() == QLocalSocket::ConnectedState)
        readData();
}

// the signal comes in and:
// 0 - config (card index)
// 1 - give me the clocks
// 2 - set value to specified file
//    signal type + new value + # + file path
//
// 4 - start timer with given interval
// 5 - stop timer
// 6 - shared mem key
// 7 - alive msg
void rpdThread::readSignalAndPerformTask() {
    const auto signal = QString(signalReceiver->readAll());
    qDebug() << "Received signal: " << signal;

    if (signal.isEmpty()) {
        qWarning() << "Received empty signal";
        return;
    }

    // every received signal confirms connection
    if (connectionCheckTimer.isActive()) {
        connectionConfirmed = true;
        connectionCheckTimer.start();
    }

    bool setValueSucces = false;
    QString confirmationMsg;

    QStringList instructions = signal.split(SEPARATOR);
    instructions.removeLast();

    int size = instructions.size();

    // Cycle through instructions
    for (int index = 0; index < size; index++) {

        // Check the first char (instruction type)
        switch (instructions.at(index)[0].toLatin1()) {

                // SIGNAL_CONFIG + SEPARATOR + CLOCKS_PATH + SEPARATOR
            case SIGNAL_CONFIG: {
                qDebug() << "Elaborating a CONFIG signal";

                if (!checkRequiredCommandLength(2, index, size)) {
                    qCritical() << "Invalid command! (index out of bounds)";
                    return;
                }

                const auto type = instructions.at(++index),
                        filePath = instructions.at(++index);

                if (!configure(type, filePath))
                    qWarning() << "Configuration failed.";

                }
                break;
                // SIGNAL_READ_CLOCKS + SEPARATOR
            case SIGNAL_READ_CLOCKS:
                qDebug() << "Elaborating a READ_CLOCKS signal";
                readData();
                break;

                // SIGNAL_SET_VALUE + SEPARATOR + VALUE + SEPARATOR + PATH + SEPARATOR
            case SIGNAL_SET_VALUE: {
                qDebug() << "Elaborating a SET_VALUE signal";

                if (!checkRequiredCommandLength(2, index, size)) {
                    qCritical() << "Invalid command! (index out of bounds)";
                    return;
                }

                const auto value = instructions.at(++index),
                        path = instructions.at(++index);

                if (setNewValue(path, value)) {
                        setValueSucces = true;
                        confirmationMsg.append(value).append(SEPARATOR);
                }

                break;
            }

                // SIGNAL_TIMER_ON + SEPARATOR + INTERVAL + SEPARATOR
            case SIGNAL_TIMER_ON: {
                qDebug() << "Elaborating a TIMER_ON signal";

                if (!checkRequiredCommandLength(1, index, size)) {
                    qCritical() << "Invalid command! (index out of bounds)";
                    return;
                }

                int inputMillis = instructions.at(++index).toInt(); // Seconds integer

                if (inputMillis < 1) {
                    qCritical() << "Invalid value TIMER_ON value: " << instructions.at(index);
                    break;
                }

                qDebug() << "Setting up timer with seconds interval: " << inputMillis;
                timer.start(inputMillis * 1000); // Config and start the timer
                break;
            }

                // SIGNAL_TIMER_OFF + SEPARATOR
            case SIGNAL_TIMER_OFF:
                qDebug() << "Elaborating a TIMER_OFF signal";
                timer.stop();
                break;

            case SIGNAL_SHAREDMEM_KEY: {

                if (!checkRequiredCommandLength(1, index, size)) {
                    qCritical() << "Invalid command! (index out of bounds)";
                    return;
                }

                const auto key = instructions.at(++index);
                qDebug() << "Shared memory key: " << key;
                configureSharedMem(key);
                break;
            }
            case SIGNAL_ALIVE:
                qDebug() << "ALIVE signal received";

                if (!checkRequiredCommandLength(1, index, size)) {
                    qCritical() << "Invalid command! (index out of bounds)";
                    return;
                }

                if (instructions.at(++index) == "0") {
                    qDebug() << "Connection confirmation is disabled";
                    connectionConfirmed = true;
                    connectionCheckTimer.stop();
                }

                break;
            default:
                qWarning() << "Unknown signal received: " << signal;
                break;
        }

    }

    if (setValueSucces)
        sendMessage(confirmationMsg);

}

bool rpdThread::checkRequiredCommandLength(unsigned required, unsigned currentIndex, unsigned size) {
    return !(size <= currentIndex + required);
}

bool rpdThread::checkPathValidity(const QString &filePath) {
    // The file path is not in whitelisted directories
    // This is a security check to prevent exploiters from reading root files
    if (!filePath.startsWith("/sys/kernel/debug/dri/") && !filePath.startsWith("/sys/class/drm/")) {
        qCritical() << "Illegal path: " << filePath;
        return false;
    }

    return true;
}

bool rpdThread::configure(const QString &type, const QString &filePath) {
    if (!checkPathValidity(filePath))
        return false;

    if (type == "pm_info") {
        QFileInfo clocksFileInfo(filePath);
        if (!clocksFileInfo.exists()) {
            // The indicated clocks file path does not exist
            qCritical() << "Indicated clocks data path does not exist: " << filePath;
            return false;
        }

        if (!clocksFileInfo.isFile()) {
            // The path is a directory, not a file
            qCritical() << "Indicated clocks data path is not a valid file: " << filePath;
            return false;
        }

        qDebug() << "The new clocks data path is " << filePath;
        clocksDataPath = filePath; // Remember the path for the next readings

        // The clocks data file will be copied in /tmp/
        // Let's calculate the destination file path
        QString destination = "/tmp/" + clocksFileInfo.fileName();
        // Example: /sys/kernel/debug/dri/0/radeon_pm_info -> /tmp/radeon_pm_info
        qDebug() << clocksDataPath << " will be copied into " << destination;

        system(QString("cp " + clocksDataPath + " " + destination).toStdString().c_str());

    } else if (type == "pwm1_enable") {
        qDebug() << "Fan control mode file: " << filePath;

        fanControlPath = filePath;
    }

    return true;
}

void rpdThread::readData() {
    if (!sharedMem.isAttached() && !sharedMem.attach()) {
        qWarning() << "Shared memory is not attached, can't write data: " << sharedMem.errorString();
        return;
    }

    QFile f(clocksDataPath);
    QByteArray data;
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "Unable to open file " << clocksDataPath;
        return;
    }

    qDebug() << "Reading file: " << clocksDataPath;
    data = f.readAll();
    f.close();

    if (data.isEmpty()) {
        qWarning() << "Unable to read file " << clocksDataPath;
        return;
    }

    if (!sharedMem.lock()) {
        qWarning() << "Shared memory can't be locked, can't write data: " << sharedMem.errorString();
        return;
    }

    char *to = (char*)sharedMem.data();
    if (to == NULL) {
        qWarning() << "Shared memory data pointer is invalid: " << sharedMem.errorString();
        sharedMem.unlock();
        return;
    }

    memcpy(sharedMem.data(), data.constData(), sharedMem.size());
    sharedMem.unlock();
}

bool rpdThread::setNewValue(const QString &filePath, const QString &newValue) {
    if (filePath.isEmpty()) {
        // The specified file path is invalid
        qWarning() << "The file path indicated to be set is empty. Lost value: " << newValue;
        return false;
    }

    if (!checkPathValidity(filePath)) {
        // The file path is not in whitelisted directories
        // This is a security check to prevent exploiters from writing files as root system wide
        qWarning() << "Illegal path to be set: " << filePath;
        return false;
    }

    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists()) {
        // The indicated path does not exist
        qWarning() << "The ndicated path to be set does not exist: " << filePath;
        return false;
    }

    if (!fileInfo.isFile()) {
        // The path is a directory, not a file
        qWarning() << "Indicated path to be set is not a valid file: " << filePath;
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        // If the file does not open successfully
        qWarning() << "Failed to open the file to be set: " << filePath;
        return false;
    }

    qDebug() << newValue << " will be written into " << filePath;
    QTextStream stream(&file);
    stream << newValue + "\n";

    if (!file.flush())
        // If writing down the changes fails
        qWarning() << "Failed writing in " << filePath;

    file.close();

    return true;
}

void rpdThread::configureSharedMem(const QString &key) {
    sharedMem.setKey(key);

    if (!sharedMem.isAttached()) {
        qDebug() << "Shared memory is not attached, trying to attach";

        if (!sharedMem.attach())
            qCritical() << "Unable to attach to shared memory:" << sharedMem.errorString();

    }
}

void rpdThread::resetSystemDefaults() {
    qWarning() << "Restoring system defaults";

    // reset fan control
    if (!fanControlPath.isEmpty()) {
        qWarning() << "Setting fan control mode to auto";
        setNewValue(fanControlPath, "2");
    }

    // reset other settings...
}
