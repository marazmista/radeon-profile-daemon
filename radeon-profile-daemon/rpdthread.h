
// copyright marazmista @ 12.05.2014

/*
* class for reading stuff about card and communicate with gui
* Things going this way. Daemon runs and listens for connection to daemonServer. 
* If connnection is established, then daemon attach itself to sharedmemory where
* it will put data about the clocks. After, it listens for signals. If signal requests
* clocks data, it place the info in shared mem where gui can read from.
* Shared mem block is owned and created by gui.
* When signal is about changing profile or power level then daemon sets what gui wants
* and that is it.
*/

#ifndef RPDTHREAD_H
#define RPDTHREAD_H

#include <QThread>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSharedMemory>
#include <QTimer>

const QString serverName = "radeon-profile-daemon-server";

class rpdThread : public QThread
{
    Q_OBJECT
public:
    explicit rpdThread();
    ~rpdThread() {
        delete signalReceiver;
        delete daemonServer;
    }

signals:

public slots:
    void newConn();
    void decodeSignal();
    void onTimer();

private:
    QLocalSocket *signalReceiver;
    QLocalServer *daemonServer;
    QSharedMemory sharedMem;
    QTimer *timer;

    struct {
        QString clocksDataPath, powerProfilePath, powerLevelPath;
    } gpuDataPaths;

    void readData();
    void setNewValue(const QString &filePath, const QString &newValue);
    void performTask(const QString &signal);
    void figureOutGpuDataPaths(const QString &gpuIndex);
};

#endif // RPDTHREAD_H
