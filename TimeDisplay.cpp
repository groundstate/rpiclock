//
// rpiclock - a time display program for the Raspberry Pi/Linux
//
// The MIT License (MIT)
//
// Copyright (c)  2014  Michael J. Wouters
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

#include <sys/timex.h>
#include <netinet/in.h> // For ntohl() (byte order conversion) 

#include <iostream>

#include <QtGui>
#include <Qt/QtNetwork>
#include <QTime>
#include <QRegExp>
#include <QUdpSocket>

#include "PowerManager.h"
#include "TimeDisplay.h"

#define VERSION_INFO "v0.1.2"

#define LEAPSECONDS 16     // whatever's current
#define GPSEPOCH 315964800 // GPS epoch in the Unix time scale
#define UNIXEPOCH 0x83aa7e80  //  Unix epoch in the NTP time scale 
#define DELTATAIGPS 19     // 
#define MAXLEAPCHECKINTERVAL 1048576 // two weeks should be good enough
#define NTPTIMEOUT 64 // waiting time for a NTP response, before declaring no sync

TimeDisplay::TimeDisplay(QStringList &args):QWidget()
{

	fullScreen=true;
	
	for (int i=1;i<args.size();i++){ // skip the first
		if (args.at(i) == "--nofullscreen")
			fullScreen=false;
		else if (args.at(i) == "--help"){
			std::cout << "rpiclock " << std::endl;
			std::cout << "Usage: rpiclock [options]" << std::endl;
			std::cout << std::endl;
			std::cout << "--help         print this help" << std::endl;
			std::cout << "--license      print this help" << std::endl;
			std::cout << "--nofullscreen run in a window" << std::endl;
			std::cout << "--version      display version" << std::endl;
			
			exit(EXIT_SUCCESS);
		}
		else if (args.at(i) == "--license"){
			std::cout << " rpiclock - a time display program for the Raspberry Pi/Linux" << std::endl;
			std::cout <<  std::endl;
			std::cout << " The MIT License (MIT)" << std::endl;
			std::cout <<  std::endl;
			std::cout << " Copyright (c)  2014  Michael J. Wouters" << std::endl;
			std::cout <<  std::endl; 
			std::cout << " Permission is hereby granted, free of charge, to any person obtaining a copy" << std::endl;
			std::cout << " of this software and associated documentation files (the \"Software\"), to deal" << std::endl;
			std::cout << " in the Software without restriction, including without limitation the rights" << std::endl;
			std::cout << " to use, copy, modify, merge, publish, distribute, sublicense, and/or sell" << std::endl;
			std::cout << " copies of the Software, and to permit persons to whom the Software is" << std::endl;
			std::cout << " furnished to do so, subject to the following conditions:" << std::endl;
			std::cout << std::endl; 
			std::cout << " The above copyright notice and this permission notice shall be included in" << std::endl;
			std::cout << " all copies or substantial portions of the Software." << std::endl;
			std::cout << std::endl;
			std::cout << " THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR" << std::endl;
			std::cout << " IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY," << std::endl;
			std::cout << " FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE" << std::endl;
			std::cout << " AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER" << std::endl;
			std::cout << " LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM," << std::endl;
			std::cout << " OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN" << std::endl;
			std::cout << " THE SOFTWARE." << std::endl;
			
			exit(EXIT_SUCCESS);
		}
		else if (args.at(i) == "--version"){
			std::cout << "rpiclock " << VERSION_INFO << std::endl;
			std::cout << std::endl;
			std::cout << "This ain't no stinkin' Perl script!" << std::endl;
			
			exit(EXIT_SUCCESS);
		}
		else{
			std::cout << "rpiclock: Unknown option '"<< args.at(i).toStdString() << "'" << std::endl;
			std::cout << "rpiclock: Use --help to get a list of available command line options"<< std::endl;
			
			exit(EXIT_SUCCESS);
		}
	}
	
	setWindowTitle(tr("rpiclock"));
	setMinimumSize(QSize(1920,1080));
	if (fullScreen)
		setWindowState(windowState() ^ Qt::WindowFullScreen);
	
	setMouseTracking(true); // so that mouse movements wake up the display
	QCursor curs;
	curs.setShape(Qt::BlankCursor);
	setCursor(curs);
	cursor().setPos(0,0);
	
	QTime on(9,0,0);
	QTime off(17,0,0);

	timeScale=Local;
	TODFormat=hhmmss;
	dateFormat=PrettyDate;
	blinkSeparator=false;
	blinkDelay=500;
	leapSeconds = LEAPSECONDS;
	hourFormat=TwelveHour;
	timezone="Australia/Sydney";
	
	defaultImage="";
	backgroundMode = Fixed;
	imagePath = "";
	calItemText="";
	logoImage="";
	dimLogo=NULL;
	
	localTimeBanner="Local time";
	UTCBanner="Coordinated Universal Time";
	UnixBanner="Unix time";
	GPSBanner="GPS time";
	
	powerManager=new PowerManager(on,off);
	powerManager->enable(false);
	
	autoUpdateLeapFile=false;
	leapFileURL=""; // no default so as to be kind to eg NIST !
	proxyServer="";
	proxyPort=-1;
	proxyUser="";
	proxyPassword="";
	
	leapsInitialized=false;
	leapFileExpiry= QDateTime(QDate(1970,1,1),QTime(0,0,0));
	lastLeapFileFetch=leapFileExpiry;
	leapFileCheckInterval=8;
	leapFile = "";
	
	dimEnable=true;
	dimMethod=Software;
	dimLevel=25;
	dimActive=false;
	lowLight=false;
	dimImage=NULL;
	lightLevelFile="";
	dimThreshold=0;
	
	fontColourName="white";
	
	// Look for a configuration file
	// The search path is ./:~/rpiclock:~/.rpiclock:/usr/local/etc:/etc
	
	QFileInfo fi;

	QString s("./rpiclock.xml");
	fi.setFile(s);
	if (fi.isReadable())
		configFile=s;
	
	if (configFile.isNull()){
		char *eptr = getenv("HOME");
		QString home("./");
		if (eptr)
			home=eptr;
		s=home+"/rpiclock/rpiclock.xml";
		fi.setFile(s);
		if (fi.isReadable())
			configFile=s;
		if (configFile.isNull()){
			s=home+"/.rpiclock/rpiclock.xml";
			fi.setFile(s);
			if (fi.isReadable())
				configFile=s;
		}
	}
	
	if (configFile.isNull()){
		s="/usr/local/etc/rpiclock.xml";
		fi.setFile(s);
		if (fi.isReadable())
			configFile=s;
	}
	
	if (configFile.isNull()){
		s="/etc/rpiclock.xml";
		fi.setFile(s);
		if (fi.isReadable())
			configFile=s;
	}
	
	if (!configFile.isNull())
		readConfig(configFile);
	
	// Layout is
	// Top level layout contains the background widget
	// The overlaying layout is parented to the background widget and consists of a vbox containing
	// hb: title
	// hb: TOD
	// hb: calendar text
	// hb: date
	// The logo is parented to the date
	
	QVBoxLayout *vb = new QVBoxLayout(this);
	vb->setContentsMargins(0,0,0,0);
	bkground = new QLabel("");
	bkground->setObjectName("Background");
	vb->addWidget(bkground);

	vb = new QVBoxLayout(bkground);
	vb->setContentsMargins(0,0,0,0);

	QHBoxLayout * hb = new QHBoxLayout();
	vb->addLayout(hb);
	title = new QLabel("",bkground);
	title->setFont(QFont("Monospace"));
	title->setAlignment(Qt::AlignCenter);
	hb->addWidget(title);

	hb = new QHBoxLayout();
	vb->addLayout(hb);
	tod = new QLabel("--:--:--",bkground);
	tod->setContentsMargins(0,160,0,160);
	tod->setFont(QFont("Monospace"));
	tod->setAlignment(Qt::AlignCenter);
	hb->addWidget(tod);

	hb = new QHBoxLayout();
	hb->setContentsMargins(0,0,0,0);
	vb->addLayout(hb,0);
	calText = new QLabel("",bkground);
	calText->setFont(QFont("Monospace"));
	calText->setAlignment(Qt::AlignCenter);
	hb->addWidget(calText,0);

	hb = new QHBoxLayout();
	vb->addLayout(hb);
	date = new QLabel("56337",bkground);
	date->setFont(QFont("Monospace"));
	date->setAlignment(Qt::AlignCenter);
	hb->addWidget(date);

	setWidgetStyleSheet();
	
	logoParentWidget= new QWidget(date);
	hb=new QHBoxLayout(logoParentWidget);
	hb->setContentsMargins(32,32,32,32);
	logo = new QLabel();
	setLogoImages();
	hb->addWidget(logo);
	
	createActions();
	setContextMenuPolicy(Qt::CustomContextMenu);
	connect(this,SIGNAL(customContextMenuRequested ( const QPoint & )),this,SLOT(createContextMenu(const QPoint &)));

	switch (timeScale){
		case Local:setLocalTime();break;
		case GPS:setGPSTime();break;
		case Unix:setUnixTime();break;
		case UTC:setUTCTime();break;
	}
	
	timezone.prepend(":");
	setenv("TZ",timezone.toStdString().c_str(),1);
	tzset();
	
	setBackground();
	
	netManager = new QNetworkAccessManager(this);
	if (proxyServer != "" && proxyPort != -1)
		netManager->setProxy(QNetworkProxy(QNetworkProxy::HttpProxy,proxyServer,proxyPort,proxyUser,proxyPassword)); // UNTESTED
	
	connect(netManager, SIGNAL(finished(QNetworkReply*)),
         this, SLOT(replyFinished(QNetworkReply*)));
	
	ntpSocket = new QUdpSocket(this);
  ntpSocket->bind(0); // get a random port
	connect(ntpSocket, SIGNAL(readyRead()), this, SLOT(readNTPDatagram()));
	lastNTPReply = lastLeapFileFetch;
	syncOK=false;
						 
	updateTimer = new QTimer(this);
	connect(updateTimer,SIGNAL(timeout()),this,SLOT(updateTime()));
	QDateTime now = QDateTime::currentDateTime();
	updateTimer->start(1000-now.time().msec()); // don't try to get the first blink right
	lastHour=now.time().hour(); 

}

