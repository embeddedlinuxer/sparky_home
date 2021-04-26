#include <QSettings>
#include <QDebug>
#include <QMessageBox>
#include <QFile>
#include <QScrollBar>
#include <QFileDialog>
#include <QThread>
#include <errno.h>
#include <QListWidget>
#include "mainwindow.h"
#include "BatchProcessor.h"
#include "modbus.h"
#include "modbus-private.h"
#include "modbus-rtu.h"
#include "ui_mainwindow.h"
#include "qextserialenumerator.h"

#define SLAVE_CALIBRATION   0xFA
#define FUNC_READ_FLOAT     3
#define FUNC_READ_INT       3
#define FUNC_READ_COIL      0
#define FUNC_WRITE_FLOAT    7
#define FUNC_WRITE_INT      5
#define FUNC_WRITE_COIL     4
#define BYTE_READ_FLOAT     2
#define BYTE_READ_INT       1
#define BYTE_READ_COIL      1
#define FLOAT_R             0
#define FLOAT_W             1
#define INT_R               2
#define INT_W               3
#define COIL_R              4
#define COIL_W              5

QT_CHARTS_USE_NAMESPACE

const int DataTypeColumn = 0;
const int AddrColumn = 1;
const int DataColumn = 2;

extern MainWindow * globalMainWin;

MainWindow::MainWindow( QWidget * _parent ) :
	QMainWindow( _parent ),
	ui( new Ui::MainWindowClass ),
	m_modbus( NULL ),
    m_modbus_2( NULL ),
    m_modbus_3( NULL ),
    m_modbus_4( NULL ),
    m_modbus_5( NULL ),
    m_modbus_6( NULL ),
    m_modbus_snipping( NULL ),
    m_serialModbus( NULL ),
    m_serialModbus_2( NULL ),
    m_serialModbus_3( NULL ),
    m_serialModbus_4( NULL ),
    m_serialModbus_5( NULL ),
    m_serialModbus_6( NULL ),
	m_poll(false)
{
	ui->setupUi(this);
    
    updateRegisters(EEA,0); // L1-P1-EEA
    initializeToolbarIcons();
    initializeGauges();
    initializeTabIcons();
    initializeValues();
    updateRegisterView();
    updateRequestPreview();
    enableHexView();
    setupModbusPorts();
    onLoopTabChanged(0);
    updateGraph();
    initializeModbusMonitor();

    ui->regTable->setColumnWidth( 0, 150 );
    m_statusInd = new QWidget;
    m_statusInd->setFixedSize( 16, 16 );
    m_statusText = new QLabel;
    ui->statusBar->addWidget( m_statusInd );
    ui->statusBar->addWidget( m_statusText, 10 );
    resetStatus();

    // INITIALIZE CONNECTIONS 
    connectLoopDependentData();
    connectRegisters();
    connectRadioButtons();
    connectSerialPort();
    connectActions();
    connectModbusMonitor();
    connectTimers();
    connectCalibrationControls();
    connectProfiler();
    connectToolbar();
}


MainWindow::~MainWindow()
{
	delete ui;
}


void
MainWindow::
initializeModbusMonitor()
{
    ui->groupBox_103->setEnabled(TRUE);
    ui->groupBox_106->setEnabled(FALSE);
    ui->groupBox_107->setEnabled(FALSE);
    ui->functionCode->setCurrentIndex(3);
}


void
MainWindow::
initializeToolbarIcons() {

    //ui->toolBar->addAction(ui->actionConnect);
    //ui->toolBar->addAction(ui->actionDisconnect);
    ui->toolBar->addAction(ui->actionOpen);
    ui->toolBar->addAction(ui->actionSave);
    ui->actionDisconnect->setDisabled(TRUE);
    ui->actionConnect->setEnabled(TRUE);
}

void
MainWindow::
keyPressEvent(QKeyEvent* event)
{
	if( event->key() == Qt::Key_Control )
	{
		//set flag to request polling
        if( m_modbus != NULL )	m_poll = true;
        if( ! m_pollTimer->isActive() )	ui->sendBtn->setText( tr("Poll") );
	}
}

void
MainWindow::
keyReleaseEvent(QKeyEvent* event)
{
	if( event->key() == Qt::Key_Control )
	{
		m_poll = false;
        if( ! m_pollTimer->isActive() )	ui->sendBtn->setText( tr("Send") );
	}
}

void MainWindow::onSendButtonPress( void )
{
	// if already polling then stop
	if( m_pollTimer->isActive() )
	{
		m_pollTimer->stop();
		ui->sendBtn->setText( tr("Send") );
	}
	else
	{
		// if polling requested then enable timer
		if( m_poll )
		{
			m_pollTimer->start( 1000 );
			ui->sendBtn->setText( tr("Stop") );
		}

		sendModbusRequest();
	}
}

void MainWindow::busMonitorAddItem( bool isRequest,
					uint8_t slave,
					uint8_t func,
					uint16_t addr,
					uint16_t nb,
					uint16_t expectedCRC,
					uint16_t actualCRC )
{
	QTableWidget * bm = ui->busMonTable;
	const int rowCount = bm->rowCount();
	bm->setRowCount( rowCount+1 );

    QTableWidgetItem * numItem;
	QTableWidgetItem * ioItem = new QTableWidgetItem( isRequest ? tr( "Req >>" ) : tr( "<< Resp" ) );
	QTableWidgetItem * slaveItem = new QTableWidgetItem( QString::number( slave ) );
	QTableWidgetItem * funcItem = new QTableWidgetItem( QString::number( func ) );
	QTableWidgetItem * addrItem = new QTableWidgetItem( QString::number( addr ) );
    (ui->radioButton_181->isChecked()) ?numItem  = new QTableWidgetItem( QString::number( 2 ) ) : numItem = new QTableWidgetItem( QString::number( 1 ) );
	QTableWidgetItem * crcItem = new QTableWidgetItem;

	if( func > 127 )
	{
		addrItem->setText( QString() );
		numItem->setText( QString() );
		funcItem->setText( tr( "Exception (%1)" ).arg( func-128 ) );
		funcItem->setForeground( Qt::red );
	}
	else
	{
		if( expectedCRC == actualCRC )
		{
			crcItem->setText( QString().sprintf( "%.4x", actualCRC ) );
		}
		else
		{
			crcItem->setText( QString().sprintf( "%.4x (%.4x)", actualCRC, expectedCRC ) );
			crcItem->setForeground( Qt::red );
		}
	}
	ioItem->setFlags( ioItem->flags() & ~Qt::ItemIsEditable );
	slaveItem->setFlags( slaveItem->flags() & ~Qt::ItemIsEditable );
	funcItem->setFlags( funcItem->flags() & ~Qt::ItemIsEditable );
	addrItem->setFlags( addrItem->flags() & ~Qt::ItemIsEditable );
	numItem->setFlags( numItem->flags() & ~Qt::ItemIsEditable );
	crcItem->setFlags( crcItem->flags() & ~Qt::ItemIsEditable );
	bm->setItem( rowCount, 0, ioItem );
	bm->setItem( rowCount, 1, slaveItem );
    bm->setItem( rowCount, 2, funcItem );
	bm->setItem( rowCount, 3, addrItem );
	bm->setItem( rowCount, 4, numItem );
	bm->setItem( rowCount, 5, crcItem );
	bm->verticalScrollBar()->setValue( bm->verticalScrollBar()->maximum() );
}


void MainWindow::busMonitorRawData( uint8_t * data, uint8_t dataLen, bool addNewline )
{
	if( dataLen > 0 )
	{
		QString dump = ui->rawData->toPlainText();
		for( int i = 0; i < dataLen; ++i )
		{
			dump += QString().sprintf( "%.2x ", data[i] );
		}
		if( addNewline )
		{
			dump += "\n";
		}
		ui->rawData->setPlainText( dump );
		ui->rawData->verticalScrollBar()->setValue( 100000 );
		ui->rawData->setLineWrapMode( QPlainTextEdit::NoWrap );
	}
}

// static
void MainWindow::stBusMonitorAddItem( modbus_t * modbus, uint8_t isRequest, uint8_t slave, uint8_t func, uint16_t addr, uint16_t nb, uint16_t expectedCRC, uint16_t actualCRC )
{
    Q_UNUSED(modbus);
    globalMainWin->busMonitorAddItem( isRequest, slave, func, addr+1, nb, expectedCRC, actualCRC );
}

// static
void MainWindow::stBusMonitorRawData( modbus_t * modbus, uint8_t * data, uint8_t dataLen, uint8_t addNewline )
{
    Q_UNUSED(modbus);
    globalMainWin->busMonitorRawData( data, dataLen, addNewline != 0 );
}

static QString descriptiveDataTypeName( int funcCode )
{
	switch( funcCode )
	{
		case MODBUS_FC_READ_COILS:
		case MODBUS_FC_WRITE_SINGLE_COIL:
		case MODBUS_FC_WRITE_MULTIPLE_COILS:
			return "Coil (binary)";
		case MODBUS_FC_READ_DISCRETE_INPUTS:
			return "Discrete Input (binary)";
		case MODBUS_FC_READ_HOLDING_REGISTERS:
		case MODBUS_FC_WRITE_SINGLE_REGISTER:
		case MODBUS_FC_WRITE_MULTIPLE_REGISTERS:
			return "Holding Register (16 bit)";
		case MODBUS_FC_READ_INPUT_REGISTERS:
			return "Input Register (16 bit)";
		default:
			break;
	}
	return "Unknown";
}


static inline QString embracedString( const QString & s )
{
	return s.section( '(', 1 ).section( ')', 0, 0 );
}


static inline int stringToHex( QString s )
{
	return s.replace( "0x", "" ).toInt( NULL, 16 );
}


void MainWindow::updateRequestPreview( void )
{
	const int slave = ui->slaveID->value();
    const int func = stringToHex( embracedString(ui->functionCode->currentText() ) );
	const int addr = ui->startAddr->value()-1;
	const int num = ui->numCoils->value();
	if( func == MODBUS_FC_WRITE_SINGLE_COIL || func == MODBUS_FC_WRITE_SINGLE_REGISTER )
	{
		ui->requestPreview->setText(
			QString().sprintf( "%.2x  %.2x  %.2x %.2x ",
					slave,
					func,
					addr >> 8,
					addr & 0xff ) );
	}
	else
	{
		ui->requestPreview->setText(
			QString().sprintf( "%.2x  %.2x  %.2x %.2x  %.2x %.2x",
					slave,
					func,
					addr >> 8,
					addr & 0xff,
					num >> 8,
					num & 0xff ) );
	}
}




void MainWindow::updateRegisterView( void )
{
	const int func = stringToHex( embracedString(ui->functionCode->currentText() ) );
	const QString funcType = descriptiveDataTypeName( func );
	const int addr = ui->startAddr->value();

	int rowCount = 0;
	switch( func )
	{
		case MODBUS_FC_WRITE_SINGLE_REGISTER:
		case MODBUS_FC_WRITE_SINGLE_COIL:
			ui->numCoils->setEnabled( false );
			rowCount = 1;
			break;
		case MODBUS_FC_WRITE_MULTIPLE_COILS:
		case MODBUS_FC_WRITE_MULTIPLE_REGISTERS:
			rowCount = ui->numCoils->value();
		default:
			ui->numCoils->setEnabled( true );
			break;
	}

	ui->regTable->setRowCount( rowCount );
	for( int i = 0; i < rowCount; ++i )
	{
		QTableWidgetItem * dtItem = new QTableWidgetItem( funcType );
		QTableWidgetItem * addrItem = new QTableWidgetItem( QString::number( addr+i ) );
		QTableWidgetItem * dataItem = new QTableWidgetItem( QString::number( 0 ) );

		dtItem->setFlags( dtItem->flags() & ~Qt::ItemIsEditable	);
		addrItem->setFlags( addrItem->flags() & ~Qt::ItemIsEditable );
		ui->regTable->setItem( i, DataTypeColumn, dtItem );
		ui->regTable->setItem( i, AddrColumn, addrItem );
		ui->regTable->setItem( i, DataColumn, dataItem );
	}

	ui->regTable->setColumnWidth( 0, 150 );
}


void MainWindow::enableHexView( void )
{
	const int func = stringToHex( embracedString(
					ui->functionCode->currentText() ) );

	bool b_enabled =
		func == MODBUS_FC_READ_HOLDING_REGISTERS ||
		func == MODBUS_FC_READ_INPUT_REGISTERS;

	ui->checkBoxHexData->setEnabled( b_enabled );
}


void MainWindow::sendModbusRequest( void )
{
    // UPDATE m_modbus_snipping WITH THE CURRENT
    if (ui->tabWidget_2->currentIndex() == 0)      m_modbus_snipping = m_modbus;
    else if (ui->tabWidget_2->currentIndex() == 1) m_modbus_snipping = m_modbus_2;
    else if (ui->tabWidget_2->currentIndex() == 2) m_modbus_snipping = m_modbus_3;
    else if (ui->tabWidget_2->currentIndex() == 3) m_modbus_snipping = m_modbus_4;
    else if (ui->tabWidget_2->currentIndex() == 4) m_modbus_snipping = m_modbus_5;
    else                                           m_modbus_snipping = m_modbus_6;

	if( m_modbus_snipping == NULL )
	{
		setStatusError( tr("Not configured!") );
		return;
	}

	const int slave = ui->slaveID->value();
	const int func = stringToHex(embracedString(ui->functionCode->currentText()));
	const int addr = ui->startAddr->value()-1;
	int num = ui->numCoils->value();
	uint8_t dest[1024];
	uint16_t * dest16 = (uint16_t *) dest;

	memset( dest, 0, 1024 );

	int ret = -1;
	bool is16Bit = false;
	bool writeAccess = false;
	const QString funcType = descriptiveDataTypeName( func );

	modbus_set_slave( m_modbus_snipping, slave );

	switch( func )
	{
		case MODBUS_FC_READ_COILS:
			ret = modbus_read_bits( m_modbus_snipping, addr, num, dest );
			break;
		case MODBUS_FC_READ_DISCRETE_INPUTS:
			ret = modbus_read_input_bits( m_modbus_snipping, addr, num, dest );
			break;
		case MODBUS_FC_READ_HOLDING_REGISTERS:
			ret = modbus_read_registers( m_modbus_snipping, addr, num, dest16 );
			is16Bit = true;
			break;
		case MODBUS_FC_READ_INPUT_REGISTERS:
			ret = modbus_read_input_registers(m_modbus_snipping, addr, num, dest16 );
			is16Bit = true;
			break;
		case MODBUS_FC_WRITE_SINGLE_COIL:
            //ret = modbus_write_bit( m_modbus_snipping, addr,ui->regTable->item( 0, DataColumn )->text().toInt(0, 0) ? 1 : 0 );
            ret = modbus_write_bit( m_modbus_snipping, addr,ui->radioButton_184->isChecked() ? 1 : 0 );
			writeAccess = true;
			num = 1;
			break;
		case MODBUS_FC_WRITE_SINGLE_REGISTER:
            //ret = modbus_write_register( m_modbus_snipping, addr,ui->regTable->item( 0, DataColumn )->text().toInt(0, 0) );
            ret = modbus_write_register( m_modbus_snipping, addr,ui->lineEdit_111->text().toInt(0, 0) );
			writeAccess = true;
			num = 1;
			break;
		case MODBUS_FC_WRITE_MULTIPLE_COILS:
		{
			uint8_t * data = new uint8_t[num];
			for( int i = 0; i < num; ++i ) data[i] = ui->regTable->item( i, DataColumn )->text().toInt(0, 0);
			ret = modbus_write_bits( m_modbus_snipping, addr, num, data );
			delete[] data;
			writeAccess = true;
			break;
		}
		case MODBUS_FC_WRITE_MULTIPLE_REGISTERS:
		{
            float value;
            QString qvalue = ui->lineEdit_109->text();
            QTextStream floatTextStream(&qvalue);
            floatTextStream >> value;
            quint16 (*reg)[2] = reinterpret_cast<quint16(*)[2]>(&value);
            uint16_t * data = new uint16_t[2];            
            data[0] = (*reg)[1];
            data[1] = (*reg)[0];
            ret = modbus_write_registers( m_modbus_snipping, addr, 2, data );
			delete[] data;
			writeAccess = true;
			break;
		}
		default:
			break;
	}

	if( ret == num  )
	{
		if( writeAccess )
		{
			m_statusText->setText(tr( "Values successfully sent" ) );
			m_statusInd->setStyleSheet( "background: #0b0;" );
			m_statusTimer->start( 2000 );
		}
        else
		{
			bool b_hex = is16Bit && ui->checkBoxHexData->checkState() == Qt::Checked;
			QString qs_num;
            QString qs_output = "0x";
            bool ok = false;

			ui->regTable->setRowCount( num );
			for( int i = 0; i < num; ++i )
			{
				int data = is16Bit ? dest16[i] : dest[i];
                QString qs_tmp;

				QTableWidgetItem * dtItem = new QTableWidgetItem( funcType );
				QTableWidgetItem * addrItem = new QTableWidgetItem(QString::number( ui->startAddr->value()+i ) );
				qs_num.sprintf( b_hex ? "0x%04x" : "%d", data);
				qs_tmp.sprintf("%04x", data);
                qs_output.append(qs_tmp);
				QTableWidgetItem * dataItem = new QTableWidgetItem( qs_num );
				dtItem->setFlags( dtItem->flags() & ~Qt::ItemIsEditable );
				addrItem->setFlags( addrItem->flags() & ~Qt::ItemIsEditable );
				dataItem->setFlags( dataItem->flags() & ~Qt::ItemIsEditable );
				ui->regTable->setItem( i, DataTypeColumn, dtItem );
				ui->regTable->setItem( i, AddrColumn, addrItem );
				ui->regTable->setItem( i, DataColumn, dataItem );
                if (ui->radioButton_182->isChecked()) ui->lineEdit_111->setText(QString::number(data));
                else if (ui->radioButton_183->isChecked())
                {
                    (data) ? ui->radioButton_184->setChecked(true) : ui->radioButton_185->setChecked(true);
                }
            }

            QByteArray array = QByteArray::fromHex(qs_output.toLatin1());
            const float d = toFloat(array);

            if (ui->radioButton_181->isChecked())
            {
                (b_hex) ? ui->lineEdit_109->setText(qs_output) : ui->lineEdit_109->setText(QString::number(d,'f',10)) ;
            }
		}
	}
	else
	{
		QString err;

		if( ret < 0 )
		{
			if(
#ifdef WIN32
					errno == WSAETIMEDOUT ||
#endif
					errno == EIO
																	)
			{
				err += tr( "I/O error" );
				err += ": ";
				err += tr( "did not receive any data from slave." );
			}
			else
			{
				err += tr( "Protocol error" );
				err += ": ";
				err += tr( "Slave threw exception '" );
				err += modbus_strerror( errno );
				err += tr( "' or function not implemented." );
			}
		}
		else
		{
			err += tr( "Protocol error" );
			err += ": ";
			err += tr( "Number of registers returned does not "
					"match number of registers requested!" );
		}

		if( err.size() > 0 )
			setStatusError( err );
	}
}


