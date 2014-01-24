/*
------------------------------------------------------------------

This file is part of the Open Ephys GUI
Copyright (C) 2013 Open Ephys

------------------------------------------------------------------

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#include <stdio.h>
#include "ISCAN.h"
#include "Editors/ISCANeditor.h"
#include "../UI/UIComponent.h"

#if JUCE_WINDOWS
#define SLEEPY_TIME Sleep(100);
#else
#define SLEEPY_TIME usleep(100000);
#endif

/*********************************************/
ISCANnode::ISCANnode()
	: GenericProcessor("Eye Tracking")

{
	sendSampleCount = false; // disable updating the continuous buffer sample counts,
								 // since this processor only sends events
	connected = false;
	eyeSamplingRateHz = 120;
	firstTime = true;
	analogXchannel = analogYchannel = analogPupilchannel = -1;
	serialCommunication = true;
	sampleCounter = 0;
	Time t;
	numTicksPerSec = t.getHighResolutionTicksPerSecond();
	offsetX= 0;
	offsetY = 0;
	gainX = 1;
	gainY = 1;
	device = "";
	calibrationMode = 0;
	screenCenterX = 1024/2; 
	screenCenterY = 768/2;
}

ISCANnode::~ISCANnode()
{
	if (connected) {
		int iscan_track_off_code = 129;
		serialPort.writeByte(iscan_track_off_code);
		SLEEPY_TIME
		serialPort.close();
	}
	
}

bool ISCANnode::disable()
{
	return true;
}

	void ISCANnode::setXchannel(int ch)
	{
		analogXchannel = ch;
	}

	void ISCANnode::setYchannel(int ch)
	{
		analogYchannel = ch;
	}


	int ISCANnode::getXchannel()
	{
		return analogXchannel;
	}

	int ISCANnode::getYchannel()
	{
		return analogYchannel;
	}

AudioProcessorEditor* ISCANnode::createEditor()
{
    editor = new ISCANeditor(this, true);
    return editor;
}

void ISCANnode::setSamplingRate(int sampleRate)
{
	eyeSamplingRateHz = sampleRate;
}

int ISCANnode::getAnalogSamplingRate()
{
	return eyeSamplingRateHz;
}


StringArray ISCANnode::getAnalogDeviceNames(Array<int> &channelNumbers)
{
	StringArray names;
	for (int k=0;k<channels.size();k++)
	{
		if (channels[k]->isADCchannel) {
			names.add(channels[k]->getName());
			channelNumbers.add(k);
		}
	}

	return names;
}

StringArray ISCANnode::getDeviceNames()
{
	StringArray names;
	devices = serialPort.getDeviceList();
	for (int k=0;k<devices.size();k++)
	{
		names.add(devices[k].getDeviceName());
	}
	return names;
}

bool ISCANnode::connect(int selectedDevice)
{
	connected = serialPort.setup(devices[selectedDevice-1].getDeviceID(), 115200);
	if (connected)
	{
		device = devices[selectedDevice-1].getDeviceName();
		int maxComponents = 3;	//horizontal H1, vertical V1, pupil diameter D1
		int maxReadQuantum = maxComponents * 7 + 2; // last entry is followed by a tab!
		int lineTerminator = 10;

		int	iscan_track_on_code = 128;
		int iscan_track_off_code = 129;

		serialPort.writeByte(iscan_track_off_code);
		SLEEPY_TIME
		serialPort.flush();
		SLEEPY_TIME
		serialPort.writeByte(iscan_track_on_code);
		SLEEPY_TIME
		int avail = serialPort.available();
		if (avail > 0)
		{
			// connection is successful.
			unsigned char *buf = new unsigned char[avail];
			int res = serialPort.readBytes(buf, avail);
			delete buf;
		}
	}
	return connected;
}

void ISCANnode::updateSettings()
{
	ISCANeditor*ed=(ISCANeditor*)getEditor();
//	ed->refreshAnalogDevices();
}