void 	TimeDisplay::keyPressEvent (QKeyEvent *ev)
{
	QWidget::keyPressEvent(ev);
	powerManager->deviceEvent();
}

void 	TimeDisplay::mouseMoveEvent (QMouseEvent *ev )
{
	QWidget::mouseMoveEvent(ev);
	powerManager->deviceEvent();
}

void 	TimeDisplay::mousePressEvent (QMouseEvent *ev )
{
	QWidget::mousePressEvent(ev);
	powerManager->deviceEvent();
}
	
void TimeDisplay::updateTime()
{
	updateLeapSeconds();
	powerManager->update();
	
	updateDimState();
	
	//struct timex tx;
	//tx.modes=0; // don't want to set the time
	//int ret = ntp_adjtime(&tx);
	
	QDateTime now = QDateTime::currentDateTime();
	
	// Each day, just after midnight, check whether the background image should be changed
	//if (lastHour != now.time().minute() && backgroundMode != Fixed){ // FIXME
	if (lastHour > now.time().hour() && backgroundMode != Fixed)
		setBackground();	
	
	lastHour = now.time().hour(); 
	
	syncOK = syncOK && (lastNTPReply.secsTo(now)< NTPTIMEOUT); 
	
	if (syncOK){
		showTime(now);
		showDate(now);
	}
	else{
		tod->setText("--:--:--");
		date->setText("Unsynchronised");
	}
	
	if (blinkSeparator){
		if (now.time().msec() < blinkDelay) 
			updateTimer->start(blinkDelay-now.time().msec());
		else
			updateTimer->start(1000-blinkDelay);
	}
	else
		updateTimer->start(1000-now.time().msec());
	
	checkConfigFile();
	writeNTPDatagram();
	
}

