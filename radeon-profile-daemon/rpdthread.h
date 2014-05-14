
// copyright marazmista @ 12.05.2014

/*
* class for reading stuff about card and communicate with gui
* Things going this way. Daemon runs and listens for connection to daemonServer.
* If connnection is established, then dataSender socket tries to connect to gui server
* to have place when it will send data abouts clocks. After connections are ok, it awaits
* for signals from gui. When signal requesting stuff about clocks comes in, daemon reads the file
* from debugfs and then sends it by dataSender socket which is connected to gui server
* When signal is about changing profile or power level then daemon sets what gui wants
* and that is it.
*/

#ifndef RPDTHREAD_H
#define RPDTHREAD_H

#include <QThread>
#include <QLocalServer>
#include <QLocalSocket>

const QString serverName = "radeon-profile-daemon-server",
    serverGuiName = "radeon-profile-gui-server";

class rpdThread : public QThread
{
    Q_OBJECT
public:
    explicit rpdThread();
    ~rpdThread() {
        delete dataSender;
        delete signalReceiver;
        delete daemonServer;
    }

signals:

public slots:
    void newConn();
    void decodeSignal();

private:
    QLocalSocket *dataSender,*signalReceiver;
    QLocalServer *daemonServer;

    void readData();
    void sendData(const QString);
    void setNewValue(const QString &filePath, const QString &newValue);
    void performTask(const QString &signal);
};

#endif // RPDTHREAD_H