void ISCANnode::setParameter(int parameterIndex, float newValue)
{
/*
	editor->updateParameterButtons(parameterIndex);

	Parameter& p =  parameters.getReference(parameterIndex);
	p.setValue(newValue, 0);

	threshold = newValue;
	*/
	//std::cout << float(p[0]) << std::endl;

}

int ISCANnode::getGainX()
{
		return gainX;
}

int ISCANnode::getGainY()
{
	return gainY;
}

void ISCANnode::setGainX(int gain)
{
	gainX = gain;
}

void ISCANnode::setGainY(int gain)
{
	gainY = gain;
}

void ISCANnode::handleEvent(int eventType, juce::MidiMessage& event, int samplePosition)
{
	if (eventType == TIMESTAMP)
    {
          const uint8* dataptr = event.getRawData();
	      memcpy(&hardware_timestamp, dataptr + 4, 8); // remember to skip first four bytes
		  memcpy(&software_timestamp, dataptr + 12, 8); // remember to skip first four bytes
	} else if (eventType == NETWORK)
	{
		// CalibrateEyePosition
		StringTS s(event);
		std::vector<String> splitted = s.splitString(' ');
  		if (splitted[0] == "CalibrateEyePosition")
		{
			float fixateXpos = splitted[1].getFloatValue();
			float fixateYpos = splitted[2].getFloatValue();
			int screenWidth = splitted[3].getFloatValue();
			int screenHeight = splitted[4].getFloatValue();

			if (calibrationMode == 1)
			{
				// easy hack. User manually inputs the gain. We only compute offsets.
				screenCenterX = screenWidth/2;
				screenCenterY = screenHeight/2;
				offsetX = prevEyePosition.x - (fixateXpos-screenCenterX) / gainX;
				offsetY = prevEyePosition.y - (fixateYpos-screenCenterY) / gainY;
			} else if (calibrationMode == 2)
			{
				// TODO...
				// We use a linear model that maps analog signal back to pixel coordinates:
				// x (pixel) = a0_x + a1_x * analog_x
				// y (pixel) = a0_y + a1_y * analog_y
				// this means we need at least two calibrate eye position events to solve this equation.
				// a1_x = (x_pix1 - x_pix2) / (analog_x1 - analog_x2)
				// a0_x = x_pix1 - a1_x*analog_x1
				//
				// in the future, this should be replaced with a proper LSQ solution that uses svd to compute the psedou inverse.
			}


		}

	}
}

void ISCANnode::postTimestamppedStringToMidiBuffer(StringTS s, MidiBuffer& events)
{
	uint8* msg_with_ts = new uint8[s.len+8]; // for the two timestamps
	memcpy(msg_with_ts, s.str, s.len);	
	memcpy(msg_with_ts+s.len, &s.timestamp, 8);
	addEvent(events, 
			 (uint8) NETWORK,
			 0,
			 0,
			 (uint8) GENERIC_EVENT,
			 (uint8) s.len+8,
			 msg_with_ts);

	delete msg_with_ts;
}



void ISCANnode::postEyePositionToMidiBuffer(EyePosition p, MidiBuffer& events)
{
	uint8* eyePositionSerialized = new uint8[8+8+8+8+8+8+8]; 
	memcpy(eyePositionSerialized, &p.x, 8);	
	memcpy(eyePositionSerialized+8, &p.y, 8);	
	memcpy(eyePositionSerialized+8+8, &p.xc, 8);	
	memcpy(eyePositionSerialized+8+8+8, &p.yc, 8);	
	memcpy(eyePositionSerialized+8+8+8+8, &p.pupil, 8);	
	memcpy(eyePositionSerialized+8+8+8+8+8, &p.software_timestamp, 8);	
	memcpy(eyePositionSerialized+8+8+8+8+8+8, &p.hardware_timestamp, 8);	

	addEvent(events, 
			 (uint8) EYE_POSITION,
			 0,
			 0,
			 (uint8) GENERIC_EVENT,
			 (uint8) 8+8+8+8+8+8+8+8, //x,y,xc,yc,p+ts
			 eyePositionSerialized);

	delete eyePositionSerialized;
}

