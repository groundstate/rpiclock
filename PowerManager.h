//
// rpiclock - a time display program for the Raspberry Pi/Linu
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

#ifndef __POWER_MANAGER_H_
#define __POWER_MANAGER_H_


#include <QDateTime>

class PowerManager
{
	public:

		enum Policy {NightTime=0x01,Weekends=0x02};
		enum VideoTool  {RaspberryPi,XSet,Unknown};
		enum PowerState {PowerSaveActive=0x01,PowerSaveInactive=0x02,PowerSaveOverridden=0x04};
		
		PowerManager(QTime &,QTime &);
		~PowerManager();

		void update();
		
		void enable(bool);
		bool isEnabled();
		void setOnTime(QTime &);
		void setOffTime(QTime &);
		void setOverrideTime(int);
		void setXWindowsVT(int);
        
		void setPolicy(int);
		void deviceEvent();
		
	private:
		
		void disableOSPowerManagment();
		void displayOn();
		void displayOff();
		
		int policy;
		QTime on,off;
		QDateTime overrideStop;
		int overrideTime; // in minutes;
		
		bool enabled;
		int powerState;
		
		int videoTool;
		QString videoToolCmd;
		int XWindowsVT; // VT X windows runs on (RPi only)
};

#endif