void MainWindow::resetStatus( void )
{
	m_statusText->setText( tr( "Ready" ) );
	m_statusInd->setStyleSheet( "background: #aaa;" );
}

void MainWindow::pollForDataOnBus( void )
{
	if( m_modbus )
	{
		modbus_poll( m_modbus );
	}
}


void MainWindow::openBatchProcessor()
{
	BatchProcessor( this, m_modbus ).exec();
}


void MainWindow::aboutQModBus( void )
{
	AboutDialog( this ).exec();
}

void MainWindow::onRtuPortActive(bool active)
{
	if (active) {
        m_modbus = this->modbus();
		if (m_modbus) {
			modbus_register_monitor_add_item_fnc(m_modbus, MainWindow::stBusMonitorAddItem);
			modbus_register_monitor_raw_data_fnc(m_modbus, MainWindow::stBusMonitorRawData);
		}
	}
	else {
		m_modbus = NULL;
	}
}

void MainWindow::onRtuPortActive_2(bool active)
{
    if (active) {
        m_modbus_2 = this->modbus_2();
        if (m_modbus_2) {
            modbus_register_monitor_add_item_fnc(m_modbus_2, MainWindow::stBusMonitorAddItem);
            modbus_register_monitor_raw_data_fnc(m_modbus_2, MainWindow::stBusMonitorRawData);
        }
    }
    else {
        m_modbus_2 = NULL;
    }
}

void MainWindow::onRtuPortActive_3(bool active)
{
    if (active) {
        m_modbus_3 = this->modbus_3();
        if (m_modbus_3) {
            modbus_register_monitor_add_item_fnc(m_modbus_3, MainWindow::stBusMonitorAddItem);
            modbus_register_monitor_raw_data_fnc(m_modbus_3, MainWindow::stBusMonitorRawData);
        }
    }
    else {
        m_modbus_3 = NULL;
    }
}

void MainWindow::onRtuPortActive_4(bool active)
{
    if (active) {
        m_modbus_4 = this->modbus_4();
        if (m_modbus_4) {
            modbus_register_monitor_add_item_fnc(m_modbus_4, MainWindow::stBusMonitorAddItem);
            modbus_register_monitor_raw_data_fnc(m_modbus_4, MainWindow::stBusMonitorRawData);
        }
    }
    else {
        m_modbus_4 = NULL;
    }
}

void MainWindow::onRtuPortActive_5(bool active)
{
    if (active) {
        m_modbus_5 = this->modbus_5();
        if (m_modbus_5) {
            modbus_register_monitor_add_item_fnc(m_modbus_5, MainWindow::stBusMonitorAddItem);
            modbus_register_monitor_raw_data_fnc(m_modbus_5, MainWindow::stBusMonitorRawData);
        }
    }
    else {
        m_modbus_5 = NULL;
    }
}

void MainWindow::onRtuPortActive_6(bool active)
{
    if (active) {
        m_modbus_6 = this->modbus_6();
        if (m_modbus_6) {
            modbus_register_monitor_add_item_fnc(m_modbus_6, MainWindow::stBusMonitorAddItem);
            modbus_register_monitor_raw_data_fnc(m_modbus_6, MainWindow::stBusMonitorRawData);
        }
    }
    else {
        m_modbus_6 = NULL;
    }
}

void
MainWindow::
setStatusError(const QString &msg)
{
    m_statusText->setText( msg );
    m_statusInd->setStyleSheet( "background: red;" );
    m_statusTimer->start( 2000 );
}


void
MainWindow::
updateGraph()
{
    chart = new QChart();
    chart->legend()->hide();

    axisX = new QValueAxis;
    axisX->setRange(0,1000);
    axisX->setTickCount(11);
    axisX->setTickInterval(100);
    axisX->setLabelFormat("%i");
    axisX->setTitleText("Frequency (Mhz)");

    axisY = new QValueAxis;
    axisY->setRange(0,100);
    axisY->setTickCount(11);
    axisY->setLabelFormat("%i");
    axisY->setTitleText("Watercut (%)");

    axisY3 = new QValueAxis;
    axisY3->setRange(0,2.5);
    axisY3->setTickCount(11);
    axisY3->setLabelFormat("%.1f");
    axisY3->setTitleText("Reflected Power (V)");

    chart->addAxis(axisX, Qt::AlignBottom);

    series = new QSplineSeries;
    axisY->setLinePenColor(series->pen().color());
    axisY->setLabelsColor(series->pen().color());
    *series << QPointF(100, 5) << QPointF(300, 48) << QPointF(600, 68) << QPointF(800, 89);
    chart->addSeries(series);
    chart->addAxis(axisY, Qt::AlignLeft);
    series->attachAxis(axisX);
    series->attachAxis(axisY);

    series = new QSplineSeries;
    axisY3->setLinePenColor(series->pen().color());
    axisY3->setLabelsColor(series->pen().color());
    *series << QPointF(1, 0.5) << QPointF(105, 1.5) << QPointF(400.4, 1.6) << QPointF(500.3, 1.7) << QPointF(600.2, 1.8) << QPointF(700.4, 2.0) << QPointF(800.3, 2.1) << QPointF(900, 2.4);
    chart->addSeries(series);
    chart->addAxis(axisY3, Qt::AlignRight);
    series->attachAxis(axisX);
    series->attachAxis(axisY3);

    QChartView *chartView = new QChartView(chart);
    chartView->setRenderHint(QPainter::Antialiasing);

    updateChartTitle();

    ui->gridLayout_5->addWidget(chartView,0,0);
}

void
MainWindow::
initializePressureGauge()
{
    m_pressureGauge = new QcGaugeWidget;
    m_pressureGauge->addBackground(99);
    QcBackgroundItem *bkg1 = m_pressureGauge->addBackground(92);
    bkg1->clearrColors();
    bkg1->addColor(0.1,Qt::black);
    bkg1->addColor(1.0,Qt::white);

    QcBackgroundItem *bkg2 = m_pressureGauge->addBackground(88);
    bkg2->clearrColors();
    bkg2->addColor(0.1,Qt::gray);
    bkg2->addColor(1.0,Qt::darkGray);

    m_pressureGauge->addArc(55);
    m_pressureGauge->addDegrees(65)->setValueRange(0,80);
    m_pressureGauge->addColorBand(50);
    m_pressureGauge->addValues(80)->setValueRange(0,80);
    m_pressureGauge->addLabel(70)->setText("Pressure (PSI)");
    QcLabelItem *lab = m_pressureGauge->addLabel(40);
    lab->setText("0");
    m_pressureNeedle = m_pressureGauge->addNeedle(60);
    m_pressureNeedle->setLabel(lab);
    m_pressureNeedle->setColor(Qt::white);
    m_pressureNeedle->setValueRange(0,80);
    m_pressureGauge->addBackground(7);
    m_pressureGauge->addGlass(88);
    ui->gridLayout_6->addWidget(m_pressureGauge);
}


void
MainWindow::
updatePressureGauge()
{}


void
MainWindow::
initializeTemperatureGauge()
{
    m_temperatureGauge = new QcGaugeWidget;
    m_temperatureGauge->addBackground(99);
    QcBackgroundItem *bkg1 = m_temperatureGauge->addBackground(92);
    bkg1->clearrColors();
    bkg1->addColor(0.1,Qt::black);
    bkg1->addColor(1.0,Qt::white);

    QcBackgroundItem *bkg2 = m_temperatureGauge->addBackground(88);
    bkg2->clearrColors();
    bkg2->addColor(0.1,Qt::gray);
    bkg2->addColor(1.0,Qt::darkGray);

    m_temperatureGauge->addArc(55);
    m_temperatureGauge->addDegrees(65)->setValueRange(0,80);
    m_temperatureGauge->addColorBand(50);
    m_temperatureGauge->addValues(80)->setValueRange(0,80);
    m_temperatureGauge->addLabel(70)->setText("Temp (C°)");
    QcLabelItem *lab = m_temperatureGauge->addLabel(40);
    lab->setText("0");
    m_temperatureNeedle = m_temperatureGauge->addNeedle(60);
    m_temperatureNeedle->setLabel(lab);
    m_temperatureNeedle->setColor(Qt::white);
    m_temperatureNeedle->setValueRange(0,80);
    m_temperatureGauge->addBackground(7);
    m_temperatureGauge->addGlass(88);
    ui->gridLayout_7->addWidget(m_temperatureGauge);
}

void
MainWindow::
updateTemperatureGauge()
{}

void
MainWindow::
initializeDensityGauge()
{
    m_densityGauge = new QcGaugeWidget;
    m_densityGauge->addBackground(99);
    QcBackgroundItem *bkg1 = m_densityGauge->addBackground(92);
    bkg1->clearrColors();
    bkg1->addColor(0.1,Qt::black);
    bkg1->addColor(1.0,Qt::white);

    QcBackgroundItem *bkg2 = m_densityGauge->addBackground(88);
    bkg2->clearrColors();
    bkg2->addColor(0.1,Qt::gray);
    bkg2->addColor(1.0,Qt::darkGray);

    m_densityGauge->addArc(55);
    m_densityGauge->addDegrees(65)->setValueRange(0,80);
    m_densityGauge->addColorBand(50);
    m_densityGauge->addValues(80)->setValueRange(0,80);
    m_densityGauge->addLabel(70)->setText("Density");
    QcLabelItem *lab = m_densityGauge->addLabel(40);
    lab->setText("0");
    m_densityNeedle = m_densityGauge->addNeedle(60);
    m_densityNeedle->setLabel(lab);
    m_densityNeedle->setColor(Qt::white);
    m_densityNeedle->setValueRange(0,80);
    m_densityGauge->addBackground(7);
    m_densityGauge->addGlass(88);
    ui->gridLayout_8->addWidget(m_densityGauge);
}

void
MainWindow::
updateDensityGauge()
{}

void
MainWindow::
initializeRPGauge()
{
    m_RPGauge = new QcGaugeWidget;
    m_RPGauge->addBackground(99);
    QcBackgroundItem *bkg1 = m_RPGauge->addBackground(92);
    bkg1->clearrColors();
    bkg1->addColor(0.1,Qt::black);
    bkg1->addColor(1.0,Qt::white);

    QcBackgroundItem *bkg2 = m_RPGauge->addBackground(88);
    bkg2->clearrColors();
    bkg2->addColor(0.1,Qt::gray);
    bkg2->addColor(1.0,Qt::darkGray);

    m_RPGauge->addArc(55);
    m_RPGauge->addDegrees(65)->setValueRange(0,80);
    m_RPGauge->addColorBand(50);
    m_RPGauge->addValues(80)->setValueRange(0,80);
    m_RPGauge->addLabel(70)->setText("RP (V)");
    QcLabelItem *lab = m_RPGauge->addLabel(40);
    lab->setText("0");
    m_RPNeedle = m_RPGauge->addNeedle(60);
    m_RPNeedle->setLabel(lab);
    m_RPNeedle->setColor(Qt::white);
    m_RPNeedle->setValueRange(0,80);
    m_RPGauge->addBackground(7);
    m_RPGauge->addGlass(88);
    ui->gridLayout_9->addWidget(m_RPGauge);
}

void
MainWindow::
updateRPGauge()
{}


void
MainWindow::
onRadioButtonPressed()
{
    ui->groupBox_6->setEnabled(TRUE);
    onRadioButton_3Pressed();
}

void
MainWindow::
onRadioButton_2Pressed()
{
    ui->radioButton_3->setChecked(TRUE);
    ui->groupBox_6->setEnabled(FALSE);
    onRadioButton_4Pressed();
}

void
MainWindow::
onRadioButton_3Pressed()
{
    ui->groupBox_125->setEnabled(TRUE);
}

void
MainWindow::
onRadioButton_4Pressed()
{
    ui->groupBox_125->setDisabled(TRUE);
    ui->radioButton_109->setChecked(TRUE);
}

void
MainWindow::
onRadioButton_7Pressed()
{
    ui->groupBox_126->setDisabled(TRUE);
    ui->radioButton_113->setChecked(TRUE);
}

void
MainWindow::
onRadioButton_8Pressed()
{
    ui->groupBox_126->setEnabled(TRUE);
}

void
MainWindow::
onRadioButton_9Pressed()
{
    ui->groupBox_11->setEnabled(TRUE);
    onRadioButton_8Pressed();
}

void
MainWindow::
onRadioButton_10Pressed()
{
    ui->radioButton_8->setChecked(TRUE);
    ui->groupBox_11->setEnabled(FALSE);
    onRadioButton_7Pressed();
}

void
MainWindow::
onRadioButton_13Pressed()
{
    ui->groupBox_127->setDisabled(TRUE);
    ui->radioButton_117->setChecked(TRUE);
}

void
MainWindow::
onRadioButton_14Pressed()
{
    ui->groupBox_127->setEnabled(TRUE);
}

void
MainWindow::
onRadioButton_15Pressed()
{
    ui->groupBox_15->setEnabled(TRUE);
    ui->groupBox_127->setEnabled(TRUE);
    onRadioButton_14Pressed();
}

void
MainWindow::
onRadioButton_16Pressed()
{
    ui->radioButton_14->setChecked(TRUE);
    ui->groupBox_15->setEnabled(FALSE);
    onRadioButton_13Pressed();
}

void
MainWindow::
onRadioButton_19Pressed()
{
    ui->groupBox_21->setEnabled(TRUE);
    onRadioButton_24Pressed();
}

void
MainWindow::
onRadioButton_20Pressed()
{
    ui->radioButton_24->setChecked(TRUE);
    ui->groupBox_21->setEnabled(FALSE);
    onRadioButton_23Pressed();
}

void
MainWindow::
onRadioButton_23Pressed()
{
    ui->radioButton_121->setChecked(TRUE);
    ui->groupBox_128->setDisabled(TRUE);
}

void
MainWindow::
onRadioButton_24Pressed()
{
    ui->groupBox_128->setEnabled(TRUE);
}

void
MainWindow::
onRadioButton_25Pressed()
{
    ui->radioButton_125->setChecked(TRUE);
    ui->groupBox_129->setDisabled(TRUE);
}

void
MainWindow::
onRadioButton_26Pressed()
{
    ui->groupBox_129->setEnabled(TRUE);
}

void
MainWindow::
onRadioButton_27Pressed()
{
    ui->groupBox_22->setEnabled(TRUE);
    onRadioButton_26Pressed();
}

void
MainWindow::
onRadioButton_28Pressed()
{
    ui->radioButton_26->setChecked(TRUE);
    ui->groupBox_22->setEnabled(FALSE);
    onRadioButton_25Pressed();
}

void
MainWindow::
onRadioButton_31Pressed()
{
   ui->radioButton_129->setChecked(TRUE);
    ui->groupBox_130->setDisabled(TRUE);
}

void
MainWindow::
onRadioButton_32Pressed()
{
    ui->groupBox_130->setEnabled(TRUE);
}

void
MainWindow::
onRadioButton_33Pressed()
{
    ui->groupBox_25->setEnabled(TRUE);
    onRadioButton_32Pressed();
}

void
MainWindow::
onRadioButton_34Pressed()
{
    ui->radioButton_32->setChecked(TRUE);
    ui->groupBox_25->setEnabled(FALSE);
    onRadioButton_31Pressed();
}

void
MainWindow::
onRadioButton_37Pressed()
{
    ui->groupBox_31->setEnabled(TRUE);
    onRadioButton_42Pressed();
}

void
MainWindow::
onRadioButton_38Pressed()
{
    ui->radioButton_42->setChecked(TRUE);
    ui->groupBox_31->setEnabled(FALSE);
    onRadioButton_41Pressed();
}