void TimeDisplay::updateDimState(){
	if (!dimEnable) return;
	
	// Check the sensor reading
	QFile lf(lightLevelFile);
	if (lf.open(QFile::ReadOnly)){
		QTextStream ts(&lf);
		int currLightLevel=255;
		ts >> currLightLevel;
		lowLight=currLightLevel < dimThreshold;
	}
	else	
		return;
	
	if (!dimActive && lowLight){
		dimActive=true;
		QString txtColour;
		txtColour.sprintf("color:rgba(%d,%d,%d,255)",
				dimFontColour.red(),dimFontColour.green(),dimFontColour.blue());
		title->setStyleSheet(txtColour);
		tod->setStyleSheet(txtColour);
		calText->setStyleSheet(txtColour);
		date->setStyleSheet(txtColour);
		bkground->setPixmap(QPixmap::fromImage(*dimImage));
		logo->setPixmap(QPixmap::fromImage(*dimLogo));
	}
	else if (dimActive && !lowLight){
		dimActive=false;
		QString txtColour;
		txtColour.sprintf("color:rgba(%d,%d,%d,255)",
				fontColour.red(),fontColour.green(),fontColour.blue());
		title->setStyleSheet(txtColour);
		tod->setStyleSheet(txtColour);
		calText->setStyleSheet(txtColour);
		date->setStyleSheet(txtColour);
		bkground->setPixmap(QPixmap(currentImage));
		logo->setPixmap(logoImage);
	}
	else if (dimActive && lowLight ){
	}
	
}

void TimeDisplay::toggleFullScreen()
{
	fullScreen=!fullScreen;
	setWindowState(windowState() ^ Qt::WindowFullScreen);
	setTODFontSize(); 
	setDateFontSize();
	setTitleFontSize();
	setCalTextFontSize();
}

void TimeDisplay::setLocalTime()
{
	timeScale=Local;
	TODFormat=hhmmss;
	dateFormat=PrettyDate;
	title->setText(localTimeBanner);
	setTODFontSize(); 
	setDateFontSize();
	setTitleFontSize();
	setCalTextFontSize();
	updateActions();
}

void TimeDisplay::setUTCTime()
{
	timeScale=UTC;
	TODFormat=hhmmss;
	dateFormat=MJD | DOY;
	title->setText(UTCBanner);
	setTODFontSize(); 
	setDateFontSize();
	setTitleFontSize();
	setCalTextFontSize();
	updateActions();
}

void TimeDisplay::setUnixTime()
{
	timeScale=Unix;
	dateFormat=MJD|DOY;
	title->setText(UnixBanner);
	setTODFontSize(); 
	setDateFontSize();
	setTitleFontSize();
	setCalTextFontSize();
	updateActions();
}

void TimeDisplay::setGPSTime()
{
	timeScale=GPS;
	dateFormat=GPSDayWeek;
	title->setText(GPSBanner);
	setTODFontSize(); 
	setDateFontSize();
	setTitleFontSize();
	setCalTextFontSize();
	updateActions();
}

void TimeDisplay::togglePowerManagement()
{
	powerManager->enable(!powerManager->isEnabled());
}

void TimeDisplay::toggleSeparatorBlinking()
{
	blinkSeparator=!blinkSeparator;
}

void TimeDisplay::setHHMMTODFormat()
{
	TODFormat=hhmm;
}

void TimeDisplay::setHHMMSSTODFormat()
{
	TODFormat=hhmmss;
}

void TimeDisplay::set12HourFormat()
{
	hourFormat=TwelveHour;
}

void TimeDisplay::set24HourFormat()
{
	hourFormat=TwentyFourHour;
}

		
void TimeDisplay::setPlainBackground()
{
	
	//bkground->setStyleSheet("QLabel#Background {background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,"
	//												 "stop: 0 #3c001e, stop: 0.2 #500130,"
  //                         "stop: 0.8 #500130, stop: 1.0 #3c001e)}");
	bkground->setStyleSheet("QLabel#Background {background-color:rgba(80,1,48,255)}");
	bkground->setPixmap(QPixmap(""));
	defaultImage="";
}

void TimeDisplay::quit()
{
	exit(1);
}

void TimeDisplay::createContextMenu(const QPoint &)
{

	toggleFullScreenAction->setChecked(fullScreen);

	QMenu *cm = new QMenu(this);
	
	cm->addAction(localTimeAction);
	cm->addAction(UTCTimeAction);
	cm->addAction(UnixTimeAction);
	cm->addAction(GPSTimeAction);
	
	cm->addSeparator();
	cm->addAction(sepBlinkingOnAction);
	cm->addAction(HHMMSSFormatAction);
	cm->addAction(HHMMFormatAction);
	cm->addAction(twelveHourFormatAction);
	cm->addAction(twentyFourHourFormatAction);
	
	cm->addSeparator();
	cm->addAction(toggleFullScreenAction);
	cm->addAction(powerManAction);

	cm->addSeparator();
	cm->addAction(testLeap);
	cm->addAction(testDimming);
	cm->addSeparator();
	cm->addAction(quitAction);
	cm->exec(QCursor::pos());
	delete cm;
}

void TimeDisplay::replyFinished(QNetworkReply *reply)
{
	qDebug() << "reply finished" << endl;
	QByteArray ba = reply->readAll();
	QString bas(ba);
	qDebug() << "reply:" <<bas ;
	if (!bas.isEmpty()){
		qDebug() << "writing " << leapFile;
		QFile f(leapFile);
		if (f.open(QIODevice::WriteOnly | QIODevice::Text)){
			QTextStream out(&f);
			out << bas;
			f.close();
			readLeapFile();
		}
	}
	reply->deleteLater();
}

void	TimeDisplay::readNTPDatagram()
{
	QByteArray data;
	data.resize(ntpSocket->pendingDatagramSize());
	ntpSocket->readDatagram(data.data(), data.size());
	unsigned int u1 = ntohl(*reinterpret_cast<const int*>(data.data()));
	unsigned int b4=(u1 & 0xff000000)>>24; 
	qDebug() << "reply li="<< ((b4 >> 6) & 0x03) << " vn=" << ((b4 >>3) & 0x07);
	syncOK = ((b4 >> 6) & 0x03) != 3;
	lastNTPReply=QDateTime::currentDateTime();
}

//
//
//

