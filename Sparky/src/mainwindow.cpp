#include <QtConcurrent>
#include <QSettings>
#include <QDebug>
#include <QMessageBox>
#include <QFile>
#include <QScrollBar>
#include <QTime>
#include <QGroupBox>
#include <QFileDialog>
#include <QThread>
#include <errno.h>
#include <QSignalMapper>
#include <QListWidget>
#include <QJsonDocument>
#include <QJsonObject>
#include <QInputDialog>
#include <QProgressDialog>
#include "mainwindow.h"
#include "modbus.h"
#include "modbus-private.h"
#include "modbus-rtu.h"
#include "ui_mainwindow.h"
#include "qextserialenumerator.h"

QT_CHARTS_USE_NAMESPACE
#define MAX_PHASE_CHECKING		5

const int DataTypeColumn = 0;
const int AddrColumn = 1;
const int DataColumn = 2;

extern MainWindow * globalMainWin;

MainWindow::MainWindow( QWidget * _parent ) :
	QMainWindow( _parent ),
	ui( new Ui::MainWindowClass ),
    m_modbus_snipping( NULL ),
	m_poll(false),
	isModbusTransmissionFailed(false)
{
	ui->setupUi(this);

    /// versioning
    setWindowTitle(SPARKY);

    readJsonConfigFile();
    onUpdateRegisters(EEA); 
    initializeToolbarIcons();
    initializeTabIcons();
    updateRegisterView();
    updateRequestPreview();
    enableHexView();
    setupModbusPorts();
    initializePipeObjects();
    initializeLoopObjects();
    initializeGraph();
    initializeModbusMonitor();
	setValidators();

	/// hide main configuration panel at start
	ui->groupBox_18->hide();
	ui->groupBox_35->hide();
	ui->groupBox_34->hide();
	ui->groupBox_33->hide();
	ui->groupBox_32->hide();
	ui->groupBox_31->hide();
	ui->groupBox_30->hide();
	ui->groupBox_29->hide();
	onActionStop();

    ui->regTable->setColumnWidth( 0, 150 );
    m_statusInd = new QWidget;
    m_statusInd->setFixedSize( 16, 16 );
    m_statusText = new QLabel;
    ui->statusBar->addWidget( m_statusInd );
    ui->statusBar->addWidget( m_statusText, 10 );
    resetStatus();

    serialNumberValidator = new QIntValidator(0, 999999, this);

    /// connections 
    connectCheckbox();
    connectModeChanged();
    connectProductBtnPressed();
    connectRadioButtons();
    connectSerialPort();
    connectActions();
    connectModbusMonitor();
    connectTimers();
    connectProfiler();
    connectToolbar();
    connectLineView();
	connectMasterPipe();

    /// clear connection at start
    updateLoopTabIcon(false);

    /// reset stability progressbar
    updatePipeStability(T_BAR,ALL,0);
}


MainWindow::~MainWindow()
{
	delete ui;
    delete m_statusInd;
    delete m_statusText;
    delete serialNumberValidator;
}


void
MainWindow::
setValidators()
{
	ui->lineEdit->setValidator( new QDoubleValidator(0, 1000000, 2, this) ); // loopVolume
	ui->lineEdit_37->setValidator( new QDoubleValidator(0, 1000000, 2, this) ); // waterRunStart
	ui->lineEdit_38->setValidator( new QDoubleValidator(0, 1000000, 2, this) ); // waterRunStop
	ui->lineEdit_39->setValidator( new QDoubleValidator(0, 1000000, 2, this) ); // oilRunStart
	ui->lineEdit_40->setValidator( new QDoubleValidator(0, 1000000, 2, this) ); // oilRunStop
}


void
MainWindow::
delay(int sec = 2)
{
    QTime dieTime= QTime::currentTime().addSecs(sec);
    while (QTime::currentTime() < dieTime)
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
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

    ui->toolBar->addAction(ui->actionOpen);
    ui->toolBar->addAction(ui->actionSave);

    ui->toolBar->addSeparator();
    ui->toolBar->addAction(ui->actionSettings);

    ui->toolBar->addSeparator();
    ui->toolBar->addAction(ui->actionStart);
    ui->toolBar->addAction(ui->actionStop);
	ui->actionStop->setVisible(false);
}

void
MainWindow::
keyPressEvent(QKeyEvent* event)
{
	if( event->key() == Qt::Key_Control )
	{
		//set flag to request polling
        if( LOOP.modbus != NULL )	m_poll = true;
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
                    uint16_t slave,
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
void MainWindow::stBusMonitorAddItem( modbus_t * modbus, uint8_t isRequest, uint16_t slave, uint8_t func, uint16_t addr, uint16_t nb, uint16_t expectedCRC, uint16_t actualCRC )
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
    if (ui->tabWidget_2->currentIndex() == 0)      m_modbus_snipping = LOOP.modbus;

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
		isModbusTransmissionFailed = false;

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
				err += tr( "Slave threw a exception '" );
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

		isModbusTransmissionFailed = true;
	}
}


void MainWindow::resetStatus( void )
{
	m_statusText->setText( tr( "Ready" ) );
	m_statusInd->setStyleSheet( "background: #aaa;" );
}

void MainWindow::pollForDataOnBus( void )
{
	if( LOOP.modbus )
	{
		modbus_poll( LOOP.modbus );
	}
}

void MainWindow::aboutQModBus( void )
{
	AboutDialog( this ).exec();
}