void
MainWindow::
onRadioButton_41Pressed()
{
    ui->radioButton_133->setChecked(TRUE);
    ui->groupBox_131->setDisabled(TRUE);
}

void
MainWindow::
onRadioButton_42Pressed()
{
     ui->groupBox_131->setEnabled(TRUE);
}

void
MainWindow::
onRadioButton_45Pressed()
{
    ui->groupBox_32->setEnabled(TRUE);
    onRadioButton_44Pressed();
}

void
MainWindow::
onRadioButton_46Pressed()
{
    ui->radioButton_44->setChecked(TRUE);
    ui->groupBox_32->setDisabled(TRUE);
    onRadioButton_43Pressed();
}

void
MainWindow::
onRadioButton_43Pressed()
{
    ui->radioButton_137->setChecked(TRUE);
    ui->groupBox_132->setDisabled(TRUE);
}

void
MainWindow::
onRadioButton_44Pressed()
{
     ui->groupBox_132->setEnabled(TRUE);
}

void
MainWindow::
onRadioButton_51Pressed()
{
    ui->groupBox_35->setEnabled(TRUE);
    onRadioButton_50Pressed();
}

void
MainWindow::
onRadioButton_52Pressed()
{
    ui->radioButton_50->setChecked(TRUE);
    ui->groupBox_35->setDisabled(TRUE);
    onRadioButton_49Pressed();
}

void
MainWindow::
onRadioButton_49Pressed()
{
    ui->radioButton_141->setChecked(TRUE);
    ui->groupBox_133->setDisabled(TRUE);
}

void
MainWindow::
onRadioButton_50Pressed()
{
     ui->groupBox_133->setEnabled(TRUE);
}

void
MainWindow::
onRadioButton_55Pressed()
{
    ui->groupBox_41->setEnabled(TRUE);
    onRadioButton_60Pressed();
}

void
MainWindow::
onRadioButton_56Pressed()
{
    ui->radioButton_60->setChecked(TRUE);
    ui->groupBox_41->setDisabled(TRUE);
    onRadioButton_59Pressed();
}

void
MainWindow::
onRadioButton_60Pressed()
{
    ui->groupBox_134->setEnabled(TRUE);
}

void
MainWindow::
onRadioButton_59Pressed()
{
    ui->radioButton_145->setChecked(TRUE);
    ui->groupBox_134->setDisabled(TRUE);
}

void
MainWindow::
onRadioButton_61Pressed()
{
    ui->radioButton_149->setChecked(TRUE);
    ui->groupBox_135->setDisabled(TRUE);
}

void
MainWindow::
onRadioButton_62Pressed()
{
    ui->groupBox_135->setEnabled(TRUE);
}

void
MainWindow::
onRadioButton_63Pressed()
{
    ui->groupBox_42->setEnabled(TRUE);
    onRadioButton_62Pressed();
}

void
MainWindow::
onRadioButton_64Pressed()
{
    ui->radioButton_62->setChecked(TRUE);
    ui->groupBox_42->setEnabled(FALSE);
    onRadioButton_61Pressed();
}

void
MainWindow::
onRadioButton_67Pressed()
{
    ui->radioButton_153->setChecked(TRUE);
    ui->groupBox_136->setDisabled(TRUE);
}

void
MainWindow::
onRadioButton_68Pressed()
{
    ui->groupBox_136->setEnabled(TRUE);
}

void
MainWindow::
onRadioButton_69Pressed()
{
    ui->groupBox_45->setEnabled(TRUE);
    onRadioButton_68Pressed();
}

void
MainWindow::
onRadioButton_70Pressed()
{
    ui->radioButton_68->setChecked(TRUE);
    ui->groupBox_45->setEnabled(FALSE);
    onRadioButton_67Pressed();
}

void
MainWindow::
onRadioButton_73Pressed()
{
    ui->groupBox_51->setEnabled(TRUE);
    onRadioButton_78Pressed();
}

void
MainWindow::
onRadioButton_74Pressed()
{
    ui->radioButton_78->setChecked(TRUE);
    ui->groupBox_51->setEnabled(FALSE);
    onRadioButton_77Pressed();
}

void
MainWindow::
onRadioButton_77Pressed()
{
    ui->radioButton_157->setChecked(TRUE);
    ui->groupBox_137->setDisabled(TRUE);
}

void
MainWindow::
onRadioButton_78Pressed()
{
    ui->groupBox_137->setEnabled(TRUE);
}

void
MainWindow::
onRadioButton_79Pressed()
{
    ui->radioButton_161->setChecked(TRUE);
    ui->groupBox_138->setDisabled(TRUE);
}

void
MainWindow::
onRadioButton_80Pressed()
{
    ui->groupBox_138->setEnabled(TRUE);
}

void
MainWindow::
onRadioButton_81Pressed()
{
    ui->groupBox_52->setEnabled(TRUE);
    onRadioButton_80Pressed();
}

void
MainWindow::
onRadioButton_82Pressed()
{
    ui->radioButton_80->setChecked(TRUE);
    ui->groupBox_52->setEnabled(FALSE);
    onRadioButton_79Pressed();
}

void
MainWindow::
onRadioButton_85Pressed()
{
    ui->radioButton_165->setChecked(TRUE);
    ui->groupBox_139->setDisabled(TRUE);
}

void
MainWindow::
onRadioButton_86Pressed()
{
    ui->groupBox_139->setEnabled(TRUE);
}

void
MainWindow::
onRadioButton_87Pressed()
{
    ui->groupBox_55->setEnabled(TRUE);
    onRadioButton_86Pressed();
}

void
MainWindow::
onRadioButton_88Pressed()
{
    ui->radioButton_86->setChecked(TRUE);
    ui->groupBox_55->setEnabled(FALSE);
    onRadioButton_85Pressed();
}

void
MainWindow::
onRadioButton_91Pressed()
{
    ui->groupBox_61->setEnabled(TRUE);
    onRadioButton_96Pressed();
}

void
MainWindow::
onRadioButton_92Pressed()
{
    ui->radioButton_96->setChecked(TRUE);
    ui->groupBox_61->setEnabled(FALSE);
    onRadioButton_95Pressed();
}

void
MainWindow::
onRadioButton_95Pressed()
{
    ui->radioButton_169->setChecked(TRUE);
    ui->groupBox_140->setDisabled(TRUE);
}

void
MainWindow::
onRadioButton_96Pressed()
{
    ui->groupBox_140->setEnabled(TRUE);
}

void
MainWindow::
onRadioButton_97Pressed()
{
    ui->radioButton_173->setChecked(TRUE);
    ui->groupBox_141->setDisabled(TRUE);
}

void
MainWindow::
onRadioButton_98Pressed()
{
    ui->groupBox_141->setEnabled(TRUE);
}

void
MainWindow::
onRadioButton_99Pressed()
{
    ui->groupBox_62->setEnabled(TRUE);
    onRadioButton_98Pressed();
}

void
MainWindow::
onRadioButton_100Pressed()
{
    ui->radioButton_98->setChecked(TRUE);
    ui->groupBox_62->setDisabled(TRUE);
    onRadioButton_97Pressed();
}

void
MainWindow::
onRadioButton_103Pressed()
{
    ui->radioButton_177->setChecked(TRUE);
    ui->groupBox_142->setDisabled(TRUE);
}

void
MainWindow::
onRadioButton_104Pressed()
{
    ui->groupBox_142->setEnabled(TRUE);
}

void
MainWindow::
onRadioButton_105Pressed()
{
    ui->groupBox_65->setEnabled(TRUE);
    onRadioButton_104Pressed();
}

void
MainWindow::
onRadioButton_106Pressed()
{
    ui->radioButton_104->setChecked(TRUE);
    ui->groupBox_65->setEnabled(FALSE);
    onRadioButton_103Pressed();
}

void
MainWindow::
connectTimers()
{
    QTimer * t = new QTimer( this );
    connect( t, SIGNAL(timeout()), this, SLOT(pollForDataOnBus()));
    t->start( 5 );

    m_pollTimer = new QTimer( this );
    connect( m_pollTimer, SIGNAL(timeout()), this, SLOT(sendModbusRequest()));

    m_statusTimer = new QTimer( this );
    connect( m_statusTimer, SIGNAL(timeout()), this, SLOT(resetStatus()));
    m_statusTimer->setSingleShot(true);
}


void
MainWindow::
connectLoopDependentData()
{
    connect(ui->tabWidget_2, SIGNAL(currentChanged(int)), this, SLOT(onLoopTabChanged(int)));
    connect(ui->tabWidget_3, SIGNAL(currentChanged(int)), this, SLOT(onLoopTabChanged(int)));
    connect(ui->tabWidget_4, SIGNAL(currentChanged(int)), this, SLOT(onLoopTabChanged(int)));
    connect(ui->tabWidget_5, SIGNAL(currentChanged(int)), this, SLOT(onLoopTabChanged(int)));
    connect(ui->tabWidget_6, SIGNAL(currentChanged(int)), this, SLOT(onLoopTabChanged(int)));
    connect(ui->tabWidget_7, SIGNAL(currentChanged(int)), this, SLOT(onLoopTabChanged(int)));
    connect(ui->tabWidget_8, SIGNAL(currentChanged(int)), this, SLOT(onLoopTabChanged(int)));
}

void
MainWindow::
connectRadioButtons()
{
    connect(ui->radioButton, SIGNAL(pressed()), this, SLOT(onRadioButtonPressed()));
    connect(ui->radioButton_2, SIGNAL(pressed()), this, SLOT(onRadioButton_2Pressed()));
    connect(ui->radioButton_3, SIGNAL(pressed()), this, SLOT(onRadioButton_3Pressed()));
    connect(ui->radioButton_4, SIGNAL(pressed()), this, SLOT(onRadioButton_4Pressed()));
    connect(ui->radioButton_7, SIGNAL(pressed()), this, SLOT(onRadioButton_7Pressed()));
    connect(ui->radioButton_8, SIGNAL(pressed()), this, SLOT(onRadioButton_8Pressed()));
    connect(ui->radioButton_9, SIGNAL(pressed()), this, SLOT(onRadioButton_9Pressed()));
    connect(ui->radioButton_10, SIGNAL(pressed()), this, SLOT(onRadioButton_10Pressed()));
    connect(ui->radioButton_13, SIGNAL(pressed()), this, SLOT(onRadioButton_13Pressed()));
    connect(ui->radioButton_14, SIGNAL(pressed()), this, SLOT(onRadioButton_14Pressed()));
    connect(ui->radioButton_15, SIGNAL(pressed()), this, SLOT(onRadioButton_15Pressed()));
    connect(ui->radioButton_16, SIGNAL(pressed()), this, SLOT(onRadioButton_16Pressed()));
    connect(ui->radioButton_19, SIGNAL(pressed()), this, SLOT(onRadioButton_19Pressed()));
    connect(ui->radioButton_20, SIGNAL(pressed()), this, SLOT(onRadioButton_20Pressed()));
    connect(ui->radioButton_23, SIGNAL(pressed()), this, SLOT(onRadioButton_23Pressed()));
    connect(ui->radioButton_24, SIGNAL(pressed()), this, SLOT(onRadioButton_24Pressed()));
    connect(ui->radioButton_25, SIGNAL(pressed()), this, SLOT(onRadioButton_25Pressed()));
    connect(ui->radioButton_26, SIGNAL(pressed()), this, SLOT(onRadioButton_26Pressed()));
    connect(ui->radioButton_27, SIGNAL(pressed()), this, SLOT(onRadioButton_27Pressed()));
    connect(ui->radioButton_28, SIGNAL(pressed()), this, SLOT(onRadioButton_28Pressed()));
    connect(ui->radioButton_31, SIGNAL(pressed()), this, SLOT(onRadioButton_31Pressed()));
    connect(ui->radioButton_32, SIGNAL(pressed()), this, SLOT(onRadioButton_32Pressed()));
    connect(ui->radioButton_33, SIGNAL(pressed()), this, SLOT(onRadioButton_33Pressed()));
    connect(ui->radioButton_34, SIGNAL(pressed()), this, SLOT(onRadioButton_34Pressed()));
    connect(ui->radioButton_37, SIGNAL(pressed()), this, SLOT(onRadioButton_37Pressed()));
    connect(ui->radioButton_38, SIGNAL(pressed()), this, SLOT(onRadioButton_38Pressed()));
    connect(ui->radioButton_41, SIGNAL(pressed()), this, SLOT(onRadioButton_41Pressed()));
    connect(ui->radioButton_42, SIGNAL(pressed()), this, SLOT(onRadioButton_42Pressed()));
    connect(ui->radioButton_43, SIGNAL(pressed()), this, SLOT(onRadioButton_43Pressed()));
    connect(ui->radioButton_44, SIGNAL(pressed()), this, SLOT(onRadioButton_44Pressed()));
    connect(ui->radioButton_45, SIGNAL(pressed()), this, SLOT(onRadioButton_45Pressed()));
    connect(ui->radioButton_46, SIGNAL(pressed()), this, SLOT(onRadioButton_46Pressed()));
    connect(ui->radioButton_49, SIGNAL(pressed()), this, SLOT(onRadioButton_49Pressed()));
    connect(ui->radioButton_50, SIGNAL(pressed()), this, SLOT(onRadioButton_50Pressed()));
    connect(ui->radioButton_51, SIGNAL(pressed()), this, SLOT(onRadioButton_51Pressed()));
    connect(ui->radioButton_52, SIGNAL(pressed()), this, SLOT(onRadioButton_52Pressed()));
    connect(ui->radioButton_55, SIGNAL(pressed()), this, SLOT(onRadioButton_55Pressed()));
    connect(ui->radioButton_56, SIGNAL(pressed()), this, SLOT(onRadioButton_56Pressed()));
    connect(ui->radioButton_59, SIGNAL(pressed()), this, SLOT(onRadioButton_59Pressed()));
    connect(ui->radioButton_60, SIGNAL(pressed()), this, SLOT(onRadioButton_60Pressed()));
    connect(ui->radioButton_61, SIGNAL(pressed()), this, SLOT(onRadioButton_61Pressed()));
    connect(ui->radioButton_62, SIGNAL(pressed()), this, SLOT(onRadioButton_62Pressed()));
    connect(ui->radioButton_63, SIGNAL(pressed()), this, SLOT(onRadioButton_63Pressed()));
    connect(ui->radioButton_64, SIGNAL(pressed()), this, SLOT(onRadioButton_64Pressed()));
    connect(ui->radioButton_67, SIGNAL(pressed()), this, SLOT(onRadioButton_67Pressed()));
    connect(ui->radioButton_68, SIGNAL(pressed()), this, SLOT(onRadioButton_68Pressed()));
    connect(ui->radioButton_69, SIGNAL(pressed()), this, SLOT(onRadioButton_69Pressed()));
    connect(ui->radioButton_70, SIGNAL(pressed()), this, SLOT(onRadioButton_70Pressed()));
    connect(ui->radioButton_73, SIGNAL(pressed()), this, SLOT(onRadioButton_73Pressed()));
    connect(ui->radioButton_74, SIGNAL(pressed()), this, SLOT(onRadioButton_74Pressed()));
    connect(ui->radioButton_77, SIGNAL(pressed()), this, SLOT(onRadioButton_77Pressed()));
    connect(ui->radioButton_78, SIGNAL(pressed()), this, SLOT(onRadioButton_78Pressed()));
    connect(ui->radioButton_79, SIGNAL(pressed()), this, SLOT(onRadioButton_79Pressed()));
    connect(ui->radioButton_80, SIGNAL(pressed()), this, SLOT(onRadioButton_80Pressed()));
    connect(ui->radioButton_81, SIGNAL(pressed()), this, SLOT(onRadioButton_81Pressed()));
    connect(ui->radioButton_82, SIGNAL(pressed()), this, SLOT(onRadioButton_82Pressed()));
    connect(ui->radioButton_85, SIGNAL(pressed()), this, SLOT(onRadioButton_85Pressed()));
    connect(ui->radioButton_86, SIGNAL(pressed()), this, SLOT(onRadioButton_86Pressed()));
    connect(ui->radioButton_87, SIGNAL(pressed()), this, SLOT(onRadioButton_87Pressed()));
    connect(ui->radioButton_88, SIGNAL(pressed()), this, SLOT(onRadioButton_88Pressed()));
    connect(ui->radioButton_91, SIGNAL(pressed()), this, SLOT(onRadioButton_91Pressed()));
    connect(ui->radioButton_92, SIGNAL(pressed()), this, SLOT(onRadioButton_92Pressed()));
    connect(ui->radioButton_95, SIGNAL(pressed()), this, SLOT(onRadioButton_95Pressed()));
    connect(ui->radioButton_96, SIGNAL(pressed()), this, SLOT(onRadioButton_96Pressed()));
    connect(ui->radioButton_97, SIGNAL(pressed()), this, SLOT(onRadioButton_97Pressed()));
    connect(ui->radioButton_98, SIGNAL(pressed()), this, SLOT(onRadioButton_98Pressed()));
    connect(ui->radioButton_99, SIGNAL(pressed()), this, SLOT(onRadioButton_99Pressed()));
    connect(ui->radioButton_100, SIGNAL(pressed()), this, SLOT(onRadioButton_100Pressed()));
    connect(ui->radioButton_103, SIGNAL(pressed()), this, SLOT(onRadioButton_103Pressed()));
    connect(ui->radioButton_104, SIGNAL(pressed()), this, SLOT(onRadioButton_104Pressed()));
    connect(ui->radioButton_105, SIGNAL(pressed()), this, SLOT(onRadioButton_105Pressed()));
    connect(ui->radioButton_106, SIGNAL(pressed()), this, SLOT(onRadioButton_106Pressed()));

    // data type in modbus request groupbox
    connect(ui->radioButton_181, SIGNAL(toggled(bool)), this, SLOT(onFloatButtonPressed(bool)));
    connect(ui->radioButton_182, SIGNAL(toggled(bool)), this, SLOT(onIntegerButtonPressed(bool)));
    connect(ui->radioButton_183, SIGNAL(toggled(bool)), this, SLOT(onCoilButtonPressed(bool)));

    // select R/W mode
    connect(ui->radioButton_187, SIGNAL(toggled(bool)), this, SLOT(onReadButtonPressed(bool)));
    connect(ui->radioButton_186, SIGNAL(toggled(bool)), this, SLOT(onWriteButtonPressed(bool)));
}