void TimeDisplay::createActions()
{
	toggleFullScreenAction = new QAction(QIcon(), tr("Full screen"), this);
	toggleFullScreenAction->setStatusTip(tr("Show full screen"));
	addAction(toggleFullScreenAction);
	connect(toggleFullScreenAction, SIGNAL(triggered()), this, SLOT(toggleFullScreen()));
	toggleFullScreenAction->setCheckable(true);
	
	QActionGroup *actionGroup = new QActionGroup(this);
	
	localTimeAction = actionGroup->addAction(QIcon(), tr("Local time"));
	localTimeAction->setStatusTip(tr("Show local time"));
	connect(localTimeAction, SIGNAL(triggered()), this, SLOT(setLocalTime()));
	localTimeAction->setCheckable(true);
	localTimeAction->setChecked(timeScale==Local);
	
	UTCTimeAction = actionGroup->addAction(QIcon(), tr("UTC time"));
	UTCTimeAction->setStatusTip(tr("Show UTC time"));
	connect(UTCTimeAction, SIGNAL(triggered()), this, SLOT(setUTCTime()));
	UTCTimeAction->setCheckable(true);
	UTCTimeAction->setChecked(timeScale==UTC);
	
	GPSTimeAction = actionGroup->addAction(QIcon(), tr("GPS time"));
	GPSTimeAction->setStatusTip(tr("Show GPS time"));
	connect(GPSTimeAction, SIGNAL(triggered()), this, SLOT(setGPSTime()));
	GPSTimeAction->setCheckable(true);
	GPSTimeAction->setChecked(timeScale==GPS);
	
	UnixTimeAction = actionGroup->addAction(QIcon(), tr("Unix time"));
	UnixTimeAction->setStatusTip(tr("Show Unix time"));
	connect(UnixTimeAction, SIGNAL(triggered()), this, SLOT(setUnixTime()));
	UnixTimeAction->setCheckable(true);
	UnixTimeAction->setChecked(timeScale==Unix);
	
	hourFormatActionGroup = new QActionGroup(this);
	
	twelveHourFormatAction=hourFormatActionGroup->addAction(QIcon(), tr("12 hour format"));
	twelveHourFormatAction->setStatusTip(tr("Set 12 hour format"));
	connect(twelveHourFormatAction, SIGNAL(triggered()), this, SLOT(set12HourFormat()));
	twelveHourFormatAction->setCheckable(true);
	twelveHourFormatAction->setChecked(hourFormat==TwelveHour);
	
	twentyFourHourFormatAction=hourFormatActionGroup->addAction(QIcon(), tr("24 hour format"));
	twentyFourHourFormatAction->setStatusTip(tr("Set 24 hour format"));
	connect(twentyFourHourFormatAction, SIGNAL(triggered()), this, SLOT(set24HourFormat()));
	twentyFourHourFormatAction->setCheckable(true);
	twentyFourHourFormatAction->setChecked(hourFormat==TwentyFourHour);
	
	powerManAction = new QAction(QIcon(), tr("Power management"), this);
	powerManAction->setStatusTip(tr("Power management"));
	addAction(powerManAction);
	connect(powerManAction, SIGNAL(triggered()), this, SLOT(togglePowerManagement()));
	powerManAction->setCheckable(true);
	powerManAction->setChecked(powerManager->isEnabled());
	
	sepBlinkingOnAction =new QAction(QIcon(), tr("Blink separator"), this);
	sepBlinkingOnAction->setStatusTip(tr("Toggle blinking of separator in time of day"));
	addAction(sepBlinkingOnAction);
	connect(sepBlinkingOnAction, SIGNAL(triggered()), this, SLOT(toggleSeparatorBlinking()));
	sepBlinkingOnAction->setCheckable(true);
	sepBlinkingOnAction->setChecked(blinkSeparator);
	
	TODFormatActionGroup = new QActionGroup(this);
	
	HHMMSSFormatAction=TODFormatActionGroup->addAction(QIcon(), tr("HHMMSS format"));
	HHMMSSFormatAction->setStatusTip(tr("Set time of day format to HH:MM:SS"));
	connect(HHMMSSFormatAction, SIGNAL(triggered()), this, SLOT(setHHMMSSTODFormat()));
	HHMMSSFormatAction->setCheckable(true);
	HHMMSSFormatAction->setChecked(TODFormat==hhmm);
	
	HHMMFormatAction=TODFormatActionGroup->addAction(QIcon(), tr("HHMM format"));
	HHMMFormatAction->setStatusTip(tr("Set time of day format to HH:MM"));
	connect(HHMMFormatAction, SIGNAL(triggered()), this, SLOT(setHHMMTODFormat()));
	HHMMFormatAction->setCheckable(true);
	HHMMFormatAction->setChecked(TODFormat==hhmmss);
	
	testLeap = new QAction(QIcon(), tr("Fetch leap table"), this);
	testLeap->setStatusTip(tr("Fetch leap second table"));
	addAction(testLeap);
	connect(testLeap, SIGNAL(triggered()), this, SLOT(updateLeapSeconds()));
	
	testDimming = new QAction(QIcon(), tr("Toggle dimmimg"), this);
	testDimming->setStatusTip(tr("Toggle dimmimg"));
	addAction(testDimming);
	connect(testDimming, SIGNAL(triggered()), this, SLOT(toggleDimming()));
	
	quitAction = new QAction(QIcon(), tr("Quit"), this);
	quitAction->setStatusTip(tr("Quit"));
	addAction(quitAction);
	connect(quitAction, SIGNAL(triggered()), this, SLOT(quit()));
	
	updateActions();
}

void TimeDisplay::updateActions()
{
	hourFormatActionGroup->setEnabled(timeScale==Local);
	TODFormatActionGroup->setEnabled(timeScale==Local);
	sepBlinkingOnAction->setEnabled(timeScale == UTC || timeScale == Local);
}