int ISCANnode::getCalibrationMode()
{
	return calibrationMode;
}

void ISCANnode::setCalibrationMode(int mode)
{
	calibrationMode = mode;
}

void ISCANnode::setSerialCommunication(bool state)
{
	serialCommunication = state;
}

String ISCANnode::getSerialDevice()
{
	return device;
}
	
void ISCANnode::setSerialDevice(String d)
{
	device = d;
}

bool ISCANnode::getSerialCommunication()
{
	return serialCommunication;
}

double ISCANnode::applyCalibration(double input, int channel)
{
	if (calibrationMode == 0)
		return input;
	if (calibrationMode == 1)
	{
		// fixed gain.
		if (channel == 0)
		{
			// X
			return (input - offsetX) * gainX + screenCenterX;
		} else
		{
			// Y
			return (input - offsetY) * gainY + screenCenterY;
		}
	}

	// TODO, add linear calibration here....
	// channel indicates whether it is X (channel = 0) or Y (channel = 1)
	return input;
}

void ISCANnode::process(AudioSampleBuffer& buffer,
							MidiBuffer& events,
							int& nSamples)
{
	checkForEvents(events);
	
	// read samples and pass them as midi events.
	if (serialCommunication)
	{
		process_serialCommunication(events);
	} else
	{
		if (analogXchannel >= 0 && analogYchannel >= 0 && analogXchannel)
		{
			// use two analog channels (if available).
			float *xbuf = buffer.getSampleData(analogXchannel,0);
			float *ybuf = buffer.getSampleData(analogYchannel,0);
			float *pbuf=nullptr;
			if (analogPupilchannel >= 0)
				pbuf = buffer.getSampleData(analogPupilchannel,0);

			int adcRate = getSampleRate();
			for (int k=0;k<nSamples;k++)
			{
				sampleCounter++;
				if (sampleCounter > adcRate / eyeSamplingRateHz)
				{
					sampleCounter = 0;
					prevEyePosition.software_timestamp = software_timestamp + ((float)k/adcRate)*numTicksPerSec;
					prevEyePosition.hardware_timestamp =hardware_timestamp + k;
					prevEyePosition.x = xbuf[k];
					prevEyePosition.y = ybuf[k];
					prevEyePosition.xc = applyCalibration(prevEyePosition.x,0);
					prevEyePosition.yc = applyCalibration(prevEyePosition.y,1);

					if (analogPupilchannel >= 0)
						prevEyePosition.pupil = pbuf[k];
					else
						prevEyePosition.pupil = 0;

					postEyePositionToMidiBuffer(prevEyePosition, events);
				}
			}
		}
	}

	 nSamples = -10; // make sure this is not processed;
	
}

