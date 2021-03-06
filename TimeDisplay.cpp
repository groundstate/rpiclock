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

// Notes on faking a leap second
// (1) Set up ntpd to use  "LOCAL CLOCK" refclock
// (2) Stop ntpd
// (3) Set time using date
// (4) Start ntpd
// (5) Set leap second flag using 'leapset' (adjtimex?)
// (6) Run rpiclock with --nocheck

#include <sys/timex.h>
#include <netinet/in.h> // For ntohl() (byte order conversion) 

#include <iostream>

#include <QAction>
#include <QApplication>
#include <QDebug>
#include <QDesktopWidget>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QtGui>
#include <QtNetwork>
#include <QTime>
#include <QRegExp>
#include <QUdpSocket>
#include <QVBoxLayout>

#include "PowerManager.h"
#include "TimeDisplay.h"

#define VERSION_INFO "v0.1.3"

#define LEAPSECONDS 18     // whatever's current
#define GPSEPOCH 315964800 // GPS epoch in the Unix time scale
#define UNIXEPOCH 0x83aa7e80  //  Unix epoch in the NTP time scale 
#define DELTATAIGPS 19     // 
#define MAXLEAPCHECKINTERVAL 1048576 // two weeks should be good enough
#define NTPTIMEOUT 64 // waiting time for a NTP response, before declaring no sync

extern QApplication *app;