void TimeDisplay::showTime(QDateTime &now)
{
	
	char sep=':';
	
	if (blinkSeparator){
		if (now.time().msec() >= blinkDelay) sep=' ';
	}
	
	QDateTime UTCnow = now.toUTC();
	
	// leap secondy stuff
	int leapCorrection=0;
	if (UTCnow.time().hour() == 23 && UTCnow.time().minute() == 59 && UTCnow.time().second() ==59) // a small sanity check
	{
		struct timex tx;
		int ret = adjtimex(&tx);
		if (ret == TIME_OOP)
			leapCorrection = 1;
	}
		
	QString s;
	switch (timeScale)
	{
		case Local:
		{
			if (TODFormat == hhmmss){
				if (hourFormat == TwentyFourHour)
					s.sprintf("%02d%c%02d%c%02d",now.time().hour(),sep,now.time().minute(),sep,now.time().second()+leapCorrection);
				else
				{
					int hr=now.time().hour();
					if (now.time().hour() > 12)  hr=now.time().hour()-12;
					if (now.time().hour() == 0 ) hr=now.time().hour()+12;
					s.sprintf("%d%c%02d%c%02d",hr,sep,now.time().minute(),sep,now.time().second()+leapCorrection);
				}
			}
			else{ 
				s.sprintf("%02d%c%02d",now.time().hour(),sep,now.time().minute());
			}
			break;
		}
		case UTC:
			if (TODFormat == hhmmss){
				s.sprintf("%02d%c%02d%c%02d",UTCnow.time().hour(),sep,UTCnow.time().minute(),sep,UTCnow.time().second()+leapCorrection);
			}
			else{
				s.sprintf("%02d%c%02d",UTCnow.time().hour(),sep,UTCnow.time().minute());
			}
			break;
		case Unix:
			s.sprintf("%i",(int) now.toTime_t());
			break;
		case GPS:
		{
			int nsecs = now.toTime_t()-GPSEPOCH+leapSeconds+leapCorrection;
			int nweeks = int(nsecs/86400/7);
			s.sprintf("%i",nsecs - nweeks*86400*7);
			break;
		}
	}
	tod->setText(s);
}

void TimeDisplay::showDate(QDateTime & now)
{
		QString s(""),stmp;
		QString sep="";
		
		if (dateFormat & ISOdate){
			s.append(sep);
			s.append(now.toString("yyyy-MM-dd"));		
			sep="  ";
		}
		if (dateFormat & PrettyDate){
			s.append(sep);
			s.append(now.toString("dd MMM yyyy"));
			sep=" ";
		}
		if (dateFormat & MJD){
			s.append(sep);
			int tt = now.toTime_t();
			stmp.sprintf("MJD %d",tt/86400 + 40587);
			s.append(stmp);
			sep=" ";
		}
		if (dateFormat & GPSDayWeek){
			s.append(sep);
			int nsecs = now.toTime_t()-GPSEPOCH+leapSeconds;
			int wn = int(nsecs/86400/7);
			int dn  = int((nsecs- wn*86400*7)/86400);
			stmp.sprintf("Wn %i Dn %i",wn,dn);
			s.append(stmp);
			sep=" ";
		}
		if (dateFormat & DOY){
			s.append(sep);
			int doy=1;
			if (timeScale == UTC || timeScale == Unix){
				QDateTime UTCnow = now.toUTC();
				doy=UTCnow.date().dayOfYear();
			}
			else
				doy=now.date().dayOfYear();
			stmp.sprintf("DOY %d",doy);
			s.append(stmp);
			sep=" ";
		}
		date->setText(s);
}

void TimeDisplay::setTODFontSize()
{
	
	QDesktopWidget *dtw = QApplication::desktop();
	//qDebug() << dtw->screenGeometry().width();
	int w=minimumWidth();
	if (fullScreen)
		w = dtw->screenGeometry().width();
	
	QFont f = tod->font();
	int tw=0;
	QFontMetrics fm(f);
	switch (timeScale)
	{
		case Local: case UTC:
			if (TODFormat == hhmmss)
				tw = fm.width("99:99:99");
			else 
				tw= fm.width("99:99");
			break;
		case Unix:
			tw = fm.width("1360930340");
			break;
		case GPS:
			tw = fm.width("99:99:99"); // looks too big if you use TOW
			break;
	}
	f.setPointSize((0.9*f.pointSize()*w)/tw);
	tod->setFont(f);

}

void TimeDisplay::setDateFontSize()
{
	QFont ftod = tod->font();
	QFont f = date->font();
	f.setPointSize(ftod.pointSize()/4);
	date->setFont(f);
}

void TimeDisplay::setTitleFontSize()
{
	QFont ftod = date->font();
	QFont f = title->font();
	f.setPointSize(ftod.pointSize());
	title->setFont(f);
}

void TimeDisplay::setCalTextFontSize()
{
	QFont ftod = tod->font();
	QFont f = calText->font();
	f.setPointSize(ftod.pointSize()/4);
	calText->setFont(f);
}

void TimeDisplay::toggleDimming()
{
	lowLight = ! lowLight;
	if (dimEnable){
		
		updateDimState();
	}
}

void TimeDisplay::updateLeapSeconds()
{
	
	// If just starting then we need to read the leap file
	//    If we don't have it then we need to fetch it
	//    If we have it then read it
	
	// If the file has already been read, then we need to check
	// expiry and fetch a new file
	//
	
	QDateTime now = QDateTime::currentDateTime();
	
	if (autoUpdateLeapFile){
		if (!leapsInitialized){
			QFileInfo fi(leapFile);
			if (fi.exists()){// Have we got a cached leap second list ?
				readLeapFile();
				if (leapFileExpiry.secsTo(now) > 0){ // time to look for a new one
					qDebug() << "the leap file has expired";
					fetchLeapSeconds();
				}
			}
			else{
				qDebug() << "no cached leap second file";
				fetchLeapSeconds(); // not cached so try to get one
			}
		}
		else{ // we have a file, so check it out ...
			if (leapFileExpiry.secsTo(now) > 0){
				fetchLeapSeconds();
			}
			else{ // have an up to date file so extract the current leap value
				QDateTime now = QDateTime::currentDateTime();
				unsigned int ttnow = now.toTime_t();
	
				for (int i=leapTable.size()-1;i>=0;i--){
					if (ttnow >= leapTable.at(i)->tleap - UNIXEPOCH){
						leapSeconds = leapTable.at(i)->dttaiutc - DELTATAIGPS;
						qDebug() << leapTable.at(i)->tleap << 
							" delta_TAI= " << leapTable.at(i)->dttaiutc << " ls =" << leapSeconds;
						break;
					}
				}
			}
		}
	}
	else{ // using a system-supplied leap second file
		if (!leapsInitialized){
			// Have we got a leap second list 
			QFileInfo fi(leapFile);
			if (fi.exists()){
				if (fi.lastModified() == leapFileLastModified){
					qDebug() << "leap file still out of date";
					return;
				}
				readLeapFile();
				if (leapFileExpiry.secsTo(now) > 0) // out of date
					leapsInitialized=false;
			}
		}
	}
	

}

