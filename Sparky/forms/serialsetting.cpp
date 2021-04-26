#include "serialsetting.h"
#include "ui_serialsetting.h"

serialsetting::serialsetting(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::serialsetting)
{
    ui->setupUi(this);
}

serialsetting::~serialsetting()
{
    delete ui;
}