TimeDisplay::TimeDisplay(QStringList &args):QWidget()
{

	srandom(currentDateTime().toTime_t());
	
	fullScreen=true;
	checkSync=true;
	
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
			std::cout << "--nocheck      disable checking of host synchronization" << std::endl;
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
		else if (args.at(i) == "--nocheck"){
			checkSync=false;
		}
		else{
			std::cout << "rpiclock: Unknown option '"<< args.at(i).toStdString() << "'" << std::endl;
			std::cout << "rpiclock: Use --help to get a list of available command line options"<< std::endl;
			
			exit(EXIT_SUCCESS);
		}
	}
	
	//QRect screen = app->desktop()->screenGeometry();
	
	setWindowTitle(tr("rpiclock"));
	
	if (fullScreen)
		setWindowState(windowState() ^ Qt::WindowFullScreen);
	else{
		// this is just a bodge for testing on a desktop so be kind
		setMinimumSize(QSize(1920,1200)); // change as appropriate for the background image
	}
	
	setMouseTracking(true); // so that mouse movements wake up the display
	QCursor curs;
	curs.setShape(Qt::BlankCursor);
	setCursor(curs);
	cursor().setPos(0,0);
	
	setDefaults();
	
	QTime on(9,0,0);
	QTime off(17,0,0);
	
	powerManager=new PowerManager(on,off);
	powerManager->enable(false);
	
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
	
	if (!configFile.isNull()){
		QFileInfo fi = QFileInfo(configFile);
		configLastModified=fi.lastModified();
		readConfig(configFile);
	}
	
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
	bkground->setAlignment(Qt::AlignCenter);
	vb->addWidget(bkground);

	vb = new QVBoxLayout(bkground);
	vb->setContentsMargins(0,0,0,0);

	QHBoxLayout * hb = new QHBoxLayout();
	vb->addLayout(hb,1);
	title = new QLabel("",bkground);
	title->setFont(QFont("Monospace"));
	title->setAlignment(Qt::AlignCenter);
	hb->addWidget(title);

	hb = new QHBoxLayout();
	vb->addLayout(hb,1);
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
	vb->addLayout(hb,1);
	date = new QLabel("56337",bkground);
	date->setFont(QFont("Monospace"));
	date->setAlignment(Qt::AlignCenter);
	hb->addWidget(date);

	hb = new QHBoxLayout();
	hb->setContentsMargins(32,0,32,12);
	vb->addLayout(hb,0);
	imageInfo = new QLabel("Credit",bkground);
	imageInfo->setFont(QFont("Monospace"));
	imageInfo->setAlignment(Qt::AlignRight);
	hb->addWidget( imageInfo);
	if (!showImageInfo) imageInfo->hide();
	
	setWidgetStyleSheet();
	
	logoParentWidget= new QWidget(date);
	hb=new QHBoxLayout(logoParentWidget);
	hb->setContentsMargins(32,32,32,0);
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
		case Countdown:setCountdownTime();break;
	}
	
	timezone.prepend(":");
	setenv("TZ",timezone.toStdString().c_str(),1);
	tzset();
	
	updateBackgroundImage(true); // force the first one
	
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
    #if QT_VERSION >= 0x050000
    updateTimer->setTimerType(Qt::PreciseTimer);
    #endif
	connect(updateTimer,SIGNAL(timeout()),this,SLOT(updateTime()));
	QDateTime now = currentDateTime();
	updateTimer->start(wakeupTime-now.time().msec()); // don't try to get the first blink right

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

	
	if (adjustFontColour && autoAdjustFontColour){
		QTime t;
		t.start();
		adjustFontColour=false;
		
		QImage im = bkground->pixmap()->toImage();
	
		QRect  imr = QRect(0,0,im.width(),im.height());
		// Compute the origin of the centred image in the parent window co-ordinate system
		// There doesn't seem to be a way to get Qt to tell you this
		int dx = 0; 
		int dy = 0;
		if (im.width() != width()){
			dx = (width() - im.width())/2;
		}
		
		if (im.height() != height()){
			dy = (height() - im.height())/2;
		}
		
		QRect lr = tod->geometry();
		// Now translate this rectangle so that it is in the coordinate system
		// of the image
		lr.translate(-dx,-dy);
		
		QRect ir = imr.intersected(lr);
	
		if (ir.isValid()){ // overlap
			double lum=0;
			for (int i=ir.left();i<=ir.right();i++) // bit slow - 20 ms on 2.4 GHz Xeon
				for (int j=ir.top();j<=ir.bottom();j++){
					QRgb px = im.pixel(i,j);
					// luminance (r * 0.3) + (g * 0.59) + (b * 0.11). 
					lum += qBlue(px)*0.11 + qRed(px)*0.3 + qGreen(px)*0.59;
				}
			lum = lum/((ir.right()-ir.left())*(ir.bottom()-ir.top()))/255.0;
			qDebug() << lum  << " " << t.elapsed();
			QColor oldColour = fontColour;
			if (lum <= 0.5)
				fontColour = darkBkFontColour;
			else
				fontColour = lightBkFontColour;

			if (fontColour != oldColour){
				QString txtColour;
				txtColour.sprintf("color:rgba(%d,%d,%d,255)",
					fontColour.red(),fontColour.green(),fontColour.blue());
				title->setStyleSheet(txtColour);
				tod->setStyleSheet(txtColour);
				calText->setStyleSheet(txtColour);
				date->setStyleSheet(txtColour);
				imageInfo->setStyleSheet(txtColour);
			}
		}
	}
	updateLeapSeconds();
	powerManager->update();
	
	QDateTime now = currentDateTime();
	syncOK = syncOK && (lastNTPReply.secsTo(now)< NTPTIMEOUT); 
	
	if (!checkSync || syncOK){
		showTime(now);
		showDate(now);
	}
	else{
		tod->setText("--:--:--");
		date->setText("Unsynchronised");
	}
	
	if (checkPPS){
		updatePPSState();
	}
	
	// Call repaint on tod and date ??
	
	updateBackgroundImage(false); // slow, so delay this
	
	updateDimState(); // slow so delay this
	
	now = currentDateTime();
	
	if (blinkSeparator){
		if (now.time().msec() < blinkDelay) 
			updateTimer->start(blinkDelay-now.time().msec());
		else
			updateTimer->start(wakeupTime-blinkDelay);
	}
	else
		updateTimer->start(wakeupTime-now.time().msec());
	
	checkConfigFile();
	if (checkSync) writeNTPDatagram();
	
}