void TimeDisplay::fetchLeapSeconds()
{
	QDateTime now = QDateTime::currentDateTime();
	qDebug() << lastLeapFileFetch.secsTo(now);
	if (lastLeapFileFetch.secsTo(now) > leapFileCheckInterval){
		qDebug() << "fetching leap second file " << leapFileURL ;
		netManager->get(QNetworkRequest(QUrl(leapFileURL)));
		lastLeapFileFetch = QDateTime::currentDateTime();
		leapFileCheckInterval *= 2;
		if (leapFileCheckInterval >  MAXLEAPCHECKINTERVAL)
			leapFileCheckInterval = MAXLEAPCHECKINTERVAL;
	}
}

void TimeDisplay::readLeapFile()
{

	qDebug() << "reading leap seconds file " << leapFile;
	
	QFile file(leapFile);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
		return;

	while (!leapTable.isEmpty())
		delete leapTable.takeFirst();
	
	QTextStream in(&file);
	unsigned int lastLeap=0,deltaTAI=0;
	
	QRegExp leapInfo("^(\\d{10})\\s+(\\d+)");
	
	while (!in.atEnd()) {
		QString line = in.readLine();
	  if (line.startsWith("#@")) // expiry time in NTP time
		{
			QRegExp re("^#@\\s+(\\d{10})");
			if (re.indexIn(line) != -1)
			{
				QStringList matches = re.capturedTexts();
				if (matches.size() == 2)
				{
					QString m = matches.at(1);
					leapFileExpiry.setTime_t(m.toUInt() - UNIXEPOCH);
					qDebug() << "leap second file expiry time " << leapFileExpiry;
				}
			}
		}
		else if (line.startsWith("#")){// comments, specials we don't care about
		}
		else
		{
			if  (leapInfo.indexIn(line) != -1)
			{
				QStringList matches = leapInfo.capturedTexts();
				if (matches.size() == 3)
				{
					QString m = matches.at(1);
					lastLeap=m.toUInt();
					m=matches.at(2);
					deltaTAI=m.toUInt();
					leapTable.push_back(new LeapInfo(lastLeap,deltaTAI));
				}
			}
		}
	}
	
	file.close();
	QFileInfo fi(leapFile);
	leapFileLastModified=fi.lastModified();
	
	QDateTime now = QDateTime::currentDateTime();
	unsigned int ttnow = now.toTime_t();
	
	for (int i=leapTable.size()-1;i>=0;i--)
	{
		if (ttnow >= leapTable.at(i)->tleap - UNIXEPOCH)
		{
			leapSeconds = leapTable.at(i)->dttaiutc - DELTATAIGPS;
			qDebug() << leapTable.at(i)->tleap << 
				" delta_TAI= " << leapTable.at(i)->dttaiutc << " ls =" << leapSeconds;
			break;
		}
	}
	
	leapsInitialized=true;
	
}