void
MainWindow::
connectSerialPort()
{
    connect( ui->groupBox_18, SIGNAL(toggled(bool)), this, SLOT(onCheckBoxChecked(bool)));
    connect( ui->groupBox_28, SIGNAL(toggled(bool)), this, SLOT(onCheckBoxChecked(bool)));
    connect( ui->groupBox_38, SIGNAL(toggled(bool)), this, SLOT(onCheckBoxChecked(bool)));
    connect( ui->groupBox_48, SIGNAL(toggled(bool)), this, SLOT(onCheckBoxChecked(bool)));
    connect( ui->groupBox_58, SIGNAL(toggled(bool)), this, SLOT(onCheckBoxChecked(bool)));
    connect( ui->groupBox_68, SIGNAL(toggled(bool)), this, SLOT(onCheckBoxChecked(bool)));
    connect( this, SIGNAL(connectionError(const QString&)), this, SLOT(setStatusError(const QString&)));
}


void
MainWindow::
connectActions()
{
    connect( ui->actionAbout_QModBus, SIGNAL( triggered() ),this, SLOT( aboutQModBus() ) );
    connect( ui->functionCode, SIGNAL( currentIndexChanged( int ) ),this, SLOT( enableHexView() ) );
}


void
MainWindow::
connectModbusMonitor()
{
    connect( ui->slaveID, SIGNAL( valueChanged( int ) ),this, SLOT( updateRequestPreview() ) );
    connect( ui->functionCode, SIGNAL( currentIndexChanged( int ) ),this, SLOT( updateRequestPreview() ) );
    connect( ui->startAddr, SIGNAL( valueChanged( int ) ),this, SLOT( updateRequestPreview() ) );
    connect( ui->numCoils, SIGNAL( valueChanged( int ) ),this, SLOT( updateRequestPreview() ) );
    connect( ui->functionCode, SIGNAL( currentIndexChanged( int ) ),this, SLOT( updateRegisterView() ) );
    connect( ui->numCoils, SIGNAL( valueChanged( int ) ),this, SLOT( updateRegisterView() ) );
    connect( ui->startAddr, SIGNAL( valueChanged( int ) ),this, SLOT( updateRegisterView() ) );
    connect( ui->sendBtn, SIGNAL(pressed()),this, SLOT( onSendButtonPress() ) );
}


void
MainWindow::
connectToolbar()
{
    connect(ui->actionSave, SIGNAL(triggered()),this,SLOT(saveCsvFile()));
    connect(ui->actionOpen, SIGNAL(triggered()),this,SLOT(loadCsvFile()));
}


void
MainWindow::
setupCalibrationRequest( void )
{
    if (ui->tabWidget_2->currentIndex() == 0)
    {
        if (m_modbus == NULL) // LOOP 1
        {
            setStatusError( tr("Loop_1 not configured!") );
            return;       
        }

        if (ui->tabWidget_3->currentIndex() == 0) // P1
        {
            if (ui->lineEdit_2->text().isEmpty()) return;

            const int slave = ui->lineEdit_2->text().toInt();
            const int addr = 5;
            uint8_t dest[1024];
            uint16_t * dest16 = (uint16_t *) dest;
            memset( dest, 0, 1024 );
            int ret = -1;
            bool is16Bit = false;
            bool writeAccess = false;
            const QString funcType = descriptiveDataTypeName( FUNC_READ_FLOAT );

            modbus_set_slave( m_serialModbus, slave );
            sendCalibrationRequest(FLOAT_R, m_serialModbus, FUNC_READ_FLOAT, addr, BYTE_READ_FLOAT, ret, dest, dest16, is16Bit, writeAccess, funcType);
        }
        else if (ui->tabWidget_3->currentIndex() == 1)  // P2
        {
            const int slave = ui->lineEdit_3->text().toInt();
            const int addr = 5;
            uint8_t dest[1024];
            uint16_t * dest16 = (uint16_t *) dest;
            memset( dest, 0, 1024 );
            int ret = -1;
            bool is16Bit = false;
            bool writeAccess = false;
            const QString funcType = descriptiveDataTypeName( FUNC_READ_FLOAT );

            modbus_set_slave( m_serialModbus, slave );
            sendCalibrationRequest(FLOAT_R, m_serialModbus, FUNC_READ_FLOAT, addr, BYTE_READ_FLOAT, ret, dest, dest16, is16Bit, writeAccess, funcType);

        }
        else {  // P3
            const int slave = ui->lineEdit_5->text().toInt();
            const int addr = 5;
            uint8_t dest[1024];
            uint16_t * dest16 = (uint16_t *) dest;
            memset( dest, 0, 1024 );
            int ret = -1;
            bool is16Bit = false;
            bool writeAccess = false;
            const QString funcType = descriptiveDataTypeName( FUNC_READ_FLOAT );

            modbus_set_slave( m_serialModbus, slave );
            sendCalibrationRequest(FLOAT_R, m_serialModbus, FUNC_READ_FLOAT, addr, BYTE_READ_FLOAT, ret, dest, dest16, is16Bit, writeAccess, funcType);

        }       
    }
    else if (ui->tabWidget_2->currentIndex() == 1)
    {
        if (m_modbus_2 == NULL) // LOOP 2
        {
            setStatusError( tr("Loop_2 not configured!") );
            return;
        }

        if (ui->tabWidget_4->currentIndex() == 0) // P1
        {

        }
        else if (ui->tabWidget_4->currentIndex() == 1) // P2
        {

        }
        else { // P3

        }

        const int addr = ui->startAddr->value()-1;
        int num = ui->numCoils->value();
        uint8_t dest[1024];
        uint16_t * dest16 = (uint16_t *) dest;

        memset( dest, 0, 1024 );

        int ret = -1;
        bool is16Bit = false;
        bool writeAccess = false;
        const QString funcType = descriptiveDataTypeName( FUNC_READ_FLOAT );

        modbus_set_slave( m_serialModbus_2, SLAVE_CALIBRATION );
        sendCalibrationRequest(FLOAT_R, m_serialModbus_2, FUNC_READ_FLOAT, addr, num, ret, dest, dest16, is16Bit, writeAccess, funcType);
    }
    else if (ui->tabWidget_2->currentIndex() == 2)
    {
        if (m_modbus_3 == NULL) // LOOP 3
        {
            setStatusError( tr("Loop_3 not configured!") );
            return;
        }
        if (ui->tabWidget_5->currentIndex() == 0)
        {

        }
        else if (ui->tabWidget_5->currentIndex() == 1)
        {

        }
        else {

        }

        const int addr = ui->startAddr->value()-1;
        int num = ui->numCoils->value();
        uint8_t dest[1024];
        uint16_t * dest16 = (uint16_t *) dest;

        memset( dest, 0, 1024 );

        int ret = -1;
        bool is16Bit = false;
        bool writeAccess = false;
        const QString funcType = descriptiveDataTypeName( FUNC_READ_FLOAT );

        modbus_set_slave( m_serialModbus_3, SLAVE_CALIBRATION );
        sendCalibrationRequest(FLOAT_R, m_serialModbus_3, FUNC_READ_FLOAT, addr, num, ret, dest, dest16, is16Bit, writeAccess, funcType);
    }
    else if (ui->tabWidget_2->currentIndex() == 3)
    {
        if (m_modbus_4 == NULL) // LOOP 4
        {
            setStatusError( tr("Loop_4 not configured!") );
            return;
        }
        if (ui->tabWidget_6->currentIndex() == 0)
        {

        }
        else if (ui->tabWidget_6->currentIndex() == 1)
        {

        }
        else {

        }

        const int addr = ui->startAddr->value()-1;
        int num = ui->numCoils->value();
        uint8_t dest[1024];
        uint16_t * dest16 = (uint16_t *) dest;

        memset( dest, 0, 1024 );

        int ret = -1;
        bool is16Bit = false;
        bool writeAccess = false;
        const QString funcType = descriptiveDataTypeName( FUNC_READ_FLOAT );

        modbus_set_slave( m_serialModbus_4, SLAVE_CALIBRATION );
        sendCalibrationRequest(FLOAT_R, m_serialModbus_4, FUNC_READ_FLOAT, addr, num, ret, dest, dest16, is16Bit, writeAccess, funcType);
    }
    else if (ui->tabWidget_2->currentIndex() == 4)
    {
        if (m_modbus_5 == NULL) // LOOP 5
        {
            setStatusError( tr("Loop_5 not configured!") );
            return;
        }
        if (ui->tabWidget_7->currentIndex() == 0)
        {

        }
        else if (ui->tabWidget_7->currentIndex() == 1)
        {

        }
        else {

        }

        const int addr = ui->startAddr->value()-1;
        int num = ui->numCoils->value();
        uint8_t dest[1024];
        uint16_t * dest16 = (uint16_t *) dest;

        memset( dest, 0, 1024 );

        int ret = -1;
        bool is16Bit = false;
        bool writeAccess = false;
        const QString funcType = descriptiveDataTypeName( FUNC_READ_FLOAT );

        modbus_set_slave( m_serialModbus_5, SLAVE_CALIBRATION );
        sendCalibrationRequest(FLOAT_R, m_serialModbus_5, FUNC_READ_FLOAT, addr, num, ret, dest, dest16, is16Bit, writeAccess, funcType);
    }
    else
    {
        if (m_modbus_6 == NULL) // LOOP 6
        {
            setStatusError( tr("Loop_6 not configured!") );
            return;
        }

        if (ui->tabWidget_8->currentIndex() == 0) // P1
        {

        }
        else if (ui->tabWidget_8->currentIndex() == 1) // P2
        {

        }
        else { // P3

        }

        const int addr = ui->startAddr->value()-1;
        int num = ui->numCoils->value();
        uint8_t dest[1024];
        uint16_t * dest16 = (uint16_t *) dest;

        memset( dest, 0, 1024 );

        int ret = -1;
        bool is16Bit = false;
        bool writeAccess = false;
        const QString funcType = descriptiveDataTypeName( FUNC_READ_FLOAT );

        modbus_set_slave( m_serialModbus_6, SLAVE_CALIBRATION );
        sendCalibrationRequest(FLOAT_R, m_serialModbus_6, FUNC_READ_FLOAT, addr, num, ret, dest, dest16, is16Bit, writeAccess, funcType);
    }
}

QString
MainWindow::
sendCalibrationRequest(int dataType, modbus_t * serialModbus, int func, int addr, int num, int ret, uint8_t * dest, uint16_t * dest16, bool is16Bit, bool writeAccess, QString funcType)
{
    switch( func )
    {
        case MODBUS_FC_READ_COILS:
            ret = modbus_read_bits( serialModbus, addr, num, dest );
            break;
        case MODBUS_FC_READ_DISCRETE_INPUTS:
            ret = modbus_read_input_bits( serialModbus, addr, num, dest );
            break;
        case MODBUS_FC_READ_HOLDING_REGISTERS:
            ret = modbus_read_registers( serialModbus, addr, num, dest16 );
            is16Bit = true;
            break;
        case MODBUS_FC_READ_INPUT_REGISTERS:
            ret = modbus_read_input_registers(serialModbus, addr, num, dest16 );
            is16Bit = true;
            break;
        case MODBUS_FC_WRITE_SINGLE_COIL:
            //ret = modbus_write_bit( m_modbus_snipping, addr,ui->regTable->item( 0, DataColumn )->text().toInt(0, 0) ? 1 : 0 );
            ret = modbus_write_bit( serialModbus, addr,ui->radioButton_184->isChecked() ? 1 : 0 );
            writeAccess = true;
            num = 1;
            break;
        case MODBUS_FC_WRITE_SINGLE_REGISTER:
            //ret = modbus_write_register( m_modbus_snipping, addr,ui->regTable->item( 0, DataColumn )->text().toInt(0, 0) );
            ret = modbus_write_register( serialModbus, addr,ui->lineEdit_111->text().toInt(0, 0) );
            writeAccess = true;
            num = 1;
            break;
        case MODBUS_FC_WRITE_MULTIPLE_COILS:
        {
            uint8_t * data = new uint8_t[num];
            for( int i = 0; i < num; ++i ) data[i] = ui->regTable->item( i, DataColumn )->text().toInt(0, 0);
            ret = modbus_write_bits( serialModbus, addr, num, data );
            delete[] data;
            writeAccess = true;
            break;
        }
        case MODBUS_FC_WRITE_MULTIPLE_REGISTERS:
        {
            float value;
            QString qvalue = ui->lineEdit_109->text();
            QTextStream floatTextStream(&qvalue);
            floatTextStream >> value;
            quint16 (*reg)[2] = reinterpret_cast<quint16(*)[2]>(&value);
            uint16_t * data = new uint16_t[2];
            data[0] = (*reg)[1];
            data[1] = (*reg)[0];
            ret = modbus_write_registers( serialModbus, addr, 2, data );
            delete[] data;
            writeAccess = true;
            break;
        }
        default:
            break;
    }

    if( ret == num  )
    {
        if( writeAccess )
        {
            m_statusText->setText(tr( "Values successfully sent" ) );
            m_statusInd->setStyleSheet( "background: #0b0;" );
            m_statusTimer->start( 2000 );
        }
        else
        {
            //bool b_hex = is16Bit && ui->checkBoxHexData->checkState() == Qt::Checked;
            QString qs_num;
            QString qs_output = "0x";
            bool ok = false;

            ui->regTable->setRowCount( num );
            for( int i = 0; i < num; ++i )
            {
                int data = is16Bit ? dest16[i] : dest[i];
                QString qs_tmp;

                //QTableWidgetItem * dtItem = new QTableWidgetItem( funcType );
                //QTableWidgetItem * addrItem = new QTableWidgetItem(QString::number( ui->startAddr->value()+i ) );
                //qs_num.sprintf( b_hex ? "0x%04x" : "%d", data);
                qs_num.sprintf("%d", data);
                qs_tmp.sprintf("%04x", data);
                qs_output.append(qs_tmp);
                /*
                QTableWidgetItem * dataItem = new QTableWidgetItem( qs_num );
                dtItem->setFlags( dtItem->flags() & ~Qt::ItemIsEditable );
                addrItem->setFlags( addrItem->flags() & ~Qt::ItemIsEditable );
                dataItem->setFlags( dataItem->flags() & ~Qt::ItemIsEditable );
                ui->regTable->setItem( i, DataTypeColumn, dtItem );
                ui->regTable->setItem( i, AddrColumn, addrItem );
                ui->regTable->setItem( i, DataColumn, dataItem );
                if (ui->radioButton_182->isChecked()) ui->lineEdit_111->setText(QString::number(data));
                else if (ui->radioButton_183->isChecked())
                {
                    (data) ? ui->radioButton_184->setChecked(true) : ui->radioButton_185->setChecked(true);
                } */
                if (dataType == FLOAT_R) // FLOAT_READ
                {// float
                    QByteArray array = QByteArray::fromHex(qs_output.toLatin1());
                    const float d = toFloat(array);
                    return QString::number(d,'f',6);
                }
                else if (dataType == INT_R)
                {
                    return QString::number(data);
                }
                else if (dataType == COIL_R)
                {
                    return (data) ? "1" : "0";
                }
            }
        }
    }
    else
    {
        QString err;

        if( ret < 0 )
        {
            if(
#ifdef WIN32
                    errno == WSAETIMEDOUT ||
#endif
                    errno == EIO
                                                                    )
            {
                err += tr( "I/O error" );
                err += ": ";
                err += tr( "did not receive any data from slave." );
            }
            else
            {
                err += tr( "Protocol error" );
                err += ": ";
                err += tr( "Slave threw exception '" );
                err += modbus_strerror( errno );
                err += tr( "' or function not implemented." );
            }
        }
        else
        {
            err += tr( "Protocol error" );
            err += ": ";
            err += tr( "Number of registers returned does not "
                    "match number of registers requested!" );
        }

        if( err.size() > 0 )
            setStatusError( err );
    }
}


void
MainWindow::
saveCsvFile()
{
    QString fileName = QFileDialog::getSaveFileName(this,tr("Save Equation"), "",tr("CSV file (*.csv);;All Files (*)"));

    if (fileName.isEmpty()) return;
    QFile file(fileName);
    QTextStream out(&file);

    if (!file.open(QIODevice::WriteOnly))
    {
        QMessageBox::information(this, tr("Unable to open file"),file.errorString());
        return;
    }

    for ( int i = 0; i < ui->tableWidget->rowCount(); i++ )
    {
        QString dataStream;

        for (int j=0; j < ui->tableWidget->item(i,3)->text().toInt()+4; j++)
        {
             dataStream.append(ui->tableWidget->item(i,j)->text()+",");
        }

        out << dataStream << endl;
    }

    file.close();
}