void TimeDisplay::updateDimState(){
	if (!dimEnable) return;
	bool lowLight=false;
	
	// Check the sensor reading
	QFile lf(lightLevelFile);
	if (lf.open(QFile::ReadOnly)){
		QTextStream ts(&lf);
		int currLightLevel=255;
		ts >> currLightLevel;
		if (ts.status() == QTextStream::Ok)
			lowLight=currLightLevel < dimThreshold;
		else
			return;
	}
	else	
		return;
	
	if (lowLight)
		integratedLightLevel--;
	else
		integratedLightLevel++;
	
	if (integratedLightLevel <= 0) 
		integratedLightLevel=0;
	if (integratedLightLevel > integrationPeriod)  
		integratedLightLevel = integrationPeriod;
	
	if (!dimActive && integratedLightLevel==0){
		dimActive=true;
		
		QString txtColour;
		txtColour.sprintf("color:rgba(%d,%d,%d,255)",
				dimFontColour.red(),dimFontColour.green(),dimFontColour.blue());
		title->setStyleSheet(txtColour);
		tod->setStyleSheet(txtColour);
		calText->setStyleSheet(txtColour);
		date->setStyleSheet(txtColour);
		imageInfo->setStyleSheet(txtColour);
		forceUpdate();
		bkground->setPixmap(QPixmap::fromImage(*dimImage));
		logo->setPixmap(QPixmap::fromImage(*dimLogo));
	}
	else if (dimActive && integratedLightLevel==5){
		dimActive=false;
		QString txtColour;
		txtColour.sprintf("color:rgba(%d,%d,%d,255)",
				fontColour.red(),fontColour.green(),fontColour.blue());
		title->setStyleSheet(txtColour);
		tod->setStyleSheet(txtColour);
		calText->setStyleSheet(txtColour);
		date->setStyleSheet(txtColour);
		imageInfo->setStyleSheet(txtColour);
		forceUpdate();
		bkground->setPixmap(QPixmap(currentImage));
		logo->setPixmap(logoImage);
	}
	else if (dimActive){
	}
	
	
}

void TimeDisplay::updatePPSState(){
	ppsOK=false;
}

void TimeDisplay::toggleFullScreen()
{
	fullScreen=!fullScreen;
	setWindowState(windowState() ^ Qt::WindowFullScreen);
	setTODFontSize(); 
	setDateFontSize();
	setTitleFontSize();
	setCalTextFontSize();
	setImageCreditFontSize();
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
	setImageCreditFontSize();
	updateActions();
	setConfig("timescale","Local");
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
	setImageCreditFontSize();
	updateActions();
	setConfig("timescale","UTC");
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
	setImageCreditFontSize();
	updateActions();
	setConfig("timescale","UNIX");
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
	setImageCreditFontSize();
	updateActions();
	setConfig("timescale","GPS");
}

void TimeDisplay::setCountdownTime()
{
	timeScale=Countdown;
	dateFormat=PrettyDate;
	title->setText(BeforeCountdownBanner);
	setTODFontSize(); 
	setDateFontSize();
	setTitleFontSize();
	setCalTextFontSize();
	setImageCreditFontSize();
	updateActions();
	setConfig("timescale","Countdown");
}

void TimeDisplay::togglePowerManagement()
{
	powerManager->enable(!powerManager->isEnabled());
}

void TimeDisplay::toggleSeparatorBlinking()
{
	blinkSeparator=!blinkSeparator;
	setConfig("blink",(blinkSeparator?"yes":"no"));
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
	setConfig("todformat","12 hour");
}

void TimeDisplay::set24HourFormat()
{
	setConfig("todformat","24 hour");
	hourFormat=TwentyFourHour;
}
	

void TimeDisplay::setTimeOffset()
{
	bool ok;
	int ret = QInputDialog::getInt(this,"Time offset","Set time offset in minutes",timeOffset,0,1440*3,1,&ok);
	if (ok)
		timeOffset=ret;
}

void TimeDisplay::setConfig(QString tag,QString val)
{
	QDomNodeList nl = doc.elementsByTagName(tag);
	if (nl.count() == 1){
		QDomElement el = nl.at(0).toElement();
		if (!(el.isNull())){
			nl.at(0).toElement().firstChild().setNodeValue(val);
		}
	}
}

