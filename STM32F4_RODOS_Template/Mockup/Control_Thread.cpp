#include "Control_Thread.hpp"

#include "rodos.h"
#include "matlib.h"

#include "../Modes.hpp"
#include "../OuterLoop/AngularVelocityControl.hpp"
#include "../InnerLoop/Hbridge.hpp"
#include "Adc.hpp"

void ControlThread::init()
{
	// Config
	using namespace config;

	// Initialize HBridge
	hbridge.initialization(pwmFrequency, pwmIncrements);

	// Initialize ADC
	adc.initialization();
}



void ControlThread::run()
{
	float desiredVoltage_percent;

	while(true)
	{	
		if (getMode() == Standby) setMode(Control_Vel);

		desiredVoltage_percent = (adc.getVoltage() / 1500.f) - 1.f;

		int percent = int(desiredVoltage_percent * 100);

        int incrementsSize = 25;

        // Make percent in increments
        int percent_step = ((int)(percent / incrementsSize)) * incrementsSize;
		
		// egde cases
		if (percent < -100 + incrementsSize/2) percent_step = -100;
		else if (percent > 100 - incrementsSize/2) percent_step = 100;

        float out = float(percent_step) * 0.001f;

		if (getMode() == Control_Vel) AngularVelocitySetpointTopic.publish(out);
		
		suspendCallerUntil(NOW() + period * MILLISECONDS);
	}
}


ControlThread controlthread;
