#include "Control_Thread.hpp"

#include "rodos.h"
#include "matlib.h"

#include "../control/AttitudeEstimation.hpp"
#include "../control/PIDController.hpp"
#include "../control/ReactionwheelControl.hpp"
#include "../control/AngularVelocityControl.hpp"
#include "../control/AngularPositionControl.hpp"

#include "../hardware/ReactionwheelEncoder.hpp"
#include "../hardware/hbridge.hpp"

#include "Debug_Thread.hpp"


static CommBuffer<TimestampedData<Attitude_Data>> AttitudeDataBuffer;
static Subscriber AttitudeDataSubsciber(AttitudeDataTopic, AttitudeDataBuffer);

static CommBuffer<TimestampedData<float>> EncoderDataBuffer;
static Subscriber EncoderDataSubsciber(EncoderDataTopic, EncoderDataBuffer);



void ControlThread::init()
{
	/**
	 * Initialize controller with PID parameters and max. Input/Output values
	 * @todo specify values in "Control_Thread.cpp", dummy data is used right now
	*/
	reactionwheelControl.init(paramsReactionWheelControl, maxVoltage, maxSpeed);
	velocitycontrol.init(paramsVelocityControl, maxSpeed, maxVelocity);
	positionControl.init(paramsPositionControl, maxVelocity);

	/**
	 * Initialize HBridge
	*/
	hbridge.initialization();
}



void ControlThread::run()
{
	TimestampedData<Attitude_Data> AttitudeDataReceiver;
	TimestampedData<float> EncoderDataReceiver;

	while (true)
	{
		AttitudeDataBuffer.get(AttitudeDataReceiver);
		EncoderDataBuffer.get(EncoderDataReceiver);

		modes current_mode = getMode();

		switch (current_mode)
		{

		case Control_Speed:
			float desiredVoltage = reactionwheelControl.update(EncoderDataReceiver);

			hbridge.setVoltage(desiredVoltage);

			break;

		case Control_Vel:
			float desiredSpeed = velocitycontrol.update(AttitudeDataReceiver);

			reactionwheelControl.setDesiredSpeed(desiredSpeed);
			float desiredVoltage = reactionwheelControl.update(EncoderDataReceiver);

			hbridge.setVoltage(desiredVoltage);

			break;

		case Control_Pos:
			float desiredVelocity = positionControl.update(AttitudeDataReceiver);

			velocitycontrol.setDesiredAngularVelocity(desiredVelocity);
			float desiredSpeed = velocitycontrol.update(AttitudeDataReceiver);

			reactionwheelControl.setDesiredSpeed(desiredSpeed);
			float desiredVoltage = reactionwheelControl.update(EncoderDataReceiver);

			hbridge.setVoltage(desiredVoltage);
			
			break;
		
		default:
			break;
		}


		suspendCallerUntil(NOW() + period * MILLISECONDS);
	}
}


ControlThread controlthread;