bool TimeDisplay::readConfig(QString s)
{
	QDomDocument doc;
	
	qDebug() << "Using configuration file " << s;
	
	logoChanged=false;
	
	QFile f(s);
	if ( !f.open( QIODevice::ReadOnly) )
	{
		qWarning() << "Can't open " << s;
		return false;
	}
	
	QString err;
	int errlineno,errcolno;
	if ( !doc.setContent( &f,true,&err,&errlineno,&errcolno ) )
	{	
		qWarning() << "PARSE ERROR " << err << " line=" << errlineno;
		f.close();
		return false;
	}
	f.close();
	
	QDomElement elem = doc.documentElement().firstChildElement();
	QString lc;

	while (!elem.isNull())
	{
		qDebug() << elem.tagName() << " " << elem.text();
		lc=elem.text().toLower();
		lc=lc.simplified();
		lc=lc.remove('"');
		if (elem.tagName()=="timezone")
			timezone=elem.text().simplified();
		else if (elem.tagName()=="timescale")
		{
			qDebug() << elem.tagName() << " " << lc;
			if (lc=="local")
				timeScale=Local;
			else if (lc=="utc")
				timeScale=UTC;
			else if (lc=="gps")
				timeScale=GPS;
			else if (lc=="unix")
				timeScale=Unix;
		}
		else if (elem.tagName()=="todformat")
		{
			if (lc=="12 hour")
				hourFormat=TwelveHour;
			else if (lc=="24 hour")
				hourFormat=TwentyFourHour;
		}
		else if (elem.tagName()=="blink")
			blinkSeparator = (lc =="yes");
		else if (elem.tagName()=="fontcolour"){
			lc=elem.text();
			fontColourName=lc.simplified();
		}		
		else if (elem.tagName()=="logo"){
			if (elem.text() != logoImage){
				logoImage=elem.text();
				logoChanged=true;
			}
		}
		else if (elem.tagName()=="background")
			readBackgroundConfig(elem.firstChildElement());
		else if (elem.tagName()=="power")
		{
			QDomElement celem=elem.firstChildElement();
			while(!celem.isNull())
			{
				lc=celem.text().toLower();
				lc=lc.simplified();
				lc=lc.remove('"');
				if (celem.tagName() == "conserve")
				{
					powerManager->enable(lc == "yes");
					qDebug() << "power::conserve=" << lc;
				}
				else if (celem.tagName() == "weekends")
				{
					if (lc=="yes") 
						powerManager->setPolicy(PowerManager::NightTime | PowerManager::Weekends);
					else
						powerManager->setPolicy(PowerManager::NightTime);
					qDebug() << "power::weekends=" << lc;
				}
				else if (celem.tagName() == "on")
				{
					QTime t=QTime::fromString(lc,"hh:mm:ss");
					if (t.isValid())
						powerManager->setOnTime(t);
					else
						qWarning() << "Invalid power on time: " << lc;
				}
				else if (celem.tagName() == "off")
				{
					QTime t=QTime::fromString(lc,"hh:mm:ss");
					if (t.isValid())
						powerManager->setOffTime(t);
					else
						qWarning() << "Invalid power off time: " << lc;
				}
				else if (celem.tagName() == "overridetime")
				{
					powerManager->setOverrideTime(celem.text().toInt());
					qDebug() << "power::overridetime=" << lc;
				}
				celem=celem.nextSiblingElement();
			}
		}
		else if (elem.tagName()=="banners")
		{
			QDomElement celem=elem.firstChildElement();
			while(!celem.isNull())
			{
				QString txt=celem.text().trimmed();
				if (celem.tagName() == "local")
					localTimeBanner=txt;
				else if (celem.tagName() == "unix")
					UnixBanner=txt;
				else if (celem.tagName() == "gps")
					GPSBanner=txt;
				else if (celem.tagName() == "utc")
					UTCBanner=txt;
				celem=celem.nextSiblingElement();
			}
		}
		else if (elem.tagName()=="leapseconds")
		{
			QDomElement celem=elem.firstChildElement();
			while(!celem.isNull())
			{
				if (celem.tagName() == "autoupdate"){
					QString lc=celem.text().toLower();
					lc=lc.simplified();
					lc=lc.remove('"');
					autoUpdateLeapFile= (lc=="yes");
				}
				else if (celem.tagName() == "url"){
					leapFileURL=celem.text().trimmed();
				}
				else if (celem.tagName() == "cachedfile"){
					leapFile=celem.text().trimmed();
				}
				else if (celem.tagName() == "proxyserver"){
					proxyServer=celem.text().trimmed();
				}
				else if (celem.tagName() == "proxyport"){
					proxyPort=celem.text().trimmed().toInt();
				}
				else if (celem.tagName() == "proxyuser"){
					proxyUser=celem.text().trimmed();
				}
				else if (celem.tagName() == "proxypassword"){
					proxyPassword=celem.text().trimmed();
				}
				celem=celem.nextSiblingElement();
			}
			
		}
		else if (elem.tagName()=="dimming"){
			QDomElement celem=elem.firstChildElement();
			
			while(!celem.isNull())
			{
				QString lc=celem.text().toLower();
				lc=lc.simplified();
				lc=lc.remove('"');
				if (celem.tagName() == "enable"){
					dimEnable= (lc=="yes");
				}
				else if (celem.tagName() == "method"){
					if (lc=="vbetool")
						dimMethod=VBETool;
					else if (lc=="software")
						dimMethod=Software;
				}
				else if (celem.tagName() == "level"){
					dimLevel=celem.text().trimmed().toInt();
				}
				else if (celem.tagName() == "file"){
					lightLevelFile=celem.text().trimmed();
				}
				else if (celem.tagName() == "threshold"){
					dimThreshold=celem.text().trimmed().toInt();
				}
				celem=celem.nextSiblingElement();
			}
			
		}
		elem=elem.nextSiblingElement();
	}
	
	if (!autoUpdateLeapFile){
		// clean the URL if necessary
		qDebug() << "leap file URL is " << leapFileURL;
		// I'll be sloppy here - just remove all occurrences of file://
		leapFileURL = leapFileURL.remove("file://");
		qDebug() << "leap file URL is now " << leapFileURL;
		leapFile = leapFileURL;
	}
	
	return true;
}

void TimeDisplay::checkConfigFile(){
	
	QFileInfo fi = QFileInfo(configFile);
	if (fi.lastModified() > configLastModified){
		qDebug() << "TimeDisplay::checkConfigFile()";
		configLastModified = fi.lastModified();
		if (readConfig(configFile)){
			setWidgetStyleSheet();
			setLogoImages();
			
			switch (timeScale){
				case Local:setLocalTime();break;
				case GPS:setGPSTime();break;
				case Unix:setUnixTime();break;
				case UTC:setUTCTime();break;
			}
	
			timezone.prepend(":");
			setenv("TZ",timezone.toStdString().c_str(),1);
			tzset();
			
			setBackground();

		}
	}
}

void TimeDisplay::setWidgetStyleSheet()
{
	// mainly to execute changes in the config file 
	fontColour=QColor(fontColourName);
	dimFontColour=fontColour.darker((int) (100*100/dimLevel));
	QString txtColour;
	txtColour.sprintf("color:rgba(%d,%d,%d,255)",
			fontColour.red(),fontColour.green(),fontColour.blue());
	
	title->setStyleSheet(txtColour); // seems weird but this is the recommended way
	tod->setStyleSheet(txtColour); // seems weird but this is the recommended way
	calText->setStyleSheet(txtColour);
	date->setStyleSheet(txtColour);

}

void TimeDisplay::setLogoImages()
{  
	if (logoChanged){
		
		qDebug() << "TimeDisplay::setLogoImages() changed";
		QPixmap pm = QPixmap(logoImage);
		logo->setPixmap(pm);
		
		if (dimLogo) delete dimLogo;
		
		dimLogo = new QImage(logoImage);
		QImage alpha;
		if (dimLogo->hasAlphaChannel())
			alpha = dimLogo->alphaChannel(); // OBSOLETE may break but pixel() does not return alpha in Qt4.6
		
		for (int i=0;i<dimLogo->width();i++){
			for (int j=0;j<dimLogo->height();j++){
				QColor col = QColor(dimLogo->pixel(i,j));
				QColor newcol = col.darker((int)(100*100/dimLevel));
				QRgb val = newcol.rgba();
				dimLogo->setPixel(i,j,val);
			}
		}
		if (dimLogo->hasAlphaChannel())
			dimLogo->setAlphaChannel(alpha); // OBSOLETE
			
		date->setMinimumHeight(pm.height()+64);
		//logoParentWidget->setFixedSize(pm.width(),pm.height());
	}
	
}		

