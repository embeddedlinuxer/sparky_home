#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QSplineSeries>
#include <QtCharts/QValueAxis>
#include <QtCharts/QCategoryAxis>
#include "modbus.h"
#include "ui_about.h"
#include "modbus-rtu.h"
#include "modbus.h"
#include "qcgaugewidget.h"

#define RAZ_REG_WATERCUT 
#define RAZ_REG_WATERCUT 
#define MAX_PIPE 18
#define RAZ true
#define EEA false

QT_CHARTS_USE_NAMESPACE

class AboutDialog : public QDialog, public Ui::AboutDialog
{
public:
    AboutDialog( QWidget * _parent ) :
        QDialog( _parent )
    {
        setupUi( this );
        aboutTextLabel->setText(aboutTextLabel->text().arg( "0.3.0" ) );
    }
};


namespace Ui
{
    class MainWindowClass;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow( QWidget * parent = 0 );
    ~MainWindow();

    modbus_t * m_serialModbus;
    modbus_t * m_serialModbus_2;
    modbus_t * m_serialModbus_3;
    modbus_t * m_serialModbus_4;
    modbus_t * m_serialModbus_5;
    modbus_t * m_serialModbus_6;

    modbus_t*  modbus() { return m_serialModbus; }
    modbus_t*  modbus_2() { return m_serialModbus_2; }
    modbus_t*  modbus_3() { return m_serialModbus_3; }
    modbus_t*  modbus_4() { return m_serialModbus_4; }
    modbus_t*  modbus_5() { return m_serialModbus_5; }
    modbus_t*  modbus_6() { return m_serialModbus_6; }

    int setupModbusPort();
    int setupModbusPort_2();
    int setupModbusPort_3();
    int setupModbusPort_4();
    int setupModbusPort_5();
    int setupModbusPort_6();

    void changeModbusInterface(const QString &port, char parity);
    void changeModbusInterface_2(const QString &port, char parity);
    void changeModbusInterface_3(const QString &port, char parity);
    void changeModbusInterface_4(const QString &port, char parity);
    void changeModbusInterface_5(const QString &port, char parity);
    void changeModbusInterface_6(const QString &port, char parity);

    void releaseSerialModbus();
    void releaseSerialModbus_2();
    void releaseSerialModbus_3();
    void releaseSerialModbus_4();
    void releaseSerialModbus_5();
    void releaseSerialModbus_6();

    void busMonitorAddItem( bool isRequest,uint8_t slave,uint8_t func,uint16_t addr,uint16_t nb,uint16_t expectedCRC,uint16_t actualCRC );
    static void stBusMonitorAddItem( modbus_t * modbus,uint8_t isOut, uint8_t slave, uint8_t func, uint16_t addr,uint16_t nb, uint16_t expectedCRC, uint16_t actualCRC );
    static void stBusMonitorRawData( modbus_t * modbus, uint8_t * data,uint8_t dataLen, uint8_t addNewline );
    void busMonitorRawData( uint8_t * data, uint8_t dataLen, bool addNewline );
    void connectRadioButtons();
    void connectSerialPort();
    void connectActions();
    void connectModbusMonitor();
    void connectTimers();
    void connectRegisters();
    void connectLoopDependentData();
    void connectCalibrationControls();
    void connectProfiler();
    void connectToolbar();
    void initializeGauges();
    void initializeValues();
    void setupModbusPorts();
    void updateTabIcon(int, bool);
    void updateChartTitle();
    void initializeTabIcons();
    float toFloat(QByteArray arr);

    // modbus monitor
    void initializeModbusMonitor();

    // select function code
    void onFunctionCodeChanges();

    // send calibartion modbus request
    QString sendCalibrationRequest(int, modbus_t *, int, int, int, int, uint8_t *, uint16_t *, bool, bool, QString);

private slots:

    void onRtuPortActive(bool);
    void onRtuPortActive_2(bool);
    void onRtuPortActive_3(bool);
    void onRtuPortActive_4(bool);
    void onRtuPortActive_5(bool);
    void onRtuPortActive_6(bool);