void MainWindow::onRtuPortActive(bool active)
{
	if (active) {
        LOOP.modbus = this->modbus();

        //LOOP[loop].modbus = this->modbus();
		if (LOOP.modbus) {
			modbus_register_monitor_add_item_fnc(LOOP.modbus, MainWindow::stBusMonitorAddItem);
			modbus_register_monitor_raw_data_fnc(LOOP.modbus, MainWindow::stBusMonitorRawData);
		}
	}
	else LOOP.modbus = NULL;
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
initializePipeObjects()
{
	/// label
    PIPE[0].pipeId = "P1";
    PIPE[1].pipeId = "P2";
    PIPE[2].pipeId = "P3";

	/// slave
	PIPE[0].slave = ui->lineEdit_2;
	PIPE[1].slave = ui->lineEdit_7;
	PIPE[2].slave = ui->lineEdit_13;

    /// on/off graph line view
    PIPE[0].lineView = ui->checkBox_19;
    PIPE[1].lineView = ui->checkBox_20;
    PIPE[2].lineView = ui->checkBox_21;

    /// on/off pipe switch
    PIPE[0].checkBox = ui->checkBox;
    PIPE[1].checkBox = ui->checkBox_2;
    PIPE[2].checkBox = ui->checkBox_3;

    /// lcdWatercut
    PIPE[0].watercut = ui->lineEdit_3;
    PIPE[1].watercut = ui->lineEdit_8;
    PIPE[2].watercut = ui->lineEdit_14;

    /// lcdStartFreq
    PIPE[0].startFreq = ui->lineEdit_4;
    PIPE[1].startFreq = ui->lineEdit_9;
    PIPE[2].startFreq = ui->lineEdit_15;

    /// lcdFreq 
    PIPE[0].freq = ui->lineEdit_5;
    PIPE[1].freq = ui->lineEdit_10;
    PIPE[2].freq = ui->lineEdit_16;

    /// lcdTemp 
    PIPE[0].temp = ui->lineEdit_6;
    PIPE[1].temp = ui->lineEdit_11;
    PIPE[2].temp = ui->lineEdit_17;

    /// lcd 
    PIPE[0].reflectedPower = ui->lineEdit_12;
    PIPE[1].reflectedPower = ui->lineEdit_19;
    PIPE[2].reflectedPower = ui->lineEdit_18;

	/// stability progressbar
	PIPE[0].freqProgress = ui->progressBar;
    PIPE[0].tempProgress = ui->progressBar_2;
    PIPE[1].freqProgress = ui->progressBar_4;
    PIPE[1].tempProgress = ui->progressBar_3;
    PIPE[2].freqProgress = ui->progressBar_6;
    PIPE[2].tempProgress = ui->progressBar_5;
}


void
MainWindow::
initializeLoopObjects()
{
	/// setup chart
    LOOP.chart->legend()->hide();
    LOOP.chartView->setChart(LOOP.chart);

    /// axisX
    LOOP.axisX->setRange(0,1000);
    LOOP.axisX->setTickCount(11);
    LOOP.axisX->setTickInterval(100);
    LOOP.axisX->setLabelFormat("%i");
    LOOP.axisX->setTitleText("Frequency (Mhz)");

    /// axisY
    LOOP.axisY->setRange(0,100);
    LOOP.axisY->setTickCount(11);
    LOOP.axisY->setLabelFormat("%i");
    LOOP.axisY->setTitleText("Watercut (%)");

    /// axisY3
    LOOP.axisY3->setRange(0,2.5);
    LOOP.axisY3->setTickCount(11);
    LOOP.axisY3->setLabelFormat("%.1f");
    LOOP.axisY3->setTitleText("Reflected Power (V)");

    /// add axisX
    LOOP.chart->addAxis(LOOP.axisX, Qt::AlignBottom);

    /// add axisY
    LOOP.chart->addAxis(LOOP.axisY, Qt::AlignLeft);
    
    /// add axisY3
    LOOP.chart->addAxis(LOOP.axisY3, Qt::AlignRight);

    /// linePenColor
    LOOP.axisY->setLinePenColor(PIPE[0].series->pen().color());
    LOOP.axisY->setLinePenColor(PIPE[1].series->pen().color());
    LOOP.axisY->setLinePenColor(PIPE[2].series->pen().color());

    /// setLabelColor
    LOOP.axisY->setLabelsColor(PIPE[0].series->pen().color());
    LOOP.axisY->setLabelsColor(PIPE[1].series->pen().color());
    LOOP.axisY->setLabelsColor(PIPE[2].series->pen().color());

    /// render hint 
    LOOP.chartView->setRenderHint(QPainter::Antialiasing);

    /// loop volume
    LOOP.loopVolume = ui->lineEdit;

    /// saltStart 
    LOOP.saltStart = ui->comboBox_31;

    /// saltStop
    LOOP.saltStop = ui->comboBox_33;

    /// oilTemp
    LOOP.oilTemp = ui->comboBox_32;

    /// waterRunStart 
    LOOP.waterRunStart = ui->lineEdit_37;

    /// waterRunStop 
    LOOP.waterRunStop = ui->lineEdit_38;

    /// oilRunStart 
    LOOP.oilRunStart = ui->lineEdit_39;

    /// oilRunStop 
    LOOP.oilRunStop = ui->lineEdit_40;
}


void
MainWindow::
initializeGraph()
{
    /// draw chart lines
    updateChart(ui->gridLayout_5,LOOP.chartView,LOOP.chart,PIPE[0].series,90,5,280,48,300,68,400,89);
    updateChart(ui->gridLayout_5,LOOP.chartView,LOOP.chart,PIPE[1].series,100,5,300,48,400,68,600,89);
    updateChart(ui->gridLayout_5,LOOP.chartView,LOOP.chart,PIPE[2].series,110,5,400,48,600,68,800,89);
}


void
MainWindow::
updateLineView()
{
    for (int pipe = 0; pipe < 3; pipe++) (PIPE[pipe].lineView) ? PIPE[pipe].series->show() : PIPE[pipe].series->hide();
}

void
MainWindow::
updateChart(QGridLayout * layout, QChartView * chartView, QChart * chart, QSplineSeries * series, double x1, double y1, double x2, double y2, double x3, double y3, double x4, double y4)
{
    *series << QPointF(x1, y1) << QPointF(x2, y2) << QPointF(x3, y3) << QPointF(x4, y4);
    chart->addSeries(series);
    layout->addWidget(chartView,0,0);
    updateLineView();
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
connectRadioButtons()
{
    connect(ui->radioButton_187, SIGNAL(toggled(bool)), this, SLOT(onReadButtonPressed(bool)));
    connect(ui->radioButton_186, SIGNAL(toggled(bool)), this, SLOT(onWriteButtonPressed(bool)));
}


void
MainWindow::
connectSerialPort()
{
    connect( ui->groupBox_18, SIGNAL(toggled(bool)), this, SLOT(onCheckBoxChecked(bool)));
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
    connect( ui->groupBox_105, SIGNAL( toggled(bool)), this, SLOT( onEquationTableChecked(bool)));
}


void
MainWindow::
connectToolbar()
{
    connect(ui->actionSave, SIGNAL(triggered()),this,SLOT(saveCsvFile()));
    connect(ui->actionOpen, SIGNAL(triggered()),this,SLOT(loadCsvFile()));
    connect(ui->actionSettings, SIGNAL(triggered()),this,SLOT(onActionSettings()));
    connect(ui->actionStart, SIGNAL(triggered()),this,SLOT(onActionStart()));
    connect(ui->actionStop, SIGNAL(triggered()),this,SLOT(onActionStop()));

    /// injection pump rates
    connect(ui->actionInjection_Pump_Rates, SIGNAL(triggered()),this,SLOT(injectionPumpRates()));

    /// injection bucket
    connect(ui->actionInjection_Bucket, SIGNAL(triggered()),this,SLOT(injectionBucket()));

    /// injection mark
    connect(ui->actionInjection_Mark, SIGNAL(triggered()),this, SLOT(injectionMark()));

    /// injection method
    connect(ui->actionInjection_Method, SIGNAL(triggered()),this, SLOT(injectionMethod()));

    /// pressure sensor slope
    connect(ui->actionPressure_Sensor_Slope, SIGNAL(triggered()),this, SLOT(onActionPressureSensorSlope()));

    /// Minimum reference temperature 
    connect(ui->actionMin_Ref_Temp, SIGNAL(triggered()), this, SLOT(onMinRefTemp()));

	/// Maximum reference temperature 
    connect(ui->actionMax_Ref_Temp, SIGNAL(triggered()),this, SLOT(onMaxRefTemp()));

	/// injection temperature 
    connect(ui->actionInjection_Temp, SIGNAL(triggered()),this, SLOT(onInjectionTemp()));

    /// Delta Stability X seconds 
    connect(ui->actionX_seconds, SIGNAL(triggered()),this, SLOT(onXDelay()));

    /// Delta Stability Frequency 
    connect(ui->actionY_KHz, SIGNAL(triggered()),this, SLOT(onYFreq()));

    /// Delta Stability Temperature
    connect(ui->actionZ_C, SIGNAL(triggered()),this, SLOT(onZTemp()));

	/// Interval - calibration 
    connect(ui->actionCalibration, SIGNAL(triggered()),this, SLOT(onIntervalSmallPump()));

	/// Interval - calibration 
    connect(ui->actionRollover, SIGNAL(triggered()),this, SLOT(onIntervalBigPump()));

	/// Loop Number 
    connect(ui->actionLOOP_NUMBER, SIGNAL(triggered()),this, SLOT(onLoopNumber()));

	/// master pipe
	connect(ui->actionMin_Watercut, SIGNAL(triggered()),this, SLOT(onActionMinMaster()));
	connect(ui->actionMax_Watercut, SIGNAL(triggered()),this, SLOT(onActionMaxMaster()));
	connect(ui->actionDelta_Master, SIGNAL(triggered()),this, SLOT(onActionDeltaMaster()));
	connect(ui->actionFinal_Delta_Watercut, SIGNAL(triggered()),this, SLOT(onActionDeltaMasterFinal()));

	/// max injection time
	connect(ui->actionWater, SIGNAL(triggered()),this, SLOT(onActionWater()));
	connect(ui->actionOil, SIGNAL(triggered()),this, SLOT(onActionOil()));

    /// file folders
    connect(ui->actionMain_Server, SIGNAL(triggered()),this,SLOT(onActionMainServer()));
    connect(ui->actionLocal_Server, SIGNAL(triggered()),this,SLOT(onActionLocalServer()));
}


void
MainWindow::
connectCheckbox()
{
    connect(ui->checkBox, SIGNAL(clicked(bool)),this, SLOT(onCheckBoxClicked(bool)));
    connect(ui->checkBox_2, SIGNAL(clicked(bool)),this, SLOT(onCheckBoxClicked(bool)));
    connect(ui->checkBox_3, SIGNAL(clicked(bool)),this, SLOT(onCheckBoxClicked(bool)));
}


void
MainWindow::
onCheckBoxClicked(const bool isChecked)
{
	(ui->checkBox->isChecked()) ?  PIPE[0].status = ENABLED : PIPE[0].status = DONE;
	(ui->checkBox_2->isChecked()) ?  PIPE[1].status = ENABLED : PIPE[1].status = DONE;
	(ui->checkBox_3->isChecked()) ?  PIPE[2].status = ENABLED : PIPE[2].status = DONE;
}


/// hide/show graph line
void
MainWindow::
toggleLineView_P1(bool b)
{
   (b) ? PIPE[0].series->show() : PIPE[0].series->hide();
}

void
MainWindow::
toggleLineView_P2(bool b)
{
   (b) ? PIPE[1].series->show() : PIPE[1].series->hide();
}

void
MainWindow::
toggleLineView_P3(bool b)
{
   (b) ? PIPE[2].series->show() : PIPE[2].series->hide();
}

void
MainWindow::
connectLineView()
{
    connect(ui->checkBox_19, SIGNAL(clicked(bool)), this, SLOT(toggleLineView_P1(bool)));
    connect(ui->checkBox_20, SIGNAL(clicked(bool)), this, SLOT(toggleLineView_P2(bool)));
    connect(ui->checkBox_21, SIGNAL(clicked(bool)), this, SLOT(toggleLineView_P3(bool)));
}

void
MainWindow::
connectMasterPipe()
{
    connect(ui->radioButton_11, SIGNAL(toggled(bool)), this, SLOT(onMasterPipeToggled(bool)));
}

void
MainWindow::
onMasterPipeToggled(const bool isEnabled)
{
	(isEnabled) ? LOOP.isMaster = true : LOOP.isMaster = false;
}

void
MainWindow::
readJsonConfigFile()
{
    bool ok = false;
	QFile file("./sparky.json");
   	file.open(QIODevice::ReadOnly);
	QString jsonString = file.readAll();
	QJsonDocument jsonDoc(QJsonDocument::fromJson(jsonString.toUtf8()));
    QJsonObject jsonObj = jsonDoc.object(); 
    QVariantMap json = jsonObj.toVariantMap();

    /// file server 
	m_mainServer = json[MAIN_SERVER].toString();
	m_localServer = json[LOCAL_SERVER].toString();

    /// calibration control variables
	LOOP.injectionOilPumpRate = json[LOOP_OIL_PUMP_RATE].toDouble();
	LOOP.injectionWaterPumpRate = json[LOOP_WATER_PUMP_RATE].toDouble();
	LOOP.injectionSmallWaterPumpRate = json[LOOP_SMALL_WATER_PUMP_RATE].toDouble();
	LOOP.injectionBucket = json[LOOP_BUCKET].toDouble();
	LOOP.injectionMark = json[LOOP_MARK].toDouble();
	LOOP.injectionMethod = json[LOOP_METHOD].toDouble();
	LOOP.pressureSensorSlope = json[LOOP_PRESSURE].toDouble();
	LOOP.minRefTemp = json[LOOP_MIN_TEMP].toInt();
	LOOP.maxRefTemp = json[LOOP_MAX_TEMP].toInt();
	LOOP.injectionTemp = json[LOOP_INJECTION_TEMP].toInt();
	LOOP.xDelay = json[LOOP_X_DELAY].toInt();
	LOOP.yFreq = json[LOOP_Y_FREQ].toDouble();
	LOOP.zTemp = json[LOOP_Z_TEMP].toDouble();
	LOOP.intervalSmallPump = json[LOOP_INTERVAL_SMALL_PUMP].toDouble();
	LOOP.intervalBigPump = json[LOOP_INTERVAL_BIG_PUMP].toDouble();
	LOOP.intervalOilPump = json[LOOP_INTERVAL_OIL_PUMP].toDouble();
	LOOP.loopNumber = json[LOOP_NUMBER].toInt();
	LOOP.masterMin = json[LOOP_MASTER_MIN].toDouble();
	LOOP.masterMax = json[LOOP_MASTER_MAX].toDouble();
	LOOP.masterDelta = json[LOOP_MASTER_DELTA].toDouble();
	LOOP.masterDeltaFinal = json[LOOP_MASTER_DELTA_FINAL].toDouble();
	LOOP.maxInjectionWater = json[LOOP_MAX_INJECTION_WATER].toInt();
	LOOP.maxInjectionOil = json[LOOP_MAX_INJECTION_OIL].toInt();
	LOOP.portIndex = json[LOOP_PORT_INDEX].toInt();

	/// main configuration panel
	ui->lineEdit_27->setText(QString::number(LOOP.injectionOilPumpRate));
	ui->lineEdit_28->setText(QString::number(LOOP.injectionWaterPumpRate)); 
	ui->lineEdit_82->setText(QString::number(LOOP.injectionSmallWaterPumpRate));

	ui->lineEdit_65->setText(QString::number(LOOP.maxInjectionOil));
	ui->lineEdit_66->setText(QString::number(LOOP.maxInjectionWater));

	ui->lineEdit_67->setText(QString::number(LOOP.minRefTemp));
	ui->lineEdit_68->setText (QString::number(LOOP.maxRefTemp));
	ui->lineEdit_69->setText(QString::number(LOOP.injectionTemp));

	ui->lineEdit_72->setText(QString::number(LOOP.xDelay));
	ui->lineEdit_70->setText(QString::number(LOOP.yFreq));
	ui->lineEdit_71->setText(QString::number(LOOP.zTemp));

	ui->lineEdit_73->setText(QString::number(LOOP.intervalSmallPump));
	ui->lineEdit_74->setText(QString::number(LOOP.intervalBigPump));
	ui->lineEdit_79->setText(QString::number(LOOP.intervalOilPump));

	ui->lineEdit_75->setText(QString::number(LOOP.masterMin));
	ui->lineEdit_76->setText(QString::number(LOOP.masterMax));
	ui->lineEdit_77->setText(QString::number(LOOP.masterDelta));
	ui->lineEdit_78->setText(QString::number(LOOP.masterDeltaFinal));

    /// done. close file.
	file.close();
}


void
MainWindow::
writeJsonConfigFile(void)
{
	QFile file(QStringLiteral("sparky.json"));
    file.open(QIODevice::WriteOnly);

	QJsonObject json;

    /// file server
    json[MAIN_SERVER] = m_mainServer;
	json[LOCAL_SERVER] = m_localServer;

	LOOP.injectionOilPumpRate = ui->lineEdit_27->text().toDouble();
	LOOP.injectionWaterPumpRate = ui->lineEdit_28->text().toDouble(); 
	LOOP.injectionSmallWaterPumpRate = ui->lineEdit_82->text().toDouble();

	LOOP.maxInjectionOil = ui->lineEdit_65->text().toDouble();
	LOOP.maxInjectionWater = ui->lineEdit_66->text().toDouble();

	LOOP.minRefTemp = ui->lineEdit_67->text().toDouble();
	LOOP.maxRefTemp = ui->lineEdit_68->text().toDouble();
	LOOP.injectionTemp = ui->lineEdit_69->text().toDouble();

	LOOP.xDelay =  ui->lineEdit_72->text().toDouble();
	LOOP.yFreq = ui->lineEdit_70->text().toDouble();
	LOOP.zTemp = ui->lineEdit_71->text().toDouble();

	LOOP.intervalSmallPump = ui->lineEdit_73->text().toDouble();
	LOOP.intervalBigPump = ui->lineEdit_74->text().toDouble();
	LOOP.intervalOilPump = ui->lineEdit_79->text().toDouble();

	LOOP.masterMin = ui->lineEdit_75->text().toDouble();
	LOOP.masterMax =  ui->lineEdit_76->text().toDouble();
	LOOP.masterDelta =  ui->lineEdit_77->text().toDouble();
	LOOP.masterDeltaFinal = ui->lineEdit_78->text().toDouble();

    /// calibration control variables
	json[LOOP_OIL_PUMP_RATE] = QString::number(LOOP.injectionOilPumpRate);
	json[LOOP_WATER_PUMP_RATE] = QString::number(LOOP.injectionWaterPumpRate);
	json[LOOP_SMALL_WATER_PUMP_RATE] = QString::number(LOOP.injectionSmallWaterPumpRate);
	json[LOOP_BUCKET] = QString::number(LOOP.injectionBucket);
	json[LOOP_MARK] = QString::number(LOOP.injectionMark);
	json[LOOP_METHOD] = QString::number(LOOP.injectionMethod);
	json[LOOP_PRESSURE] = QString::number(LOOP.pressureSensorSlope);
	json[LOOP_MIN_TEMP] = QString::number(LOOP.minRefTemp);
	json[LOOP_MAX_TEMP] = QString::number(LOOP.maxRefTemp);
	json[LOOP_INJECTION_TEMP] = QString::number(LOOP.injectionTemp);
	json[LOOP_X_DELAY] = QString::number(LOOP.xDelay);
	json[LOOP_Y_FREQ] = QString::number(LOOP.yFreq);
	json[LOOP_Z_TEMP] = QString::number(LOOP.zTemp);
	json[LOOP_INTERVAL_SMALL_PUMP] = QString::number(LOOP.intervalSmallPump);
	json[LOOP_INTERVAL_BIG_PUMP] = QString::number(LOOP.intervalBigPump);
	json[LOOP_INTERVAL_OIL_PUMP] = QString::number(LOOP.intervalOilPump);
	json[LOOP_NUMBER] = QString::number(LOOP.loopNumber);
	json[LOOP_MASTER_MIN] = QString::number(LOOP.masterMin);
	json[LOOP_MASTER_MAX] = QString::number(LOOP.masterMax);
	json[LOOP_MASTER_DELTA] = QString::number(LOOP.masterDelta);
	json[LOOP_MASTER_DELTA_FINAL] = QString::number(LOOP.masterDeltaFinal);
	json[LOOP_MAX_INJECTION_WATER] = QString::number(LOOP.maxInjectionWater);
	json[LOOP_MAX_INJECTION_OIL] = QString::number(LOOP.maxInjectionOil);
	json[LOOP_PORT_INDEX] = QString::number(LOOP.portIndex);

    /// file server 
	json[MAIN_SERVER] = m_mainServer;
	json[LOCAL_SERVER] = m_localServer;

	file.write(QJsonDocument(json).toJson());
	file.close();
}

void
MainWindow::
injectionPumpRates()
{
    bool ok;
    LOOP.injectionOilPumpRate = QInputDialog::getDouble(this,tr("Injection Oil Pump Rate"),tr("Enter Injection Oil Pump Rate [mL/min]"),LOOP.injectionOilPumpRate , -10000, 10000, 2, &ok,Qt::WindowFlags(), 1);
    LOOP.injectionWaterPumpRate = QInputDialog::getDouble(this,tr("Injection Water Pump Rate"),tr("Enter Injection Water Pump Rate [mL/min]"), LOOP.injectionWaterPumpRate, -10000, 10000, 2, &ok,Qt::WindowFlags(), 1);
    LOOP.injectionSmallWaterPumpRate = QInputDialog::getDouble(this,tr("Injection Small Water Pump Rate"),tr("Enter Injection Small Water Pump Rate [mL/min]"),LOOP.injectionSmallWaterPumpRate , -10000, 10000, 2, &ok,Qt::WindowFlags(), 1);
    
    /// update json config file
    writeJsonConfigFile();
}

void
MainWindow::
injectionBucket()
{
    bool ok;
    LOOP.injectionBucket = QInputDialog::getDouble(this, tr("Injection Bucket"),tr("Enter Injection Bucket [L]"),LOOP.injectionBucket , -10000, 10000, 2, &ok,Qt::WindowFlags(), 1);
     
    /// update json config file
    writeJsonConfigFile();
}

void
MainWindow::
injectionMark()
{
    bool ok;
    LOOP.injectionMark = QInputDialog::getDouble(this,tr("Injection Mark"),tr("Enter Injection Mark [L]"), LOOP.injectionMark, -10000, 10000, 2, &ok,Qt::WindowFlags(), 1);
     
    /// update json config file
    writeJsonConfigFile();
}


void
MainWindow::
injectionMethod()
{
    bool ok;
    LOOP.injectionMethod = QInputDialog::getDouble(this,tr("Injection Method"),tr("Enter Injection Method: "), LOOP.injectionMethod, -10000, 10000, 2, &ok,Qt::WindowFlags(), 1);

    /// update json config file
    writeJsonConfigFile();
}


void
MainWindow::
onActionPressureSensorSlope()
{
    bool ok;
    LOOP.pressureSensorSlope = QInputDialog::getDouble(this,tr("Pressure Sensor Slope"),tr("Enter Pressure Sensor Slope: "), LOOP.pressureSensorSlope, -10000, 10000, 2, &ok,Qt::WindowFlags(), 1);

    /// update json config file
    writeJsonConfigFile();
}

void
MainWindow::
onMinRefTemp()
{
    bool ok;
    LOOP.minRefTemp = QInputDialog::getInt(this,tr("Minimum Reference Temperature"),tr("Enter Minimum Reference Temperature: "), LOOP.minRefTemp, 0, 1000, 2, &ok);

    /// update json config file
    writeJsonConfigFile();
}

void
MainWindow::
onMaxRefTemp()
{
    bool ok;
    LOOP.maxRefTemp = QInputDialog::getInt(this,tr("Maximum Reference Temperature"),tr("Enter Maximum Reference Temperature: "), LOOP.maxRefTemp, 0, 1000, 2, &ok);

    /// update json config file
    writeJsonConfigFile();
}

void
MainWindow::
onInjectionTemp()
{
    bool ok;
    LOOP.injectionTemp = QInputDialog::getInt(this,tr("Injection Temperature"),tr("Enter Injection Temperature: "), LOOP.injectionTemp, 0, 1000, 2, &ok);

    /// update json config file
    writeJsonConfigFile();
}


void
MainWindow::
onXDelay()
{
    bool ok;
    LOOP.xDelay = QInputDialog::getInt(this,tr("X Delay"),tr("Enter Delay Peroid (seconds): "), LOOP.xDelay, 0, 3600, 2, &ok);

    /// update json config file
    writeJsonConfigFile();
}

void
MainWindow::
onYFreq()
{
    bool ok;
    LOOP.yFreq = QInputDialog::getDouble(this,tr("Y Delta Frequency"),tr("Enter Y Delta Fequency (KHz): 1000000"), LOOP.yFreq, 0, 1000000, 2, &ok,Qt::WindowFlags(), 1);

    /// update json config file
    writeJsonConfigFile();
}


void
MainWindow::
onZTemp()
{
    bool ok;
    LOOP.zTemp = QInputDialog::getDouble(this,tr("Z Delta Temperature"),tr("Enter Z Delta Temperature (°C): 0.1"), LOOP.zTemp, 0, 1000, 2, &ok,Qt::WindowFlags(), 1);

    /// update json config file
    writeJsonConfigFile();
}


void
MainWindow::
onIntervalSmallPump()
{
    bool ok;
    LOOP.intervalSmallPump = QInputDialog::getDouble(this,tr("Small Pump Interval [ % ]"),tr("Enter Small Pump Interval (%): 0.25"), LOOP.intervalSmallPump, 0, 1000, 2, &ok,Qt::WindowFlags(), 1);

    /// update json config file
    writeJsonConfigFile();
}


void
MainWindow::
onIntervalBigPump()
{
    bool ok;
    LOOP.intervalBigPump = QInputDialog::getDouble(this,tr("Big Pump Interval [ % ]"),tr("Enter Big Pump Interval (%): 1.0"), LOOP.intervalBigPump, 0, 1000, 2, &ok,Qt::WindowFlags(), 1);

    /// update json config file
    writeJsonConfigFile();
}


void
MainWindow::
onLoopNumber()
{
    bool ok;
    LOOP.loopNumber = QInputDialog::getInt(this,tr("Loop Number"),tr("Enter Loop Number : "), LOOP.loopNumber, 0, 999999, 2, &ok);

    /// update json config file
    writeJsonConfigFile();
}


void
MainWindow::
onActionMinMaster()
{
    bool ok;
    LOOP.masterMin = QInputDialog::getDouble(this,tr("Master Pipe"),tr("Enter Minimum Master Watercut (%): "), LOOP.masterMin, -1000, 1000, 2, &ok,Qt::WindowFlags(), 1);

    /// update json config file
    writeJsonConfigFile();
}


void
MainWindow::
onActionMaxMaster()
{
    bool ok;
    LOOP.masterMax = QInputDialog::getDouble(this,tr("Master Pipe"),tr("Enter Maximum Master Watercut (%): +0.15"), LOOP.masterMax, -1000, 1000, 2, &ok,Qt::WindowFlags(), 1);

    /// update json config file
    writeJsonConfigFile();
}

void
MainWindow::
onActionDeltaMaster()
{
    bool ok;
    LOOP.masterDelta = QInputDialog::getDouble(this,tr("Master Pipe"),tr("Enter Initial Delta Master Watercut (%): +0.15"), LOOP.masterDelta, -1000, 1000, 2, &ok,Qt::WindowFlags(), 1);

    /// update json config file
    writeJsonConfigFile();
}

void
MainWindow::
onActionDeltaMasterFinal()
{
    bool ok;
    LOOP.masterDeltaFinal = QInputDialog::getDouble(this,tr("Master Pipe"),tr("Enter Final Delta Master Watercut (%): +0.15"), LOOP.masterDeltaFinal, -1000, 1000, 2, &ok,Qt::WindowFlags(), 1);

    /// update json config file
    writeJsonConfigFile();
}

void
MainWindow::
onActionMainServer()
{
    QString dirName = QFileDialog::getExistingDirectory(this, tr("Open Directory"),m_mainServer,QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (!dirName.isEmpty() && !dirName.isNull()) m_mainServer = dirName;
    
    /// update json config file
    writeJsonConfigFile();
}

void
MainWindow::
onActionLocalServer()
{
    QString dirName = QFileDialog::getExistingDirectory(this, tr("Open Directory"),m_localServer,QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    
    if (!dirName.isEmpty() && !dirName.isNull()) m_localServer = dirName;

    /// update json config file
    writeJsonConfigFile();
}


void
MainWindow::
onActionWater()
{
    bool ok;
    LOOP.maxInjectionWater = QInputDialog::getInt(this,tr("Loop Number"),tr("Enter Max Injection Time for Water [ S ]: "), LOOP.maxInjectionWater, 0, 9999, 2, &ok);

    /// update json config file
    writeJsonConfigFile();
}

void
MainWindow::
onActionOil()
{
    bool ok;
    LOOP.maxInjectionOil = QInputDialog::getInt(this,tr("Loop Number"),tr("Enter Max Injection Time for Oil : [ S ]"), LOOP.maxInjectionOil, 0, 9999, 2, &ok);

    /// update json config file
    writeJsonConfigFile();
}


void
MainWindow::
onEquationTableChecked(bool isTable)
{
    if (!isTable) ui->tableWidget->setRowCount(0);
}


double
MainWindow::
sendCalibrationRequest(int dataType, modbus_t * serialModbus, int func, int address, int num, int ret, uint8_t * dest, uint16_t * dest16, bool is16Bit, bool writeAccess, QString funcType)
{
    //////// address offset /////////
    int addr = address - ADDR_OFFSET;
    /////////////////////////////////

    switch( func )
    {
		case MODBUS_FC_READ_COILS:
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
            ret = modbus_write_bit( serialModbus, addr,ui->radioButton_184->isChecked() ? 1 : 0 );
            writeAccess = true;
            num = 1;
            break;
        case MODBUS_FC_WRITE_SINGLE_REGISTER:
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
		isModbusTransmissionFailed = false;

        if( writeAccess )
        {
            m_statusInd->setStyleSheet( "background: #0b0;" );
            m_statusTimer->start( 2000 );
        }
        else
        {
            QString qs_num;
            QString qs_output = "0x";
            bool ok = false;

            ui->regTable->setRowCount( num );
            for( int i = 0; i < num; ++i )
            {
                int data = is16Bit ? dest16[i] : dest[i];
                QString qs_tmp;

                qs_num.sprintf("%d", data);
                qs_tmp.sprintf("%04x", data);
                qs_output.append(qs_tmp);

				if (dataType == INT_R)  // INT_READ
            	{
					ui->lineEdit_111->setText(QString::number(data));
                	return data;
            	}
            	else if (dataType == COIL_R)  // COIL_READ
            	{
					(data) ? ui->radioButton_184->setChecked(true) : ui->radioButton_185->setChecked(true);
                	return (data) ? 1 : 0;
            	}
            }
            
            double d;
			if (dataType == FLOAT_R) // FLOAT_READ
            {
                QByteArray array = QByteArray::fromHex(qs_output.toLatin1());
                d = toFloat(array);
				ui->lineEdit_109->setText(QString::number(d,'f',3)) ;
                return d;
            }
        }
    }
	else
	{
		isModbusTransmissionFailed = true;
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

        for (int j=0; j < ui->tableWidget->item(i,6)->text().toInt()+7; j++)
        {
             dataStream.append(ui->tableWidget->item(i,j)->text()+",");
        }

        out << dataStream << endl;
    }

    file.close();
}


void
MainWindow::
onActionStart()
{
    ui->actionStop->setVisible(true);
    ui->actionStart->setVisible(false);
	delay(1);

	/// start calibration
	onCalibrationButtonPressed();
}

void
MainWindow::
onActionStop()
{
    ui->actionStart->setVisible(true);
    ui->actionStop->setVisible(false);
	delay(1);

	LOOP.runMode = STOP_MODE;

	/// stop calibration
	stopCalibration();
}

void
MainWindow::
onActionSettings()
{
	static bool on = true;

	if (on) 
	{
		/// calibration configuration
    	readJsonConfigFile();
		ui->groupBox_18->show();
		ui->groupBox_35->show();
		ui->groupBox_34->show();
		ui->groupBox_33->show();
		ui->groupBox_32->show();
		ui->groupBox_31->show();
		ui->groupBox_30->show();
		ui->groupBox_29->show();

		/// main loop configuration 
		ui->groupBox_12->hide();
		ui->groupBox_20->hide();
	}
	else
	{
		/// calibration configuration
    	writeJsonConfigFile();
		ui->groupBox_18->hide();
		ui->groupBox_35->hide();
		ui->groupBox_34->hide();
		ui->groupBox_33->hide();
		ui->groupBox_32->hide();
		ui->groupBox_31->hide();
		ui->groupBox_30->hide();
		ui->groupBox_29->hide();

		/// main loop configuration 
		ui->groupBox_12->show();
		ui->groupBox_20->show();
	}

	on = !on;
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
            while (ui->tableWidget->columnCount() < valueList[6].toInt()+7)
            {
                ui->tableWidget->insertColumn(ui->tableWidget->columnCount());
            }

            // fill the data in the talbe cell
            ui->tableWidget->setItem( ui->tableWidget->rowCount()-1, 0, new QTableWidgetItem(valueList[0])); // Name
            ui->tableWidget->setItem( ui->tableWidget->rowCount()-1, 1, new QTableWidgetItem(valueList[1])); // Slave
            ui->tableWidget->setItem( ui->tableWidget->rowCount()-1, 2, new QTableWidgetItem(valueList[2])); // Address
            ui->tableWidget->setItem( ui->tableWidget->rowCount()-1, 3, new QTableWidgetItem(valueList[3])); // Type
            ui->tableWidget->setItem( ui->tableWidget->rowCount()-1, 4, new QTableWidgetItem(valueList[4])); // Scale
            ui->tableWidget->setItem( ui->tableWidget->rowCount()-1, 5, new QTableWidgetItem(valueList[5])); // RW
            ui->tableWidget->setItem( ui->tableWidget->rowCount()-1, 6, new QTableWidgetItem(valueList[6])); // Qty
            ui->tableWidget->setItem( ui->tableWidget->rowCount()-1, 7, new QTableWidgetItem(valueList[7])); // Value

            // enable uploadEquationButton
            ui->startEquationBtn->setEnabled(1);
        }
    }

    // set column width
    ui->tableWidget->setColumnWidth(0,120); // Name
    ui->tableWidget->setColumnWidth(1,30);  // Slave
    ui->tableWidget->setColumnWidth(2,50);  // Address
    ui->tableWidget->setColumnWidth(3,40);  // Type
    ui->tableWidget->setColumnWidth(4,30);  // Scale
    ui->tableWidget->setColumnWidth(5,30);  // RW
    ui->tableWidget->setColumnWidth(6,30);  // Qty

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
            while (ui->tableWidget->columnCount() < valueList[6].toInt()+7)
            {
                ui->tableWidget->insertColumn(ui->tableWidget->columnCount());
            }

            // fill the data in the talbe cell
            ui->tableWidget->setItem( ui->tableWidget->rowCount()-1, 0, new QTableWidgetItem(valueList[0])); // Name
            ui->tableWidget->setItem( ui->tableWidget->rowCount()-1, 1, new QTableWidgetItem(valueList[1])); // Slave
            ui->tableWidget->setItem( ui->tableWidget->rowCount()-1, 2, new QTableWidgetItem(valueList[2])); // Address
            ui->tableWidget->setItem( ui->tableWidget->rowCount()-1, 3, new QTableWidgetItem(valueList[3])); // Type
            ui->tableWidget->setItem( ui->tableWidget->rowCount()-1, 4, new QTableWidgetItem(valueList[4])); // Scale
            ui->tableWidget->setItem( ui->tableWidget->rowCount()-1, 5, new QTableWidgetItem(valueList[5])); // RW
            ui->tableWidget->setItem( ui->tableWidget->rowCount()-1, 6, new QTableWidgetItem(valueList[6])); // Qty
            ui->tableWidget->setItem( ui->tableWidget->rowCount()-1, 7, new QTableWidgetItem(valueList[7])); // Value

            // fill the value list
            for (int j = 0; j < valueList[6].toInt(); j++)
            {
                QString cellData = valueList[7+j];
                if (valueList[3].contains("int")) cellData = cellData.mid(0, cellData.indexOf("."));

                ui->tableWidget->setItem( ui->tableWidget->rowCount()-1, j+7, new QTableWidgetItem(cellData));
            }

            // enable uploadEquationButton
            ui->startEquationBtn->setEnabled(1);
        }
    }

    // set column width
    ui->tableWidget->setColumnWidth(0,120); // Name
    ui->tableWidget->setColumnWidth(1,30);  // Slave
    ui->tableWidget->setColumnWidth(2,50);  // Address
    ui->tableWidget->setColumnWidth(3,40);  // Type
    ui->tableWidget->setColumnWidth(4,30);  // Scale
    ui->tableWidget->setColumnWidth(5,30);  // RW
    ui->tableWidget->setColumnWidth(6,30);  // Qty

    // close file
    file.close();
}

