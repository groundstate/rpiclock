//
// rpiclock - a time display program for the Raspberry Pi/Linux
//
// The MIT License (MIT)
//
// Copyright (c) 2014  Michael J. Wouters
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#ifndef TIMEDISPLAY_H
#define TIMEDISPLAY_H


#include <QList>
#include <QWidget>
#include <QDateTime>
#include <Qt/QtXml>

class QAction;
class QActionGroup;
class QKeyEvent;
class QLabel;
class QMouseEvent;
class QNetworkAccessManager;
class QNetworkReply;
class QTimer;
class QUdpSocket;

class PowerManager;

class LeapInfo
{
public:
    LeapInfo(unsigned int tl, unsigned int dt)
    {
        tleap=tl;
        dttaiutc=dt;
    }
    unsigned int tleap; // NTP time
    unsigned int dttaiutc; // delta TAI UTC
};

class CalendarItem
{
public:
    CalendarItem() {startDay=startMonth=stopDay=stopMonth=-1;}

    int startDay,startMonth,stopDay,stopMonth;
    QString image;
    QString description;
};

class TimeDisplay : public QWidget
{
    Q_OBJECT

public:

    TimeDisplay();

    enum TimeScale  { Local, UTC, Unix, GPS };
    enum TODFormat  {hhmm,hhmmss};
    enum HourFormat {TwelveHour,TwentyFourHour};
    enum BackgroundMode  {Fixed,Slideshow};


    enum DateFlags  {ISOdate=0x01,
                     PrettyDate=0x02,
                     MJD=0x04,
                     GPSDayWeek=0x08,
                     DOY=0x10
                    };
protected slots:

    virtual void 	keyPressEvent (QKeyEvent *);
    virtual void 	mouseMoveEvent (QMouseEvent * );
    virtual void 	mousePressEvent (QMouseEvent * );
		
private slots:

    void updateTime();

    void toggleFullScreen();

    void setLocalTime();
    void setUnixTime();
    void setGPSTime();
    void setUTCTime();
    void set12HourFormat();
    void set24HourFormat();

    void togglePowerManagement();
    void toggleSeparatorBlinking();
    void setHHMMTODFormat();
    void setHHMMSSTODFormat();
    void quit();

    void createContextMenu(const QPoint &);

    void updateLeapSeconds();
    void replyFinished(QNetworkReply*);

		void readNTPDatagram();
		
private:

    void createActions();
    void updateActions();

    void showTime(QDateTime &);
    void showDate(QDateTime &);

    void setTODFontSize();
    void setDateFontSize();
    void setTitleFontSize();
		void setCalTextFontSize();
		
    void fetchLeapSeconds();
    void readLeapFile();

		void	writeNTPDatagram();
		
    void readConfig(QString s);
		void readBackgroundConfig(QDomElement);
		void setPlainBackground();
		QString pickCalendarImage();
		QString pickSlideShowImage();
		
    void setBackground();

    PowerManager   *powerManager;

		bool autoUpdateLeapFile;
		QString leapFileURL;
		QString proxyServer;
		int     proxyPort;
		QString proxyUser;
		QString proxyPassword;
    int leapSeconds; // the current value
    QDateTime leapFileExpiry;
    QDateTime lastLeapFileFetch;
		QDateTime leapFileLastModified;
		int leapFileCheckInterval;
    QString leapFile;
    bool leapsInitialized;
    QList<LeapInfo *> leapTable;

		QUdpSocket *ntpSocket;
		QDateTime  lastNTPReply;
		bool syncOK;
		
    int timeScale;
    int TODFormat;
    int hourFormat;
    int dateFormat;
    QString timezone;

		QString localTimeBanner,UTCBanner,UnixBanner,GPSBanner;
		
    bool blinkSeparator;
    int  blinkDelay;
    QDateTime lastTimeCode;
    QString defaultImage; // the default image
    QString currentImage;
		QString logoImage;
		
    int backgroundMode;
    QList<CalendarItem *> calendarItems;
    QString imagePath;
		QString calItemText;
		int lastHour; // for tracking day rollover
    bool fullScreen;

    QNetworkAccessManager *netManager;
    QTimer  *updateTimer;
    QLabel  *bkground,*title,*tod,*date,*logo,*img,*calText;
    QAction *toggleFullScreenAction,*localTimeAction,*UnixTimeAction,*GPSTimeAction,*UTCTimeAction;
    QAction *twelveHourFormatAction,*twentyFourHourFormatAction;
    QAction *sepBlinkingOnAction,*HHMMSSFormatAction,*HHMMFormatAction;
    QAction *powerManAction;
    QAction *quitAction;

    QActionGroup *hourFormatActionGroup,*TODFormatActionGroup;

    QAction *testLeap;
};

#endif