/******************************************************************************
The ahrs.c in RaspberryPilot project is placed under the MIT license

Copyright (c) 2016 jellyice1986 (Tung-Cheng Wu)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

=============================================================

ATTENTION:

The code in this file is mostly copied and rewritten from

X-IO Technologies:
http://x-io.co.uk/open-source-imu-and-ahrs-algorithms/

******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <math.h>
#include "commonLib.h"
#include "mpu6050.h"
#include "ahrs.h"

//#define MADGWICK_AHRS
#define MAHONY_AHRS

#if defined(MADGWICK_AHRS)
#define Kp 1.f
#elif defined(MAHONY_AHRS)
#define twoKpDef	(2.0f * 0.5f)	// 2 * proportional gain
#define twoKiDef	(2.0f * 0.05f)	// 2 * integral gain											// 2 * integral gain (Ki)
volatile float integralFBx = 0.0f,  integralFBy = 0.0f, integralFBz = 0.0f;	// integral error terms scaled by Ki
#else
#define Kp 2.5f
static float exInt = 0, eyInt = 0, ezInt = 0;
#endif

static struct timeval last_tv;
static float q0 = 1, q1 = 0, q2 = 0, q3 = 0;

float invSqrt(float x);

/**
 * init ahrs
 *
 * @param
 * 		void
 *
 * @return
 *		void
 *
 */
void ahrsInit() {
	q0 = 1;
	q1 = 0;
	q2 = 0;
	q3 = 0;
}

/**
 * fast inverse square root
 *
 * @param x
 * 		input value
 *
 * @return
 *		inverse square
 *
 */
float invSqrt(float x) {
	float halfx = 0.5f * x;
	float y = x;
	long i = *(long*) &y;

	i = 0x5f3759df - (i >> 1);
	y = *(float*) &i;
	y = y * (1.5f - (halfx * y * y));

	return y;
}

/**
 * Madgwick's IMU update method
 *
 * reference:
 * S. O. H. Madgwick, An efficient orientation filter for inertial and inertial/magnetic sensor arrays, Technical report, University of. Bristol University, UK, 2010
 *
 * @param gx
 * 		Gyroscope x axis measurement in radians/s
 *
 * @param gy
 * 		Gyroscope y axis measurement in radians/s
 *
 * @param gz
 * 		Gyroscope z axis measurement in radians/s
 *
 * @param ax
 * 		Accelerometer x axis measurement in any calibrated units
 *
 * @param ay
 * 		Accelerometer y axis measurement in any calibrated units
 *
 * @param az
 * 		Accelerometer z axis measurement in any calibrated units
 *
 * @param q
 * 		quaternion
 *
 * @return
 *		void
 *
 */