void ISCANnode::process_serialCommunication(MidiBuffer& events)
{

	int bytesAvail = serialPort.available();
	if (serialBuffer.length() == 0 || firstTime)
	{
		 software_ts = timer.getHighResolutionTicks();
		 firstTime = false;
		 packetCounter = 0;
	}

	if (bytesAvail > 0)
	{
		unsigned char *pktdata = new unsigned char[bytesAvail];
		serialPort.readBytes(pktdata,bytesAvail);
	 
		for (int k=0;k<bytesAvail;k++)
			serialBuffer += pktdata[k];

	delete pktdata;
		
		while (true)
		{
			// now search for a line termination.
			// if one is present, we can extract the packet.
			bool lineTerminationFound = false;
			int line_termination_pos=-1;
			for (int k=0;k<serialBuffer.length();k++)
			{
				if (serialBuffer[k] == '\n')
				{
					lineTerminationFound = true;
					line_termination_pos = k;
					break;
				}
			}
			if (lineTerminationFound)
			{
				EyePosition e;
				if (sscanf(serialBuffer.c_str(),"%lf %lf %lf", &e.x, &e.y, &e.pupil) == 3)
				{
					// post new eye position message.
					packetCounter++;
					if (prevEyePosition.x != e.x || prevEyePosition.y != e.y  || prevEyePosition.pupil != e.pupil)
					{
						// send midi message
						int64 timestamp = software_ts+ packetCounter * 1.0/eyeSamplingRateHz * timer.getHighResolutionTicksPerSecond();
						prevEyePosition.software_timestamp = timestamp;
						prevEyePosition.x = e.x;
						prevEyePosition.y = e.y;
						prevEyePosition.xc = applyCalibration(prevEyePosition.x,0);
						prevEyePosition.yc = applyCalibration(prevEyePosition.y,1);
						prevEyePosition.pupil = e.pupil;
						postEyePositionToMidiBuffer(prevEyePosition, events);

					}
				} 
				serialBuffer = serialBuffer.substr(	1+line_termination_pos);
				
			} else
			{
				break;
			}
		}
	}

}

bool ISCANnode::isReady()
{
   
        return true;
    
}


float ISCANnode::getDefaultSampleRate()
{
    return 30000.0f;
}

int ISCANnode::getDefaultNumOutputs()
{
    return 0;
}

float ISCANnode::getDefaultBitVolts()
{
    return 0.05f;
}

void ISCANnode::enabledState(bool t)
{

    isEnabled = t;

}

bool ISCANnode::isSource()
{
	return false;
}

void ISCANnode::saveCustomParametersToXml(XmlElement* parentElement)
{
    XmlElement* mainNode = parentElement->createNewChildElement("ISCAN");
    mainNode->setAttribute("serialCommunication", serialCommunication);
	mainNode->setAttribute("device", device);
	mainNode->setAttribute("analogXchannel", analogXchannel);
	mainNode->setAttribute("analogYchannel", analogYchannel);

	mainNode->setAttribute("gainX", gainX);
	mainNode->setAttribute("gainY", gainY);

	mainNode->setAttribute("offsetX", offsetX);
	mainNode->setAttribute("offsetY", offsetY);

	mainNode->setAttribute("eyeSamplingRateHz",eyeSamplingRateHz);
	mainNode->setAttribute("calibrationMode",calibrationMode);

	mainNode->setAttribute("screenCenterX",screenCenterX);
	mainNode->setAttribute("screenCenterY",screenCenterY);
}
	
	


void ISCANnode::loadCustomParametersFromXml()
{
	if (parametersAsXml != nullptr)
	{
		forEachXmlChildElement(*parametersAsXml, mainNode)
		{
			if (mainNode->hasTagName("ISCAN"))
			{

				serialCommunication = mainNode->getBoolAttribute("serialCommunication");
				analogXchannel = mainNode->getIntAttribute("analogXchannel");
				analogYchannel = mainNode->getIntAttribute("analogYchannel");
				device = mainNode->getStringAttribute("device");
				screenCenterX = mainNode->getDoubleAttribute("screenCenterX",1024/2);
				screenCenterY = mainNode->getDoubleAttribute("screenCenterY",768/2);
				gainX = mainNode->getDoubleAttribute("gainX",1.0);
				gainY = mainNode->getDoubleAttribute("gainY",1.0);
				offsetX = mainNode->getDoubleAttribute("offsetX",0);
				offsetY= mainNode->getDoubleAttribute("offsetY",0);
				eyeSamplingRateHz = mainNode->getIntAttribute("eyeSamplingRateHz",120);
				calibrationMode = mainNode->getIntAttribute("calibrationMode",0);
				if (serialCommunication && device != "")
				{
					StringArray serialDevices = getDeviceNames();
					for (int k=0;k<serialDevices.size();k++)
					{
						if (serialDevices[k] == device) {
							connect(k+1);
							break;
						}
					}
				}
			}
		}
	}
}