void	TimeDisplay::writeNTPDatagram()
{
	char pkt[48] = { 
		0xe3, 0x00, 0x04, 0xfa, 
		0x00, 0x01, 0x00, 0x00, 
		0x00, 0x01, 0x00, 0x00, 
		0x00, 0x00, 0x00, 0x00, 
		0x00, 0x00, 0x00, 0x00, 
		0x00, 0x00, 0x00, 0x00, 
		0x00, 0x00, 0x00, 0x00, 
		0x00, 0x00, 0x00, 0x00, 
		0x00, 0x00, 0x00, 0x00, 
		0x00, 0x00, 0x00, 0x00, 
		0x00, 0x00, 0x00, 0x00, 
		0x00, 0x00, 0x00, 0x00};

	if (ntpSocket->writeDatagram(pkt, sizeof(pkt), QHostAddress::LocalHost, 123) != sizeof(pkt)) {
		qDebug() << "ntpSocket error " << ntpSocket->errorString();
	}
}

void TimeDisplay::readBackgroundConfig(QDomElement elem)
{
	QString lc;
	
	reloadBackgroundImage=false;
	
	QString currCalImage=pickCalendarImage();
	
	while (!calendarItems.isEmpty())
		delete calendarItems.takeFirst();

	while (!elem.isNull())
	{
		qDebug() << elem.tagName() << " " << elem.text();
		if (elem.tagName() == "default"){
			if  (elem.text() != defaultImage)
				reloadBackgroundImage=true;
			defaultImage = elem.text().trimmed();
			QFileInfo fi = QFileInfo(defaultImage);
			if (!fi.exists())
				defaultImage="";
		}
		else if (elem.tagName() == "mode"){
			lc=elem.text().toLower();
			lc=lc.simplified();
			int oldMode=backgroundMode;
			if (lc == "fixed")
				backgroundMode = Fixed;
			else if (lc == "slideshow")
				backgroundMode = Slideshow;
			if( oldMode != backgroundMode)
				reloadBackgroundImage=true;
		}
		else if (elem.tagName() == "imagepath"){
			lc=elem.text();
			if (lc != imagePath)
				reloadBackgroundImage=true;
			imagePath=lc;
		}
		else if (elem.tagName() == "event"){
			CalendarItem *calItem = new CalendarItem();
			QDomElement child = elem.firstChildElement();
			
			while (!child.isNull()){
				if (child.tagName() == "startday"){
					QString num = child.text().simplified();
					calItem->startDay=num.toInt();
				}
				if (child.tagName() == "startmonth"){
					QString num = child.text().simplified();
					calItem->startMonth=num.toInt();
				}
				else if (child.tagName() == "stopday"){
					QString num = child.text().simplified();
					calItem->stopDay=num.toInt();
				}
				else if (child.tagName() == "stopmonth"){
					QString num = child.text().simplified();
					calItem->stopMonth=num.toInt();
				}
				else if (child.tagName() == "image"){
					calItem->image = child.text().trimmed();
				}
				else if (child.tagName() == "description"){
					calItem->description = child.text().trimmed();
				}
				child=child.nextSiblingElement();
			}
			calendarItems.append(calItem);
		}
		elem=elem.nextSiblingElement();
	}
	
	// since calendar image overrides, a simple test of whether the current image is the same as that according to the calendar
	// is enough
	QString im=pickCalendarImage();
	if (im != currCalImage ){
		calItemText=""; // in case there is now no calendar item
		reloadBackgroundImage = true;
	}
}

void TimeDisplay::setBackground()
{
	if (!reloadBackgroundImage) return;
	
	qDebug() << "TimeDisplay::setBackground()";
	
	QString im="";
	
	switch (backgroundMode){
		case Fixed:
			im=defaultImage;
			break;
		case Slideshow:
			im = pickSlideShowImage();
			break;
	}
	
	currentImage = im;
	
	// Calendar events override everything else
	im = pickCalendarImage();
	if (!im.isEmpty()){
		calText->setText(calItemText);	
		currentImage=im;
	}
	
	calText->setVisible(!(calItemText.isEmpty()));
	
	if (currentImage.isEmpty())
		setPlainBackground();
	else{
		bkground->setStyleSheet("* {background-color:rgba(0,0,0,0)}");
		if (dimEnable){
			// calculate and cache the dimmed image
			if (NULL != dimImage)
				delete dimImage;
			dimImage = new QImage(currentImage);
			for (int i=0;i<dimImage->width();i++){
				for (int j=0;j<dimImage->height();j++){
					QColor col = QColor(dimImage->pixel(i,j));
					QColor newcol = col.darker((int)(100*100/dimLevel));
					QRgb val = newcol.rgb();
					dimImage->setPixel(i,j,val);
				}
			}
			if (dimActive && lowLight){
				bkground->setPixmap(QPixmap::fromImage(*dimImage));
				return;
			}
		}
		bkground->setPixmap(QPixmap(currentImage));
	}
	
}

QString TimeDisplay::pickCalendarImage()
{
	QString res="";
	QDate today=QDate::currentDate();
	
	for (int i=0;i<calendarItems.length();++i){
		CalendarItem *ci = calendarItems.at(i);
		// calculations are easiest if we make QDates using the current year
		QDate start(today.year(),ci->startMonth,ci->startDay);
		QDate stop(today.year(),ci->stopMonth,ci->stopDay);
		// Take care here with leap years - presumably specifying Feb 29 on a non-leap year results in an invalid date
		if (start.isValid() && stop.isValid() && stop >= start){
			if (today >= start && today <= stop){
				qDebug() << "Picked " << ci->image;
				QFileInfo fi = QFileInfo(ci->image);
				if (fi.exists()){
					res = ci->image;
					calItemText=ci->description;
				}
				else
					res="";
				return res;
			}
		}
	}
	return res;
}

QString TimeDisplay::pickSlideShowImage()
{
	QString res="";
	QDir imPath(imagePath);
	if (!imPath.exists()) return res;
	
	QStringList filters;
	filters << "*.png" << "*.jpeg" << "*.jpg" << "*.tiff" << "*.bmp";
	QFileInfoList imList=imPath.entryInfoList(filters,QDir::Files|QDir::Readable);
	for (int i=0;i<imList.length();i++)
		qDebug() << imList.at(i).absoluteFilePath();
	if (imList.length()==0) return res;
	
	int r = trunc(imList.length()*(double) (random())/(double) (RAND_MAX));
	if (r==imList.length()) r=imList.length()-1;
	res= imList.at(r).absoluteFilePath();
	
	return res;
}