void
MainWindow::
loadCsvTemplate()
{
    int line = 0;
    QFile file;
    QString razorTemplatePath = QCoreApplication::applicationDirPath()+"/razor.csv";
    QString eeaTemplatePath = QCoreApplication::applicationDirPath()+"/eea.csv";

    if (ui->radioButton_190->isChecked()) // eea
        file.setFileName(eeaTemplatePath);
    else
        file.setFileName(razorTemplatePath);

    if (!file.open(QIODevice::ReadOnly)) return;

    QTextStream str(&file);

    ui->tableWidget->clearContents();
    ui->tableWidget->setRowCount(0);

    while (!str.atEnd()) {

        QString s = str.readLine();
        if (s.size() == 0)
        {
            file.close();
            break;
        }
        else {
            line++;
        }

        // split data
        QStringList valueList = s.split(',');

        if (!valueList[0].contains("*"))
        {
            // insert a new row
            ui->tableWidget->insertRow( ui->tableWidget->rowCount() );

            // insert columns
            while (ui->tableWidget->columnCount() < valueList[6].toInt()+4)
            {
                ui->tableWidget->insertColumn(ui->tableWidget->columnCount());
            }

            // fill the data in the talbe cell
            ui->tableWidget->setItem( ui->tableWidget->rowCount()-1, 0, new QTableWidgetItem(valueList[0])); // Name
            ui->tableWidget->setItem( ui->tableWidget->rowCount()-1, 1, new QTableWidgetItem(valueList[2])); // Starting Address
            ui->tableWidget->setItem( ui->tableWidget->rowCount()-1, 2, new QTableWidgetItem(valueList[3])); // Data Type
            ui->tableWidget->setItem( ui->tableWidget->rowCount()-1, 3, new QTableWidgetItem(valueList[6])); // Qty

            // enable uploadEquationButton
            ui->startEquationBtn->setEnabled(1);
        }
    }

    // set column width
    ui->tableWidget->setColumnWidth(0,120);
    ui->tableWidget->setColumnWidth(1,60);
    ui->tableWidget->setColumnWidth(2,50);
    ui->tableWidget->setColumnWidth(3,40);

    // close file
    file.close();
}

void
MainWindow::
loadCsvFile()
{
    int line = 0;

    QString fileName = QFileDialog::getOpenFileName( this, tr("Open CSV file"), QDir::currentPath(), tr("CSV files (*.csv)") );
    QFile file(fileName);

    if (!file.open(QIODevice::ReadOnly)) return;

    QTextStream str(&file);

    ui->tableWidget->clearContents();
    ui->tableWidget->setRowCount(0);

    while (!str.atEnd()) {

        QString s = str.readLine();
        if (s.size() == 0)
        {
            file.close();
            break;
        }
        else {
            line++;
        }

        QStringList valueList = s.split(',');
        if (!valueList[0].contains("*"))
        {
            // insert a new row
            ui->tableWidget->insertRow( ui->tableWidget->rowCount() );

            // insert columns
            while (ui->tableWidget->columnCount() < valueList[6].toInt()+4)
            {
                ui->tableWidget->insertColumn(ui->tableWidget->columnCount());
            }

            // fill the data in the talbe cell
            ui->tableWidget->setItem( ui->tableWidget->rowCount()-1, 0, new QTableWidgetItem(valueList[0])); // Register
            ui->tableWidget->setItem( ui->tableWidget->rowCount()-1, 1, new QTableWidgetItem(valueList[2])); // Address
            ui->tableWidget->setItem( ui->tableWidget->rowCount()-1, 2, new QTableWidgetItem(valueList[3])); // Type
            ui->tableWidget->setItem( ui->tableWidget->rowCount()-1, 3, new QTableWidgetItem(valueList[6])); // Qty

            // fill the actual value
            for (int j = 0; j < valueList[6].toInt(); j++)
            {
                QString cellData = valueList[7+j];;
                if (valueList[3].contains("int")) cellData = cellData.mid(0, cellData.indexOf("."));

                ui->tableWidget->setItem( ui->tableWidget->rowCount()-1, j+4, new QTableWidgetItem(cellData));
            }

            // enable uploadEquationButton
            ui->startEquationBtn->setEnabled(1);
        }
    }

    // set column width
    ui->tableWidget->setColumnWidth(0,120);
    ui->tableWidget->setColumnWidth(1,60);
    ui->tableWidget->setColumnWidth(2,50);
    ui->tableWidget->setColumnWidth(3,40);

    // close file
    file.close();
}

void
MainWindow::
onEquationButtonPressed()
{
    if (ui->radioButton_188->isChecked())
    {
        if( m_pollTimer->isActive() )
        {
            m_pollTimer->stop();
            ui->startEquationBtn->setText( tr("Loading") );
        }
        else
        {
            // if polling requested then enable timer
            if( m_poll )
            {
                m_pollTimer->start( 1000 );
                ui->sendBtn->setText( tr("Loading") );
            }

            onUploadEquation();
        }
    }
    else
    {
        if( m_pollTimer->isActive() )
        {
            m_pollTimer->stop();
            ui->startEquationBtn->setText( tr("Loading") );
        }
        else
        {
            // if polling requested then enable timer
            if( m_poll )
            {
                m_pollTimer->start( 1000 );
                ui->sendBtn->setText( tr("Loading") );
            }

            ui->tableWidget->clearContents();
            ui->tableWidget->setRowCount(0);

            onDownloadEquation();
        }
    }

    ui->startEquationBtn->setText(tr("Start"));
}


void
MainWindow::
onDownloadButtonChecked(bool isChecked)
{
    if (isChecked)
    {
        ui->checkBox->setChecked(false);
        ui->checkBox_2->setChecked(false);
        ui->startEquationBtn->setEnabled(true);

    }
    else {
        ui->checkBox->setChecked(true);
        ui->checkBox_2->setChecked(true);
        ui->startEquationBtn->setEnabled(true);
    }
}


void
MainWindow::
onDownloadEquation()
{
    ui->slaveID->setValue(1);                           // set slave ID
    ui->radioButton_187->setChecked(true);              // read mode
    ui->startEquationBtn->setEnabled(false);

    // load empty equation file
    loadCsvTemplate();

    for (int i = 0; i < ui->tableWidget->rowCount(); i++)
    {
         int regAddr = ui->tableWidget->item(i,1)->text().toInt();

         if (ui->tableWidget->item(i,2)->text().contains("float"))
         {
             ui->numCoils->setValue(2);                  // 2 bytes
             ui->radioButton_181->setChecked(TRUE);      // float type
             ui->functionCode->setCurrentIndex(3);       // function code
             for (int x=0; x < ui->tableWidget->item(i,3)->text().toInt(); x++)
             {
                 ui->startAddr->setValue(regAddr);       // set address
                 onSendButtonPress();                    // send
                 QThread::sleep(1);
                 regAddr = regAddr+2;                    // update reg address
                 ui->tableWidget->setItem( i, x+4, new QTableWidgetItem(ui->lineEdit_109->text()));
             }
         }
         else if (ui->tableWidget->item(i,2)->text().contains("int"))
         {
             ui->numCoils->setValue(1);                  // 1 byte
             ui->radioButton_182->setChecked(TRUE);      // int type
             ui->functionCode->setCurrentIndex(3);       // function code
             ui->startAddr->setValue(regAddr);           // address
             onSendButtonPress();                        // send
             QThread::sleep(1);
             ui->tableWidget->setItem( i, 4, new QTableWidgetItem(ui->lineEdit_111->text()));
         }
         else
         {
             ui->numCoils->setValue(1);                  // 1 byte
             ui->radioButton_183->setChecked(TRUE);      // coil type
             ui->functionCode->setCurrentIndex(0);       // function code
             ui->startAddr->setValue(regAddr);           // address
             onSendButtonPress();                        // send
             QThread::sleep(1);
         }
     }
}


void
MainWindow::
onUploadEquation()
{        
    ui->slaveID->setValue(1);                           // set slave ID
    ui->radioButton_186->setChecked(true);              // write mode    
    ui->startEquationBtn->setEnabled(false);

    if (ui->checkBox->isChecked())                      // unlock coil 999
    {
        ui->numCoils->setValue(1);                      // 1 byte
        ui->radioButton_183->setChecked(TRUE);          // coil type
        ui->functionCode->setCurrentIndex(4);           // function type
        ui->startAddr->setValue(999);                   // address
        ui->radioButton_184->setChecked(true);          // set value

        onSendButtonPress();
        QThread::sleep(1);
    }

   for (int i = 0; i < ui->tableWidget->rowCount(); i++)
   {
        int regAddr = ui->tableWidget->item(i,1)->text().toInt();

        if (ui->tableWidget->item(i,2)->text().contains("float"))
        {
            ui->numCoils->setValue(2);                  // 2 bytes
            ui->radioButton_181->setChecked(TRUE);      // float type
            ui->functionCode->setCurrentIndex(7);       // function code
            for (int x=0; x < ui->tableWidget->item(i,3)->text().toInt(); x++)
            {
                ui->startAddr->setValue(regAddr);       // set address
                ui->lineEdit_109->setText(ui->tableWidget->item(i,4+x)->text()); // set value
                onSendButtonPress();                    // send
                regAddr = regAddr+2;                    // update reg address
                QThread::sleep(1);
            }
        }
        else if (ui->tableWidget->item(i,2)->text().contains("int"))
        {
            ui->numCoils->setValue(1);                  // 1 byte
            ui->radioButton_182->setChecked(TRUE);      // int type
            ui->functionCode->setCurrentIndex(5);       // function code
            ui->lineEdit_111->setText(ui->tableWidget->item(i,4)->text()); // set value
            ui->startAddr->setValue(regAddr);           // address
            onSendButtonPress();                        // send
            QThread::sleep(1);
        }
        else
        {
            ui->numCoils->setValue(1);                  // 1 byte
            ui->radioButton_183->setChecked(TRUE);      // coil type
            ui->functionCode->setCurrentIndex(4);       // function code
            ui->startAddr->setValue(regAddr);           // address
            ui->radioButton_184->setChecked(true);
            onSendButtonPress();                        // send
            QThread::sleep(1);
        }
    }

    if (ui->checkBox_2->isChecked())                    // unlock coil 9999
    {
        ui->numCoils->setValue(1);                      // 1 byte
        ui->radioButton_183->setChecked(TRUE);          // coil type
        ui->functionCode->setCurrentIndex(4);           // function code
        ui->startAddr->setValue(9999);                  // address
        ui->radioButton_184->setChecked(true);          // set value
        onSendButtonPress();
        QThread::sleep(1);
    }
}


void
MainWindow::
connectProfiler()
{
    connect(ui->startEquationBtn, SIGNAL(pressed()), this, SLOT(onEquationButtonPressed()));
    connect(ui->radioButton_189, SIGNAL(toggled(bool)), this, SLOT(onDownloadButtonChecked(bool)));
}


void
MainWindow::
initializeGauges()
{
    initializePressureGauge();
    updatePressureGauge();

    initializeTemperatureGauge();
    updateTemperatureGauge();

    initializeDensityGauge();
    updateDensityGauge();

    initializeRPGauge();
    updateRPGauge();
}

void
MainWindow::
setupModbusPorts()
{
    setupModbusPort();
    setupModbusPort_2();
    setupModbusPort_3();
    setupModbusPort_4();
    setupModbusPort_5();
    setupModbusPort_6();
}

int
MainWindow::
setupModbusPort()
{
    QSettings s;

    int portIndex = 0;
    int i = 0;
    ui->comboBox->disconnect();
    ui->comboBox->clear();
    foreach( QextPortInfo port, QextSerialEnumerator::getPorts() )
    {
        ui->comboBox->addItem( port.friendName );

        if( port.friendName == s.value( "serialinterface" ) )
        {
            portIndex = i;
        }
        ++i;
    }
    ui->comboBox->setCurrentIndex( portIndex );
    ui->comboBox_2->setCurrentIndex(0);
    ui->comboBox_3->setCurrentIndex(0);
    ui->comboBox_4->setCurrentIndex(0);
    ui->comboBox_5->setCurrentIndex(0);

    connect( ui->comboBox, SIGNAL( currentIndexChanged( int ) ),this, SLOT( changeSerialPort( int ) ) );
    connect( ui->comboBox_2, SIGNAL( currentIndexChanged( int ) ),this, SLOT( changeSerialPort( int ) ) );
    connect( ui->comboBox_3, SIGNAL( currentIndexChanged( int ) ),this, SLOT( changeSerialPort( int ) ) );
    connect( ui->comboBox_4, SIGNAL( currentIndexChanged( int ) ),this, SLOT( changeSerialPort( int ) ) );
    connect( ui->comboBox_5, SIGNAL( currentIndexChanged( int ) ),this, SLOT( changeSerialPort( int ) ) );

    changeSerialPort( portIndex );
    return portIndex;
}

int
MainWindow::
setupModbusPort_2()
{
    QSettings s;

    int portIndex = 0;
    int i = 0;
    ui->comboBox_6->disconnect();
    ui->comboBox_6->clear();
    foreach( QextPortInfo port, QextSerialEnumerator::getPorts() )
    {
#ifdef Q_OS_WIN
        ui->comboBox_6->addItem( port.friendName );
#else
        ui->comboBox_6->addItem( port.physName );
#endif
        if( port.friendName == s.value( "serialinterface" ) )
        {
            portIndex = i;
        }
        ++i;
    }
    ui->comboBox_6->setCurrentIndex( portIndex );
    ui->comboBox_7->setCurrentIndex(0);
    ui->comboBox_8->setCurrentIndex(0);
    ui->comboBox_9->setCurrentIndex(0);
    ui->comboBox_10->setCurrentIndex(0);

    connect( ui->comboBox_6, SIGNAL( currentIndexChanged( int ) ),this, SLOT( changeSerialPort_2( int ) ) );
    connect( ui->comboBox_7, SIGNAL( currentIndexChanged( int ) ),this, SLOT( changeSerialPort_2( int ) ) );
    connect( ui->comboBox_8, SIGNAL( currentIndexChanged( int ) ),this, SLOT( changeSerialPort_2( int ) ) );
    connect( ui->comboBox_9, SIGNAL( currentIndexChanged( int ) ),this, SLOT( changeSerialPort_2( int ) ) );
    connect( ui->comboBox_10, SIGNAL( currentIndexChanged( int ) ),this, SLOT( changeSerialPort_2( int ) ) );

    changeSerialPort_2( portIndex );
    return portIndex;
}

int
MainWindow::
setupModbusPort_3()
{
    QSettings s;

    int portIndex = 0;
    int i = 0;
    ui->comboBox_11->disconnect();
    ui->comboBox_11->clear();
    foreach( QextPortInfo port, QextSerialEnumerator::getPorts() )
    {
#ifdef Q_OS_WIN
        ui->comboBox_11->addItem( port.friendName );
#else
        ui->comboBox_11->addItem( port.physName );
#endif
        if( port.friendName == s.value( "serialinterface" ) )
        {
            portIndex = i;
        }
        ++i;
    }
    ui->comboBox_11->setCurrentIndex( portIndex );
    ui->comboBox_12->setCurrentIndex(0);
    ui->comboBox_13->setCurrentIndex(0);
    ui->comboBox_14->setCurrentIndex(0);
    ui->comboBox_15->setCurrentIndex(0);

    connect( ui->comboBox_11, SIGNAL( currentIndexChanged( int ) ),this, SLOT( changeSerialPort_3( int ) ) );
    connect( ui->comboBox_12, SIGNAL( currentIndexChanged( int ) ),this, SLOT( changeSerialPort_3( int ) ) );
    connect( ui->comboBox_13, SIGNAL( currentIndexChanged( int ) ),this, SLOT( changeSerialPort_3( int ) ) );
    connect( ui->comboBox_14, SIGNAL( currentIndexChanged( int ) ),this, SLOT( changeSerialPort_3( int ) ) );
    connect( ui->comboBox_15, SIGNAL( currentIndexChanged( int ) ),this, SLOT( changeSerialPort_3( int ) ) );

    changeSerialPort_3( portIndex );
    return portIndex;
}

int
MainWindow::
setupModbusPort_4()
{
    QSettings s;

    int portIndex = 0;
    int i = 0;
    ui->comboBox_16->disconnect();
    ui->comboBox_16->clear();
    foreach( QextPortInfo port, QextSerialEnumerator::getPorts() )
    {
#ifdef Q_OS_WIN
        ui->comboBox_16->addItem( port.friendName );
#else
        ui->comboBox_16->addItem( port.physName );
#endif
        if( port.friendName == s.value( "serialinterface" ) )
        {
            portIndex = i;
        }
        ++i;
    }
    ui->comboBox_16->setCurrentIndex( portIndex );

    ui->comboBox_17->setCurrentIndex(0);
    ui->comboBox_18->setCurrentIndex(0);
    ui->comboBox_19->setCurrentIndex(0);
    ui->comboBox_20->setCurrentIndex(0);

    connect( ui->comboBox_16, SIGNAL( currentIndexChanged( int ) ),this, SLOT( changeSerialPort_4( int ) ) );
    connect( ui->comboBox_17, SIGNAL( currentIndexChanged( int ) ),this, SLOT( changeSerialPort_4( int ) ) );
    connect( ui->comboBox_18, SIGNAL( currentIndexChanged( int ) ),this, SLOT( changeSerialPort_4( int ) ) );
    connect( ui->comboBox_19, SIGNAL( currentIndexChanged( int ) ),this, SLOT( changeSerialPort_4( int ) ) );
    connect( ui->comboBox_20, SIGNAL( currentIndexChanged( int ) ),this, SLOT( changeSerialPort_4( int ) ) );

    changeSerialPort_4( portIndex );
    return portIndex;
}