void TimeDisplay::saveSettings()
{
	QFile file( configFile );
	if( !file.open( QIODevice::WriteOnly | QIODevice::Text ) ){
		qDebug( "Failed to open file for writing." );
		return;
	}
	QTextStream stream( &file );
	stream << doc.toString();
	file.close();
	
	QFileInfo fi = QFileInfo(configFile);
	configLastModified= fi.lastModified();
		
}

void TimeDisplay::quit()
{
	close();
}

void TimeDisplay::createContextMenu(const QPoint &)
{

	toggleFullScreenAction->setChecked(fullScreen);

	QMenu *cm = new QMenu(this);
	
	cm->addAction(localTimeAction);
	cm->addAction(UTCTimeAction);
	cm->addAction(UnixTimeAction);
	cm->addAction(GPSTimeAction);
	cm->addAction(CountdownTimeAction);
	
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
	cm->addAction(offsetTime);
	
	cm->addSeparator();
	cm->addAction(saveSettingsAction);
	
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
	char *pdata = data.data();
	unsigned int u1 = ntohl(*reinterpret_cast<const int*>(pdata));
	unsigned int b4=(u1 & 0xff000000)>>24;
	unsigned int b3=(u1 & 0x00ff0000)>>16;
	char b1=(u1 & 0x000000ff);
	qDebug() << "reply li="<< ((b4 >> 6) & 0x03) << " vn=" << ((b4 >>3) & 0x07) << " st = " << b3 << " pr=" <<  (int) b1;
	pdata += 16;
	unsigned int u2 = ntohl(*reinterpret_cast<const int*>(pdata));
	qDebug() << u2-UNIXEPOCH << " " << time(NULL);
	//syncOK = ((b4 >> 6) & 0x03) != 3;
	// syncOK = (b3 >0) && (b3<16);
	syncOK= time(NULL) - (u2-UNIXEPOCH) < syncLossThreshold && ((b3 >0) && (b3<16)); 
	lastNTPReply=currentDateTime();
}

//
//
//

