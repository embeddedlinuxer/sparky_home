#ifndef SERIALSETTING_H
#define SERIALSETTING_H

#include <QDialog>

namespace Ui {
class serialsetting;
}

class serialsetting : public QDialog
{
    Q_OBJECT

public:
    explicit serialsetting(QWidget *parent = nullptr);
    ~serialsetting();

private:
    Ui::serialsetting *ui;
};

#endif // SERIALSETTING_H