    void changeSerialPort(int);
    void changeSerialPort_2(int);
    void changeSerialPort_3(int);
    void changeSerialPort_4(int);
    void changeSerialPort_5(int);
    void changeSerialPort_6(int);

    void calibration_1_1();
    void calibration_1_2();
    void calibration_1_3();
    void calibration_2_1();
    void calibration_2_2();
    void calibration_2_3();
    void calibration_3_1();
    void calibration_3_2();
    void calibration_3_3();
    void calibration_4_1();
    void calibration_4_2();
    void calibration_4_3();
    void calibration_5_1();
    void calibration_5_2();
    void calibration_5_3();
    void calibration_6_1();
    void calibration_6_2();
    void calibration_6_3();

    void initializeToolbarIcons(void);
    void initializePressureGauge();
    void initializeTemperatureGauge();
    void initializeDensityGauge();
    void initializeRPGauge();
    void onLoopTabChanged(int);
    void clearMonitors( void );
    void updateRequestPreview( void );
    void updateRegisterView( void );
    void enableHexView( void );
    void sendModbusRequest( void );
    void onSendButtonPress( void );
    void pollForDataOnBus( void );
    void openBatchProcessor();
    void aboutQModBus( void );
    void onCheckBoxChecked(bool);
    void resetStatus( void );
    void setStatusError(const QString &msg);    
    void updatePressureGauge();    
    void updateTemperatureGauge();    
    void updateDensityGauge();    
    void updateRPGauge();
    void onFloatButtonPressed(bool);
    void onIntegerButtonPressed(bool);
    void onCoilButtonPressed(bool);
    void onReadButtonPressed(bool);
    void onWriteButtonPressed(bool);
    void onEquationButtonPressed();
    void loadCsvFile();
    void loadCsvTemplate();
    void onUploadEquation();
    void onDownloadEquation();
    void updateRegisters(const bool, const int);
    void onProductBtnPressed();
    void onDownloadButtonChecked(bool);
    void saveCsvFile();
    void setupCalibrationRequest();

    // radio buttons
    void onRadioButtonPressed();
    void onRadioButton_2Pressed();
    void onRadioButton_3Pressed();
    void onRadioButton_4Pressed();
    void onRadioButton_7Pressed();
    void onRadioButton_8Pressed();
    void onRadioButton_9Pressed();
    void onRadioButton_10Pressed();
    void onRadioButton_13Pressed();
    void onRadioButton_14Pressed();
    void onRadioButton_15Pressed();
    void onRadioButton_16Pressed();
    void onRadioButton_19Pressed();
    void onRadioButton_20Pressed();
    void onRadioButton_23Pressed();
    void onRadioButton_24Pressed();
    void onRadioButton_25Pressed();
    void onRadioButton_26Pressed();
    void onRadioButton_27Pressed();
    void onRadioButton_28Pressed();
    void onRadioButton_31Pressed();
    void onRadioButton_32Pressed();
    void onRadioButton_33Pressed();
    void onRadioButton_34Pressed();
    void onRadioButton_37Pressed();
    void onRadioButton_38Pressed();
    void onRadioButton_41Pressed();
    void onRadioButton_42Pressed();
    void onRadioButton_43Pressed();
    void onRadioButton_44Pressed();
    void onRadioButton_45Pressed();
    void onRadioButton_46Pressed();
    void onRadioButton_49Pressed();
    void onRadioButton_50Pressed();
    void onRadioButton_51Pressed();
    void onRadioButton_52Pressed();
    void onRadioButton_55Pressed();
    void onRadioButton_56Pressed();
    void onRadioButton_59Pressed();
    void onRadioButton_60Pressed();
    void onRadioButton_61Pressed();
    void onRadioButton_62Pressed();
    void onRadioButton_63Pressed();
    void onRadioButton_64Pressed();
    void onRadioButton_67Pressed();
    void onRadioButton_68Pressed();
    void onRadioButton_69Pressed();
    void onRadioButton_70Pressed();
    void onRadioButton_73Pressed();
    void onRadioButton_74Pressed();
    void onRadioButton_77Pressed();
    void onRadioButton_78Pressed();
    void onRadioButton_79Pressed();
    void onRadioButton_80Pressed();
    void onRadioButton_81Pressed();
    void onRadioButton_82Pressed();
    void onRadioButton_85Pressed();
    void onRadioButton_86Pressed();
    void onRadioButton_87Pressed();
    void onRadioButton_88Pressed();
    void onRadioButton_91Pressed();
    void onRadioButton_92Pressed();
    void onRadioButton_95Pressed();
    void onRadioButton_96Pressed();
    void onRadioButton_97Pressed();
    void onRadioButton_98Pressed();
    void onRadioButton_99Pressed();
    void onRadioButton_100Pressed();
    void onRadioButton_103Pressed();
    void onRadioButton_104Pressed();
    void onRadioButton_105Pressed();
    void onRadioButton_106Pressed();