void TimeDisplay::setDefaults()
{
	syncLossThreshold = 3600;
	
	timeScale=Local;
	TODFormat=hhmmss;
	dateFormat=PrettyDate;
	blinkSeparator=false;
	blinkDelay=500;
	leapSeconds = LEAPSECONDS;
	hourFormat=TwelveHour;
	timezone="Australia/Sydney";
	
	displayDelay=0;
 	wakeupTime=1000+displayDelay;

	defaultImage="";
	backgroundMode = Fixed;
	imagePath = "";
	calItemText="";
	logoImage="";
	dimLogo=NULL;
	slideshowPeriod=1;
	showImageInfo=true;
	
	localTimeBanner="Local time";
	UTCBanner="Coordinated Universal Time";
	UnixBanner="Unix time";
	GPSBanner="GPS time";
	BeforeCountdownBanner="Until ...";
	AfterCountdownBanner="Since ...";
	
	countdownDateTime=QDateTime(QDate(2017,9,29),QTime(16,36)); // defaults to local time
	
	// leap seconds
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
	// Look for a system leap file
	// On modern Linuxen, typically this is /usr/share/zoneinfo/leap-seconds.list
	QFileInfo fi("/usr/share/zoneinfo/leap-seconds.list");
	if (fi.exists())
		leapFile = "/usr/share/zoneinfo/leap-seconds.list";
	
	if (leapFile.isEmpty()){
		fi.setFile("/etc/leap-seconds.list");
		if (fi.exists())
			leapFile="/etc/leap-seconds.list";
	}
	
	if (leapFile.isEmpty()){
		fi.setFile("/etc/ntp/leap-seconds.list");
		if (fi.exists())
			leapFile="/etc/ntp/leap-seconds.list";
	}
	qDebug() << "Found system leap file " << leapFile;
	
	// system PPS
	checkPPS=false;
	ppsDeviceNumber=0;
	ppsOK=false;
	
	// dimming
	dimEnable=true;
	dimMethod=Software;
	dimLevel=25;
	dimActive=false;
	dimImage=NULL;
	lightLevelFile="";
	dimThreshold=0;
	integrationPeriod=5;
	integratedLightLevel=integrationPeriod;
	
		
	autoAdjustFontColour=false;
	lightBkFontColourName="yellow";
	darkBkFontColourName="white";
	currFontColourName = darkBkFontColourName;
	adjustFontColour=false;
	
	timeOffset=0; // for debugging
}

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
	
	CountdownTimeAction = actionGroup->addAction(QIcon(), tr("Countdown time"));
	CountdownTimeAction->setStatusTip(tr("Show Countdown time"));
	connect(CountdownTimeAction, SIGNAL(triggered()), this, SLOT(setCountdownTime()));
	CountdownTimeAction->setCheckable(true);
	CountdownTimeAction->setChecked(timeScale==Countdown);
	
	
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
	
	offsetTime = new QAction(QIcon(), tr("Set time offset"), this);
	offsetTime->setStatusTip(tr("Set time offset"));
	addAction(offsetTime);
	connect(offsetTime, SIGNAL(triggered()), this, SLOT(setTimeOffset()));
	
	saveSettingsAction = new QAction(QIcon(), tr("Save settings"), this);
	saveSettingsAction->setStatusTip(tr("Save settings"));
	addAction(saveSettingsAction);
	connect(saveSettingsAction, SIGNAL(triggered()), this, SLOT(saveSettings()));
	
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
	struct timex tx;
	tx.modes=0;
	int ret = adjtimex(&tx);
	qDebug() << UTCnow.time() <<  " " <<  UTCnow.time().msec() <<" " << ret << " " << tx.status ;
	if (UTCnow.time().hour() == 23 && UTCnow.time().minute() == 59 && UTCnow.time().second() ==59) // a small sanity check
	{
		tx.modes=0;		
		ret = adjtimex(&tx);

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
		case Countdown:
		{
			int dt = now.toTime_t() - countdownDateTime.toTime_t();
			if (dt <0){
				title->setText(BeforeCountdownBanner);
				dt *= -1;
			}
			else{
				title->setText(AfterCountdownBanner);
			}
			s.sprintf("%i s", dt); 
			break;
		}
	}
	tod->setText(s);
}

void TimeDisplay::showDate(QDateTime & now)
{
		QString s(""),stmp;
		QString sep="";
		
		QDateTime tmpdt;
		if (timeScale != Countdown)
			tmpdt=now;
		else
			tmpdt=countdownDateTime;
		
		if (dateFormat & ISOdate){
			s.append(sep);
			s.append(tmpdt.toString("yyyy-MM-dd"));		
			sep="  ";
		}
		if (dateFormat & PrettyDate){
			s.append(sep);
			s.append(tmpdt.toString("dd MMM yyyy"));
            s.remove('.'); // kludge Qt5 is adding a period after month name. Locale?
			sep=" ";
		}
		if (dateFormat & MJD){
			s.append(sep);
			int tt = tmpdt.toTime_t();
			stmp.sprintf("MJD %d",tt/86400 + 40587);
			s.append(stmp);
			sep=" ";
		}
		if (dateFormat & GPSDayWeek){
			s.append(sep);
			int nsecs = tmpdt.toTime_t()-GPSEPOCH+leapSeconds;
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
				QDateTime UTCtmpdt = tmpdt.toUTC();
				doy=UTCtmpdt.date().dayOfYear();
			}
			else
				doy=tmpdt.date().dayOfYear();
			stmp.sprintf("DOY %d",doy);
			s.append(stmp);
			sep=" ";
		}
		date->setText(s);
}

