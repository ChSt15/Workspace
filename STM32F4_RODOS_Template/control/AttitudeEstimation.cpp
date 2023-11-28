#include "rodos.h"
#include "matlib.h"

#include "../timestamp.hpp"
#include "../hardware/imu.hpp"

#include "AttitudeEstimation.hpp"


Topic<TimestampedData<Attitude_Data>> AttitudeDataTopic(-1, "AttitudeData");


QEKF::QEKF()
{
	P.r[0][0] = 1;
	P.r[1][1] = 1;
	P.r[2][2] = 1;
	P.r[3][3] = 1;
	P.r[4][4] = 1;
	P.r[5][5] = 1;
	P.r[6][6] = 1;
	P.r[7][7] = 1;
	P.r[8][8] = 1;
	P.r[9][9] = 1;

	Q.r[0][0] = sigma_gyro.x * sigma_gyro.x;
	Q.r[1][1] = sigma_gyro.y * sigma_gyro.y;
	Q.r[2][2] = sigma_gyro.z * sigma_gyro.z;

	Q.r[3][3] = sigma_gyro_drift * sigma_gyro_drift;
	Q.r[4][4] = sigma_gyro_drift * sigma_gyro_drift;
	Q.r[5][5] = sigma_gyro_drift * sigma_gyro_drift;

	R_a.r[0][0] = sigma_accel.x * sigma_accel.x;
	R_a.r[1][1] = sigma_accel.y * sigma_accel.y;
	R_a.r[2][2] = sigma_accel.z * sigma_accel.z;

	R_m.r[0][0] = sigma_yaw * sigma_yaw;

	eye_10x10.r[0][0] = 1;
	eye_10x10.r[1][1] = 1;
	eye_10x10.r[2][2] = 1;
	eye_10x10.r[3][3] = 1;
	eye_10x10.r[4][4] = 1;
	eye_10x10.r[5][5] = 1;
	eye_10x10.r[6][6] = 1;
	eye_10x10.r[7][7] = 1;
	eye_10x10.r[8][8] = 1;
	eye_10x10.r[9][9] = 1;
}

void QEKF::init(IMUData& imudata)
{
	// Calc roll, pitch from accelerometer
	double roll  =  atan2(-imudata.acceleration.y, sqrt(pow(imudata.acceleration.x, 2) + pow(imudata.acceleration.z, 2)));
	double pitch = -atan2(-imudata.acceleration.x, sqrt(pow(imudata.acceleration.y, 2) + pow(imudata.acceleration.z, 2)));

	// Calc yaw from magnetometer
	double magx_h = imudata.magneticField.x * cos(pitch)			 + imudata.magneticField.y * sin(pitch);
	double magy_h = imudata.magneticField.x	* sin(pitch) * sin(roll) + imudata.magneticField.y * cos(roll) - imudata.magneticField.z * sin(roll) * cos(pitch);
	double yaw = atan2(-magy_h, magx_h);

	// Convert to quaternion
	Quaternion_F quat = Quaternion_F(YPR_F(yaw, pitch, roll));
	this->X.r[0][0] = quat.q0;
	this->X.r[1][0] = quat.q.x;
	this->X.r[2][0] = quat.q.y;
	this->X.r[3][0] = quat.q.z;

	last_t = SECONDS_NOW();

	is_initialized = true;
}

#define q0 	X.r[0][0]
#define q1 	X.r[1][0]
#define q2 	X.r[2][0]
#define q3 	X.r[3][0]

#define wx 	X.r[4][0]
#define wy 	X.r[5][0]
#define wz 	X.r[6][0]

#define driftx 	X.r[7][0]
#define drifty 	X.r[8][0]
#define driftz 	X.r[9][0]


TimestampedData<Attitude_Data>& QEKF::estimate(TimestampedData<IMUData>& imudata)
{
	this->propagate(Vector3D_F(imudata.data.angularVelocity.x, imudata.data.angularVelocity.y, imudata.data.angularVelocity.z));

	this->update_accel(Vector3D_F(imudata.data.acceleration.x, imudata.data.acceleration.y, imudata.data.acceleration.z));
	this->update_mag(Vector3D_F(imudata.data.magneticField.x, imudata.data.magneticField.y, imudata.data.magneticField.z));

	data.timestamp = NOW();
	data.data.attitude = Quaternion(q0, q1, q2, q3);
	data.data.angularVelocity = Vector3D(wx, wy, wz);

	return data;
}



