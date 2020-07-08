
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

#define SEPARATOR '#'
#define SIGNAL_CONFIG '0'
#define SIGNAL_READ_CLOCKS '1'
#define SIGNAL_SET_VALUE '2'
#define SIGNAL_TIMER_ON '4'
#define SIGNAL_TIMER_OFF '5'
#define SIGNAL_SHAREDMEM_KEY '6'
#define SIGNAL_ALIVE '7'

const QString appVersion = "20190603";
const QString serverSocketPath = "/run/radeon-profile-daemon/radeon-profile-daemon-server";

class rpdThread : public QThread
{
    Q_OBJECT
public:
    explicit rpdThread();
    ~rpdThread() = default;

signals:

public slots:
    void newConnection();
    void onTimer();
    void disconnected();
    void checkConnection();
    void readSignalAndPerformTask();

private:
    QLocalSocket *signalReceiver;
    QLocalServer daemonServer;
    QSharedMemory sharedMem;
    QTimer timer, connectionCheckTimer;
    QString clocksDataPath, fanControlPath;
    bool connectionConfirmed;


    void readData();
    bool setNewValue(const QString &filePath, const QString &newValue);
    bool configure(const QString &type, const QString &filePath);
    void configureSharedMem(const QString &key);
    bool checkRequiredCommandLength(unsigned required, unsigned currentIndex, unsigned size);
    void createServer();
    void closeConnection();
    void sendMessage(const QString &msg);
    void resetSystemDefaults();
    bool checkPathValidity(const QString &filePath);
};

#endif // RPDTHREAD_H