void TimeDisplay::forceUpdate()
{
	QDateTime now = QDateTime::currentDateTime();
	if (!checkSync || syncOK){
		showTime(now);
		showDate(now);
	}
	else{
		tod->setText("--:--:--");
		date->setText("Unsynchronised");
	}
	tod->repaint();
	date->repaint();
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
		case Countdown:
			tw=fm.width("999999999 s");
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

void TimeDisplay::setImageCreditFontSize()
{
	QFont ftod = tod->font();
	QFont f = imageInfo->font();
	f.setPointSize(ftod.pointSize()/12);
	imageInfo->setFont(f);
}

void TimeDisplay::updateLeapSeconds()
{
	
	// If just starting then we need to read the leap file
	//    If we don't have it then we need to fetch it
	//    If we have it then read it
	
	// If the file has already been read, then we need to check
	// expiry and fetch a new file
	//
	
	QDateTime now = currentDateTime();
	
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
				QDateTime now = currentDateTime();
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
	QDateTime now = currentDateTime();
	qDebug() << lastLeapFileFetch.secsTo(now);
	if (lastLeapFileFetch.secsTo(now) > leapFileCheckInterval){
		qDebug() << "fetching leap second file " << leapFileURL ;
		netManager->get(QNetworkRequest(QUrl(leapFileURL)));
		lastLeapFileFetch = currentDateTime();
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
	
	QDateTime now = currentDateTime();
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
	
	proxyServer="";
	proxyPort=-1;
	proxyUser="";
	proxyPassword="";
	
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
		lc=elem.text().toLower();
		lc=lc.simplified();
		lc=lc.remove('"');
		if (elem.tagName()=="timezone")
			timezone=elem.text().simplified();
		else if (elem.tagName()=="timescale")
		{
			if (lc=="local")
				timeScale=Local;
			else if (lc=="utc")
				timeScale=UTC;
			else if (lc=="gps")
				timeScale=GPS;
			else if (lc=="unix")
				timeScale=Unix;
			else if (lc=="countdown")
				timeScale=Countdown;
		}
		else if (elem.tagName()=="todformat")
		{
			if (lc=="12 hour")
				hourFormat=TwelveHour;
			else if (lc=="24 hour")
				hourFormat=TwentyFourHour;
		}
		else if (elem.tagName()=="countdowndate")
		{
			QDateTime tmp = QDateTime::fromString(lc,"yyyy-MM-dd HH:mm:ss");
			if (tmp.isValid())
				countdownDateTime = tmp;
		}
		else if (elem.tagName()=="delay"){
			displayDelay=elem.text().toInt();
			wakeupTime = 1000+displayDelay;
		}
		else if (elem.tagName()=="blink")
			blinkSeparator = (lc =="yes");
		else if (elem.tagName()=="fontcolour"){
			lc=elem.text();
			currFontColourName=lc.simplified();
		}		
		else if (elem.tagName()=="logo"){
			if (elem.text() != logoImage){
				logoImage=elem.text();
				logoChanged=true;
			}
		}
		else if (elem.tagName()=="background")
			readBackgroundConfig(elem.firstChildElement());
		else if (elem.tagName()=="font"){
			QDomElement celem=elem.firstChildElement();
			while(!celem.isNull())
			{
				lc=celem.text().toLower();
				lc=lc.simplified();
				lc=lc.remove('"');
				if (celem.tagName() == "autoadjustcolour"){
					autoAdjustFontColour = (lc=="yes");
				}
				else if (celem.tagName() =="lightbkcolour"){
					lightBkFontColourName = lc;
					lightBkFontColour = QColor(lightBkFontColourName );
				}
				else if (celem.tagName() =="darkbkcolour"){
					darkBkFontColourName = lc;
					darkBkFontColour = QColor(darkBkFontColourName); 
				}
				celem=celem.nextSiblingElement();
			}
		}
		else if (elem.tagName()=="power")
		{
			QDomElement celem=elem.firstChildElement();
			while(!celem.isNull())
			{
				lc=celem.text().toLower();
				lc=lc.simplified();
				lc=lc.remove('"');
				if (celem.tagName() == "conserve")
					powerManager->enable(lc == "yes");
				else if (celem.tagName() == "weekends")
				{
					if (lc=="yes") 
						powerManager->setPolicy(PowerManager::NightTime | PowerManager::Weekends);
					else
						powerManager->setPolicy(PowerManager::NightTime);
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
				}
				else if (celem.tagName() == "xwinvt")
				{
					powerManager->setXWindowsVT(celem.text().toInt());
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
				else if (celem.tagName() == "countdown"){
					BeforeCountdownBanner="Until " + txt;
					AfterCountdownBanner="Since " + txt;
				}
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
	
	if (!autoUpdateLeapFile && !leapFileURL.isEmpty()){ // empty means use system file
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
				case Countdown:setCountdownTime();break;
			}
	
			timezone.prepend(":");
			setenv("TZ",timezone.toStdString().c_str(),1);
			tzset();
			
			if (backgroundChanged) updateBackgroundImage(true);
			
			if (proxyServer != "" && proxyPort != -1){ // need minimal config for proxy server
				
				if (NULL==netManager){ // may have just configured it 
					netManager = new QNetworkAccessManager(this);
					netManager->setProxy(QNetworkProxy(QNetworkProxy::HttpProxy,proxyServer,proxyPort,proxyUser,proxyPassword)); // UNTESTED
					connect(netManager, SIGNAL(finished(QNetworkReply*)),
						this, SLOT(replyFinished(QNetworkReply*)));
				}
				else{ // valid new config and currently configured 
					QNetworkProxy np = netManager->proxy();
					if (np.hostName() != proxyServer || np.port() != proxyPort ||
							np.user() != proxyUser || np.password() !=  proxyPassword){ // if it ch         ed
						delete netManager;
						netManager = new QNetworkAccessManager(this);
						netManager->setProxy(QNetworkProxy(QNetworkProxy::HttpProxy,proxyServer,proxyPort,proxyUser,proxyPassword)); // UNTESTED
						connect(netManager, SIGNAL(finished(QNetworkReply*)),
							this, SLOT(replyFinished(QNetworkReply*)));
					}
				}
			}
			
		}
	}
}

void TimeDisplay::setWidgetStyleSheet()
{
	// mainly to execute changes in the config file 
	fontColour=QColor(currFontColourName);
	dimFontColour=fontColour.darker((int) (100*100/dimLevel));
	QString txtColour;
	txtColour.sprintf("color:rgba(%d,%d,%d,255)",
			fontColour.red(),fontColour.green(),fontColour.blue());
	
	title->setStyleSheet(txtColour); // seems weird but this is the recommended way
	tod->setStyleSheet(txtColour);  //  still seems weird but this is the recommended way
	calText->setStyleSheet(txtColour);
	date->setStyleSheet(txtColour);
	imageInfo->setStyleSheet(txtColour);
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
	unsigned char pkt[48] = { 
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

	if (ntpSocket->writeDatagram((char *) pkt, sizeof(pkt), QHostAddress::LocalHost, 123) != sizeof(pkt)) {
		qDebug() << "ntpSocket error " << ntpSocket->errorString();
	}
}

void TimeDisplay::readBackgroundConfig(QDomElement elem)
{
	QString lc;
	
	backgroundChanged=false;
	
	QString currCalImage=pickCalendarImage();
	
	while (!calendarItems.isEmpty())
		delete calendarItems.takeFirst();

	while (!elem.isNull())
	{
		//qDebug() << elem.tagName() << " " << elem.text();
		if (elem.tagName() == "default"){
			if  (elem.text() != defaultImage)
				backgroundChanged=true;
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
				backgroundChanged=true;
		}
		else if (elem.tagName() == "imagepath"){
			lc=elem.text();
			if (lc != imagePath)
				backgroundChanged=true;
			imagePath=lc;
		}
		else if (elem.tagName() == "showinfo"){
			lc=elem.text().toLower();
			showImageInfo = (lc == "yes");
		}
		else if (elem.tagName() == "slideshowperiod"){
			int oldSlideshowPeriod=slideshowPeriod;
			slideshowPeriod=elem.text().toInt();
			if (slideshowPeriod < 1)
				slideshowPeriod=1;
			if (oldSlideshowPeriod != slideshowPeriod)
				backgroundChanged=true;
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
		backgroundChanged = true;
	}
}

void TimeDisplay::setBackgroundFromCalendar()
{
	calItemText="";
	QString im = pickCalendarImage();
	if (!im.isEmpty()){
		calText->setText(calItemText);	
		currentImage=im;
	}
	calText->setVisible(!(calItemText.isEmpty()));
}

void TimeDisplay::setBackgroundFromSlideShow()
{
	currentImage=pickSlideShowImage();
	nextSlideUpdate=currentDateTime();
	int secs = nextSlideUpdate.time().minute()*60 +  nextSlideUpdate.time().second();
	nextSlideUpdate=nextSlideUpdate.addSecs(3600*slideshowPeriod-secs);
}


void TimeDisplay::updateBackgroundImage(bool force)
{
	
	bool updateImage=force;
	QDateTime now = currentDateTime();
	
	if (force){
		qDebug() << "Forcing image update";
		currentImage=defaultImage; // if a calendar item has become inactive, then this (and the next line) puts us in the right state
		if (backgroundMode == Slideshow)
			setBackgroundFromSlideShow();
		setBackgroundFromCalendar(); // this overrides everything
	}
	else{ // determine whether the backgound image must be updated
		QString im=currentImage;
		if (backgroundMode==Fixed)
			currentImage=defaultImage;
		if (backgroundMode == Slideshow && (now > nextSlideUpdate)){
			setBackgroundFromSlideShow();
		}
		setBackgroundFromCalendar();
		updateImage = (im != currentImage);
	}
	
	lastBackgroundCheck = now;
	
	if (!updateImage) return;
	
	forceUpdate();

	if (currentImage.isEmpty()){
		//bkground->setStyleSheet("QLabel#Background {background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,"
	//												 "stop: 0 #3c001e, stop: 0.2 #500130,"
  //                         "stop: 0.8 #500130, stop: 1.0 #3c001e)}");
		bkground->setStyleSheet("QLabel#Background {background-color:rgba(80,1,48,255)}");
		bkground->setPixmap(QPixmap(""));
		defaultImage="";
	}
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
			if (dimActive){
				bkground->setPixmap(QPixmap::fromImage(*dimImage));
				return;
			}
		}
		bkground->setPixmap(QPixmap(currentImage));
		imageInfo->setText(makeImageInfo(currentImage));
		adjustFontColour=true;
	}

	
}

QString TimeDisplay::pickCalendarImage()
{
	
	QString res="";
	QDate today=currentDateTime().date();
	
	for (int i=0;i<calendarItems.length();++i){
		CalendarItem *ci = calendarItems.at(i);
		// calculations are easiest if we make QDates using the current year
		QDate start(today.year(),ci->startMonth,ci->startDay);
		QDate stop(today.year(),ci->stopMonth,ci->stopDay);
		// Take care here with leap years - presumably specifying Feb 29 on a non-leap year results in an invalid date
		if (start.isValid() && stop.isValid() && stop >= start){
			if (today >= start && today <= stop){
				qDebug() << "Picked calendar image" << ci->image;
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
	if (imList.length()==0) return res;
	
	int r = trunc(imList.length()*(double) (random())/(double) (RAND_MAX));
	if (r==imList.length()) r=imList.length()-1;
	res= imList.at(r).absoluteFilePath();
	qDebug() << "Picked slide show image " << res;
	return res;
}

QString TimeDisplay::makeImageInfo(QString &fname)
{
	// Parses the image filename into a formatted string
	// The image file name should be in the format AUTHOR__TITLE__whatever
	// Anything before the first separator is taken to be the AUTHOR
	// If the second separator is missing, then the title is left blank
	QString info="";
	QFileInfo fi(fname);
	QStringList tmp=fi.baseName().split("__");
	qDebug() << tmp;
	if (2==tmp.size()){ // Author only
		info=tmp.at(0);
	}
	else if (3==tmp.size()){
		info=tmp.at(0)+" - "+tmp.at(1);
	}
	qDebug() << info;
	return info;
}

QDateTime TimeDisplay::currentDateTime(){
	// This is for debugging - it allows us to add some extra time to the current time to force events
	QDateTime now = QDateTime::currentDateTime();
	now=now.addSecs(timeOffset*60);
	return now;
}
