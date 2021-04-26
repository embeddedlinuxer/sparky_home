#ifndef SERIALSETTING_H
#define SERIALSETTING_H

#include <QWidget>
#include <QDialog>
#include "imodbus.h"
#include "modbus.h"

namespace Ui {
class serialsetting;
}

class serialsetting : public QDialog, public IModbus
{
    Q_OBJECT

public:
    explicit serialsetting(QWidget *parent = nullptr);
    ~serialsetting();

    Ui::serialsetting *ui;
    modbus_t*  modbus() { return m_serialModbus; }
    modbus_t * m_serialModbus;
    int setupModbusPort();
    void changeModbusInterface(const QString &port, char parity);
    void releaseSerialModbus();

signals:
    void serialPortActive(bool active);
    void connectionError(const QString &msg);

private slots:
    void changeSerialPort(int);
    void onButtonBoxAccepted(bool);

};

#endif // SERIALSETTING_H
