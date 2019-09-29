#ifndef _PUTZ_H_
#define _PUTZ_H_

#if QT_VERSION >= 0x050000
#include <QtWidgets>
#else
#include <QtGui>
#endif
#include "ui_putz.h"

class Putz : public QMainWindow, public Ui::MainWindow
{
   Q_OBJECT 

public:
   Putz();

public slots:

private:
};

#endif /* _PUTZ_H_ */