void QEKF::propagate(Vector3D_F w)
{
	float dt = SECONDS_NOW() - last_t;
	last_t = SECONDS_NOW();

	float old_q0 = q0;
	float old_q1 = q1;
	float old_q2 = q2;
	float old_q3 = q3;

	float old_wx = wx;
	float old_wy = wy;
	float old_wz = wz;
	
	// State prediction
	q0 = old_q0 + 0.5 * (-old_q1 * old_wx - old_q2 * old_wy - old_q3 * old_wz) * dt;
	q1 = old_q1 + 0.5 * ( old_q0 * old_wx - old_q3 * old_wy + old_q2 * old_wz) * dt;
	q2 = old_q2 + 0.5 * ( old_q3 * old_wx + old_q0 * old_wy - old_q1 * old_wz) * dt;
	q3 = old_q3 + 0.5 * (-old_q2 * old_wx + old_q1 * old_wy + old_q0 * old_wz) * dt;
	wx = w.x - driftx;
	wy = w.y - drifty;
	wz = w.z - driftz;

	// Normalize quaternion
	float quat_length = sqrt(q0*q0 + q1*q1 + q2*q2 + q3*q3);
	q0 = q0 / quat_length;
	q1 = q1 / quat_length;
	q2 = q2 / quat_length;
	q3 = q3 / quat_length;
	
	// Jakobian of state prediction (with respect to X)
	A.r[0][0] =  1;					A.r[0][1] =  0.5 * w.x * dt;	A.r[0][2] =  0.5 * w.y * dt;	A.r[0][3] =  0.5 * w.z * dt;
	A.r[1][0] = -0.5 * w.x * dt;	A.r[1][1] =  1;					A.r[1][2] = -0.5 * w.z * dt;	A.r[1][3] =  0.5 * w.y * dt;
	A.r[2][0] = -0.5 * w.y * dt;	A.r[2][1] =  0.5 * w.z * dt;	A.r[2][2] =  1;					A.r[2][3] = -0.5 * w.x * dt;
	A.r[3][0] = -0.5 * w.z * dt;	A.r[3][1] = -0.5 * w.y * dt;	A.r[3][2] =  0.5 * w.x * dt;	A.r[3][3] =  1;

	A.r[0][4] = -0.5 * old_q1 * dt;	A.r[0][5] = -0.5 * old_q2 * dt;	A.r[0][6] = -0.5 * old_q3 * dt;
	A.r[1][4] =  0.5 * old_q0 * dt;	A.r[1][5] = -0.5 * old_q3 * dt;	A.r[1][6] =  0.5 * old_q2 * dt;
	A.r[2][4] =  0.5 * old_q3 * dt;	A.r[2][5] =  0.5 * old_q0 * dt;	A.r[2][6] = -0.5 * old_q1 * dt;
	A.r[3][4] = -0.5 * old_q2 * dt;	A.r[3][5] =  0.5 * old_q1 * dt;	A.r[3][6] =  0.5 * old_q0 * dt;

	A.r[4][7] = -1;
	A.r[5][8] = -1;
	A.r[6][9] = -1;

	A.r[7][7] = 1;
	A.r[8][8] = 1;
	A.r[9][9] = 1;

	// Jakobian of state prediction (with respect to input w)
	G.r[4][0] = 1;
	G.r[5][1] = 1;
	G.r[6][2] = 1;
	G.r[7][3] = 1;
	G.r[8][4] = 1;
	G.r[9][5] = 1;

	// Covariance prediction
	P = A * P * A.transpose() + G * Q * G.transpose();

	// Force symmetric covariance
	P = (P + P.transpose()) / 2;
}