    // update graph
    void updateGraph();


signals:
    void connectionError(const QString &msg);

private:
    void keyPressEvent(QKeyEvent* event);
    void keyReleaseEvent(QKeyEvent* event);

    Ui::MainWindowClass * ui;
    
    modbus_t * m_modbus;
    modbus_t * m_modbus_2;
    modbus_t * m_modbus_3;
    modbus_t * m_modbus_4;
    modbus_t * m_modbus_5;
    modbus_t * m_modbus_6;
    modbus_t * m_modbus_snipping;

    QWidget * m_statusInd;
    QLabel * m_statusText;
    QTimer * m_pollTimer;
    QTimer * m_statusTimer;

    bool m_tcpActive;
    bool m_poll;

    // 3 axis line graph display
    QChart *chart;
    QValueAxis *axisX;
    QValueAxis *axisY;
    QValueAxis *axisY3;
    QSplineSeries *series;
    QChartView *chartView;

    //
    // gauge display
    //
    QcGaugeWidget * m_pressureGauge;
    QcNeedleItem * m_pressureNeedle;
    QcGaugeWidget * m_temperatureGauge;
    QcNeedleItem * m_temperatureNeedle;
    QcGaugeWidget * m_densityGauge;
    QcNeedleItem * m_densityNeedle;
    QcGaugeWidget * m_RPGauge;
    QcNeedleItem * m_RPNeedle;
    