void
MainWindow::
onEquationButtonPressed()
{
    ui->startEquationBtn->setEnabled(false);
    ui->startEquationBtn->setText( tr("Loading") );

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
    ui->startEquationBtn->setEnabled(true);
}


void
MainWindow::
onDownloadButtonChecked(bool isChecked)
{
    if (isChecked)
    {
        ui->startEquationBtn->setEnabled(true);
    }
    else {
        ui->startEquationBtn->setEnabled(true);
    }
}


void
MainWindow::
onDownloadEquation()
{
    int value = 0;
    int rangeMax = 0;

    ui->slaveID->setValue(1);                           // set slave ID
    ui->radioButton_187->setChecked(true);              // read mode
    ui->startEquationBtn->setEnabled(false);

    // load empty equation file
    loadCsvTemplate();

    /// get rangeMax of progressDialog
    for (int i = 0; i < ui->tableWidget->rowCount(); i++) rangeMax+=ui->tableWidget->item(i,6)->text().toInt();

    QProgressDialog progress("Downloading...", "Abort", 0, rangeMax, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setAutoClose(true);
    progress.setAutoReset(true);

    for (int i = 0; i < ui->tableWidget->rowCount(); i++)
    {
         int regAddr = ui->tableWidget->item(i,2)->text().toInt();

         if (ui->tableWidget->item(i,3)->text().contains("float"))
         {
             ui->numCoils->setValue(2);                  // 2 bytes
             ui->radioButton_181->setChecked(TRUE);      // float type
             ui->functionCode->setCurrentIndex(3);       // function code
             for (int x=0; x < ui->tableWidget->item(i,6)->text().toInt(); x++)
             {
                if (progress.wasCanceled()) return;
                if (ui->tableWidget->item(i,6)->text().toInt() > 1) progress.setLabelText("Downloading \""+ui->tableWidget->item(i,0)->text()+"\" "+QString::number(x+1));
                else progress.setLabelText("Downloading \""+ui->tableWidget->item(i,0)->text()+"\"");
                progress.setValue(value++);

                 ui->startAddr->setValue(regAddr);       // set address
                 onSendButtonPress();                    // send
                 delay();
                 regAddr = regAddr+2;                    // update reg address
                 ui->tableWidget->setItem( i, x+7, new QTableWidgetItem(ui->lineEdit_109->text()));
             }
         }
         else if (ui->tableWidget->item(i,3)->text().contains("int"))
         {
            if (progress.wasCanceled()) return;
            progress.setLabelText("Downloading \""+ui->tableWidget->item(i,0)->text()+"\"");
            progress.setValue(value++);

             ui->numCoils->setValue(1);                  // 1 byte
             ui->radioButton_182->setChecked(TRUE);      // int type
             ui->functionCode->setCurrentIndex(3);       // function code
             ui->startAddr->setValue(regAddr);           // address
             onSendButtonPress();                        // send
             delay();
             ui->tableWidget->setItem( i, 7, new QTableWidgetItem(ui->lineEdit_111->text()));
         }
         else
         {
            if (progress.wasCanceled()) return;
            progress.setLabelText("Downloading \""+ui->tableWidget->item(i,0)->text()+"\"");
            progress.setValue(value++);

             ui->numCoils->setValue(1);                  // 1 byte
             ui->radioButton_183->setChecked(TRUE);      // coil type
             ui->functionCode->setCurrentIndex(0);       // function code
             ui->startAddr->setValue(regAddr);           // address
             onSendButtonPress();                        // send
             delay();
         }
     }
}

bool
MainWindow::
informUser(const QString t1, const QString t2, const QString t3 = "")
{
    QMessageBox msgBox;
    msgBox.setWindowTitle(t1);
    msgBox.setText(t2);
    msgBox.setInformativeText(t3);
    msgBox.setStandardButtons(QMessageBox::Ok);
    int ret = msgBox.exec();
    switch (ret) {
        case QMessageBox::Ok: return true;
        default: return true;
    }
}

bool
MainWindow::
isUserInputYes(const QString t1, const QString t2)
{
    QMessageBox msgBox;
    msgBox.setText(t1);
    msgBox.setInformativeText(t2);
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
    msgBox.setDefaultButton(QMessageBox::Yes);
    int ret = msgBox.exec();
    switch (ret) {
        case QMessageBox::Yes: return true;
        case QMessageBox::Cancel: return false;
        default: return true;
    }
}


void
MainWindow::
onUploadEquation()
{
    int value = 0;
    int rangeMax = 0;
    bool isReinit = false;
    QMessageBox msgBox;
	isModbusTransmissionFailed = false;

    /// get rangeMax of progressDialog
    for (int i = 0; i < ui->tableWidget->rowCount(); i++) rangeMax+=ui->tableWidget->item(i,6)->text().toInt();
    
    msgBox.setText("You can reinitialize existing registers and coils.");
    msgBox.setInformativeText("Do you want to reinitialize registers and coils?");
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
    msgBox.setDefaultButton(QMessageBox::Cancel);
    int ret = msgBox.exec();
    switch (ret) {
        case QMessageBox::Yes:
            isReinit = true;
            break;
        case QMessageBox::No:
            isReinit = false;
            break;
        case QMessageBox::Cancel:
        default: return;
    }
        
    ui->slaveID->setValue(1);                           // set slave ID
    ui->radioButton_186->setChecked(true);              // write mode    

    QProgressDialog progress("Uploading...", "Abort", 0, rangeMax, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setAutoClose(true);
    progress.setAutoReset(true);

    /// unlock fct default regs & coils (999)
    ui->numCoils->setValue(1);                      // 1 byte
    ui->radioButton_183->setChecked(TRUE);          // coil type
    ui->functionCode->setCurrentIndex(4);           // function type
    ui->startAddr->setValue(999);                   // address
    ui->radioButton_184->setChecked(true);          // set value
    if (progress.wasCanceled()) return;
    progress.setValue(0);
    progress.setLabelText("Unlocking factory registers....");
    onSendButtonPress();
    delay();
	if (isModbusTransmissionFailed) 
	{
		isModbusTransmissionFailed = false;
		msgBox.setText("Modbus Transmission Failed.");
    	msgBox.setInformativeText("Do you want to continue with next item?");
    	msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    	msgBox.setDefaultButton(QMessageBox::No);
    	int ret = msgBox.exec();
    	switch (ret) {
        	case QMessageBox::Yes:
            	break;
        	case QMessageBox::No:
        	default: return;
    	}

	}

    if (isReinit)
    {
        ui->numCoils->setValue(1);                      // set byte count 1
        ui->radioButton_183->setChecked(TRUE);          // set type coil 
        ui->functionCode->setCurrentIndex(4);           // set function type
        ui->radioButton_185->setChecked(true);          // set coils unlocked 

        ui->startAddr->setValue(25);                    // set address 25
        if (progress.wasCanceled()) return;
        progress.setLabelText("Reinitializing registers....");
        progress.setValue(0);
        onSendButtonPress();
        delay();
		if (isModbusTransmissionFailed) 
		{
			isModbusTransmissionFailed = false;
			msgBox.setText("Modbus Transmission Failed.");
    		msgBox.setInformativeText("Do you want to continue with next item?");
    		msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    		msgBox.setDefaultButton(QMessageBox::No);
    		int ret = msgBox.exec();
    		switch (ret) {
        		case QMessageBox::Yes: break;
        		case QMessageBox::No:
        		default: return;
    		}

		}
        ui->startAddr->setValue(26);                    // set address 26
        if (progress.wasCanceled()) return;
        progress.setValue(0);
        onSendButtonPress();
        delay(8);                                       // need extra time to restart
		if (isModbusTransmissionFailed) 
		{
			isModbusTransmissionFailed = false;
			msgBox.setText("Modbus Transmission Failed.");
    		msgBox.setInformativeText("Do you want to continue with next item?");
    		msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    		msgBox.setDefaultButton(QMessageBox::No);
    		int ret = msgBox.exec();
    		switch (ret) {
        		case QMessageBox::Yes: break;
        		case QMessageBox::No:
        		default: return;
    		}

		}	
        /// unlock fct default regs & coils (999)
        ui->numCoils->setValue(1);                      // 1 byte
        ui->radioButton_183->setChecked(TRUE);          // coil type
        ui->functionCode->setCurrentIndex(4);           // function type
        ui->startAddr->setValue(999);                   // address
        ui->radioButton_184->setChecked(true);          // set value
        if (progress.wasCanceled()) return;
        progress.setValue(0);
        progress.setLabelText("Unlocking factory registers....");
        onSendButtonPress();
        delay();
		if (isModbusTransmissionFailed) 
		{
			isModbusTransmissionFailed = false;
			msgBox.setText("Modbus Transmission Failed.");
    		msgBox.setInformativeText("Do you want to continue with next item?");
    		msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    		msgBox.setDefaultButton(QMessageBox::No);
    		int ret = msgBox.exec();
    		switch (ret) {
        		case QMessageBox::Yes: break;
        		case QMessageBox::No:
        		default: return;
    		}
		}
   }

   for (int i = 0; i < ui->tableWidget->rowCount(); i++)
   {
        int regAddr = ui->tableWidget->item(i,2)->text().toInt();
        if (ui->tableWidget->item(i,3)->text().contains("float"))
        {
            ui->numCoils->setValue(2);                  // 2 bytes
            ui->radioButton_181->setChecked(TRUE);      // float type
            ui->functionCode->setCurrentIndex(7);       // function code
            for (int x=0; x < ui->tableWidget->item(i,6)->text().toInt(); x++)
            {
                QString val = ui->tableWidget->item(i,7+x)->text();
                ui->startAddr->setValue(regAddr);       // set address
                ui->lineEdit_109->setText(val);         // set value
                if (progress.wasCanceled()) return;
                if (ui->tableWidget->item(i,6)->text().toInt() > 1) progress.setLabelText("Uploading \""+ui->tableWidget->item(i,0)->text()+"["+QString::number(x+1)+"]"+"\""+","+" \""+val+"\"");
                else progress.setLabelText("Uploading \""+ui->tableWidget->item(i,0)->text()+"\""+","+" \""+val+"\"");
                progress.setValue(value++);
                onSendButtonPress();                    // send
                regAddr += 2;                           // update reg address
                delay();
				if (isModbusTransmissionFailed) 
				{
					isModbusTransmissionFailed = false;
					if (ui->tableWidget->item(i,6)->text().toInt() > 1) msgBox.setText("Modbus Transmission Failed: "+ui->tableWidget->item(i,0)->text()+"["+QString::number(x+1)+"]");
					else msgBox.setText("Modbus Transmission Failed: "+ui->tableWidget->item(i,0)->text());
    				msgBox.setInformativeText("Do you want to continue with next item?");
    				msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    				msgBox.setDefaultButton(QMessageBox::No);
    				int ret = msgBox.exec();
    				switch (ret) {
        				case QMessageBox::Yes: break;
        				case QMessageBox::No:
        				default: return;
    				}
				}
            }
        }
        else if (ui->tableWidget->item(i,3)->text().contains("int"))
        {
            QString val = ui->tableWidget->item(i,7)->text();
            ui->numCoils->setValue(1);                  // 1 byte
            ui->radioButton_182->setChecked(TRUE);      // int type
            ui->functionCode->setCurrentIndex(5);       // function code
            ui->lineEdit_111->setText(val);             // set value
            ui->startAddr->setValue(regAddr);           // address
            if (progress.wasCanceled()) return;
            progress.setLabelText("Uploading \""+ui->tableWidget->item(i,0)->text()+"\""+","+" \""+val+"\"");
            progress.setValue(value++);
            onSendButtonPress();                        // send
            delay();
			if (isModbusTransmissionFailed) 
			{
				isModbusTransmissionFailed = false;
				msgBox.setText("Modbus Transmission Failed: "+ui->tableWidget->item(i,0)->text());
    			msgBox.setInformativeText("Do you want to continue with next item?");
    			msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    			msgBox.setDefaultButton(QMessageBox::No);
    			int ret = msgBox.exec();
    			switch (ret) {
        			case QMessageBox::Yes: break;
        			case QMessageBox::No:
        			default: return;
    			}
			}
        }
        else
        {
            ui->numCoils->setValue(1);                  // 1 byte
            ui->radioButton_183->setChecked(TRUE);      // coil type
            ui->functionCode->setCurrentIndex(4);       // function code
            ui->startAddr->setValue(regAddr);           // address
            if (ui->tableWidget->item(i,7)->text().toInt() == 1)
            {
                ui->radioButton_184->setChecked(true);  // TRUE
                progress.setLabelText("Uploading \""+ui->tableWidget->item(i,0)->text()+"\""+","+" \"1\"");
            }
            else 
            {
                ui->radioButton_185->setChecked(true);  // FALSE
                progress.setLabelText("Uploading \""+ui->tableWidget->item(i,0)->text()+"\""+","+" \"0\"");
            }
            if (progress.wasCanceled()) return;
            progress.setValue(value++);
            onSendButtonPress();                        // send
            delay();
			if (isModbusTransmissionFailed) 
			{
				isModbusTransmissionFailed = false;
				msgBox.setText("Modbus Transmission Failed: "+ui->tableWidget->item(i,0)->text());
    			msgBox.setInformativeText("Do you want to continue with next item?");
    			msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    			msgBox.setDefaultButton(QMessageBox::No);
    			int ret = msgBox.exec();
    			switch (ret) {
        			case QMessageBox::Yes: break;
        			case QMessageBox::No:
        			default: return;
    			}
			}
        }
    }

    /// update factory default values
    ui->numCoils->setValue(1);                      // 1 byte
    ui->radioButton_183->setChecked(TRUE);          // coil type
    ui->functionCode->setCurrentIndex(4);           // function code
    ui->radioButton_184->setChecked(true);          // set value

    /// unlock factory default registers
    ui->startAddr->setValue(999);                   // address 999
    onSendButtonPress();
    delay();

    /// update factory default registers
    ui->startAddr->setValue(9999);                  // address 99999
    onSendButtonPress();
    delay();
}

void
MainWindow::
onUnlockFactoryDefaultBtnPressed()
{
    ui->numCoils->setValue(1);                      // 1 byte
    ui->radioButton_183->setChecked(TRUE);          // coil type
    ui->functionCode->setCurrentIndex(4);           // function code
    ui->radioButton_184->setChecked(true);          // set value

    /// unlock factory default registers
    ui->startAddr->setValue(999);                   // address 999
    onSendButtonPress();
    delay();
}


void
MainWindow::
onLockFactoryDefaultBtnPressed()
{
    ui->numCoils->setValue(1);                      // 1 byte
    ui->radioButton_183->setChecked(TRUE);          // coil type
    ui->functionCode->setCurrentIndex(4);           // function code
    ui->radioButton_185->setChecked(true);          // set value

    /// unlock factory default registers
    ui->startAddr->setValue(999);                   // address 999
    onSendButtonPress();
    delay();
}


void
MainWindow::
onUpdateFactoryDefaultPressed()
{
    if ( ui->radioButton_192->isChecked()) return;

    QMessageBox msgBox;

    msgBox.setText("Factory default values will be permanently changed.");
    msgBox.setInformativeText("Are you sure you want to do this?");
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox.setDefaultButton(QMessageBox::No);
    int ret = msgBox.exec();
    switch (ret) {
        case QMessageBox::Yes: break;
        case QMessageBox::No:
        default: return;
    }
 
    ui->numCoils->setValue(1);                      // 1 byte
    ui->radioButton_183->setChecked(TRUE);          // coil type
    ui->functionCode->setCurrentIndex(4);           // function code
    ui->radioButton_184->setChecked(true);          // set value

    /// update factory default registers
    ui->startAddr->setValue(9999);                  // address 99999
    onSendButtonPress();
    delay();
}


void
MainWindow::
connectProfiler()
{
    connect(ui->radioButton_193, SIGNAL(pressed()), this, SLOT(onUnlockFactoryDefaultBtnPressed()));
    connect(ui->radioButton_192, SIGNAL(pressed()), this, SLOT(onLockFactoryDefaultBtnPressed()));
    connect(ui->pushButton_2, SIGNAL(pressed()), this, SLOT(onUpdateFactoryDefaultPressed()));
    connect(ui->startEquationBtn, SIGNAL(pressed()), this, SLOT(onEquationButtonPressed()));
    connect(ui->radioButton_189, SIGNAL(toggled(bool)), this, SLOT(onDownloadButtonChecked(bool)));
}


void
MainWindow::
setupModbusPorts()
{
    setupModbusPort();
}


int
MainWindow::
setupModbusPort()
{
    QSettings s;

    int portIndex = LOOP.portIndex;
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


//static inline QString embracedString( const QString & s )
//{
    //return s.section( '(', 1 ).section( ')', 0, 0 );
//}


void
MainWindow::
releaseSerialModbus()
{
    modbus_close( LOOP.serialModbus );
    modbus_free( LOOP.serialModbus );
    LOOP.serialModbus = NULL;
    updateLoopTabIcon(false);
}


void
MainWindow::
changeSerialPort( int )
{
    const int iface = ui->comboBox->currentIndex();
    LOOP.portIndex = iface;
    writeJsonConfigFile();

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
changeModbusInterface(const QString& port, char parity)
{
    releaseSerialModbus();
    LOOP.serialModbus = modbus_new_rtu( port.toLatin1().constData(),ui->comboBox_2->currentText().toInt(),parity,ui->comboBox_3->currentText().toInt(),ui->comboBox_4->currentText().toInt() );
            
    if( modbus_connect( LOOP.serialModbus ) == -1 )
    {
        emit connectionError( tr( "Could not connect serial port at LOOP " )+QString::number(0) );
        releaseSerialModbus();
    }
    else
        updateLoopTabIcon(true);
}


void
MainWindow::
onCheckBoxChecked(bool checked)
{
    clearMonitors();

    if (checked) 
	{
		setupModbusPort();
		updateLoopTabIcon(true);
	}
	else updateLoopTabIcon(false);

    onRtuPortActive(checked);
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
updatePipeStability(const bool isF, const int pipe, const int value)
{
    if (pipe == ALL) 
    {
		for (int i=0; i<3; i++) 
		{
			PIPE[i].freqProgress->setValue(value);
			PIPE[i].tempProgress->setValue(value);
		}
    }
	else
	{
		if (PIPE[pipe].status == ENABLED) (isF) ? PIPE[pipe].freqProgress->setValue(value) : PIPE[pipe].tempProgress->setValue(value);
	} 
}


///////////////////////////////////////////
/// depending on the mode,
/// we use some fields differently.
///////////////////////////////////////////

void
MainWindow::
onModeChanged(bool isLow)
{
    ui->groupBox_65->setEnabled(!isLow); // salt
    ui->groupBox_113->setEnabled(!isLow); // oil temp
	ui->groupBox_10->setEnabled(!isLow); // oil run

    if (isLow)
    {
        ui->groupBox_9->setTitle("WATERCUT [%]");
        ui->label_4->setText("HIGH");
        ui->label_5->setText("LOW");
        ui->lineEdit_37->setText("0");
        ui->lineEdit_38->setText("0");
    }
    else
    {
        ui->groupBox_9->setTitle("WATER RUN [%]");
        ui->label_4->setText("START");
        ui->label_5->setText("STOP");
        ui->lineEdit_37->setText("0");
        ui->lineEdit_38->setText("0");
    }
}

void
MainWindow::
connectModeChanged()
{
    connect(ui->radioButton_6, SIGNAL(toggled(bool)), this, SLOT(onModeChanged(bool)));
}



void
MainWindow::
connectProductBtnPressed()
{
    connect(ui->radioButton, SIGNAL(toggled(bool)), this, SLOT(onUpdateRegisters(bool)));
}


void
MainWindow::
updateLoopTabIcon(const bool connected)
{
    QIcon icon(QLatin1String(":/green.ico"));
    QIcon icoff(QLatin1String(":/red.ico"));
    (connected) ? ui->tabWidget_2->setTabIcon(0,icon) : ui->tabWidget_2->setTabIcon(0,icoff);
}


void
MainWindow::
initializeTabIcons()
{
    QIcon icon(QLatin1String(":/red.ico"));
    ui->tabWidget_2->setTabIcon(0,icon);
}


void
MainWindow::
createInjectionFile(const int sn, const int pipe, const QString startValue, const QString stopValue, const QString saltValue, const QString filename)
{
    /// headers 
    QDateTime currentDataTime = QDateTime::currentDateTime();
    QString header0;
    LOOP.isEEA ? header0 = EEA_INJECTION_FILE : header0 = RAZ_INJECTION_FILE;
    QString header1("SN"+QString::number(sn)+" | "+LOOP.mode.split("\\").at(1) +" | "+currentDataTime.toString()+" | L"+QString::number(LOOP.loopNumber)+PIPE[pipe].pipeId+" | "+PROJECT+RELEASE_VERSION); 
    QString header2("INJECTION:  "+startValue+" % "+"to "+stopValue+" % "+"Watercut at "+"1 % "+"Salinity\n");
    QString header3 = HEADER3;
    QString header4 = HEADER4;
    QString header5 = HEADER5;
	QString header21; // LOWCUT ONLY
    QString header22; // LOWCUT ONLY

	/// CALIBRAT, ADJUSTED, ROLLOVER (LOWCUT ONLY)
    if (LOOP.mode == LOW)
    {
        /// create headers
        header2 = "TEMPERATURE:  "+startValue+" °C "+"to "+stopValue+" °C\n";
        header21 = "INJECTION:  "+LOOP.oilRunStart->text()+" % "+"to "+LOOP.oilRunStop->text()+" % Watercut\n";
        header22 = "ROLLOVER:  "+QString::number(LOOP.watercut)+" % "+"to "+"rollover\n";

		/// set filenames
		PIPE[pipe].fileCalibrate.setFileName(PIPE[pipe].mainDirPath+"\\"+QString("CALIBRAT").append(LOOP.calExt));
		PIPE[pipe].fileAdjusted.setFileName(PIPE[pipe].mainDirPath+"\\"+QString("ADJUSTED").append(LOOP.adjExt));
		PIPE[pipe].fileRollover.setFileName(PIPE[pipe].mainDirPath+"\\"+QString("ROLLOVER").append(LOOP.rolExt));

        /// update PIPE object 
        if (filename == "CALIBRAT") 
		{
			/// CALIBRAT
        	if (!QFileInfo(PIPE[pipe].fileCalibrate).exists()) 
        	{
            	QTextStream streamCalibrate(&PIPE[pipe].fileCalibrate);
            	PIPE[pipe].fileCalibrate.open(QIODevice::WriteOnly | QIODevice::Text);
            	streamCalibrate << header0 << '\n' << header1 << '\n' << header21 << '\n' << header3 << '\n' << header4 << '\n' << header5 << '\n';
            	PIPE[pipe].fileCalibrate.close();
		
				/// update file list	
				updateFileList(QFileInfo(PIPE[pipe].fileCalibrate).fileName(), sn, pipe);
        	}
		}
        else if (filename == "ADJUSTED") 
		{
        	if (!QFileInfo(PIPE[pipe].fileAdjusted).exists())
        	{ 
            	QTextStream streamAdjusted(&PIPE[pipe].fileAdjusted);
            	PIPE[pipe].fileAdjusted.open(QIODevice::WriteOnly | QIODevice::Text);
            	streamAdjusted << header0 << '\n' << header1 << '\n' << header21 << '\n' << header3 << '\n' << header4 << '\n' << header5 << '\n';
            	PIPE[pipe].fileAdjusted.close();

				/// update file list	
				updateFileList(QFileInfo(PIPE[pipe].fileAdjusted).fileName(), sn, pipe);
        	}	
		}
        else if (filename == "ROLLOVER") 
		{
	        /// ROLLOVER 
   		    if (!QFileInfo(PIPE[pipe].fileRollover).exists()) 
        	{
            	QTextStream streamRollover(&PIPE[pipe].fileRollover);
            	PIPE[pipe].fileRollover.open(QIODevice::WriteOnly | QIODevice::Text);
            	streamRollover << header0 << '\n' << header1 << '\n' << header22 << '\n' << header3 << '\n' << header4 << '\n' << header5 << '\n';
            	PIPE[pipe].fileRollover.close();
			
				/// update file list	
				updateFileList(QFileInfo(PIPE[pipe].fileRollover).fileName(), sn, pipe);
        	}
		}
    }
	else if (LOOP.mode == MID)
	{
		/// OIL_INJECTION TEMP
   	    if (!QFileInfo(PIPE[pipe].file).exists()) 
       	{
           	QTextStream stream(&PIPE[pipe].file);
           	PIPE[pipe].file.open(QIODevice::WriteOnly | QIODevice::Text);
           	stream << header0 << '\n' << header1 << '\n' << header2 << '\n' << header3 << '\n' << header4 << '\n' << header5 << '\n';
           	PIPE[pipe].file.close();
		
			/// update file list	
			updateFileList(QFileInfo(PIPE[pipe].file).fileName(), sn, pipe);
       	}
	}
}


void
MainWindow::
updateFileList(const QString fileName, const int sn, const int pipe)
{
    QString header0;
    QFile file;
    QTextStream streamList(&file);
    QDateTime currentDataTime = QDateTime::currentDateTime();

    LOOP.isEEA ? header0 = EEA_INJECTION_FILE : header0 = RAZ_INJECTION_FILE;
    QString header1("SN"+QString::number(sn)+" | "+LOOP.mode.split("\\").at(1) +" | "+currentDataTime.toString()+" | L"+QString::number(LOOP.loopNumber)+PIPE[pipe].pipeId+" | "+PROJECT+RELEASE_VERSION); 
 
    file.setFileName(PIPE[pipe].mainDirPath+"\\"+FILE_LIST);
    file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);

	/// write header to streamList
    if (QFileInfo(file).exists())
	{
		if (QFileInfo(file).size() == 0) streamList << header0 << '\n' << header1 << '\n' << '\n' << fileName << '\n';
    	else streamList << fileName << '\n';
	}

    file.close();
}


void
MainWindow::
createTempRunFile(const int sn, const QString startValue, const QString stopValue, const QString saltValue, const int pipe)
{
	/// set file names
    QDateTime currentDataTime = QDateTime::currentDateTime();

    /// headers 
    QString header0;
    LOOP.isEEA ? header0 = EEA_INJECTION_FILE : header0 = RAZ_INJECTION_FILE;
    QString header1("SN"+QString::number(sn)+" | "+LOOP.mode.split("\\").at(1) +" | "+currentDataTime.toString()+" | L"+QString::number(LOOP.loopNumber)+PIPE[pipe].pipeId+" | "+PROJECT+RELEASE_VERSION); 
    QString header2("INJECTION:  "+startValue+" % "+"to "+stopValue+" % "+"Watercut at "+saltValue+" % "+"Salinity\n");
    if ((LOOP.mode == LOW) || (!LOOP.isEEA)) header2 = "TEMPERATURE:  "+startValue+" °C "+"to "+stopValue+" °C\n";
    QString header3 = HEADER3;
    QString header4 = HEADER4;
    QString header5 = HEADER5;

    /// stream
    QTextStream stream(&PIPE[pipe].file);

    /// open file
    PIPE[pipe].file.open(QIODevice::WriteOnly | QIODevice::Text);

    /// write headers to stream
    stream << header0 << '\n' << header1 << '\n' << header2 << '\n' << header3 << '\n' << header4 << '\n' << header5 << '\n';

    /// close file
    PIPE[pipe].file.close();

	/// update file list
	updateFileList(QFileInfo(PIPE[pipe].file).fileName(), sn, pipe);
}


void
MainWindow::
setProductAndCalibrationMode()
{
   	/// product
   	LOOP.isEEA = ui->radioButton->isChecked();  
    
	/// mode
   	if (ui->radioButton_3->isChecked()) LOOP.mode = HIGH;
   	else if (ui->radioButton_4->isChecked()) LOOP.mode = FULL;
   	else if (ui->radioButton_5->isChecked()) LOOP.mode = MID;
   	else if (ui->radioButton_6->isChecked()) LOOP.mode = LOW;

	/// set file extensions
	if (LOOP.mode == HIGH) 
	{
		LOOP.filExt = ".HCI";
		LOOP.calExt = ".HCI";
		LOOP.adjExt = ".HCI";
		LOOP.rolExt = ".HCR";
	}
	else if (LOOP.mode == FULL) 
	{
		LOOP.filExt = ".FCI";
		LOOP.calExt = ".FCI";
		LOOP.adjExt = ".FCI";
		LOOP.rolExt = ".FCR";
	}
	else if (LOOP.mode == MID) 
	{
		LOOP.filExt = ".MCI";
		LOOP.calExt = ".MCI";
		LOOP.adjExt = ".MCI";
		LOOP.rolExt = ".MCR";
	}
	else if (LOOP.mode == LOW) 
	{
		LOOP.filExt = ".LCT";
		LOOP.calExt = ".LCI";
		LOOP.adjExt = ".LCI";
		LOOP.rolExt = ".LCR";
	}
}


bool
MainWindow::
validateSerialNumber(modbus_t * serialModbus)
{
	for (int pipe=0; pipe<3; pipe++)
	{
		if (PIPE[pipe].status == ENABLED)
		{
			uint8_t dest[1024];
    		uint16_t * dest16 = (uint16_t *) dest;
    		int ret = -1;
    		bool is16Bit = false;
    		bool writeAccess = false;
    		const QString funcType = descriptiveDataTypeName( FUNC_READ_INT );

			/// set slave
   			memset( dest, 0, 1024 );
   			modbus_set_slave( serialModbus, PIPE[pipe].slave->text().toInt());

			/// unlock FCT registers
   			modbus_write_register(serialModbus,999,1);

   			/// read pipe serial number
  			sendCalibrationRequest(FLOAT_R, serialModbus, FUNC_READ_INT, LOOP.ID_SN_PIPE, BYTE_READ_INT, ret, dest, dest16, is16Bit, writeAccess, funcType);

			/// verify if serial number matches with pipe
   			if (*dest16 != PIPE[pipe].slave->text().toInt()) 
			{
   				informUser(QString("LOOP ")+QString::number(LOOP.loopNumber),QString("LOOP ")+QString::number(LOOP.loopNumber)+QString(" PIPE ")+QString::number(pipe + 1),"Invalid Serial Port!");

				/// lock FCT registers
   				modbus_write_register(serialModbus,999,0);

				/// cancel calibration
				LOOP.isCal = false;
				PIPE[pipe].status = DISABLED;

				/// lock FCT registers
   				modbus_write_register(serialModbus,999,0);

				/// sn is valid but serial port invalid then it's an error 
				return false;
			}
			else 
			{
				PIPE[pipe].status = ENABLED;
   				PIPE[pipe].checkBox->setChecked(true);
			}

			/// lock FCT registers
   			modbus_write_register(serialModbus,999,0);
		}
		else
		{
			PIPE[pipe].status = DISABLED;
   			PIPE[pipe].checkBox->setChecked(false);
		}
	}

    return true;
}


bool
MainWindow::
prepareCalibration()
{
	/// check existence of pipe ids 
	int p;
	for (p=0; p<3; p++) (PIPE[p].slave->text().isEmpty()) ? PIPE[p].status = DISABLED : PIPE[p].status = ENABLED;

	if (PIPE[0].slave->text().isEmpty() && PIPE[1].slave->text().isEmpty() && PIPE[2].slave->text().isEmpty()) 
	{
		onActionStop();
       	informUser(QString("LOOP ")+QString::number(LOOP.loopNumber),QString("LOOP ")+QString::number(LOOP.loopNumber),"No valid serial number exists!");
		return false;
	}

	/// check loop volume
	if (LOOP.loopVolume->text().toDouble() < 1)
	{
		onActionStop();
       	informUser(QString("LOOP ")+QString::number(0 + 1),QString("LOOP ")+QString::number(0 + 1),"No valid loop volume exists!");
		return false;
	}

	/// check serial port
   	if (LOOP.modbus == NULL)
   	{
       	/// update tab icon
		onActionStop();
       	informUser(QString("LOOP "),QString("LOOP "),"Bad Serial Connection");
       	return false;
   	}

	/// check id
	if (!validateSerialNumber(LOOP.serialModbus))
	{
		/// stop calibration
		onActionStop();
       	return false;
	}

	/// reset initial triggers
	LOOP.isAMB = true;
	LOOP.isMinRef = true;
	LOOP.isMaxRef = true;
	LOOP.isInjection = true;

	/// set product & calibration mode & file extension
	setProductAndCalibrationMode();

	/// pipe specific vars
	for (int pipe = 0; pipe < 3; pipe++)
	{
   		/// start calibration
   		updatePipeStability(F_BAR, pipe, 0);
   		updatePipeStability(T_BAR, pipe, 0);

		PIPE[pipe].tempStability = 0;
   		PIPE[pipe].freqStability = 0;
   		PIPE[pipe].etimer->restart();
   		PIPE[pipe].mainDirPath = m_mainServer+LOOP.mode+QString::number(((int)(PIPE[pipe].slave->text().toInt()/100))*100).append("'s").append("\\")+LOOP.mode.split("\\").at(2)+PIPE[pipe].slave->text(); 

		/// set AMB_ filename
		if (PIPE[pipe].status == ENABLED) prepareForNextFile(pipe, QString("AMB").append("_").append(QString::number(LOOP.minRefTemp)).append(LOOP.filExt));

		if (ui->radioButton_7->isChecked()) PIPE[pipe].osc = 1;
   		else if (ui->radioButton_8->isChecked()) PIPE[pipe].osc = 2;
   		else if (ui->radioButton_9->isChecked()) PIPE[pipe].osc = 3;
   		else PIPE[pipe].osc = 4;
	}

	return true;
}


void
MainWindow::
onCalibrationButtonPressed()
{
	LOOP.isCal = !LOOP.isCal;

	if (!LOOP.isCal) 
	{
		onActionStop();
		return;
	}

    /// scan calibration variables
    if (!prepareCalibration()) return;
	
    /// LOOP calibration on
    if (LOOP.isCal)
    {
		LOOP.runMode = TEMP_RUN_MODE;

		for (int pipe = 0; pipe < 3; pipe++)
		{
			if (PIPE[pipe].status == ENABLED)
			{
				PIPE[pipe].isStartFreq = true;
      			QDir dir;
       			int fileCounter = 2;

       			/// create file directory "g:/FULLCUT/FC" + "8756"
       			if (!dir.exists(PIPE[pipe].mainDirPath)) dir.mkpath(PIPE[pipe].mainDirPath);
       			else
       			{
           			while (1)
           			{
               			if (!dir.exists(PIPE[pipe].mainDirPath+"_"+QString::number(fileCounter))) 
               			{
                   			PIPE[pipe].mainDirPath += "_"+QString::number(fileCounter);
                   			dir.mkpath(PIPE[pipe].mainDirPath);

							if ((LOOP.mode == LOW) || !LOOP.isEEA)
							{
								prepareForNextFile(pipe, QString("AMB").append("_").append(QString::number(LOOP.minRefTemp)).append(LOOP.filExt));
							}
                   			break;
               			}
               			else fileCounter++;
           			}
       			}
			}
    	}

		/// start calibration
		while (LOOP.isCal)
		{
       		if (LOOP.runMode == TEMP_RUN_MODE) runTempRun();
			else if (LOOP.runMode == INJECTION_MODE) runInjection();
			else if (!LOOP.isCal) return;

			delay(LOOP.xDelay);
		}
	}
	else
    {
		onActionStop();
      	informUser(QString("LOOP ")+QString::number(LOOP.loopNumber),QString("                                    "),"Calibration cancelled!");
	}
}		


void
MainWindow::
stopCalibration()
{
	int i;

    LOOP.isCal = false;
	LOOP.isMaster = false;
    LOOP.isEEA = false;
    LOOP.isAMB = false;
    LOOP.isMinRef = false;
    LOOP.isMaxRef = false;
    LOOP.isInjection = false;

	for (i=0;i<3;i++)
	{
		PIPE[i].freqProgress->setValue(0);
		PIPE[i].tempProgress->setValue(0);
		PIPE[i].status = DISABLED;
		PIPE[i].checkBox->setChecked(false);
		PIPE[i].isStartFreq = true;
		PIPE[i].tempStability = 0;
		PIPE[i].freqtability = 0;
	}

	return;
}


void
MainWindow::
readPipe(const int pipe, const bool isStability)
{
    uint8_t dest[1024];
    uint16_t * dest16 = (uint16_t *) dest;
    int ret = -1;
    bool is16Bit = false;
    bool writeAccess = false;
    const QString funcType = descriptiveDataTypeName( FUNC_READ_FLOAT );

    /// reset connection
    memset( dest, 0, 1024 );
    modbus_set_slave( LOOP.serialModbus, PIPE[pipe].slave->text().toInt() );
 
    /// get temperature
    PIPE[pipe].temperature = sendCalibrationRequest(FLOAT_R, LOOP.serialModbus, FUNC_READ_FLOAT, LOOP.ID_TEMPERATURE, BYTE_READ_FLOAT, ret, dest, dest16, is16Bit, writeAccess, funcType);
	//delay(SLEEP_TIME);

	if (isStability)
	{
		/// check temp stability
    	if (PIPE[pipe].tempStability < 5) 
		{
			if (abs(PIPE[pipe].temperature - PIPE[pipe].temperature_prev) <= LOOP.zTemp) PIPE[pipe].tempStability++;
			else PIPE[pipe].tempStability = 0;

			PIPE[pipe].temperature_prev = PIPE[pipe].temperature;
		}
		else PIPE[pipe].tempStability = 5;

    	if (!isModbusTransmissionFailed) updatePipeStability(T_BAR, pipe, PIPE[pipe].tempStability*20);
	}
	else
	{
	    PIPE[pipe].freqStability = 0;
   		PIPE[pipe].tempStability = 0;

		updatePipeStability(F_BAR, pipe, 0);
		updatePipeStability(T_BAR, pipe, 0);
	}

    /// get frequency
    PIPE[pipe].frequency = sendCalibrationRequest(FLOAT_R, LOOP.serialModbus, FUNC_READ_FLOAT, LOOP.ID_FREQ, BYTE_READ_FLOAT, ret, dest, dest16, is16Bit, writeAccess, funcType);
	//delay(SLEEP_TIME);

	if (isStability)
	{
		/// check freq stability
		if (PIPE[pipe].freqStability < 5) 
		{
       		if (abs(PIPE[pipe].frequency - PIPE[pipe].frequency_prev) <= LOOP.yFreq) PIPE[pipe].freqStability++;
			else PIPE[pipe].freqStability = 0;

       		PIPE[pipe].frequency_prev = PIPE[pipe].frequency;
		}
		else PIPE[pipe].freqStability = 5;

    	if (!isModbusTransmissionFailed) updatePipeStability(F_BAR, pipe, PIPE[pipe].freqStability*20);
	}
	else
	{
	    PIPE[pipe].freqStability = 0;
   		PIPE[pipe].tempStability = 0;

		updatePipeStability(F_BAR, pipe, 0);
		updatePipeStability(T_BAR, pipe, 0);
	}

    /// get oil_rp 
    PIPE[pipe].oilrp = sendCalibrationRequest(FLOAT_R, LOOP.serialModbus, FUNC_READ_FLOAT, LOOP.ID_OIL_RP, BYTE_READ_FLOAT, ret, dest, dest16, is16Bit, writeAccess, funcType);
	//delay(SLEEP_TIME);

    /// get measured ai
    PIPE[pipe].measai = sendCalibrationRequest(FLOAT_R, LOOP.serialModbus, FUNC_READ_FLOAT, RAZ_MEAS_AI, BYTE_READ_FLOAT, ret, dest, dest16, is16Bit, writeAccess, funcType);
	//delay(SLEEP_TIME);

    /// get trimmed ai
    PIPE[pipe].trimai = sendCalibrationRequest(FLOAT_R, LOOP.serialModbus, FUNC_READ_FLOAT, RAZ_TRIM_AI, BYTE_READ_FLOAT, ret, dest, dest16, is16Bit, writeAccess, funcType);
	//delay(SLEEP_TIME);

    /// update pipe reading
	if (PIPE[pipe].status == ENABLED) updatePipeStatus(pipe, LOOP.watercut, PIPE[pipe].frequency_start, PIPE[pipe].frequency, PIPE[pipe].temperature, PIPE[pipe].oilrp);
}


void
MainWindow::
readMasterPipe()
{
    uint8_t dest[1024];
    uint16_t * dest16 = (uint16_t *) dest;
    int ret = -1;
    bool is16Bit = false;
    bool writeAccess = false;
    const QString funcType = descriptiveDataTypeName( FUNC_READ_FLOAT );
	double val = 0;

    /// reset connection
    memset( dest, 0, 1024 );
    modbus_set_slave( LOOP.serialModbus, CONTROLBOX_SLAVE);
 
    /// get watercut
    val = sendCalibrationRequest(FLOAT_R, LOOP.serialModbus, FUNC_READ_FLOAT, LOOP.ID_MASTER_WATERCUT, BYTE_READ_FLOAT, ret, dest, dest16, is16Bit, writeAccess, funcType);
	//delay(SLEEP_TIME);
	if (!isModbusTransmissionFailed) 
	{
		LOOP.masterWatercut = val;	
		ui->lineEdit_20->setText(QString::number(val));
	}

	/// get salinity
    val = sendCalibrationRequest(FLOAT_R, LOOP.serialModbus, FUNC_READ_FLOAT, LOOP.ID_MASTER_SALINITY, BYTE_READ_FLOAT, ret, dest, dest16, is16Bit, writeAccess, funcType);
	//delay(SLEEP_TIME);
	if (!isModbusTransmissionFailed) 
	{
		LOOP.masterSalinity = val;
		ui->lineEdit_22->setText(QString::number(val));
	}

	/// get oil adjust 
    val = sendCalibrationRequest(FLOAT_R, LOOP.serialModbus, FUNC_READ_FLOAT, LOOP.ID_MASTER_OIL_ADJUST, BYTE_READ_FLOAT, ret, dest, dest16, is16Bit, writeAccess, funcType);
	//delay(SLEEP_TIME);
	if (!isModbusTransmissionFailed) 
	{
		LOOP.masterOilAdj = val;
		ui->lineEdit_21->setText(QString::number(val));
	}
   
	/// master oil rp 
	val = sendCalibrationRequest(FLOAT_R, LOOP.serialModbus, FUNC_READ_FLOAT, LOOP.ID_MASTER_OIL_RP, BYTE_READ_FLOAT, ret, dest, dest16, is16Bit, writeAccess, funcType);
	//delay(SLEEP_TIME);
	if (!isModbusTransmissionFailed) 
	{
		LOOP.masterOilRp = val;
	}

	/// master temp
	val = sendCalibrationRequest(FLOAT_R, LOOP.serialModbus, FUNC_READ_FLOAT, LOOP.ID_MASTER_TEMPERATURE, BYTE_READ_FLOAT, ret, dest, dest16, is16Bit, writeAccess, funcType);
	//delay(SLEEP_TIME);
	if (!isModbusTransmissionFailed) 
	{
		LOOP.masterTemp = val;
		ui->lineEdit_29->setText(QString::number(val));
	}

	/// master freq
	val = sendCalibrationRequest(FLOAT_R, LOOP.serialModbus, FUNC_READ_FLOAT, LOOP.ID_MASTER_FREQ, BYTE_READ_FLOAT, ret, dest, dest16, is16Bit, writeAccess, funcType);
	//delay(SLEEP_TIME);
	if (!isModbusTransmissionFailed) 
	{
		LOOP.masterFreq = val;
		ui->lineEdit_30->setText(QString::number(val));
	}

	/// master phase
	val = sendCalibrationRequest(FLOAT_R, LOOP.serialModbus, FUNC_READ_FLOAT, LOOP.ID_MASTER_PHASE, BYTE_READ_FLOAT, ret, dest, dest16, is16Bit, writeAccess, funcType);
	//delay(SLEEP_TIME);
	if (!isModbusTransmissionFailed) 
	{
		LOOP.masterPhase = val;
		if (LOOP.masterPhase == PHASE_OIL ) ui->lineEdit_31->setText("OIL PHASE");
		else if (LOOP.masterPhase == PHASE_WATER) ui->lineEdit_31->setText("WATER PHASE");
		else ui->lineEdit_31->setText("ERROR");
	}
}


void
MainWindow::
runTempRun()
{
   	QString data_stream;

   	if (LOOP.isCal)
   	{
		readMasterPipe(); // read master pipe no matter what

		/// set point : minTemp 
   		if ((QFileInfo(PIPE[0].file).fileName() == QString("AMB").append("_").append(QString::number(LOOP.minRefTemp)).append(LOOP.filExt)) ||
       		(QFileInfo(PIPE[1].file).fileName() == QString("AMB").append("_").append(QString::number(LOOP.minRefTemp)).append(LOOP.filExt)) ||
       		(QFileInfo(PIPE[2].file).fileName() == QString("AMB").append("_").append(QString::number(LOOP.minRefTemp)).append(LOOP.filExt)))
   		{
			if (LOOP.isAMB)
			{
				LOOP.isAMB = false;
   				bool ok;

				/// popup questions 
   				LOOP.operatorName = QInputDialog::getText(this, QString("LOOP ")+QString::number(LOOP.loopNumber)+QString(" "),tr(qPrintable("Enter Operator's Name.")), QLineEdit::Normal," ", &ok);
   				LOOP.oilRunStart->setText(QInputDialog::getText(this, QString("LOOP ")+QString::number(LOOP.loopNumber)+QString(" "),tr(qPrintable("Enter Measured Initial Watercut.")), QLineEdit::Normal,"0.0", &ok));
				LOOP.watercut = LOOP.oilRunStart->text().toDouble();
   				if (!isUserInputYes(QString("LOOP ")+QString::number(LOOP.loopNumber),"Fill The Water Container To The Mark."))
				{
					onActionStop();
					return;
				}

				/// master pipe validation 
				if (LOOP.isMaster)
				{
					if (LOOP.masterWatercut > LOOP.masterMax) 
					{
						if (!isUserInputYes(QString("Master Pipe Raw watercut Value Is Greater Than ")+QString::number(LOOP.masterMax), "Do You Want To Continue?"))
						{
							onActionStop();
							return;
						}
					}
					if (LOOP.masterWatercut < LOOP.masterMin) 
					{
						if (!isUserInputYes(QString("Master Pipe Raw watercut Value Is Less Than ")+QString::number(LOOP.masterMin), "Do You Want To Continue?"))
						{
							onActionStop();
							return;
						}
					}
					if (abs(LOOP.masterWatercut - LOOP.oilRunStart->text().toDouble()) > LOOP.masterDelta) 
					{
						if (!isUserInputYes(QString("The difference between master watercut and measured initial watercut is greater than ")+QString::number(LOOP.masterDelta), "Do You Want To Continue?"))
						{
							onActionStop();
							return;
						}
					}
				}

				if (!isUserInputYes(QString("Set The Heat Exchanger Temperature"), QString::number(LOOP.minRefTemp).append("°C")))
				{
					onActionStop();
					return;
				}

				/// add a new file
				for (int pipe = 0; pipe < 3; pipe++)
				{
       	    		if (PIPE[pipe].status == ENABLED)
					{
						if (!QFileInfo(PIPE[pipe].file).exists()) createTempRunFile(PIPE[pipe].slave->text().toInt(), "AMB", QString::number(LOOP.minRefTemp), LOOP.saltStop->currentText(), pipe);
					}
				}
			}
           
			/// start reading values      
			for (int pipe = 0; pipe < 3; pipe++)
			{
	    		/// validate stability 
           		if ((PIPE[pipe].status == ENABLED) && PIPE[pipe].checkBox->isChecked() && ((PIPE[pipe].tempStability != 5) || (PIPE[pipe].freqStability != 5)))
           		{
					/// read data
					if (abs(LOOP.minRefTemp - PIPE[pipe].temperature) < 2.0) readPipe(pipe, STABILITY_CHECK); 
					else readPipe(pipe, NO_STABILITY_CHECK);
					data_stream = QString("%1 %2 %3 %4 %5 %6 %7 %8 %9 %10 %11 %12 %13 %14 %15 %16 %17 %18 %19 %20").arg(PIPE[pipe].etimer->elapsed()/1000, 9, 'g', -1, ' ').arg(LOOP.watercut,7,'f',2,' ').arg(PIPE[pipe].osc, 4, 'g', -1, ' ').arg(" INT").arg(1, 7, 'g', -1, ' ').arg(PIPE[pipe].frequency,9,'f',3,' ').arg(0,8,'f',2,' ').arg(PIPE[pipe].oilrp,9,'f',2,' ').arg(PIPE[pipe].temperature,11,'f',2,' ').arg(0,8,'f',2,' ').arg(PIPE[pipe].measai,12,'f',2,' ').arg(PIPE[pipe].trimai,12,'f',2,' ').arg(0,10,'f',2,' ').arg(LOOP.masterTemp, 11,'f',2,' ').arg(LOOP.masterOilAdj, 11,'f',2,' ').arg(LOOP.masterFreq, 11,'f',2,' ').arg(LOOP.masterWatercut, 11,'f',2,' ').arg(LOOP.masterOilRp, 11,'f',2,' ').arg(LOOP.masterPhase, 6,'f',1,' ').arg(0,8,'f',2,' ');

					/// write to file
               		writeToCalFile(pipe, data_stream);
           		}
           		else 
           		{
					if (PIPE[pipe].status == ENABLED) 
					{
						PIPE[pipe].status = DONE; /// a pipe stops if it reaches stability
               			prepareForNextFile(pipe, QString::number(LOOP.minRefTemp).append("_").append(QString::number(LOOP.maxRefTemp)).append(LOOP.filExt));
					}
           		}
			}
		}
  		else if ((QFileInfo(PIPE[0].file).fileName() == QString::number(LOOP.minRefTemp).append("_").append(QString::number(LOOP.maxRefTemp)).append(LOOP.filExt)) ||
      			 (QFileInfo(PIPE[1].file).fileName() == QString::number(LOOP.minRefTemp).append("_").append(QString::number(LOOP.maxRefTemp)).append(LOOP.filExt)) ||
       			 (QFileInfo(PIPE[2].file).fileName() == QString::number(LOOP.minRefTemp).append("_").append(QString::number(LOOP.maxRefTemp)).append(LOOP.filExt)))
   	 	{
			if (LOOP.isMinRef)
			{
				LOOP.isMinRef = false;

				if (!isUserInputYes(QString("Set The Heat Exchanger Temperature"), QString::number(LOOP.maxRefTemp).append("°C")))
				{
					onActionStop();
					return;
				}

				/// add a new file
				for (int pipe = 0; pipe < 3; pipe++)
				{
       	    		if (PIPE[pipe].status == DONE) 
					{
						PIPE[pipe].status = ENABLED;
						if (!QFileInfo(PIPE[pipe].file).exists()) createTempRunFile(PIPE[pipe].slave->text().toInt(), QString::number(LOOP.minRefTemp), QString::number(LOOP.maxRefTemp), LOOP.saltStop->currentText(), pipe);
					}
				}
			}

			for (int pipe = 0; pipe < 3; pipe++)
			{
           		if ((PIPE[pipe].status == ENABLED) && PIPE[pipe].checkBox->isChecked() && ((PIPE[pipe].tempStability != 5) || (PIPE[pipe].freqStability != 5)))
           		{
					/// read data
					(abs(LOOP.maxRefTemp - PIPE[pipe].temperature) < 2.0) ? readPipe(pipe, STABILITY_CHECK) : readPipe(pipe, NO_STABILITY_CHECK);
					LOOP.watercut = LOOP.oilRunStart->text().toDouble();
					data_stream = QString("%1 %2 %3 %4 %5 %6 %7 %8 %9 %10 %11 %12 %13 %14 %15 %16 %17 %18 %19 %20").arg(PIPE[pipe].etimer->elapsed()/1000, 9, 'g', -1, ' ').arg(LOOP.watercut,7,'f',2,' ').arg(PIPE[pipe].osc, 4, 'g', -1, ' ').arg(" INT").arg(1, 7, 'g', -1, ' ').arg(PIPE[pipe].frequency,9,'f',3,' ').arg(0,8,'f',2,' ').arg(PIPE[pipe].oilrp,9,'f',2,' ').arg(PIPE[pipe].temperature,11,'f',2,' ').arg(0,8,'f',2,' ').arg(PIPE[pipe].measai,12,'f',2,' ').arg(PIPE[pipe].trimai,12,'f',2,' ').arg(0,10,'f',2,' ').arg(LOOP.masterTemp, 11,'f',2,' ').arg(LOOP.masterOilAdj, 11,'f',2,' ').arg(LOOP.masterFreq, 11,'f',2,' ').arg(LOOP.masterWatercut, 11,'f',2,' ').arg(LOOP.masterOilRp, 11,'f',2,' ').arg(LOOP.masterPhase, 6,'f',1,' ').arg(0,8,'f',2,' ');

					/// write to file
   	            	writeToCalFile(pipe, data_stream);
   	        	}
   	        	else 
   	        	{
					if (PIPE[pipe].status == ENABLED) 
					{
						PIPE[pipe].status = DONE; /// a pipe stops if it reaches stability
   	           			prepareForNextFile(pipe, QString::number(LOOP.maxRefTemp).append("_").append(QString::number(LOOP.injectionTemp)).append(LOOP.filExt));
					}
   	        	}
			}
		}
   		else if ((QFileInfo(PIPE[0].file).fileName() == QString::number(LOOP.maxRefTemp).append("_").append(QString::number(LOOP.injectionTemp)).append(LOOP.filExt)) ||
       		 	(QFileInfo(PIPE[1].file).fileName() == QString::number(LOOP.maxRefTemp).append("_").append(QString::number(LOOP.injectionTemp)).append(LOOP.filExt)) ||
       		 	(QFileInfo(PIPE[2].file).fileName() == QString::number(LOOP.maxRefTemp).append("_").append(QString::number(LOOP.injectionTemp)).append(LOOP.filExt)))
       	{
			if (LOOP.isMaxRef)
			{
				LOOP.isMaxRef = false;

				if (!isUserInputYes(QString("Set The Heat Exchanger Temperature"), QString::number(LOOP.injectionTemp).append("°C")))
				{
					onActionStop();
					return;
				}

				/// add a new file
				for (int pipe = 0; pipe < 3; pipe++)
				{
       	    		if (PIPE[pipe].status == DONE) 
					{
						PIPE[pipe].status = ENABLED;
						if (!QFileInfo(PIPE[pipe].file).exists()) createTempRunFile(PIPE[pipe].slave->text().toInt(), QString::number(LOOP.maxRefTemp), QString::number(LOOP.injectionTemp), LOOP.saltStop->currentText(), pipe);
					}
				}
			}

			for (int pipe = 0; pipe < 3; pipe++)
			{
				if ((PIPE[pipe].status == ENABLED) && PIPE[pipe].checkBox->isChecked() && ((PIPE[pipe].tempStability != 5) || (PIPE[pipe].freqStability != 5)))
       			{
					/// read data
					(abs(LOOP.injectionTemp - PIPE[pipe].temperature) < 2.0) ? readPipe(pipe, STABILITY_CHECK) : readPipe(pipe, NO_STABILITY_CHECK);
					data_stream = QString("%1 %2 %3 %4 %5 %6 %7 %8 %9 %10 %11 %12 %13 %14 %15 %16 %17 %18 %19 %20").arg(PIPE[pipe].etimer->elapsed()/1000, 9, 'g', -1, ' ').arg(LOOP.watercut,7,'f',2,' ').arg(PIPE[pipe].osc, 4, 'g', -1, ' ').arg(" INT").arg(1, 7, 'g', -1, ' ').arg(PIPE[pipe].frequency,9,'f',3,' ').arg(0,8,'f',2,' ').arg(PIPE[pipe].oilrp,9,'f',2,' ').arg(PIPE[pipe].temperature,11,'f',2,' ').arg(0,8,'f',2,' ').arg(PIPE[pipe].measai,12,'f',2,' ').arg(PIPE[pipe].trimai,12,'f',2,' ').arg(0,10,'f',2,' ').arg(LOOP.masterTemp, 11,'f',2,' ').arg(LOOP.masterOilAdj, 11,'f',2,' ').arg(LOOP.masterFreq, 11,'f',2,' ').arg(LOOP.masterWatercut, 11,'f',2,' ').arg(LOOP.masterOilRp, 11,'f',2,' ').arg(LOOP.masterPhase, 6,'f',1,' ').arg(0,8,'f',2,' ');

					/// write to file
           			writeToCalFile(pipe, data_stream);
       			}
       			else 
       			{
					if (PIPE[pipe].status == ENABLED) 
					{
						PIPE[pipe].status = DONE; // pipe temprun stops at reaching stability
           				if (LOOP.mode == LOW) prepareForNextFile(pipe,"CALIBRAT.LCI");
           				else if (LOOP.mode == MID) prepareForNextFile(pipe,QString("OIL__").append(QString::number(LOOP.injectionTemp)).append(".MCI"));
						readPipe(pipe, STABILITY_CHECK);
						PIPE[pipe].frequency_start = PIPE[pipe].frequency;

						/// check condition for injection
						if (LOOP.isCal && (PIPE[0].checkBox->isChecked() || PIPE[1].checkBox->isChecked() || PIPE[2].checkBox->isChecked())) 
						{
							if ((PIPE[0].status != ENABLED) && (PIPE[1].status != ENABLED) && (PIPE[2].status != ENABLED)) 
							{
								LOOP.runMode = INJECTION_MODE;
								return;
							}
						}
					}
       			}
   			}
		}
	}
}


void
MainWindow::
runInjection()
{
	static double injectionTime = 0;
	static double totalInjectionTime = 0;
	static double accumulatedInjectionTime_prev = 0;
	static double correctedWatercut = 0;
	static double measuredWatercut = 0;
   	static double totalInjectionVolume = 0;
   	QString data_stream;

	readMasterPipe(); /// read master pipe no matter what

	if ((QFileInfo(PIPE[0].file).fileName() == "CALIBRAT.LCI") || 
   	 	(QFileInfo(PIPE[1].file).fileName() == "CALIBRAT.LCI") || 
        (QFileInfo(PIPE[2].file).fileName() == "CALIBRAT.LCI") ||
        (QFileInfo(PIPE[0].file).fileName() == QString("OIL__").append(QString::number(LOOP.injectionTemp)).append(".MCI")) ||
        (QFileInfo(PIPE[1].file).fileName() == QString("OIL__").append(QString::number(LOOP.injectionTemp)).append(".MCI")) ||
        (QFileInfo(PIPE[2].file).fileName() == QString("OIL__").append(QString::number(LOOP.injectionTemp)).append(".MCI")) ||
		(QFileInfo(PIPE[0].file).fileName() == QString("OIL__").append(QString::number(LOOP.injectionTemp)).append(".HCI")) ||
        (QFileInfo(PIPE[1].file).fileName() == QString("OIL__").append(QString::number(LOOP.injectionTemp)).append(".HCI")) ||
        (QFileInfo(PIPE[2].file).fileName() == QString("OIL__").append(QString::number(LOOP.injectionTemp)).append(".HCI")) ||
		(QFileInfo(PIPE[0].file).fileName() == QString("OIL__").append(QString::number(LOOP.injectionTemp)).append(".FCI")) ||
        (QFileInfo(PIPE[1].file).fileName() == QString("OIL__").append(QString::number(LOOP.injectionTemp)).append(".FCI")) ||
        (QFileInfo(PIPE[2].file).fileName() == QString("OIL__").append(QString::number(LOOP.injectionTemp)).append(".FCI")))
	{   
		if (LOOP.isInjection)
		{
			bool ok;
   			if (!isUserInputYes(QString("LOOP ")+QString::number(LOOP.loopNumber),"Please make sure there is enough water in the water injection bucket"))
			{
				onActionStop();
				return;
			}

       		LOOP.oilRunStart->setText(QInputDialog::getText(this, QString("LOOP ")+QString::number(LOOP.loopNumber),tr(qPrintable("Enter Measured Initial Watercut")), QLineEdit::Normal,"0.0", &ok));
			LOOP.watercut = LOOP.oilRunStart->text().toDouble();
			LOOP.oilPhaseInjectCounter = 0;

			for (int pipe = 0; pipe < 3; pipe++) 
			{
				if (PIPE[pipe].status == DONE) 
				{
					PIPE[pipe].status = ENABLED;
					if (LOOP.mode == LOW) createInjectionFile(PIPE[pipe].slave->text().toInt(), pipe, LOOP.oilRunStart->text(), LOOP.oilRunStop->text(), 0, "CALIBRAT");
					else if (LOOP.mode == MID) createInjectionFile(PIPE[pipe].slave->text().toInt(), pipe, "OIL", LOOP.oilRunStop->text(), 0, "MID");
					updatePipeStatus(pipe, LOOP.watercut, PIPE[pipe].frequency, PIPE[pipe].frequency, PIPE[pipe].temperature, PIPE[pipe].oilrp);
				}
			}
		}

   		if ((LOOP.oilRunStop->text().toDouble() >= LOOP.watercut) && (LOOP.oilPhaseInjectCounter <= MAX_PHASE_CHECKING))
   		{
			updateLoopStatus(LOOP.watercut, 0, injectionTime, injectionTime*LOOP.injectionWaterPumpRate/60);

			//////////////////////////////
			//// READ DATA AND UPDATE FILE 
			//////////////////////////////
			for (int pipe = 0; pipe < 3; pipe++)
			{
				if ((PIPE[pipe].status == ENABLED) && PIPE[pipe].checkBox->isChecked())
				{
					/// read data
					readPipe(pipe, NO_STABILITY_CHECK);
					if (LOOP.isMaster) data_stream = QString("%1 %2 %3 %4 %5 %6 %7 %8 %9 %10 %11 %12 %13 %14 %15 %16 %17 %18 %19 %20").arg(PIPE[pipe].etimer->elapsed()/1000, 9, 'g', -1, ' ').arg(LOOP.masterWatercut,7,'f',2,' ').arg(PIPE[pipe].osc, 4, 'g', -1, ' ').arg(" INT").arg(1, 7, 'g', -1, ' ').arg(PIPE[pipe].frequency,9,'f',3,' ').arg(0,8,'f',2,' ').arg(PIPE[pipe].oilrp,9,'f',2,' ').arg(PIPE[pipe].temperature,11,'f',2,' ').arg(0,8,'f',2,' ').arg(PIPE[pipe].measai,12,'f',2,' ').arg(PIPE[pipe].trimai,12,'f',2,' ').arg(0,10,'f',2,' ').arg(LOOP.masterTemp, 11,'f',2,' ').arg(LOOP.masterOilAdj, 11,'f',2,' ').arg(LOOP.masterFreq, 11,'f',2,' ').arg(LOOP.masterWatercut, 11,'f',2,' ').arg(LOOP.masterOilRp, 11,'f',2,' ').arg(LOOP.masterPhase, 6,'f',1,' ').arg(0,8,'f',2,' ');
					else data_stream = QString("%1 %2 %3 %4 %5 %6 %7 %8 %9 %10 %11 %12 %13 %14 %15 %16 %17 %18 %19 %20").arg(PIPE[pipe].etimer->elapsed()/1000, 9, 'g', -1, ' ').arg(LOOP.watercut,7,'f',2,' ').arg(PIPE[pipe].osc, 4, 'g', -1, ' ').arg(" INT").arg(1, 7, 'g', -1, ' ').arg(PIPE[pipe].frequency,9,'f',3,' ').arg(0,8,'f',2,' ').arg(PIPE[pipe].oilrp,9,'f',2,' ').arg(PIPE[pipe].temperature,11,'f',2,' ').arg(0,8,'f',2,' ').arg(PIPE[pipe].measai,12,'f',2,' ').arg(PIPE[pipe].trimai,12,'f',2,' ').arg(0,10,'f',2,' ').arg(LOOP.masterTemp, 11,'f',2,' ').arg(LOOP.masterOilAdj, 11,'f',2,' ').arg(LOOP.masterFreq, 11,'f',2,' ').arg(LOOP.masterWatercut, 11,'f',2,' ').arg(LOOP.masterOilRp, 11,'f',2,' ').arg(LOOP.masterPhase, 6,'f',1,' ').arg(0,8,'f',2,' ');

					/// write to calibration file
           			writeToCalFile(pipe, data_stream);

					/// reset LOOP.watercut and disable initialization
					if (LOOP.isInjection)
					{
						LOOP.watercut = 0; 
						LOOP.isInjection = false;
					}
				}
			}

			///////////////////////////////////////
			//// INJECT WATER IN MASTER PIPE MODE
			///////////////////////////////////////
			if (LOOP.isMaster)
			{
				int masterInjectionStartTime = PIPE[0].etimer->elapsed()/1000;
				
				/// start injection	upon master pipe phase
				if (LOOP.masterPhase == PHASE_OIL) 
				{
					inject(COIL_WATER_PUMP,true);
					LOOP.oilPhaseInjectCounter = 0;
				}
				else 
				{
					inject(COIL_WATER_PUMP,false);
					LOOP.oilPhaseInjectCounter++;
				
					if (LOOP.oilPhaseInjectCounter > MAX_PHASE_CHECKING)
					{
      					if (LOOP.masterPhase == PHASE_WATER) informUser("Master Piple Is In Water Phase Now", QString("Watercut ").append(QString::number(LOOP.masterWatercut)).append(" %"));
      					else informUser("Master Piple Is ERROR Phase",QString("Phase Value Is ").append(QString::number(LOOP.masterPhase)));
					}
				}

				while (LOOP.masterWatercut < LOOP.watercut)
				{
					int masterInjectionTime = PIPE[0].etimer->elapsed()/1000;

					/// validate injection time
					if ((masterInjectionTime - masterInjectionStartTime) > LOOP.maxInjectionWater)
					{
						/// stop water injection?
   						if (!isUserInputYes(QString("Injection Time ")+QString::number(masterInjectionTime - masterInjectionStartTime)+QString(" Is Greater Than Max Water Injection Time ")+QString::number(LOOP.maxInjectionWater), "Do You Want To Continue?"))
						{
							inject(COIL_WATER_PUMP,false);
							onActionStop();
							return;
						}
					}

					readMasterPipe();
				}

				/// stop water injection
				inject(COIL_WATER_PUMP,false);
				int masterInjectionEndTime = PIPE[0].etimer->elapsed()/1000;
				totalInjectionVolume += (masterInjectionEndTime-masterInjectionStartTime)*LOOP.injectionWaterPumpRate/60;
				totalInjectionTime += masterInjectionEndTime-masterInjectionStartTime;

				/// set next watercut
				(LOOP.mode == LOW) ? LOOP.watercut += LOOP.intervalSmallPump : LOOP.watercut += LOOP.intervalBigPump; 
			}
			///////////////////////////////////////
			//// INJECT WATER IN PUMP RATE MODE 
			///////////////////////////////////////
			else
			{
				/// next injection time and update totalInjectionTime
				double accumulatedInjectionTime = -(LOOP.loopVolume->text().toDouble()/(LOOP.injectionWaterPumpRate/60))*log((1-(LOOP.watercut - LOOP.oilRunStart->text().toDouble())/100));
				injectionTime = accumulatedInjectionTime - accumulatedInjectionTime_prev;
				totalInjectionTime += injectionTime;
				totalInjectionVolume = totalInjectionTime*LOOP.injectionWaterPumpRate/60;
				accumulatedInjectionTime_prev = accumulatedInjectionTime;

				/// validate injection time
				if (injectionTime > LOOP.maxInjectionWater)
				{
					/// stop water injection?
   					if (!isUserInputYes(QString("Injection Time ")+QString::number(injectionTime)+QString(" Is Greater Than Max Water Injection Time ")+QString::number(LOOP.maxInjectionWater), "Do You Want To Continue?"))
					{
						onActionStop();
						return;
					}
				}

				/// inject water to the pipe for "injectionTime" seconds
				inject(COIL_WATER_PUMP,true);
				delay(injectionTime*1000);
				inject(COIL_WATER_PUMP,false);

				/// set next watercut
				(LOOP.mode == LOW) ? LOOP.watercut += LOOP.intervalSmallPump : LOOP.watercut += LOOP.intervalBigPump; 
			}
   		}
   		else 
   		{
   	   		/// enter measured watercut and injected volume
			bool ok;
   	   		measuredWatercut = QInputDialog::getDouble(this, QString("LOOP ")+QString::number(LOOP.loopNumber),tr(qPrintable("Enter Measured Watercut [%]")), 0.0, 0, 100, 2, &ok,Qt::WindowFlags(), 1);

			if (LOOP.isMaster) 
			{
				if (abs(LOOP.masterWatercut - measuredWatercut) > LOOP.masterDeltaFinal) 
				{
					/// stop water injection?
   					if (!isUserInputYes(QString("MASTER PIPE ")+QString::number(LOOP.loopNumber)+QString(" Difference between measured watercut and master watercut is greater than ")+QString::number(LOOP.masterDeltaFinal), "Do You Want To Continue?"))
					{
						onActionStop();
						return;
					}
				}
			}

			/// finalize and close
  			QDateTime currentDataTime = QDateTime::currentDateTime();
  			QString data_stream   = QString("Total injection time   = %1 s").arg(totalInjectionTime, 10, 'g', -1, ' ');
  			QString data_stream_2 = QString("Total injection volume = %1 mL").arg(totalInjectionVolume, 10, 'g', -1, ' ');
  			QString data_stream_3 = QString("Initial loop volume    = %1 mL").arg(LOOP.loopVolume->text().toDouble(), 10, 'g', -1, ' ');
  			QString data_stream_4 = QString("Measured watercut      = %1 %").arg(measuredWatercut, 10, 'f', 2, ' ');
  			QString data_stream_5 = QString("[%1] [%2]").arg(currentDataTime.toString()).arg(LOOP.operatorName);

			for (int pipe=0; pipe<3; pipe++)
			{
				if ((PIPE[pipe].status == ENABLED) && (QFileInfo(PIPE[pipe].file).exists()))
				{
					QTextStream stream(&PIPE[pipe].file);
   					PIPE[pipe].file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
   					stream << '\n' << '\n' << data_stream << '\n' << data_stream_2 << '\n' << data_stream_3 << '\n' << data_stream_4 << '\n' << data_stream_5 << '\n';
   					PIPE[pipe].file.close();
				}
			}

			////////////////////////////////////////////////////
			////////////////////////////////////////////////////
			/// if LOOP.mode is not LOWCUT then, it's done.
			////////////////////////////////////////////////////
			////////////////////////////////////////////////////
			if (LOOP.mode != LOW) 
			{
				/// finish calibration
      			informUser(QString("LOOP ")+QString::number(LOOP.loopNumber),("                                    "),"Calibration has finished successfully.");
				onActionStop();
				return;
			}
					
			////////////////////////////////////////////////////
			////////////////////////////////////////////////////
			/// ADJUSTED.LCI
			////////////////////////////////////////////////////
			////////////////////////////////////////////////////
		 	if (abs(totalInjectionVolume - (LOOP.injectionWaterPumpRate/60)*totalInjectionTime) > 0) 
			{
				for (int pipe=0; pipe<3; pipe++)
				{
					if ((PIPE[pipe].status == ENABLED) && PIPE[pipe].fileCalibrate.open(QIODevice::ReadOnly))
					{
						int i = 0;
						PIPE[pipe].fileAdjusted.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
   						QTextStream in(&PIPE[pipe].fileCalibrate);
   						QTextStream out(&PIPE[pipe].fileAdjusted);
   						while (!in.atEnd())
   						{
   							QString line = in.readLine();
							if (i > 6) 
							{
								/// process string and correct watercut
								QStringList data = line.split(" ");
								QString newData[data.size()];
								int k = 0;
								int x = 0;

								for (int j=0; j<data.size(); j++)
								{
									if (data[j] != "") 
									{	
										newData[k] = data[j];
										if (k==1) x = j; /// saving index for watercut
										if (k==12) 
										{
											correctedWatercut = (LOOP.oilRunStart->text().toDouble() + 100) - (100*exp(-(LOOP.injectionWaterPumpRate/60)*QString(newData[k]).toDouble()/LOOP.loopVolume->text().toDouble()));
											data[x] = QString("%1").arg(correctedWatercut,7,'f',2,' ');
										}
										k++;
									}
								}

								/// re-create a corrected data_stream
								line = QString("%1 %2 %3 %4 %5 %6 %7 %8 %9 %10 %11 %12 %13 %14 %15 %16 %17 %18 %19").arg(newData[0].toDouble(), 9, 'g', -1, ' ').arg(correctedWatercut,7,'f',2,' ').arg(newData[2].toDouble(), 4, 'g', -1, ' ').arg(" INT").arg(1, 7, 'g', -1, ' ').arg(newData[5].toDouble(),9,'f',3,' ').arg(0,8,'f',2,' ').arg(newData[7].toDouble(),9,'f',2,' ').arg(newData[8].toDouble(),11,'f',2,' ').arg(0,8,'f',2,' ').arg(newData[10].toDouble(),12,'f',2,' ').arg(newData[11].toDouble(),12,'f',2,' ').arg(0,10,'f',2,' ').arg(newData[13].toDouble(), 11,'f',2,' ').arg(newData[14].toDouble(), 11,'f',2,' ').arg(newData[15].toDouble(), 11,'f',2,' ').arg(newData[16].toDouble(), 11,'f',2,' ').arg(newData[17].toDouble(), 11,'f',2,' ').arg(0,12,'f',2,' ');
							}

							out << line << '\n';
							i++;
   						}

						PIPE[pipe].fileCalibrate.close();
 						PIPE[pipe].fileAdjusted.close();
					}
				}
			}

			/// finalize current file
			for (int pipe=0; pipe<3; pipe++)
			{
				if ((PIPE[pipe].status == ENABLED) && QFileInfo(PIPE[pipe].fileCalibrate).exists())
				{
					QTextStream stream(&PIPE[pipe].fileCalibrate);
   					PIPE[pipe].fileCalibrate.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
   					stream << '\n' << '\n' << data_stream << '\n' << data_stream_2 << '\n' << data_stream_3 << '\n' << data_stream_4 << '\n' << data_stream_5 << '\n';
   					PIPE[pipe].fileCalibrate.close();
				}

				if (QFileInfo(PIPE[pipe].fileAdjusted).exists())
				{
					QTextStream stream(&PIPE[pipe].fileAdjusted);
   					PIPE[pipe].fileAdjusted.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
   					stream << '\n' << '\n' << data_stream << '\n' << data_stream_2 << '\n' << data_stream_3 << '\n' << data_stream_4 << '\n' << data_stream_5 << '\n';
   					PIPE[pipe].fileAdjusted.close();
				}
			}

			/// prepare for ROLLOVER.LCR	
           	informUser(QString("LOOP ")+QString::number(LOOP.loopNumber),QString("                                    "),"Please Switch The Injection Pump.");
			LOOP.watercut += LOOP.intervalBigPump;
			for (int pipe = 0; pipe < 3; pipe++) 
			{
				if (PIPE[pipe].status == ENABLED) 
				{
					prepareForNextFile(pipe,"ROLLOVER.LCR");
					LOOP.watercut = correctedWatercut;
					PIPE[pipe].rolloverTracker = 0;
				}
			}
   		}
    }
    else if ((QFileInfo(PIPE[0].file).fileName() == "ROLLOVER.LCR") || 
          	 (QFileInfo(PIPE[1].file).fileName() == "ROLLOVER.LCR") || 
           	 (QFileInfo(PIPE[2].file).fileName() == "ROLLOVER.LCR")) // ROLLOVER.LCR 
	{   
		for (int pipe = 0; pipe < 3; pipe++)
		{
			if ((PIPE[pipe].status == ENABLED) && PIPE[pipe].checkBox->isChecked())
			{
				readPipe(pipe, NO_STABILITY_CHECK);

				if (PIPE[pipe].frequency < PIPE[pipe].frequency_prev)
				{
					if (PIPE[pipe].rolloverTracker > 2) PIPE[pipe].status == DONE;
					else PIPE[pipe].rolloverTracker++;
					PIPE[pipe].frequency_prev = PIPE[pipe].frequency;
				}
				else PIPE[pipe].rolloverTracker = 0;
					
				/// read data
				data_stream = QString("%1 %2 %3 %4 %5 %6 %7 %8 %9 %10 %11 %12 %13 %14 %15 %16 %17 %18 %19 %20").arg(PIPE[pipe].etimer->elapsed()/1000, 9, 'g', -1, ' ').arg(LOOP.watercut,7,'f',2,' ').arg(PIPE[pipe].osc, 4, 'g', -1, ' ').arg(" INT").arg(1, 7, 'g', -1, ' ').arg(PIPE[pipe].frequency,9,'f',3,' ').arg(0,8,'f',2,' ').arg(PIPE[pipe].oilrp,9,'f',2,' ').arg(PIPE[pipe].temperature,11,'f',2,' ').arg(0,8,'f',2,' ').arg(PIPE[pipe].measai,12,'f',2,' ').arg(PIPE[pipe].trimai,12,'f',2,' ').arg(0,10,'f',2,' ').arg(LOOP.masterTemp, 11,'f',2,' ').arg(LOOP.masterOilAdj, 11,'f',2,' ').arg(LOOP.masterFreq, 11,'f',2,' ').arg(LOOP.masterWatercut, 11,'f',2,' ').arg(LOOP.masterOilRp, 11,'f',2,' ').arg(LOOP.masterPhase, 6,'f',1,' ').arg(0,8,'f',2,' ');

   				/// create a new file if needed
  				if (!QFileInfo(PIPE[pipe].file).exists()) 
   				{
       				/// re-read data
					createInjectionFile(PIPE[pipe].slave->text().toInt(), pipe, "Rollver", QString::number(LOOP.watercut), 0, "ROLLOVER");
					updatePipeStatus(pipe, LOOP.watercut, PIPE[pipe].frequency, PIPE[pipe].frequency, PIPE[pipe].temperature, PIPE[pipe].oilrp);
   				}

   				writeToCalFile(pipe, data_stream);
			}
		}

		if (LOOP.isMaster)
		{
			int masterInjectionStartTime = PIPE[0].etimer->elapsed()/1000;

			while (LOOP.masterWatercut < LOOP.watercut)
			{
				int masterInjectionTime = PIPE[0].etimer->elapsed()/1000;

				inject(COIL_WATER_PUMP,true);

				/// validate injection time
				if ((masterInjectionTime - masterInjectionStartTime) > LOOP.maxInjectionWater)
				{
					/// stop water injection
					if (!isUserInputYes(QString("Injection Time")+QString::number(masterInjectionTime - masterInjectionStartTime)+QString(" Is Greater Than Max Water Injection Time ")+QString::number(LOOP.maxInjectionWater), "Do You Want To Continue?"))
					{
						inject(COIL_WATER_PUMP,false);
						onActionStop();
						return;
					}
				}
			}

			/// stop water injection
			inject(COIL_WATER_PUMP,false);

			/// set next watercut
			LOOP.watercut += LOOP.intervalBigPump;
		}
		else
		{
			/// next injection time and update totalInjectionTime
			double accumulatedInjectionTime = -(LOOP.loopVolume->text().toDouble()/(LOOP.injectionWaterPumpRate/60))*log((1-(LOOP.watercut - LOOP.oilRunStart->text().toDouble())/100));
			injectionTime = accumulatedInjectionTime - accumulatedInjectionTime_prev;
			totalInjectionTime += injectionTime;
			totalInjectionVolume = totalInjectionTime*LOOP.injectionWaterPumpRate/60;
			accumulatedInjectionTime_prev = accumulatedInjectionTime;

			/// validate injection time
			if (injectionTime > LOOP.maxInjectionWater)
			{
				if (!isUserInputYes(QString("Injection Time ")+QString::number(injectionTime)+QString(" Is Greater Than Max Water Injection Time ")+QString::number(LOOP.maxInjectionWater), "Do You Want To Continue?"))
				{
					onActionStop();
					return;
				}
			}

			/// inject water to the pipe for "injectionTime" seconds
			inject(COIL_WATER_PUMP,true);
			delay(injectionTime*1000);
			inject(COIL_WATER_PUMP,false);

			/// set next watercut
			LOOP.watercut += LOOP.intervalBigPump;
		}
	}
	else
	{
   		/// finalize and close
   		QDateTime currentDataTime = QDateTime::currentDateTime();
   		QString data_stream   = QString("Total injection time   = %1 s").arg(totalInjectionTime, 10, 'g', -1, ' ');
   		QString data_stream_2 = QString("Total injection volume = %1 mL").arg(totalInjectionVolume, 10, 'g', -1, ' ');
   		QString data_stream_3 = QString("Initial loop volume    = %1 mL").arg(LOOP.loopVolume->text().toDouble(), 10, 'g', -1, ' ');
  		QString data_stream_4 = QString("[%1] [%2]").arg(currentDataTime.toString()).arg(LOOP.operatorName);

		for (int pipe=0; pipe<3; pipe++)
		{
			if ((PIPE[pipe].status == DONE) && QFileInfo(PIPE[pipe].fileRollover).exists())
			{
				QTextStream stream(&PIPE[pipe].fileRollover);
   				PIPE[pipe].fileRollover.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
   				stream << '\n' << '\n' << data_stream << '\n' << data_stream_2 << '\n' << data_stream_3 << '\n' << data_stream_4 << '\n';
   				PIPE[pipe].fileRollover.close();
   				PIPE[pipe].checkBox->setChecked(false);
			}
		}

		/// finish calibration
		LOOP.runMode = STOP_MODE;
      	informUser(QString("LOOP ")+QString::number(LOOP.loopNumber),QString("                                    "),"Calibration has finished successfully.");
		onActionStop();
   }

	return;
}


void
MainWindow::
inject(const int coil,const bool value)
{
	uint8_t dest[1024];

	/// set slave
   	memset( dest, 0, 1024 );
   	modbus_set_slave(LOOP.serialModbus, CONTROLBOX_SLAVE);
    modbus_write_bit(LOOP.serialModbus,coil-ADDR_OFFSET, value );
}


void
MainWindow::
prepareForNextFile(const int pipe, const QString nextFileId)
{
    PIPE[pipe].file.setFileName(PIPE[pipe].mainDirPath+"\\"+nextFileId);
    PIPE[pipe].freqStability = 0;
    PIPE[pipe].tempStability = 0;

    updatePipeStability(F_BAR, pipe, 0);
    updatePipeStability(T_BAR, pipe, 0);
}


void
MainWindow::
writeToCalFile(int pipe, QString data_stream)
{
    /// write to file
    QTextStream stream(&PIPE[pipe].file);
    PIPE[pipe].file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    stream << data_stream << '\n' ;
    PIPE[pipe].file.close();
   	//delay(SLEEP_TIME);
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

    return (sign * pow(2, exponent) * (mantissa + 1.0));
}


void
MainWindow::
onUpdateRegisters(const bool isEEA)
{
    if (isEEA)
    {
        LOOP.ID_SN_PIPE = 1;
        LOOP.ID_WATERCUT = 11;
        LOOP.ID_SALINITY = 21;
        LOOP.ID_OIL_ADJUST = 23;
        LOOP.ID_TEMPERATURE = 15;
        LOOP.ID_WATER_ADJUST = 25;
        LOOP.ID_FREQ = 111;
        LOOP.ID_OIL_RP = 115;
    }
    else
    {
        LOOP.ID_SN_PIPE = 201;
        LOOP.ID_WATERCUT = 3;
        LOOP.ID_TEMPERATURE = 33; /// REG_TEMP_USER
        LOOP.ID_SALINITY = 9;
        LOOP.ID_OIL_ADJUST = 15;
        LOOP.ID_WATER_ADJUST = 17;
        LOOP.ID_FREQ = 19;
        LOOP.ID_OIL_RP = 61;
    }

	/// master pipe EEA
	LOOP.ID_MASTER_WATERCUT = 29;
	LOOP.ID_MASTER_TEMPERATURE = 15; 
	LOOP.ID_MASTER_SALINITY = 21;
	LOOP.ID_MASTER_OIL_ADJUST = 23;
	LOOP.ID_MASTER_OIL_RP = 115; 
	LOOP.ID_MASTER_FREQ = 111; 
	LOOP.ID_MASTER_PHASE = 17; 
/*
	/// master pipe RAZOR
	LOOP.ID_MASTER_WATERCUT = 3;
	LOOP.ID_MASTER_TEMPERATURE = 33; 
	LOOP.ID_MASTER_SALINITY = 9;
	LOOP.ID_MASTER_OIL_ADJUST = 15;
	LOOP.ID_MASTER_OIL_RP = 61; 
	LOOP.ID_MASTER_FREQ = 19; 
*/
}


void
MainWindow::
setInputValidator(void)
{
    /// serial number 
    ui->lineEdit_2->setValidator(serialNumberValidator);
    ui->lineEdit_7->setValidator(serialNumberValidator);
    ui->lineEdit_13->setValidator(serialNumberValidator);
}


void
MainWindow::
updatePipeStatus(const int pipe, const double watercut, const double startfreq, const double freq, const double temp, const double rp)
{
	if ((PIPE[pipe].status == ENABLED) && (!isModbusTransmissionFailed))
	{
    	PIPE[pipe].watercut->setText(QString::number(watercut));
    	if (PIPE[pipe].isStartFreq) PIPE[pipe].startFreq->setText(QString::number(freq));
    	PIPE[pipe].freq->setText(QString::number(freq));
    	PIPE[pipe].temp->setText(QString::number(temp));
    	PIPE[pipe].reflectedPower->setText(QString::number(rp));
		PIPE[pipe].isStartFreq = false;
	}
} 


void
MainWindow::
updateLoopStatus(const double watercut, const double salinity, const double injectionTime, const double injectionVol)
{
	if (!isModbusTransmissionFailed)
	{
		ui->lineEdit_26->setText(QString::number(watercut));
		ui->lineEdit_25->setText(QString::number(salinity));
		ui->lineEdit_24->setText(QString::number(injectionTime));
		ui->lineEdit_23->setText(QString::number(injectionVol));
	}
}