int
MainWindow::
setupModbusPort_5()
{
    QSettings s;

    int portIndex = 0;
    int i = 0;
    ui->comboBox_21->disconnect();
    ui->comboBox_21->clear();
    foreach( QextPortInfo port, QextSerialEnumerator::getPorts() )
    {
#ifdef Q_OS_WIN
        ui->comboBox_21->addItem( port.friendName );
#else
        ui->comboBox_21->addItem( port.physName );
#endif
        if( port.friendName == s.value( "serialinterface" ) )
        {
            portIndex = i;
        }
        ++i;
    }
    ui->comboBox_21->setCurrentIndex( portIndex );
    ui->comboBox_22->setCurrentIndex(0);
    ui->comboBox_23->setCurrentIndex(0);
    ui->comboBox_24->setCurrentIndex(0);
    ui->comboBox_25->setCurrentIndex(0);

    connect( ui->comboBox_21, SIGNAL( currentIndexChanged( int ) ),this, SLOT( changeSerialPort_5( int ) ) );
    connect( ui->comboBox_22, SIGNAL( currentIndexChanged( int ) ),this, SLOT( changeSerialPort_5( int ) ) );
    connect( ui->comboBox_23, SIGNAL( currentIndexChanged( int ) ),this, SLOT( changeSerialPort_5( int ) ) );
    connect( ui->comboBox_24, SIGNAL( currentIndexChanged( int ) ),this, SLOT( changeSerialPort_5( int ) ) );
    connect( ui->comboBox_25, SIGNAL( currentIndexChanged( int ) ),this, SLOT( changeSerialPort_5( int ) ) );

    changeSerialPort_5( portIndex );
    return portIndex;
}

int
MainWindow::
setupModbusPort_6()
{
    QSettings s;

    int portIndex = 0;
    int i = 0;
    ui->comboBox_26->disconnect();
    ui->comboBox_26->clear();
    foreach( QextPortInfo port, QextSerialEnumerator::getPorts() )
    {
#ifdef Q_OS_WIN
        ui->comboBox_26->addItem( port.friendName );
#else
        ui->comboBox_26->addItem( port.physName );
#endif
        if( port.friendName == s.value( "serialinterface" ) )
        {
            portIndex = i;
        }
        ++i;
    }
    ui->comboBox_26->setCurrentIndex( portIndex );
    ui->comboBox_27->setCurrentIndex(0);
    ui->comboBox_28->setCurrentIndex(0);
    ui->comboBox_29->setCurrentIndex(0);
    ui->comboBox_30->setCurrentIndex(0);

    connect( ui->comboBox_26, SIGNAL( currentIndexChanged( int ) ),this, SLOT( changeSerialPort_6( int ) ) );
    connect( ui->comboBox_27, SIGNAL( currentIndexChanged( int ) ),this, SLOT( changeSerialPort_6( int ) ) );
    connect( ui->comboBox_28, SIGNAL( currentIndexChanged( int ) ),this, SLOT( changeSerialPort_6( int ) ) );
    connect( ui->comboBox_29, SIGNAL( currentIndexChanged( int ) ),this, SLOT( changeSerialPort_6( int ) ) );
    connect( ui->comboBox_30, SIGNAL( currentIndexChanged( int ) ),this, SLOT( changeSerialPort_6( int ) ) );

    changeSerialPort_6( portIndex );
    return portIndex;
}


//static inline QString embracedString( const QString & s )
//{
    //return s.section( '(', 1 ).section( ')', 0, 0 );
//}


void
MainWindow::
releaseSerialModbus()
{
    if( m_serialModbus )
    {
        modbus_close( m_serialModbus );
        modbus_free( m_serialModbus );
        m_serialModbus = NULL;
        updateTabIcon(0, false);
    }
}

void
MainWindow::
releaseSerialModbus_2()
{
    if( m_serialModbus_2 )
    {
        modbus_close( m_serialModbus_2 );
        modbus_free( m_serialModbus_2 );
        m_serialModbus_2 = NULL;
        updateTabIcon(1, false);
    }
}

void
MainWindow::
releaseSerialModbus_3()
{
    if( m_serialModbus_3 )
    {
        modbus_close( m_serialModbus_3 );
        modbus_free( m_serialModbus_3 );
        m_serialModbus_3 = NULL;
        updateTabIcon(2, false);
    }
}

void
MainWindow::
releaseSerialModbus_4()
{
    if( m_serialModbus_4 )
    {
        modbus_close( m_serialModbus_4);
        modbus_free( m_serialModbus_4 );
        m_serialModbus_4 = NULL;
        updateTabIcon(3, false);
    }
}


void
MainWindow::
releaseSerialModbus_5()
{
    if( m_serialModbus_5 )
    {
        modbus_close( m_serialModbus_5 );
        modbus_free( m_serialModbus_5 );
        m_serialModbus_5 = NULL;
        updateTabIcon(4, false);
    }
}


void
MainWindow::
releaseSerialModbus_6()
{
    if( m_serialModbus_6 )
    {
        modbus_close( m_serialModbus_6 );
        modbus_free( m_serialModbus_6 );
        m_serialModbus_6 = NULL;
        updateTabIcon(5, false);
    }
}

void
MainWindow::
changeSerialPort( int )
{
    const int iface = ui->comboBox->currentIndex();

    QList<QextPortInfo> ports = QextSerialEnumerator::getPorts();
    if( !ports.isEmpty() )
    {
        QSettings settings;
        settings.setValue( "serialinterface", ports[iface].friendName );
        settings.setValue( "serialbaudrate", ui->comboBox_2->currentText() );
        settings.setValue( "serialparity", ui->comboBox_3->currentText() );
        settings.setValue( "serialdatabits", ui->comboBox_4->currentText() );
        settings.setValue( "serialstopbits", ui->comboBox_5->currentText() );
        QString port = ports[iface].portName;

        // is it a serial port in the range COM1 .. COM9?
        if ( port.startsWith( "COM" ) )
        {
            // use windows communication device name "\\.\COMn"
            port = "\\\\.\\" + port;
        }

        char parity;
        switch( ui->comboBox_3->currentIndex() )
        {
            case 1: parity = 'O'; break;
            case 2: parity = 'E'; break;
            default:
            case 0: parity = 'N'; break;
        }

        changeModbusInterface(port, parity);
        onRtuPortActive(true);
    }
    else emit connectionError( tr( "No serial port found at Loop_1" ) );
}


void
MainWindow::
changeSerialPort_2( int )
{
    const int iface = ui->comboBox_6->currentIndex();

    QList<QextPortInfo> ports = QextSerialEnumerator::getPorts();
    if( !ports.isEmpty() )
    {
        QSettings settings;
        settings.setValue( "serialinterface", ports[iface].friendName );
        settings.setValue( "serialbaudrate", ui->comboBox_7->currentText() );
        settings.setValue( "serialparity", ui->comboBox_8->currentText() );
        settings.setValue( "serialdatabits", ui->comboBox_9->currentText() );
        settings.setValue( "serialstopbits", ui->comboBox_10->currentText() );
        QString port = ports[iface].portName;

        // is it a serial port in the range COM1 .. COM9?
        if ( port.startsWith( "COM" ) )
        {
            // use windows communication device name "\\.\COMn"
            port = "\\\\.\\" + port;
        }

        char parity;
        switch( ui->comboBox_8->currentIndex() )
        {
            case 1: parity = 'O'; break;
            case 2: parity = 'E'; break;
            default:
            case 0: parity = 'N'; break;
        }

        changeModbusInterface_2(port, parity);
        onRtuPortActive_2(true);
    }
    else emit connectionError( tr( "No serial port found at Loop_2" ) );
}


void
MainWindow::
changeSerialPort_3( int )
{
    const int iface = ui->comboBox_11->currentIndex();

    QList<QextPortInfo> ports = QextSerialEnumerator::getPorts();
    if( !ports.isEmpty() )
    {
        QSettings settings;
        settings.setValue( "serialinterface", ports[iface].friendName );
        settings.setValue( "serialbaudrate", ui->comboBox_12->currentText() );
        settings.setValue( "serialparity", ui->comboBox_13->currentText() );
        settings.setValue( "serialdatabits", ui->comboBox_14->currentText() );
        settings.setValue( "serialstopbits", ui->comboBox_15->currentText() );
        QString port = ports[iface].portName;

        // is it a serial port in the range COM1 .. COM9?
        if ( port.startsWith( "COM" ) )
        {
            // use windows communication device name "\\.\COMn"
            port = "\\\\.\\" + port;
        }

        char parity;
        switch( ui->comboBox_13->currentIndex() )
        {
            case 1: parity = 'O'; break;
            case 2: parity = 'E'; break;
            default:
            case 0: parity = 'N'; break;
        }

        changeModbusInterface_3(port, parity);
        onRtuPortActive_3(true);
    }
    else emit connectionError( tr( "No serial port found" ) );
}


void
MainWindow::
changeSerialPort_4( int )
{
    const int iface = ui->comboBox_16->currentIndex();

    QList<QextPortInfo> ports = QextSerialEnumerator::getPorts();
    if( !ports.isEmpty() )
    {
        QSettings settings;
        settings.setValue( "serialinterface", ports[iface].friendName );
        settings.setValue( "serialbaudrate", ui->comboBox_17->currentText() );
        settings.setValue( "serialparity", ui->comboBox_18->currentText() );
        settings.setValue( "serialdatabits", ui->comboBox_19->currentText() );
        settings.setValue( "serialstopbits", ui->comboBox_20->currentText() );

        QString port = ports[iface].portName;

        // is it a serial port in the range COM1 .. COM9?
        if ( port.startsWith( "COM" ) )
        {
            // use windows communication device name "\\.\COMn"
            port = "\\\\.\\" + port;
        }

        char parity;
        switch( ui->comboBox_18->currentIndex() )
        {
            case 1: parity = 'O'; break;
            case 2: parity = 'E'; break;
            default:
            case 0: parity = 'N'; break;
        }

        changeModbusInterface_4(port, parity);
        onRtuPortActive_4(true);
    }
    else emit connectionError( tr( "No serial port found" ) );
}


void
MainWindow::
changeSerialPort_5( int )
{
    const int iface = ui->comboBox_21->currentIndex();

    QList<QextPortInfo> ports = QextSerialEnumerator::getPorts();
    if( !ports.isEmpty() )
    {
        QSettings settings;
        settings.setValue( "serialinterface", ports[iface].friendName );
        settings.setValue( "serialbaudrate", ui->comboBox_22->currentText() );
        settings.setValue( "serialparity", ui->comboBox_23->currentText() );
        settings.setValue( "serialdatabits", ui->comboBox_24->currentText() );
        settings.setValue( "serialstopbits", ui->comboBox_25->currentText() );

        QString port = ports[iface].portName;

        // is it a serial port in the range COM1 .. COM9?
        if ( port.startsWith( "COM" ) )
        {
            // use windows communication device name "\\.\COMn"
            port = "\\\\.\\" + port;
        }

        char parity;
        switch( ui->comboBox_23->currentIndex() )
        {
            case 1: parity = 'O'; break;
            case 2: parity = 'E'; break;
            default:
            case 0: parity = 'N'; break;
        }

        changeModbusInterface_5(port, parity);
        onRtuPortActive_5(true);
    }
    else emit connectionError( tr( "No serial port found" ) );
}


void
MainWindow::
changeSerialPort_6( int )
{
    const int iface = ui->comboBox_26->currentIndex();

    QList<QextPortInfo> ports = QextSerialEnumerator::getPorts();
    if( !ports.isEmpty() )
    {
        QSettings settings;
        settings.setValue( "serialinterface", ports[iface].friendName );
        settings.setValue( "serialbaudrate", ui->comboBox_27->currentText() );
        settings.setValue( "serialparity", ui->comboBox_28->currentText() );
        settings.setValue( "serialdatabits", ui->comboBox_29->currentText() );
        settings.setValue( "serialstopbits", ui->comboBox_30->currentText() );

        QString port = ports[iface].portName;

        // is it a serial port in the range COM1 .. COM9?
        if ( port.startsWith( "COM" ) )
        {
            // use windows communication device name "\\.\COMn"
            port = "\\\\.\\" + port;
        }

        char parity;
        switch( ui->comboBox_28->currentIndex() )
        {
            case 1: parity = 'O'; break;
            case 2: parity = 'E'; break;
            default:
            case 0: parity = 'N'; break;
        }

        changeModbusInterface_6(port, parity);
        onRtuPortActive_6(true);
    }
    else emit connectionError( tr( "No serial port found" ) );
}


void
MainWindow::
changeModbusInterface(const QString& port, char parity)
{
    releaseSerialModbus();

    m_serialModbus = modbus_new_rtu( port.toLatin1().constData(),
            ui->comboBox_2->currentText().toInt(),
            parity,
            ui->comboBox_3->currentText().toInt(),
            ui->comboBox_4->currentText().toInt() );

    if( modbus_connect( m_serialModbus ) == -1 )
    {
        emit connectionError( tr( "Could not connect serial port at Loop_1!" ) );

        releaseSerialModbus();
    }
    else
        updateTabIcon(0, true);
}

void
MainWindow::
changeModbusInterface_2(const QString& port, char parity)
{
    releaseSerialModbus_2();

    m_serialModbus_2 = modbus_new_rtu( port.toLatin1().constData(),
            ui->comboBox_7->currentText().toInt(),
            parity,
            ui->comboBox_8->currentText().toInt(),
            ui->comboBox_9->currentText().toInt() );

    if( modbus_connect( m_serialModbus_2 ) == -1 )
    {
        emit connectionError( tr( "Could not connect serial port at Loop_2!" ) );

        releaseSerialModbus_2();
    }
    else 
        updateTabIcon(1, true);
}

void
MainWindow::
changeModbusInterface_3(const QString& port, char parity)
{
    releaseSerialModbus_3();

    m_serialModbus_3 = modbus_new_rtu( port.toLatin1().constData(),
            ui->comboBox_12->currentText().toInt(),
            parity,
            ui->comboBox_13->currentText().toInt(),
            ui->comboBox_14->currentText().toInt() );

    if( modbus_connect( m_serialModbus_3 ) == -1 )
    {
        emit connectionError( tr( "Could not connect serial port at Loop_3!" ) );

        releaseSerialModbus_3();
    }
    else
        updateTabIcon(2, true);
}

void
MainWindow::
changeModbusInterface_4(const QString& port, char parity)
{
    releaseSerialModbus_4();

    m_serialModbus_4 = modbus_new_rtu( port.toLatin1().constData(),
            ui->comboBox_17->currentText().toInt(),
            parity,
            ui->comboBox_18->currentText().toInt(),
            ui->comboBox_19->currentText().toInt() );

    if( modbus_connect( m_serialModbus_4 ) == -1 )
    {
        emit connectionError( tr( "Could not connect serial port at Loop_4!" ) );

        releaseSerialModbus_4();
    }
    else
        updateTabIcon(3, true);
}


void
MainWindow::
changeModbusInterface_5(const QString& port, char parity)
{
    releaseSerialModbus_5();

    m_serialModbus_5 = modbus_new_rtu( port.toLatin1().constData(),
            ui->comboBox_22->currentText().toInt(),
            parity,
            ui->comboBox_23->currentText().toInt(),
            ui->comboBox_24->currentText().toInt() );

    if( modbus_connect( m_serialModbus_5 ) == -1 )
    {
        emit connectionError( tr( "Could not connect serial port at Loop_5!" ) );

        releaseSerialModbus_5();
    }
    else
        updateTabIcon(4, true);
}

void
MainWindow::
changeModbusInterface_6(const QString& port, char parity)
{
    releaseSerialModbus_6();

    m_serialModbus_6 = modbus_new_rtu( port.toLatin1().constData(),
            ui->comboBox_27->currentText().toInt(),
            parity,
            ui->comboBox_28->currentText().toInt(),
            ui->comboBox_29->currentText().toInt() );

    if( modbus_connect( m_serialModbus_6 ) == -1 )
    {
        emit connectionError( tr( "Could not connect serial port at Loop_6" ) );

        releaseSerialModbus_6();
    }
    else
        updateTabIcon(5, true);
}