void QEKF::update_accel(Vector3D_F a)
{
	// Measurment prediction
	z_a.r[0][0] = -2 * (q1*q3 - q0*q2);
	z_a.r[1][0] = -2 * (q2*q3 + q0*q1);
	z_a.r[2][0] = -(q0*q0 - q1*q1 - q2*q2 + q3*q3);

	// Jakobian of mesurment prediction (with respect to X)
	C_a.r[0][0] =  2 * q2;	C_a.r[0][1] = -2 * q3;	C_a.r[0][2] =  2 * q0;	C_a.r[0][3] = -2 * q1;
	C_a.r[1][0] = -2 * q1;	C_a.r[1][1] = -2 * q0;	C_a.r[1][2] = -2 * q3;	C_a.r[1][3] = -2 * q2;
	C_a.r[2][0] = -2 * q0;	C_a.r[2][1] =  2 * q1;	C_a.r[2][2] =  2 * q2;	C_a.r[2][3] = -2 * q3;

	// Measurment innovation
	v_a = a.normalize() - z_a;

	// Covariance innovation
	S_a = C_a * P * C_a.transpose() + R_a;

	// Kalman gain
	K_a = P * C_a.transpose() * S_a.invert();

	// Update state
	X = X + K_a * v_a;

	// Normalize quaternion
	float quat_length = sqrt(q0*q0 + q1*q1 + q2*q2 + q3*q3);
	q0 = q0 / quat_length;
	q1 = q1 / quat_length;
	q2 = q2 / quat_length;
	q3 = q3 / quat_length;

	// Update covariance
	P = (eye_10x10 - K_a * C_a) * P;

	// Force symmetric covariance
	P = (P + P.transpose()) / 2;
}

void QEKF::update_mag(Vector3D_F m)
{
	
	// Rotations
	nav2body = Matrix3D_F(Quaternion_F(q0, q1, q2, q3));
	body2nav = nav2body.transpose();

	// Correct tilt
	Vector3D_F mn = m.matVecMult(body2nav);
	Vector3D_F mnh = Vector3D_F(mn.x, mn.y, 0);
	Vector3D_F mb = mnh.matVecMult(nav2body);
	
	// Yaw measurment 
	y_yaw = atan2(-mb.y, mb.x);
	PRINTF("%f\n\n", rad2Grad(y_yaw));
	// Measurment prediction
	z_yaw = atan2(2 * (q0 * q3 + q1 * q2), 1 - 2 * (q2 * q2 + q3 * q3));

	// Jakobian of mesurment prediction (with respect to X)
	C_m.r[0][0] = (2 * q3 * (1 - 2 * (q3 * q3 + q2 * q2))) / (4 * pow((q3 * q0 + q1 * q2), 2) + pow(1 - 2 * (q3 * q3 + q2 * q2), 2));
	C_m.r[0][1] = (2 * q2 * (1 - 2 * (q3 * q3 + q2 * q2))) / (4 * pow((q2 * q1 + q3 * q0), 2) + pow(1 - 2 * (q3 * q3 + q2 * q2), 2));
	C_m.r[0][2] = (2 * (q1 + 2 * q1 * q2 * q2 + 4 * q0 * q2 * q3 - 2 * q1 *q3 * q3)) / (1 + 4 * (q2 * q2 * q2 * q2 + 2 * q0 * q1 * q2 * q3 + q3 * q3 * (-1 + q0 * q0 + q3 * q3) + q2 * q2 * (-1 + q1 * q1 + 2 * q3 * q3)));
	C_m.r[0][3] = (q0 * (-4 * q2 * q2 + 4 * q3 * q3 + 2) + 8 * q1 * q2 * q3) / (4 * (q0 * q0 - 1) * q3 * q3 + 8 * q0 * q1 * q2 * q3 + 4 * q2 * q2 * (q1 * q1 + 2 * q3 * q3 - 1) + 4 * q2 * q2 * q2 *q2 + 4 * q3 * q3 * q3 * q3 + 1);

	// Measurment innovation
	v_yaw = y_yaw - z_yaw;

	// Covariance innovation
	S_m = C_m * P * C_m.transpose() + R_m;

	// Kalman gain
	K_m = P * C_m.transpose() * S_m.invert();

	// Update state
	X = X + K_m * v_yaw;

	// Normalize quaternion
	float quat_length = sqrt(q0*q0 + q1*q1 + q2*q2 + q3*q3);
	q0 = q0 / quat_length;
	q1 = q1 / quat_length;
	q2 = q2 / quat_length;
	q3 = q3 / quat_length;

	// Update covariance
	P = (eye_10x10 - K_m * C_m) * P;

	// Force symmetric covariance
	P = (P + P.transpose()) / 2;
}


QEKF qekf;
