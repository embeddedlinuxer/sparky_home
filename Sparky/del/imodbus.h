#ifndef IMODBUS_H
#define IMODBUS_H

#include "modbus.h"

class IModbus
{
public:
	virtual modbus_t*  modbus() = 0;
	virtual modbus_t*  modbus_2() = 0;
	virtual modbus_t*  modbus_3() = 0;
	virtual modbus_t*  modbus_4() = 0;
	virtual modbus_t*  modbus_5() = 0;
	virtual modbus_t*  modbus_6() = 0;
	virtual int        setupModbusPort() = 0;
	virtual int        setupModbusPort_2() = 0;
	virtual int        setupModbusPort_3() = 0;
	virtual int        setupModbusPort_4() = 0;
	virtual int        setupModbusPort_5() = 0;
	virtual int        setupModbusPort_6() = 0;
};

#endif // IMODBUS_H