void
MainWindow::
onCheckBoxChecked(bool checked)
{
    clearMonitors();

    if (ui->tabWidget_2->currentIndex() == 0)
    {
        if (checked) setupModbusPort();
        else releaseSerialModbus();

        ui->tabWidget_3->setEnabled(checked);
        onRtuPortActive(checked);
    }
    else if (ui->tabWidget_2->currentIndex() == 1)
    {
        if (checked) setupModbusPort_2();
        else releaseSerialModbus_2();

        ui->tabWidget_4->setEnabled(checked);
        onRtuPortActive_2(checked);
    }   
    else if (ui->tabWidget_2->currentIndex() == 2)
    {
        if (checked) setupModbusPort_3();
        else releaseSerialModbus_3();

        ui->tabWidget_5->setEnabled(checked);
        onRtuPortActive_3(checked);
    }   
    else if (ui->tabWidget_2->currentIndex() == 3)
    {
        if (checked) setupModbusPort_4();
        else releaseSerialModbus_4();

        ui->tabWidget_6->setEnabled(checked);
        onRtuPortActive_4(checked);
    }   
    else if (ui->tabWidget_2->currentIndex() == 4)
    {
        if (checked) setupModbusPort_5();
        else releaseSerialModbus_5();

        ui->tabWidget_7->setEnabled(checked);
        onRtuPortActive_5(checked);
    }
    else
    {
        if (checked) setupModbusPort_6();
        else releaseSerialModbus_6();

        ui->tabWidget_8->setEnabled(checked);
        onRtuPortActive_6(checked);
    }   
}


void
MainWindow::
clearMonitors()
{
    ui->rawData->clear();
    ui->regTable->setRowCount(0);
    ui->busMonTable->setRowCount(0);
}


void
MainWindow::
connectRegisters()
{
    connect(ui->radioButton_2, SIGNAL(pressed()), this,  SLOT(onProductBtnPressed()));
    connect(ui->radioButton_10, SIGNAL(pressed()), this,  SLOT(onProductBtnPressed()));
    connect(ui->radioButton_16, SIGNAL(pressed()), this,  SLOT(onProductBtnPressed()));
    connect(ui->radioButton_28, SIGNAL(pressed()), this,  SLOT(onProductBtnPressed()));
    connect(ui->radioButton_34, SIGNAL(pressed()), this,  SLOT(onProductBtnPressed()));
    connect(ui->radioButton_38, SIGNAL(pressed()), this,  SLOT(onProductBtnPressed()));
    connect(ui->radioButton_46, SIGNAL(pressed()), this,  SLOT(onProductBtnPressed()));
    connect(ui->radioButton_70, SIGNAL(pressed()), this,  SLOT(onProductBtnPressed()));
    connect(ui->radioButton_74, SIGNAL(pressed()), this,  SLOT(onProductBtnPressed()));
    connect(ui->radioButton_82, SIGNAL(pressed()), this,  SLOT(onProductBtnPressed()));
    connect(ui->radioButton_92, SIGNAL(pressed()), this,  SLOT(onProductBtnPressed()));
    connect(ui->radioButton_100, SIGNAL(pressed()), this,  SLOT(onProductBtnPressed()));
    connect(ui->radioButton_106, SIGNAL(pressed()), this,  SLOT(onProductBtnPressed()));
}


void
MainWindow::
onProductBtnPressed()
{
    (ui->radioButton_2->isChecked()) ? updateRegisters(RAZ,0) : updateRegisters(EEA,0);
    (ui->radioButton_10->isChecked()) ? updateRegisters(RAZ,1) : updateRegisters(EEA,1);
    (ui->radioButton_16->isChecked()) ? updateRegisters(RAZ,2) : updateRegisters(EEA,2);
    (ui->radioButton_20->isChecked()) ? updateRegisters(RAZ,3) : updateRegisters(EEA,3);
    (ui->radioButton_28->isChecked()) ? updateRegisters(RAZ,4) : updateRegisters(EEA,4);
    (ui->radioButton_34->isChecked()) ? updateRegisters(RAZ,5) : updateRegisters(EEA,5);
    (ui->radioButton_38->isChecked()) ? updateRegisters(RAZ,6) : updateRegisters(EEA,6);
    (ui->radioButton_46->isChecked()) ? updateRegisters(RAZ,7) : updateRegisters(EEA,7);
    (ui->radioButton_52->isChecked()) ? updateRegisters(RAZ,8) : updateRegisters(EEA,8);
    (ui->radioButton_56->isChecked()) ? updateRegisters(RAZ,9) : updateRegisters(EEA,8);
    (ui->radioButton_64->isChecked()) ? updateRegisters(RAZ,10) : updateRegisters(EEA,10);
    (ui->radioButton_70->isChecked()) ? updateRegisters(RAZ,11) : updateRegisters(EEA,11);
    (ui->radioButton_74->isChecked()) ? updateRegisters(RAZ,12) : updateRegisters(EEA,12);
    (ui->radioButton_82->isChecked()) ? updateRegisters(RAZ,13) : updateRegisters(EEA,13);
    (ui->radioButton_88->isChecked()) ? updateRegisters(RAZ,14) : updateRegisters(EEA,14);
    (ui->radioButton_92->isChecked()) ? updateRegisters(RAZ,15) : updateRegisters(EEA,15);
    (ui->radioButton_100->isChecked()) ? updateRegisters(RAZ,16) : updateRegisters(EEA,16);
    (ui->radioButton_106->isChecked()) ? updateRegisters(RAZ,17) : updateRegisters(EEA,17);
}


void
MainWindow::
onLoopTabChanged(int index)
{
    clearMonitors();    

    if (index == 0)
    {
        if (ui->tabWidget_3->currentIndex() == 0)
        {
            ui->slaveID->setValue((int)ui->lcdNumber_8->value());
            (ui->radioButton_2->isChecked()) ? updateRegisters(RAZ,0) : updateRegisters(EEA,0);
        }
        else if (ui->tabWidget_3->currentIndex() == 1)
        {
            ui->slaveID->setValue((int)ui->lcdNumber_9->value());
            (ui->radioButton_10->isChecked()) ? updateRegisters(RAZ,1) : updateRegisters(EEA,1);
        }
        else
        {
            ui->slaveID->setValue((int)ui->lcdNumber_10->value());
            (ui->radioButton_16->isChecked()) ? updateRegisters(RAZ,2) : updateRegisters(EEA,2);
        }
    } 
    else if (index == 1)
    {
        if (ui->tabWidget_4->currentIndex() == 0)
        {
            ui->slaveID->setValue((int)ui->lcdNumber_11->value());
            (ui->radioButton_20->isChecked()) ? updateRegisters(RAZ,3) : updateRegisters(EEA,3);
        }
        else if (ui->tabWidget_4->currentIndex() == 1)
        {
            ui->slaveID->setValue((int)ui->lcdNumber_12->value());
            (ui->radioButton_28->isChecked()) ? updateRegisters(RAZ,4) : updateRegisters(EEA,4);
        }
        else
        {
            ui->slaveID->setValue((int)ui->lcdNumber_13->value());
            (ui->radioButton_34->isChecked()) ? updateRegisters(RAZ,5) : updateRegisters(EEA,5);
        }
    }
    else if (index == 2)
    {
        if (ui->tabWidget_5->currentIndex() == 0)
        {
            ui->slaveID->setValue((int)ui->lcdNumber_14->value());
            (ui->radioButton_38->isChecked()) ? updateRegisters(RAZ,6) : updateRegisters(EEA,6);
        }
        else if (ui->tabWidget_5->currentIndex() == 1)
        {
            ui->slaveID->setValue((int)ui->lcdNumber_15->value());
            (ui->radioButton_46->isChecked()) ? updateRegisters(RAZ,7) : updateRegisters(EEA,7);
        }
        else
        {
            ui->slaveID->setValue((int)ui->lcdNumber_16->value());
            (ui->radioButton_52->isChecked()) ? updateRegisters(RAZ,8) : updateRegisters(EEA,8);
        }
    }
    else if (index == 3)
    {

        if (ui->tabWidget_6->currentIndex() == 0)
        {
            ui->slaveID->setValue((int)ui->lcdNumber_17->value());
            (ui->radioButton_56->isChecked()) ? updateRegisters(RAZ,9) : updateRegisters(EEA,9);
        }
        else if (ui->tabWidget_6->currentIndex() == 1)
        {
            ui->slaveID->setValue((int)ui->lcdNumber_18->value());
            (ui->radioButton_64->isChecked()) ? updateRegisters(RAZ,10) : updateRegisters(EEA,10);
        }
        else
        {
            ui->slaveID->setValue((int)ui->lcdNumber_19->value());
            (ui->radioButton_70->isChecked()) ? updateRegisters(RAZ,11) : updateRegisters(EEA,11);
        }
    }
    else if (index == 4)
    {

        if (ui->tabWidget_7->currentIndex() == 0)
        {
            ui->slaveID->setValue((int)ui->lcdNumber_20->value());
            (ui->radioButton_74->isChecked()) ? updateRegisters(RAZ,12) : updateRegisters(EEA,12);
        }
        else if (ui->tabWidget_7->currentIndex() == 1)
        {
            ui->slaveID->setValue((int)ui->lcdNumber_21->value());
            (ui->radioButton_82->isChecked()) ? updateRegisters(RAZ,13) : updateRegisters(EEA,13);
        }
        else
        {
            ui->slaveID->setValue((int)ui->lcdNumber_22->value());
            (ui->radioButton_88->isChecked()) ? updateRegisters(RAZ,14) : updateRegisters(EEA,14);
        }
    }
    else
    {

        if (ui->tabWidget_8->currentIndex() == 0)
        {
            ui->slaveID->setValue((int)ui->lcdNumber_23->value());
            (ui->radioButton_92->isChecked()) ? updateRegisters(RAZ,15) : updateRegisters(EEA,15);
        }
        else if (ui->tabWidget_8->currentIndex() == 1)
        {
            ui->slaveID->setValue((int)ui->lcdNumber_24->value());
            (ui->radioButton_100->isChecked()) ? updateRegisters(RAZ,16) : updateRegisters(EEA,16);
        }
        else
        {
            ui->slaveID->setValue((int)ui->lcdNumber_25->value());
            (ui->radioButton_106->isChecked()) ? updateRegisters(RAZ,17) : updateRegisters(EEA,17);
        }
    }

    updateGraph();
}


void
MainWindow::
updateTabIcon(int index, bool connected)
{
    QIcon icon(QLatin1String(":/green.ico"));
    QIcon icoff(QLatin1String(":/red.ico"));
    (connected) ? ui->tabWidget_2->setTabIcon(index,icon) : ui->tabWidget_2->setTabIcon(index,icoff);
}


void
MainWindow::
initializeTabIcons()
{
    QIcon icon(QLatin1String(":/red.ico"));
    for (int i = 0; i < 6; i++) ui->tabWidget_2->setTabIcon(i,icon);
}

void 
MainWindow::
initializeValues()
{
    ui->lcdNumber_8->display(1);
    ui->lcdNumber_9->display(12);
    ui->lcdNumber_10->display(13);
    ui->lcdNumber_11->display(1);
    ui->lcdNumber_12->display(22);
    ui->lcdNumber_13->display(23);
    ui->lcdNumber_14->display(31);
    ui->lcdNumber_15->display(32);
    ui->lcdNumber_16->display(33);
    ui->lcdNumber_17->display(41);
    ui->lcdNumber_18->display(42);
    ui->lcdNumber_19->display(43);
    ui->lcdNumber_20->display(51);
    ui->lcdNumber_21->display(52);
    ui->lcdNumber_22->display(53);
    ui->lcdNumber_23->display(61);
    ui->lcdNumber_24->display(62);
    ui->lcdNumber_25->display(63);

    ui->numCoils->setValue(2);
}

void
MainWindow::
updateChartTitle()
{

         if ((ui->tabWidget_2->currentIndex() == 0) && (ui->tabWidget_3->currentIndex() == 0)) chart->setTitle("Pipe 1 at Loop 1");
    else if ((ui->tabWidget_2->currentIndex() == 0) && (ui->tabWidget_3->currentIndex() == 1)) chart->setTitle("Pipe 2 at Loop 1");
    else if ((ui->tabWidget_2->currentIndex() == 0) && (ui->tabWidget_3->currentIndex() == 2)) chart->setTitle("Pipe 3 at Loop 1");
    else if ((ui->tabWidget_2->currentIndex() == 1) && (ui->tabWidget_4->currentIndex() == 0)) chart->setTitle("Pipe 1 at Loop 2");
    else if ((ui->tabWidget_2->currentIndex() == 1) && (ui->tabWidget_4->currentIndex() == 1)) chart->setTitle("Pipe 2 at Loop 2");
    else if ((ui->tabWidget_2->currentIndex() == 1) && (ui->tabWidget_4->currentIndex() == 2)) chart->setTitle("Pipe 3 at Loop 2");
    else if ((ui->tabWidget_2->currentIndex() == 2) && (ui->tabWidget_5->currentIndex() == 0)) chart->setTitle("Pipe 1 at Loop 3");
    else if ((ui->tabWidget_2->currentIndex() == 2) && (ui->tabWidget_5->currentIndex() == 1)) chart->setTitle("Pipe 2 at Loop 3");
    else if ((ui->tabWidget_2->currentIndex() == 2) && (ui->tabWidget_5->currentIndex() == 2)) chart->setTitle("Pipe 3 at Loop 3");
    else if ((ui->tabWidget_2->currentIndex() == 3) && (ui->tabWidget_6->currentIndex() == 0)) chart->setTitle("Pipe 1 at Loop 4");
    else if ((ui->tabWidget_2->currentIndex() == 3) && (ui->tabWidget_6->currentIndex() == 1)) chart->setTitle("Pipe 2 at Loop 4");
    else if ((ui->tabWidget_2->currentIndex() == 3) && (ui->tabWidget_6->currentIndex() == 2)) chart->setTitle("Pipe 3 at Loop 4");
    else if ((ui->tabWidget_2->currentIndex() == 4) && (ui->tabWidget_7->currentIndex() == 0)) chart->setTitle("Pipe 1 at Loop 5");
    else if ((ui->tabWidget_2->currentIndex() == 4) && (ui->tabWidget_7->currentIndex() == 1)) chart->setTitle("Pipe 2 at Loop 5");
    else if ((ui->tabWidget_2->currentIndex() == 4) && (ui->tabWidget_7->currentIndex() == 2)) chart->setTitle("Pipe 3 at Loop 5");
    else if ((ui->tabWidget_2->currentIndex() == 5) && (ui->tabWidget_8->currentIndex() == 0)) chart->setTitle("Pipe 1 at Loop 6");
    else if ((ui->tabWidget_2->currentIndex() == 5) && (ui->tabWidget_8->currentIndex() == 1)) chart->setTitle("Pipe 2 at Loop 6");
    else if ((ui->tabWidget_2->currentIndex() == 5) && (ui->tabWidget_8->currentIndex() == 2)) chart->setTitle("Pipe 3 at Loop 6");
}

void
MainWindow::
connectCalibrationControls()
{
    connect(ui->pushButton_4, SIGNAL(pressed()), this, SLOT(calibration_1_1()));
    connect(ui->pushButton_9, SIGNAL(pressed()), this, SLOT(calibration_1_2()));
    connect(ui->pushButton_57, SIGNAL(pressed()), this, SLOT(calibration_1_3()));
    connect(ui->pushButton_12, SIGNAL(pressed()), this, SLOT(calibration_2_1()));
    connect(ui->pushButton_15, SIGNAL(pressed()), this, SLOT(calibration_2_2()));
    connect(ui->pushButton_18, SIGNAL(pressed()), this, SLOT(calibration_2_3()));
    connect(ui->pushButton_21, SIGNAL(pressed()), this, SLOT(calibration_3_1()));
    connect(ui->pushButton_24, SIGNAL(pressed()), this, SLOT(calibration_3_2()));
    connect(ui->pushButton_27, SIGNAL(pressed()), this, SLOT(calibration_3_3()));
    connect(ui->pushButton_30, SIGNAL(pressed()), this, SLOT(calibration_4_1()));
    connect(ui->pushButton_33, SIGNAL(pressed()), this, SLOT(calibration_4_2()));
    connect(ui->pushButton_36, SIGNAL(pressed()), this, SLOT(calibration_4_3()));
    connect(ui->pushButton_39, SIGNAL(pressed()), this, SLOT(calibration_5_1()));
    connect(ui->pushButton_51, SIGNAL(pressed()), this, SLOT(calibration_5_2()));
    connect(ui->pushButton_54, SIGNAL(pressed()), this, SLOT(calibration_5_3()));
    connect(ui->pushButton_42, SIGNAL(pressed()), this, SLOT(calibration_6_1()));
    connect(ui->pushButton_45, SIGNAL(pressed()), this, SLOT(calibration_6_2()));
    connect(ui->pushButton_48, SIGNAL(pressed()), this, SLOT(calibration_6_3()));
}


void
MainWindow::
calibration_1_1()
{
    static bool isCalibration = false;
    ui->pushButton_4->setText(tr("P A U S E"));   
    setupCalibrationRequest();
}


void
MainWindow::
calibration_1_2()
{
    static bool isCalibration = false;
    ui->pushButton_9->setText(tr("P A U S E"));
    setupCalibrationRequest();
}

void
MainWindow::
calibration_1_3()
{
    static bool isCalibration = false;
    ui->pushButton_57->setText(tr("P A U S E"));
    setupCalibrationRequest();
}

void
MainWindow::
calibration_2_1()
{
    if (ui->radioButton_20->isChecked()) qDebug("Razor");
    else qDebug("EEA");

    static bool isCalibration = false;
    ui->pushButton_12->setText(tr("P A U S E"));
    setupCalibrationRequest();
}

