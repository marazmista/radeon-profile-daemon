#include <QCoreApplication>
#include "rpdthread.h"

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    rpdThread daemon;
    daemon.start();

    return a.exec();
}