    int REG_SN_PIPE[MAX_PIPE];
    int REG_WATERCUT[MAX_PIPE];
    int REG_TEMPERATURE[MAX_PIPE];
    int REG_EMULSTION_PHASE[MAX_PIPE];
    int REG_SALINITY[MAX_PIPE];
    int REG_HARDWARE_VERSION[MAX_PIPE];
    int REG_FIRMWARE_VERSION[MAX_PIPE];
    int REG_OIL_ADJUST[MAX_PIPE];
    int REG_WATER_ADJUST[MAX_PIPE];
    int REG_FREQ[MAX_PIPE];
    int REG_FREQ_AVG[MAX_PIPE];
    int REG_WATERCUT_AVG[MAX_PIPE];
    int REG_WATERCUT_RAW[MAX_PIPE];
    int REG_ANALYZER_MODE[MAX_PIPE];
    int REG_TEMP_AVG[MAX_PIPE];
    int REG_TEMP_ADJUST[MAX_PIPE];
    int REG_TEMP_USER[MAX_PIPE];
    int REG_PROC_AVGING[MAX_PIPE];
    int REG_OIL_INDEX[MAX_PIPE];
    int REG_OIL_P0[MAX_PIPE];
    int REG_OIL_P1[MAX_PIPE];
    int REG_OIL_FREQ_LOW[MAX_PIPE];
    int REG_OIL_FREQ_HIGH[MAX_PIPE];
    int REG_SAMPLE_PERIOD[MAX_PIPE];
    int REG_AO_LRV[MAX_PIPE];
    int REG_AO_URV[MAX_PIPE];
    int REG_AO_DAMPEN[MAX_PIPE];
    int REG_BAUD_RATE[MAX_PIPE];
    int REG_SLAVE_ADDRESS[MAX_PIPE];
    int REG_STOP_BITS[MAX_PIPE];
    int REG_OIL_RP[MAX_PIPE];
    int REG_WATER_RP[MAX_PIPE];
    int REG_DENSITY_MODE[MAX_PIPE];
    int REG_OIL_CALC_MAX[MAX_PIPE];
    int REG_OIL_PHASE_CUTOFF[MAX_PIPE];
    int REG_TEMP_OIL_NUM_CURVES[MAX_PIPE];
    int REG_STREAM[MAX_PIPE];
    int REG_OIL_RP_AVG[MAX_PIPE];
    int REG_PLACE_HOLDER[MAX_PIPE];
    int REG_OIL_SAMPLE[MAX_PIPE];
    int REG_RTC_SEC[MAX_PIPE];
    int REG_RTC_MIN[MAX_PIPE];
    int REG_RTC_HR[MAX_PIPE];
    int REG_RTC_DAY[MAX_PIPE];
    int REG_RTC_MON[MAX_PIPE];
    int REG_RTC_YR[MAX_PIPE];
    int REG_RTC_SEC_IN[MAX_PIPE];
    int REG_RTC_MIN_IN[MAX_PIPE];
    int REG_RTC_HR_IN[MAX_PIPE];
    int REG_RTC_DAY_IN[MAX_PIPE];
    int REG_RTC_MON_IN[MAX_PIPE];
    int REG_RTC_YR_IN[MAX_PIPE];
    int REG_AO_MANUAL_VAL[MAX_PIPE];
    int REG_AO_TRIMLO[MAX_PIPE];
    int REG_AO_TRIMHI[MAX_PIPE];
    int REG_DENSITY_ADJ[MAX_PIPE];
    int REG_DENSITY_UNIT[MAX_PIPE];
    int REG_WC_ADJ_DENS[MAX_PIPE];
    int REG_DENSITY_D3[MAX_PIPE];
    int REG_DENSITY_D2[MAX_PIPE];
    int REG_DENSITY_D1[MAX_PIPE];
    int REG_DENSITY_D0[MAX_PIPE];
    int REG_DENSITY_CAL_VAL[MAX_PIPE];
    int REG_MODEL_CODE_0[MAX_PIPE];
    int REG_MODEL_CODE_1[MAX_PIPE];
    int REG_MODEL_CODE_2[MAX_PIPE];
    int REG_MODEL_CODE_3[MAX_PIPE];
    int REG_LOGGING_PERIOD[MAX_PIPE];
    int REG_PASSWORD[MAX_PIPE];
    int REG_STATISTICS[MAX_PIPE];
    int REG_ACTIVE_ERROR[MAX_PIPE];
    int REG_AO_ALARM_MODE[MAX_PIPE];
    int REG_AO_OUTPUT[MAX_PIPE];
    int REG_PHASE_HOLD_CYCLES[MAX_PIPE];
    int REG_RELAY_DELAY[MAX_PIPE];
    int REG_RELAY_SETPOINT[MAX_PIPE];
    int REG_AO_MODE[MAX_PIPE];
    int REG_OIL_DENSITY[MAX_PIPE];
    int REG_OIL_DENSITY_MODBUS[MAX_PIPE];
    int REG_OIL_DENSITY_AI[MAX_PIPE];
    int REG_OIL_DENSITY_MANUAL[MAX_PIPE];
    int REG_OIL_DENSITY_AI_LRV[MAX_PIPE];
    int REG_OIL_DENSITY_AI_URV[MAX_PIPE];
    int REG_OIL_DENS_CORR_MODE[MAX_PIPE];
    int REG_AI_TRIMLO[MAX_PIPE];
    int REG_AI_TRIMHI[MAX_PIPE];
    int REG_AI_MEASURE[MAX_PIPE];
    int REG_AI_TRIMMED[MAX_PIPE];
};

#endif // MAINWINDOW_H