void
MainWindow::
calibration_2_2()
{
    if (ui->radioButton_28->isChecked()) qDebug("Razor");
    else qDebug("EEA");
    static bool isCalibration = false;
    ui->pushButton_15->setText(tr("P A U S E"));
    setupCalibrationRequest();
}

void
MainWindow::
calibration_2_3()
{
    if (ui->radioButton_34->isChecked()) qDebug("Razor");
    else qDebug("EEA");
    static bool isCalibration = false;
    ui->pushButton_18->setText(tr("P A U S E"));
    setupCalibrationRequest();
}

void
MainWindow::
calibration_3_1()
{
    if (ui->radioButton_38->isChecked()) qDebug("Razor");
    else qDebug("EEA");
    static bool isCalibration = false;
    ui->pushButton_21->setText(tr("P A U S E"));
    setupCalibrationRequest();
}

void
MainWindow::
calibration_3_2()
{
    if (ui->radioButton_46->isChecked()) qDebug("Razor");
    else qDebug("EEA");
    static bool isCalibration = false;
    ui->pushButton_24->setText(tr("P A U S E"));
    setupCalibrationRequest();
}

void
MainWindow::
calibration_3_3()
{
    if (ui->radioButton_52->isChecked()) qDebug("Razor");
    else qDebug("EEA");
    static bool isCalibration = false;
    ui->pushButton_27->setText(tr("P A U S E"));
    setupCalibrationRequest();
}

void
MainWindow::
calibration_4_1()
{
    if (ui->radioButton_56->isChecked()) qDebug("Razor");
    else qDebug("EEA");
    static bool isCalibration = false;
    ui->pushButton_30->setText(tr("P A U S E"));
    setupCalibrationRequest();
}

void
MainWindow::
calibration_4_2()
{
    if (ui->radioButton_64->isChecked()) qDebug("Razor");
    else qDebug("EEA");
    static bool isCalibration = false;
    ui->pushButton_33->setText(tr("P A U S E"));
    setupCalibrationRequest();
}

void
MainWindow::
calibration_4_3()
{
    if (ui->radioButton_70->isChecked()) qDebug("Razor");
    else qDebug("EEA");
    static bool isCalibration = false;
    ui->pushButton_36->setText(tr("P A U S E"));
    setupCalibrationRequest();
}

void
MainWindow::
calibration_5_1()
{
    if (ui->radioButton_74->isChecked()) qDebug("Razor");
    else qDebug("EEA");
    static bool isCalibration = false;
    ui->pushButton_39->setText(tr("P A U S E"));
    setupCalibrationRequest();
}

void
MainWindow::
calibration_5_2()
{
    if (ui->radioButton_82->isChecked()) qDebug("Razor");
    else qDebug("EEA");
    static bool isCalibration = false;
    ui->pushButton_51->setText(tr("P A U S E"));
    setupCalibrationRequest();
}

void
MainWindow::
calibration_5_3()
{
    if (ui->radioButton_88->isChecked()) qDebug("Razor");
    else qDebug("EEA");
    static bool isCalibration = false;
    ui->pushButton_54->setText(tr("P A U S E"));
    setupCalibrationRequest();
}

void
MainWindow::
calibration_6_1()
{
    if (ui->radioButton_92->isChecked()) qDebug("Razor");
    else qDebug("EEA");
    static bool isCalibration = false;
    ui->pushButton_42->setText(tr("P A U S E"));
    setupCalibrationRequest();
}

void
MainWindow::
calibration_6_2()
{
    if (ui->radioButton_100->isChecked()) qDebug("Razor");
    else qDebug("EEA");
    static bool isCalibration = false;
    ui->pushButton_45->setText(tr("P A U S E"));
    setupCalibrationRequest();
}

void
MainWindow::
calibration_6_3()
{
    if (ui->radioButton_106->isChecked()) qDebug("Razor");
    else qDebug("EEA");
    static bool isCalibration = false;
    ui->pushButton_48->setText(tr("P A U S E"));
    setupCalibrationRequest();
}


void
MainWindow::
onFunctionCodeChanges()
{
    if (ui->radioButton_181->isChecked()) // float
    {
        if (ui->radioButton_187->isChecked()) // read
        {
            ui->functionCode->setCurrentIndex(3);
        }
        else
        {
            ui->functionCode->setCurrentIndex(7);
        }
    }
    else if (ui->radioButton_182->isChecked()) // integer
    {
        if (ui->radioButton_187->isChecked()) // read
        {
            ui->functionCode->setCurrentIndex(3);
        }
        else
        {
            ui->functionCode->setCurrentIndex(5);
        }
    }
    else // coil
    {
        if (ui->radioButton_187->isChecked()) // read
        {
            ui->functionCode->setCurrentIndex(0);
        }
        else
        {
            ui->functionCode->setCurrentIndex(4);
        }
    }
}


void
MainWindow::
onFloatButtonPressed(bool enabled)
{
    if (enabled) ui->numCoils->setValue(2); // quantity
    if (ui->radioButton_187->isChecked())
    {
        onReadButtonPressed(true);
    }
    else
    {
        onWriteButtonPressed(true);
    }

    ui->groupBox_103->setEnabled(TRUE);
    ui->groupBox_106->setEnabled(FALSE);
    ui->groupBox_107->setEnabled(FALSE);
    ui->lineEdit_111->clear();
}

void
MainWindow::
onIntegerButtonPressed(bool enabled)
{
    if (enabled) ui->numCoils->setValue(1);
    if (ui->radioButton_187->isChecked())
    {
        onReadButtonPressed(true);
    }
    else
    {
        onWriteButtonPressed(true);
    }
    ui->groupBox_103->setEnabled(FALSE);
    ui->groupBox_106->setEnabled(TRUE);
    ui->groupBox_107->setEnabled(FALSE);
    ui->lineEdit_109->clear();
 }

void
MainWindow::
onCoilButtonPressed(bool enabled)
{
    if (enabled) ui->numCoils->setValue(1);
    if (ui->radioButton_187->isChecked())
    {
        onReadButtonPressed(true);
    }
    else
    {
        onWriteButtonPressed(true);
    }

    ui->groupBox_103->setEnabled(FALSE);
    ui->groupBox_106->setEnabled(FALSE);
    ui->groupBox_107->setEnabled(TRUE);
    ui->lineEdit_109->clear();
    ui->lineEdit_111->clear();
}

void
MainWindow::
onReadButtonPressed(bool enabled)
{
    if (enabled)
    {
        ui->lineEdit_109->setReadOnly(true);
        ui->lineEdit_111->setReadOnly(true);
        ui->groupBox_107->setEnabled(true);
    }

    onFunctionCodeChanges();
}

void
MainWindow::
onWriteButtonPressed(bool enabled)
{
    if (enabled)
    {
        if (ui->radioButton_181->isChecked())
            ui->lineEdit_109->setReadOnly(false);
        else if (ui->radioButton_182->isChecked())
            ui->lineEdit_111->setReadOnly(false);
        else
            ui->groupBox_107->setEnabled(true);
    }

    onFunctionCodeChanges();
}

float 
MainWindow::
toFloat(QByteArray f)
{
    bool ok;
    int sign = 1;

    f = f.toHex(); // Convert to Hex

    qDebug() << "QByteArrayToFloat: QByteArray hex = " << f;

    f = QByteArray::number(f.toLongLong(&ok, 16), 2);    // Convert hex to binary

    if(f.length() == 32) {
        if(f.at(0) == '1') sign =-1;     // If bit 0 is 1 number is negative
        f.remove(0,1);                   // Remove sign bit
    }

    QByteArray fraction = f.right(23);  // Get the fractional part
    double mantissa = 0;
    for(int i = 0; i < fraction.length(); i++){  // Iterate through the array to claculate the fraction as a decimal.
        if(fraction.at(i) == '1')
            mantissa += 1.0 / (pow(2, i+1));
    }

    int exponent = f.left(f.length() - 23).toLongLong(&ok, 2) - 127;     // Calculate the exponent

    qDebug() << "QByteArrayToFloat: float number = "<< QString::number(sign * pow(2, exponent) * (mantissa + 1.0),'f', 5);

    return (sign * pow(2, exponent) * (mantissa + 1.0));
}

void
MainWindow::
updateRegisters(const bool isRazor, const int i)
{
    if (isRazor)
    {
        REG_SN_PIPE[i] = 1;
        REG_WATERCUT[i] = 3;
        REG_TEMPERATURE[i] = 5;
        REG_EMULSTION_PHASE[i] = 7;
        REG_SALINITY[i] = 9;
        REG_HARDWARE_VERSION[i] = 11;
        REG_FIRMWARE_VERSION[i] = 13;
        REG_OIL_ADJUST[i] = 15;
        REG_WATER_ADJUST[i] = 17;
        REG_FREQ[i] = 19;
        REG_FREQ_AVG[i] = 21;
        REG_WATERCUT_AVG[i] = 23;
        REG_WATERCUT_RAW[i] = 25;
        REG_ANALYZER_MODE[i] = 27;
        REG_TEMP_AVG[i] = 29;
        REG_TEMP_ADJUST[i] = 31;
        REG_TEMP_USER[i] = 33;
        REG_PROC_AVGING[i] = 35;
        REG_OIL_INDEX[i] = 37;
        REG_OIL_P0[i] = 39;
        REG_OIL_P1[i] = 41;
        REG_OIL_FREQ_LOW[i] = 43 ;
        REG_OIL_FREQ_HIGH[i] = 45;
        REG_SAMPLE_PERIOD[i] = 47;
        REG_AO_LRV[i] = 49;
        REG_AO_URV[i] = 51;
        REG_AO_DAMPEN[i] = 53;
        REG_BAUD_RATE[i] = 55;
        REG_SLAVE_ADDRESS[i] = 57;
        REG_STOP_BITS[i] = 59;
        REG_OIL_RP[i] = 61;
        REG_WATER_RP[i] = 63;
        REG_DENSITY_MODE[i] = 65;
        REG_OIL_CALC_MAX[i] = 67;
        REG_OIL_PHASE_CUTOFF[i] = 69;
        REG_TEMP_OIL_NUM_CURVES[i] = 71;
        REG_STREAM[i] = 73;
        REG_OIL_RP_AVG[i] = 75;
        REG_PLACE_HOLDER[i] = 77;
        REG_OIL_SAMPLE[i] = 79;
        REG_RTC_SEC[i] = 81;
        REG_RTC_MIN[i] = 83;
        REG_RTC_HR[i] = 85;
        REG_RTC_DAY[i] = 87;
        REG_RTC_MON[i] = 89;
        REG_RTC_YR[i] = 91;
        REG_RTC_SEC_IN[i] = 93;
        REG_RTC_MIN_IN[i] = 95;
        REG_RTC_HR_IN[i] = 97;
        REG_RTC_DAY_IN[i] = 99;
        REG_RTC_MON_IN[i] = 101;
        REG_RTC_YR_IN[i] = 103;
        REG_AO_MANUAL_VAL[i] = 105;
        REG_AO_TRIMLO[i] = 107;
        REG_AO_TRIMHI[i] = 109;
        REG_DENSITY_ADJ[i] = 111;
        REG_DENSITY_UNIT[i] = 113;
        REG_WC_ADJ_DENS[i] = 115;
        REG_DENSITY_D3[i] = 117;
        REG_DENSITY_D2[i] = 119;
        REG_DENSITY_D1[i] = 121;
        REG_DENSITY_D0[i] = 123;
        REG_DENSITY_CAL_VAL[i] = 125;
        REG_MODEL_CODE_0[i] = 127;
        REG_MODEL_CODE_1[i] = 129;
        REG_MODEL_CODE_2[i] = 131;
        REG_MODEL_CODE_3[i] = 133;
        REG_LOGGING_PERIOD[i] = 135;
        REG_PASSWORD[i] = 137;
        REG_STATISTICS[i] = 139;
        REG_ACTIVE_ERROR[i] = 141;
        REG_AO_ALARM_MODE[i] = 143;
        REG_AO_OUTPUT[i] = 145;
        REG_PHASE_HOLD_CYCLES[i] = 147;
        REG_RELAY_DELAY[i] = 149;
        REG_RELAY_SETPOINT[i] = 151;
        REG_AO_MODE[i] = 153;
        REG_OIL_DENSITY[i] = 155;
        REG_OIL_DENSITY_MODBUS[i] = 157;
        REG_OIL_DENSITY_AI[i] = 159;
        REG_OIL_DENSITY_MANUAL[i] = 161;
        REG_OIL_DENSITY_AI_LRV[i] = 163;
        REG_OIL_DENSITY_AI_URV[i] = 165;
        REG_OIL_DENS_CORR_MODE[i] = 167;
        REG_AI_TRIMLO[i] = 169;
        REG_AI_TRIMHI[i] = 171;
        REG_AI_MEASURE[i] = 173;
        REG_AI_TRIMMED[i] = 175;
    }
    else
    {
        REG_SN_PIPE[i] = 1;
        REG_WATERCUT[i] = 3;
        REG_TEMPERATURE[i] = 5;
        REG_EMULSTION_PHASE[i] = 7;
        REG_SALINITY[i] = 9;
        REG_HARDWARE_VERSION[i] = 11;
        REG_FIRMWARE_VERSION[i] = 13;
        REG_OIL_ADJUST[i] = 15;
        REG_WATER_ADJUST[i] = 17;
        REG_FREQ[i] = 19;
        REG_FREQ_AVG[i] = 21;
        REG_WATERCUT_AVG[i] = 23;
        REG_WATERCUT_RAW[i] = 25;
        REG_ANALYZER_MODE[i] = 27;
        REG_TEMP_AVG[i] = 29;
        REG_TEMP_ADJUST[i] = 31;
        REG_TEMP_USER[i] = 33;
        REG_PROC_AVGING[i] = 35;
        REG_OIL_INDEX[i] = 37;
        REG_OIL_P0[i] = 39;
        REG_OIL_P1[i] = 41;
        REG_OIL_FREQ_LOW[i] = 43 ;
        REG_OIL_FREQ_HIGH[i] = 45;
        REG_SAMPLE_PERIOD[i] = 47;
        REG_AO_LRV[i] = 49;
        REG_AO_URV[i] = 51;
        REG_AO_DAMPEN[i] = 53;
        REG_BAUD_RATE[i] = 55;
        REG_SLAVE_ADDRESS[i] = 57;
        REG_STOP_BITS[i] = 59;
        REG_OIL_RP[i] = 61;
        REG_WATER_RP[i] = 63;
        REG_DENSITY_MODE[i] = 65;
        REG_OIL_CALC_MAX[i] = 67;
        REG_OIL_PHASE_CUTOFF[i] = 69;
        REG_TEMP_OIL_NUM_CURVES[i] = 71;
        REG_STREAM[i] = 73;
        REG_OIL_RP_AVG[i] = 75;
        REG_PLACE_HOLDER[i] = 77;
        REG_OIL_SAMPLE[i] = 79;
        REG_RTC_SEC[i] = 81;
        REG_RTC_MIN[i] = 83;
        REG_RTC_HR[i] = 85;
        REG_RTC_DAY[i] = 87;
        REG_RTC_MON[i] = 89;
        REG_RTC_YR[i] = 91;
        REG_RTC_SEC_IN[i] = 93;
        REG_RTC_MIN_IN[i] = 95;
        REG_RTC_HR_IN[i] = 97;
        REG_RTC_DAY_IN[i] = 99;
        REG_RTC_MON_IN[i] = 101;
        REG_RTC_YR_IN[i] = 103;
        REG_AO_MANUAL_VAL[i] = 105;
        REG_AO_TRIMLO[i] = 107;
        REG_AO_TRIMHI[i] = 109;
        REG_DENSITY_ADJ[i] = 111;
        REG_DENSITY_UNIT[i] = 113;
        REG_WC_ADJ_DENS[i] = 115;
        REG_DENSITY_D3[i] = 117;
        REG_DENSITY_D2[i] = 119;
        REG_DENSITY_D1[i] = 121;
        REG_DENSITY_D0[i] = 123;
        REG_DENSITY_CAL_VAL[i] = 125;
        REG_MODEL_CODE_0[i] = 127;
        REG_MODEL_CODE_1[i] = 129;
        REG_MODEL_CODE_2[i] = 131;
        REG_MODEL_CODE_3[i] = 133;
        REG_LOGGING_PERIOD[i] = 135;
        REG_PASSWORD[i] = 137;
        REG_STATISTICS[i] = 139;
        REG_ACTIVE_ERROR[i] = 141;
        REG_AO_ALARM_MODE[i] = 143;
        REG_AO_OUTPUT[i] = 145;
        REG_PHASE_HOLD_CYCLES[i] = 147;
        REG_RELAY_DELAY[i] = 149;
        REG_RELAY_SETPOINT[i] = 151;
        REG_AO_MODE[i] = 153;
        REG_OIL_DENSITY[i] = 155;
        REG_OIL_DENSITY_MODBUS[i] = 157;
        REG_OIL_DENSITY_AI[i] = 159;
        REG_OIL_DENSITY_MANUAL[i] = 161;
        REG_OIL_DENSITY_AI_LRV[i] = 163;
        REG_OIL_DENSITY_AI_URV[i] = 165;
        REG_OIL_DENS_CORR_MODE[i] = 167;
        REG_AI_TRIMLO[i] = 169;
        REG_AI_TRIMHI[i] = 171;
        REG_AI_MEASURE[i] = 173;
        REG_AI_TRIMMED[i] = 175;
    }
}