void IMUupdate(float gx, float gy, float gz, float ax, float ay, float az,
		float q[]) {

#if defined(MADGWICK_AHRS)

	float recipNorm;
	float s0, s1, s2, s3;
	float qDot1, qDot2, qDot3, qDot4;
	float _2q0, _2q1, _2q2, _2q3, _4q0, _4q1, _4q2 ,_8q1, _8q2, q0q0, q1q1, q2q2, q3q3;
	float timeDiff = 0.f;
	struct timeval tv;

	gettimeofday(&tv, NULL);

	if (last_tv.tv_sec != 0) {
		
		// Rate of change of quaternion from gyroscope
		qDot1 = 0.5f * (-q1 * gx - q2 * gy - q3 * gz);
		qDot2 = 0.5f * (q0 * gx + q2 * gz - q3 * gy);
		qDot3 = 0.5f * (q0 * gy - q1 * gz + q3 * gx);
		qDot4 = 0.5f * (q0 * gz + q1 * gy - q2 * gx);

		// Compute feedback only if accelerometer measurement valid (avoids NaN in accelerometer normalisation)
		if(!((ax == 0.0f) && (ay == 0.0f) && (az == 0.0f))) {
			timeDiff = ((float) (tv.tv_sec - last_tv.tv_sec)
				+ (float) (tv.tv_usec - last_tv.tv_usec) * 0.000001f);
			
			// Normalise accelerometer measurement
			recipNorm = invSqrt(ax * ax + ay * ay + az * az);
			ax *= recipNorm;
			ay *= recipNorm;
			az *= recipNorm;   

			// Auxiliary variables to avoid repeated arithmetic
			_2q0 = 2.0f * q0;
			_2q1 = 2.0f * q1;
			_2q2 = 2.0f * q2;
			_2q3 = 2.0f * q3;
			_4q0 = 4.0f * q0;
			_4q1 = 4.0f * q1;
			_4q2 = 4.0f * q2;
			_8q1 = 8.0f * q1;
			_8q2 = 8.0f * q2;
			q0q0 = q0 * q0;
			q1q1 = q1 * q1;
			q2q2 = q2 * q2;
			q3q3 = q3 * q3;

			// Gradient decent algorithm corrective step
			s0 = _4q0 * q2q2 + _2q2 * ax + _4q0 * q1q1 - _2q1 * ay;
			s1 = _4q1 * q3q3 - _2q3 * ax + 4.0f * q0q0 * q1 - _2q0 * ay - _4q1 + _8q1 * q1q1 + _8q1 * q2q2 + _4q1 * az;
			s2 = 4.0f * q0q0 * q2 + _2q0 * ax + _4q2 * q3q3 - _2q3 * ay - _4q2 + _8q2 * q1q1 + _8q2 * q2q2 + _4q2 * az;
			s3 = 4.0f * q1q1 * q3 - _2q1 * ax + 4.0f * q2q2 * q3 - _2q2 * ay;
			recipNorm = invSqrt(s0 * s0 + s1 * s1 + s2 * s2 + s3 * s3); // normalise step magnitude
			s0 *= recipNorm;
			s1 *= recipNorm;
			s2 *= recipNorm;
			s3 *= recipNorm;

			// Apply feedback step
			qDot1 -= Kp * s0;
			qDot2 -= Kp * s1;
			qDot3 -= Kp * s2;
			qDot4 -= Kp * s3;
		}

			// Integrate rate of change of quaternion to yield quaternion
		q0 += qDot1 * ( timeDiff);
		q1 += qDot2 * ( timeDiff);
		q2 += qDot3 * ( timeDiff);
		q3 += qDot4 * (timeDiff);

		// Normalise quaternion
		recipNorm = invSqrt(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
		q0 *= recipNorm;
		q1 *= recipNorm;
		q2 *= recipNorm;
		q3 *= recipNorm;
		q[0] = q0;
		q[1] = q1;
		q[2] = q2;
		q[3] = q3;
	}

	last_tv.tv_usec = tv.tv_usec;
	last_tv.tv_sec = tv.tv_sec;

#elif defined(MAHONY_AHRS)

	float recipNorm;
	float halfvx, halfvy, halfvz;
	float halfex, halfey, halfez;
	float qa, qb, qc;
	float timeDiff = 0.f;
	struct timeval tv;

	gettimeofday(&tv, NULL);

	if (last_tv.tv_sec != 0) {

	// Compute feedback only if accelerometer measurement valid (avoids NaN in accelerometer normalisation)
		if(!((ax == 0.0f) && (ay == 0.0f) && (az == 0.0f))) {

			timeDiff = ((float) (tv.tv_sec - last_tv.tv_sec)
				+ (float) (tv.tv_usec - last_tv.tv_usec) * 0.000001f);
			
			// Normalise accelerometer measurement
			recipNorm = invSqrt(ax * ax + ay * ay + az * az);
			ax *= recipNorm;
			ay *= recipNorm;
			az *= recipNorm;		
	
			// Estimated direction of gravity and vector perpendicular to magnetic flux
			halfvx = q1 * q3 - q0 * q2;
			halfvy = q0 * q1 + q2 * q3;
			halfvz = q0 * q0 - 0.5f + q3 * q3;
		
			// Error is sum of cross product between estimated and measured direction of gravity
			halfex = (ay * halfvz - az * halfvy);
			halfey = (az * halfvx - ax * halfvz);
			halfez = (ax * halfvy - ay * halfvx);
	
			// Compute and apply integral feedback if enabled
			if(twoKiDef > 0.0f) {
				integralFBx += twoKiDef * halfex * (timeDiff);	// integral error scaled by Ki
				integralFBy += twoKiDef * halfey * (timeDiff);
				integralFBz += twoKiDef * halfez * (timeDiff);
				gx += integralFBx;	// apply integral feedback
				gy += integralFBy;
				gz += integralFBz;
			}
			else {
				integralFBx = 0.0f; // prevent integral windup
				integralFBy = 0.0f;
				integralFBz = 0.0f;
			}
	
			// Apply proportional feedback
			gx += twoKpDef * halfex;
			gy += twoKpDef * halfey;
			gz += twoKpDef * halfez;
		}
		
		// Integrate rate of change of quaternion
		gx *= (0.5f * timeDiff); 	// pre-multiply common factors
		gy *= (0.5f * timeDiff);
		gz *= (0.5f * timeDiff);
		qa = q0;
		qb = q1;
		qc = q2;
		q0 += (-qb * gx - qc * gy - q3 * gz);
		q1 += (qa * gx + qc * gz - q3 * gy);
		q2 += (qa * gy - qb * gz + q3 * gx);
		q3 += (qa * gz + qb * gy - qc * gx); 
		
		// Normalise quaternion
		recipNorm = invSqrt(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
		q0 *= recipNorm;
		q1 *= recipNorm;
		q2 *= recipNorm;
		q3 *= recipNorm;
		q[0] = q0;
		q[1] = q1;
		q[2] = q2;
		q[3] = q3;

	}

	last_tv.tv_usec = tv.tv_usec;
	last_tv.tv_sec = tv.tv_sec;


#else
	
	float norm = 0.f;
	float vx = 0.f, vy = 0.f, vz = 0.f;
	float ex = 0.f, ey = 0.f, ez = 0.f;
	float timeDiff = 0.f;
	struct timeval tv;

	gettimeofday(&tv, NULL);

	if (last_tv.tv_sec != 0) {

		timeDiff = ((float) (tv.tv_sec - last_tv.tv_sec)
				+ (float) (tv.tv_usec - last_tv.tv_usec) * 0.000001f);

		_DEBUG(DEBUG_IMUUPDATE_INTVAL, "(%s-%d) interval=%.5f Sec\n", __func__,
				__LINE__, timeDiff);

		// normalise the measurements
		norm = invSqrt(ax * ax + ay * ay + az * az);
		ax = ax * norm;
		ay = ay * norm;
		az = az * norm;

		// estimated direction of gravity
		vx = 2 * (q1 * q3 - q0 * q2);
		vy = 2 * (q0 * q1 + q2 * q3);
		vz = q0 * q0 - q1 * q1 - q2 * q2 + q3 * q3;

		// error is sum of cross product between reference direction of field and direction measured by sensor
		ex = (ay * vz - az * vy);
		ey = (az * vx - ax * vz);
		ez = (ax * vy - ay * vx);

		// integral error scaled integral gain
		exInt = exInt + (ex * timeDiff);
		eyInt = eyInt + (ey * timeDiff);
		ezInt = ezInt + (ez * timeDiff);

		// adjusted gyroscope measurements
		gx = gx + Kp * ex + exInt;
		gy = gy + Kp * ey + eyInt;
		gz = gz + Kp * ez + ezInt;

		// integrate quaternion rate and normalise
		q0 = q0 + (-q1 * gx - q2 * gy - q3 * gz) * timeDiff * 0.5f;
		q1 = q1 + (q0 * gx + q2 * gz - q3 * gy) * timeDiff * 0.5f;
		q2 = q2 + (q0 * gy - q1 * gz + q3 * gx) * timeDiff * 0.5f;
		q3 = q3 + (q0 * gz + q1 * gy - q2 * gx) * timeDiff * 0.5f;

		// normalise quaternion
		norm = invSqrt(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
		q0 = q0 * norm;
		q1 = q1 * norm;
		q2 = q2 * norm;
		q3 = q3 * norm;
		q[0] = q0;
		q[1] = q1;
		q[2] = q2;
		q[3] = q3;

	}

	last_tv.tv_usec = tv.tv_usec;
	last_tv.tv_sec = tv.tv_sec;

#endif
}

