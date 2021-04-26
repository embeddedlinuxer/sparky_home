/*
 * main.cpp - main file for QModBus
 *
 * Copyright (c) 2009-2014 Tobias Junghans / Electronic Design Chemnitz
 *
 * This file is part of QModBus - http://qmodbus.sourceforge.net
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */


#include <QApplication>
#include "mainwindow.h"
#include <QApplication>
#include <QtCore>
#include <QPixmap>
#include <QSplashScreen>
#include <QWidget>
#include <QMainWindow>
#include <QTimer>
#include <QThread>

class I : public QThread
{
public:
        static void sleep(unsigned long secs) {
                QThread::sleep(secs);
        }
};

MainWindow * globalMainWin = NULL;

int main(int argc, char *argv[])
{
    static int const RESTART_CODE = 1000;
    int return_code = 0;


   QWidget * top = 0;
 
    do {
           QApplication a(argc, argv);

           QPixmap pixmap(":/splash.png"); //Insert your splash page image here
           QSplashScreen splash(pixmap);
           splash.show();

           splash.showMessage(QObject::tr("Checking for the latest update...\n\n\n\n\n\n"),Qt::AlignCenter | Qt::AlignBottom, Qt::black);  //This line represents the alignment of text, color and position
           //I::sleep(3);
           MainWindow w;
    
        //   QScrollArea* scroller = new QScrollArea;
        //   scroller->setWidget(&w);
        //   top = scroller;

           w.show();
           //top->show();
           globalMainWin = &w;
           
           splash.finish(&w);
           splash.raise();

           return_code = a.exec();

       } while( return_code == RESTART_CODE);

    return return_code;
}
