#include <iostream>
#include <ros/ros.h>
#include <ros/console.h>
#include <sensor_msgs/Imu.h>
#include <sensor_msgs/Range.h>
#include <nav_msgs/Odometry.h>
#include <Eigen/Eigen>
#include <Eigen/Geometry>
#include <ros/time.h>
#include <tf/transform_broadcaster.h>
#include <tf/transform_datatypes.h>
#include <math.h>

#define DEBUG_ODOM true
#define DEBUG_IMU false
#define DEBUG_TF false

using namespace std;
using namespace Eigen;
ros::Publisher odom_pub;
MatrixXd Q = MatrixXd::Identity(12, 12); // For propogate, IMU noise
MatrixXd Rt = MatrixXd::Identity(6, 6);   // For update, Odometry noise


// ros::Time current_time, last_time;
ros::Time current_time;
ros::Time last_time;

/*  Mean and covariance matrixs
    ps: present state
    ba: state propogated
    ns: next state
	[x1] x(0)  x(1)  x(2) = position, x y z
	[x2] x(3)  x(4)  x(5) = orientation, roll pitch yaw, ZXY Euler
	[x3] x(6)  x(7)  x(8) = linear velocity, x y z
	[x4] x(9)  x(10) x(11)= gyroscope bias
	[x5] x(12) x(13) x(14)= accelerator bias   							*/
VectorXd mean_ps= MatrixXd::Zero(15, 1 );
VectorXd mean_ba= MatrixXd::Zero(15, 1 );
VectorXd mean_ns= MatrixXd::Zero(15, 1 );
MatrixXd cov_ps = MatrixXd::Zero(15, 15);
MatrixXd cov_ba = MatrixXd::Zero(15, 15);
MatrixXd cov_ns = MatrixXd::Zero(15, 15);
/* Observation Model	*/
VectorXd g_ut(6);

// Initially use a constant, later need to read from the environment
float g = 9.81;
bool IMU_UPDATED = false;
bool CAMERA_UPDATED = false;

/*  Process model, since IMU is an internal measurement
    the gyro bias and accelerator bias is part to the state
    From the process model, IMU measurement - bias = real state
    Nov. 16th, 2016 note:
    1. Everything IMU has an input, propogate once;
    2. Getting g ~ 9.8 m.s^-2 is tricky
    3. Use MATLAB to from the A, B, and U
    Optimal:
    4. The best way to handle timestamp difference is to store the propogated
       and origin data, and trace back to the most recent propogated value
       and repropogate after the odometry reading comes
*/
void imu_callback(const sensor_msgs::Imu::ConstPtr &msg)
{
	IMU_UPDATED = true;
	#if DEBUG_IMU
		// cout << "Before process, the mean_ps is: " << endl << mean_ps << endl;
		// cout << "The cov_ps is: " << endl << cov_ps << endl;
    	ROS_INFO("Imu Seq: [%d]", msg->header.seq);
    	ROS_INFO("Imu linear acceleration x: [%f], y: [%f], z: [%f]", \
		msg->linear_acceleration.x,msg->linear_acceleration.y,msg->linear_acceleration.z);
    	ROS_INFO("Imu angular velocity x: [%f], y: [%f], z: [%f]", \
    	msg->angular_velocity.x,msg->angular_velocity.y,msg->angular_velocity.z);
	#endif
    MatrixXd At = MatrixXd::Identity(15, 15);
    MatrixXd Ut = MatrixXd::Identity(15, 12);
    MatrixXd Ft = MatrixXd::Identity(15, 15);
    MatrixXd Vt = MatrixXd::Identity(15, 12);
    last_time = current_time;
	float dt = 0.0;
	if	(current_time < msg->header.stamp) { // Check for its first time
		current_time = msg->header.stamp;
    	dt = current_time.toSec() - last_time.toSec();
	}  else  {
		current_time = msg->header.stamp;
		dt = 0.0;
		return;
	}

	// double x11= mean_ps(0), x12= mean_ps(1), x13= mean_ps(2);
	double x21= mean_ps(3), x22= mean_ps(4), x23= mean_ps(5);
	double x31= mean_ps(6), x32= mean_ps(7), x33= mean_ps(8);
	double x41= mean_ps(9), x42= mean_ps(10),x43= mean_ps(11);
	double x51= mean_ps(12),x52= mean_ps(13),x53= mean_ps(14);
	// input u : acceleration, augular_velocity
	double am1= msg->linear_acceleration.x;
	double am2= msg->linear_acceleration.y;
	double am3= msg->linear_acceleration.z;
	double wm1= msg->angular_velocity.x;
	double wm2= msg->angular_velocity.y;
	double wm3= msg->angular_velocity.z;
	// g_ut << am1, am2, am3, wm1, wm2, wm3;
	// noise n: linear_acceleration_covariance, angular_velocity_covariance,
	//          acc bias noise, gyro bias noise
	double na1= 0 , na2= 0, na3= 0;
	double ng1= 0 , ng2= 0, ng3= 0;
	double nba1=0 , nba2=0, nba3=0;
	double nbg1=0 , nbg2=0, nbg3=0;

	/**************************************************************************/
	/* updated to f(mu_t_1, u_t, 0), At(mu_t_1, u_t, 0), and Ut               */
	/* Generated by matlab ELEC6910P_Project2Phase2.m                         */
	/**************************************************************************/

	VectorXd f_t_1(15);
	f_t_1 <<
		x31,
		x32,
		x33,
		wm1*cos(x22) - x41*cos(x22) + wm3*sin(x22) - x43*sin(x22),
		-(- wm2*cos(x21) + x42*cos(x21) + wm3*cos(x22)*sin(x21) - x43*cos(x22)*sin(x21) - wm1*sin(x21)*sin(x22) + x41*sin(x21)*sin(x22))/cos(x21),
		-(- wm3*cos(x22) + x43*cos(x22) + wm1*sin(x22) - x41*sin(x22))/cos(x21),
		(cos(x23)*sin(x22) - cos(x22)*sin(x21)*sin(x23))*(- am3 + x53) - (cos(x22)*cos(x23) + sin(x21)*sin(x22)*sin(x23))*(- am1 + x51) - cos(x21)*sin(x23)*(- am2 + x52),
		(cos(x22)*sin(x23) - cos(x23)*sin(x21)*sin(x22))*(- am1 + x51) - (sin(x22)*sin(x23) + cos(x22)*cos(x23)*sin(x21))*(- am3 + x53) - cos(x21)*cos(x23)*(- am2 + x52),
		sin(x21)*(- am2 + x52) - cos(x21)*cos(x22)*(- am3 + x53) - cos(x21)*sin(x22)*(- am1 + x51) - g,
		0,
		0,
		0,
		0,
		0,
		0;

	At <<
		0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0,                                                   wm3*cos(x22) - x43*cos(x22) - wm1*sin(x22) + x41*sin(x22),                                                                                                                                                                             0, 0, 0, 0,                     -cos(x22),  0,                    -sin(x22),                                                0,                  0,                                                0,
		0, 0, 0,                              (- wm3*cos(x22) + x43*cos(x22) + wm1*sin(x22) - x41*sin(x22))/cos(x21) * cos(x21),                      -(sin(x21)*(- wm1*cos(x22) + x41*cos(x22) - wm3*sin(x22) + x43*sin(x22)))/cos(x21),                                                                                                                                                                             0, 0, 0, 0, -(sin(x21)*sin(x22))/cos(x21), -1, (cos(x22)*sin(x21))/cos(x21),                                                0,                  0,                                                0,
		0, 0, 0,                  -(sin(x21)*(- wm3*cos(x22) + x43*cos(x22) + wm1*sin(x22) - x41*sin(x22)))/cos(x21) * cos(x21),                                  (- wm1*cos(x22) + x41*cos(x22) - wm3*sin(x22) + x43*sin(x22))/cos(x21),                                                                                                                                                                             0, 0, 0, 0,             sin(x22)/cos(x21),  0,           -cos(x22)/cos(x21),                                                0,                  0,                                                0,
		0, 0, 0, sin(x21)*sin(x23)*(- am2 + x52) - cos(x21)*cos(x22)*sin(x23)*(- am3 + x53) - cos(x21)*sin(x22)*sin(x23)*(- am1 + x51),   (cos(x23)*sin(x22) - cos(x22)*sin(x21)*sin(x23))*(- am1 + x51) + (cos(x22)*cos(x23) + sin(x21)*sin(x22)*sin(x23))*(- am3 + x53), (cos(x22)*sin(x23) - cos(x23)*sin(x21)*sin(x22))*(- am1 + x51) - (sin(x22)*sin(x23) + cos(x22)*cos(x23)*sin(x21))*(- am3 + x53) - cos(x21)*cos(x23)*(- am2 + x52), 0, 0, 0,                             0,  0,                            0, - cos(x22)*cos(x23) - sin(x21)*sin(x22)*sin(x23), -cos(x21)*sin(x23),   cos(x23)*sin(x22) - cos(x22)*sin(x21)*sin(x23),
		0, 0, 0, cos(x23)*sin(x21)*(- am2 + x52) - cos(x21)*cos(x23)*sin(x22)*(- am1 + x51) - cos(x21)*cos(x22)*cos(x23)*(- am3 + x53), - (sin(x22)*sin(x23) + cos(x22)*cos(x23)*sin(x21))*(- am1 + x51) - (cos(x22)*sin(x23) - cos(x23)*sin(x21)*sin(x22))*(- am3 + x53), (cos(x22)*cos(x23) + sin(x21)*sin(x22)*sin(x23))*(- am1 + x51) - (cos(x23)*sin(x22) - cos(x22)*sin(x21)*sin(x23))*(- am3 + x53) + cos(x21)*sin(x23)*(- am2 + x52), 0, 0, 0,                             0,  0,                            0,   cos(x22)*sin(x23) - cos(x23)*sin(x21)*sin(x22), -cos(x21)*cos(x23), - sin(x22)*sin(x23) - cos(x22)*cos(x23)*sin(x21),
		0, 0, 0,                            cos(x21)*(- am2 + x52) + cos(x22)*sin(x21)*(- am3 + x53) + sin(x21)*sin(x22)*(- am1 + x51),                                                                 cos(x21)*sin(x22)*(- am3 + x53) - cos(x21)*cos(x22)*(- am1 + x51),                                                                                                                                                                             0, 0, 0, 0,                             0,  0,                            0,                               -cos(x21)*sin(x22),           sin(x21),                               -cos(x21)*cos(x22),
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0;

	Ut <<
		0,                  0,                                                0,                             0,  0,                            0, 0, 0, 0, 0, 0, 0,
		0,                  0,                                                0,                             0,  0,                            0, 0, 0, 0, 0, 0, 0,
		0,                  0,                                                0,                             0,  0,                            0, 0, 0, 0, 0, 0, 0,
		0,                  0,                                                0,                     -cos(x22),  0,                    -sin(x22), 0, 0, 0, 0, 0, 0,
		0,                  0,                                                0, -(sin(x21)*sin(x22))/cos(x21), -1, (cos(x22)*sin(x21))/cos(x21), 0, 0, 0, 0, 0, 0,
		0,                  0,                                                0,             sin(x22)/cos(x21),  0,           -cos(x22)/cos(x21), 0, 0, 0, 0, 0, 0,
	 - cos(x22)*cos(x23) - sin(x21)*sin(x22)*sin(x23), -cos(x21)*sin(x23),   cos(x23)*sin(x22) - cos(x22)*sin(x21)*sin(x23),             0, 0, 0, 0, 0, 0, 0, 0, 0,
	   cos(x22)*sin(x23) - cos(x23)*sin(x21)*sin(x22), -cos(x21)*cos(x23), - sin(x22)*sin(x23) - cos(x22)*cos(x23)*sin(x21),             0, 0, 0, 0, 0, 0, 0, 0, 0,
	                               -cos(x21)*sin(x22),           sin(x21),                               -cos(x21)*cos(x22),             0, 0, 0, 0, 0, 0, 0, 0, 0,
        0,                  0,                                                0,                             0,  0,                            0, 0, 0, 0, 1, 0, 0,
        0,                  0,                                                0,                             0,  0,                            0, 0, 0, 0, 0, 1, 0,
        0,                  0,                                                0,                             0,  0,                            0, 0, 0, 0, 0, 0, 1,
        0,                  0,                                                0,                             0,  0,                            0, 1, 0, 0, 0, 0, 0,
        0,                  0,                                                0,                             0,  0,                            0, 0, 1, 0, 0, 0, 0,
        0,                  0,                                                0,                             0,  0,                            0, 0, 0, 1, 0, 0, 0;

	Ft = MatrixXd::Identity(15, 15) + dt * At;
    Vt = dt * Ut;

    // Calculate the propogagted mean and covariance
    mean_ba = mean_ps + dt * f_t_1;
    cov_ba = Ft * cov_ps * Ft.transpose() + Vt * Q * Vt.transpose();
	#if DEBUG_IMU
		cout<< "After process, the mean_ba became" << endl << mean_ba << endl;
		cout<< "The cov_ba became" << endl << cov_ba << endl;
	#endif
}


void odom_callback(const nav_msgs::Odometry::ConstPtr &msg)
{
	CAMERA_UPDATED = true;
    //your code for update
    //camera position in the IMU frame = (0, -0.04, -0.02)
    //camera orientaion in the IMU frame = Quaternion(0, 0, 1, 0); w x y z, respectively
    //			   RotationMatrix << -1, 0, 0,
    //							      0, 1, 0,
    //                                0, 0, -1;

	VectorXd zt(6); // Camera reading in x,y,z, ZXY Euler

	/*                     Transformation from TF                           */
	// Get the world frame in camera frame transformation from the msg
	tf::Transform camera_pose_cw;
	tf::poseMsgToTF(msg->pose.pose, camera_pose_cw);

	// Record the camera frame in the IMU frame from TA
	tf::Transform transform_ic;
	transform_ic.setOrigin( tf::Vector3(0, -0.04, -0.02) );
	transform_ic.setRotation( tf::Quaternion(0, 0, -1, 0) );
	tf::Transform camera_pose_iw = transform_ic * camera_pose_cw;
	tf::Transform camera_pose_wi = camera_pose_iw.inverse();

	geometry_msgs::Transform camera_pose_wi_geo;
	tf::transformTFToMsg(camera_pose_wi, camera_pose_wi_geo);

	/*                     Transformation from Eigen                       */
	geometry_msgs::Quaternion q_cw = msg->pose.pose.orientation;
	// camera to tag world
	Matrix3d R_cw = Quaterniond(q_cw.w, q_cw.x, q_cw.y, q_cw.z).toRotationMatrix();
	Vector3d T_cw;
	T_cw << msg->pose.pose.position.x, msg->pose.pose.position.y, msg->pose.pose.position.z;
	MatrixXd H_cw(4,4);
	H_cw.col(0) << R_cw.col(0), 0;
	H_cw.col(1) << R_cw.col(1), 0;
	H_cw.col(2) << R_cw.col(2), 0;
	H_cw.col(3) << T_cw   , 1;

	// IMU to camera frame
	Matrix3d R_ic = Quaterniond(0, 0, -1, 0).toRotationMatrix();
    Vector3d T_ic = Vector3d(0, -0.04, -0.02);
	MatrixXd H_ic(4,4);
	H_ic.col(0) << R_ic.col(0), 0;
	H_ic.col(1) << R_ic.col(1), 0;
	H_ic.col(2) << R_ic.col(2), 0;
	H_ic.col(3) << T_ic   , 1;

	// IMU to tag world
	// Matrix3d R_iw = R_ic * R_cw;
	// Vector3d T_iw = R_ic * T_cw + T_ic;

	// tag world to IMU
	MatrixXd H_wi(4,4);
	H_wi = H_cw.inverse() * H_ic.inverse();
	// Matrix3d R_wi = R_iw.inverse();
	// Vector3d T_wi = -R_wi*T_iw;
	Matrix3d R_wi;
	Vector3d T_wi;
	R_wi = H_wi.topLeftCorner(3, 3);
	T_wi = H_wi.topRightCorner(3, 1);
	Quaterniond R_wi_q(R_wi);

	#if DEBUG_TF
		cout << "camera_pose_wi_geo transformation from TF is: " << endl;
		cout << camera_pose_wi_geo.translation.x << endl;
		cout << camera_pose_wi_geo.translation.y << endl;
		cout << camera_pose_wi_geo.translation.z << endl;
		cout << "quaternion from TF is: " << endl;
		cout << camera_pose_wi_geo.rotation.w << endl;
		cout << camera_pose_wi_geo.rotation.x << endl;
		cout << camera_pose_wi_geo.rotation.y << endl;
		cout << camera_pose_wi_geo.rotation.z << endl;
		cout << "camera_pose_wi_geo transformation from Eigen is: " << endl;
		cout << T_wi(0) << endl;
		cout << T_wi(1) << endl;
		cout << T_wi(2) << endl;
		cout << "quaternion from Eigen is: " << endl;
		cout << R_wi_q.w() << endl;
		cout << R_wi_q.x() << endl;
		cout << R_wi_q.y() << endl;
		cout << R_wi_q.z() << endl;
	#endif
	if (DEBUG_TF) {
		zt(0) = camera_pose_wi_geo.translation.x;
		zt(1) = camera_pose_wi_geo.translation.y;
		zt(2) = camera_pose_wi_geo.translation.z;
		// From quaternion to rotation matrix and then ZXY Euler
		Eigen::Quaterniond R_wi_quat;
		R_wi_quat.w() = camera_pose_wi_geo.rotation.w;
		R_wi_quat.x() = camera_pose_wi_geo.rotation.x;
		R_wi_quat.y() = camera_pose_wi_geo.rotation.y;
		R_wi_quat.z() = camera_pose_wi_geo.rotation.z;
		Matrix3d R_wi_tf = R_wi_quat.toRotationMatrix();
		// JacobiSVD<MatrixXd> svd_tf(R_wi_tf, ComputeFullU | ComputeFullV);
		// Matrix3d R_wi_tf_norm = svd_tf.matrixU() * svd_tf.matrixV().transpose();
		// zt(3) = asin(R_wi_tf_norm(1, 2)); // roll
		// zt(4) = atan2(-R_wi_tf_norm(2, 0), R_wi_tf_norm(2, 2)); // pitch
		// zt(5) = atan2(-R_wi_tf_norm(0, 1), R_wi_tf_norm(1, 1)); // yaw
		zt(3) = asin(R_wi_tf(1, 2)); // roll
		zt(4) = atan2(-R_wi_tf(2, 0) / cos(zt(3)), R_wi_tf(2, 2) / cos(zt(3)) ); // pitch
		zt(5) = atan2(-R_wi_tf(0, 1) / cos(zt(3)), R_wi_tf(1, 1) / cos(zt(3)) ); // yaw
	}
	else {
		// Use the result from Eigen in rviz
		zt.head(3) = T_wi;
		// zt(0) = T_wi(0);
		// zt(1) = T_wi(1);
		// zt(2) = T_wi(2);
		// JacobiSVD<MatrixXd> svd(R_wi, ComputeFullU | ComputeFullV);
		// Matrix3d R_wi_norm = svd.matrixU() * svd.matrixV().transpose();
		// zt(3) = asin(R_wi_norm(1, 2)); // roll
		// zt(4) = atan2(-R_wi_norm(2, 0), R_wi_norm(2, 2)); // pitch
		// zt(5) = atan2(-R_wi_norm(0, 1), R_wi_norm(1, 1)); // yaw
		zt(3) = asin(R_wi(1, 2)); // roll
		zt(4) = atan2(-R_wi(2, 0), R_wi(2, 2)); // pitch
		zt(5) = atan2(-R_wi(0, 1), R_wi(1, 1)); // yaw
	}

	if (msg->header.seq == 0) { // first time callback, initialize all messages
		mean_ps << zt, MatrixXd::Zero(9, 1);
		mean_ba << zt, MatrixXd::Zero(9, 1);
		mean_ns << zt, MatrixXd::Zero(9, 1);
	}

	// Check if the angle passes the singularity point for ZXY Euler angle
	double phi_ppg = mean_ba(3);
	double the_ppg = mean_ba(4);
	double psi_ppg = mean_ba(5);
	double phi = zt(3);
	double the = zt(4);
	double psi = zt(5);
	// if (phi_ppg - phi >  2 * M_PI) { zt(3) += 2 * M_PI; cout<<" phi changes up   2*pi: " << phi << endl; }
	// if (phi_ppg - phi < -2 * M_PI) { zt(3) -= 2 * M_PI; cout<<" phi changes down 2*pi: " << phi << endl; }
	// if (the_ppg - the >  2 * M_PI) { zt(4) += 2 * M_PI; cout<<" the changes up   2*pi: " << the << endl; }
	// if (the_ppg - the < -2 * M_PI) { zt(4) -= 2 * M_PI; cout<<" the changes down 2*pi: " << the << endl; }
	// if (psi_ppg - psi >  2 * M_PI) { zt(5) += 2 * M_PI; cout<<" psi changes up   2*pi: " << psi << endl; }
	// if (psi_ppg - psi < -2 * M_PI) { zt(5) -= 2 * M_PI; cout<<" psi changes down 2*pi: " << psi << endl; }

    #if DEBUG_ODOM
        // cout<<" The x of camera: " << zt(0) <<endl;
        // cout<<" The y of camera: " << zt(1) <<endl;
        // cout<<" The z of camera: " << zt(2) <<endl;
        // cout<<" The roll of camera in ZXY Euler angle : " << phi <<endl;
        // cout<<" The pitch of camera in ZXY Euler angle: " << the <<endl;
        // cout<<" The yaw of camera in ZXY Euler angle  : " << psi <<endl;
		cout<<" zt(3) : " << zt(3) <<endl;
        cout<<" zt(4) : " << zt(4) <<endl;
        cout<<" zt(5) : " << zt(5) <<endl;
        // cout<<" The pose of camera is in the coordinate frame: " <<  msg->header.frame_id <<endl;
        // cout<<" The twist of camera is in the child frame: " <<  msg->child_frame_id <<endl;
    #endif

    /*     Update, with C and W matrix										  */
    /*     Linear for this case, but still use  Extended Kalman Filter        */
    MatrixXd Kt = MatrixXd::Identity(15, 6); // Kalman
    MatrixXd Ct = MatrixXd::Identity(6, 15);
    // MatrixXd Wt = MatrixXd::Identity(6, 6);

    Kt = cov_ba * Ct.transpose() * ((Ct * cov_ba * Ct.transpose() + Rt).inverse());
	g_ut << mean_ba(0), mean_ba(1), mean_ba(2), mean_ba(3), mean_ba(4), mean_ba(5);
    mean_ns = mean_ba + Kt * (zt - g_ut);
    cov_ns  = cov_ba  + Kt * Ct * cov_ba;
    mean_ps = mean_ns;
    cov_ps  = cov_ns;

    AngleAxisd rollAngle(mean_ns(3), Vector3d::UnitX());
    AngleAxisd pitchAngle(mean_ns(4), Vector3d::UnitY());
    AngleAxisd yawAngle(mean_ns(5), Vector3d::UnitZ());

    Quaternion<double> Q_output = yawAngle * rollAngle * pitchAngle;

    nav_msgs::Odometry ekf_odom;
	if	(IMU_UPDATED) {
		ekf_odom.header.seq = msg->header.seq;
		ekf_odom.header.stamp = msg->header.stamp;
		ekf_odom.header.frame_id = "world";
		ekf_odom.pose.pose.position.x = mean_ns(0);
		ekf_odom.pose.pose.position.y = mean_ns(1);
		ekf_odom.pose.pose.position.z = mean_ns(2);
		ekf_odom.pose.pose.orientation.w = Q_output.w();
		ekf_odom.pose.pose.orientation.x = Q_output.x();
		ekf_odom.pose.pose.orientation.y = Q_output.y();
		ekf_odom.pose.pose.orientation.z = Q_output.z();
	}
	else {
		ekf_odom = *msg;
	}

    odom_pub.publish(ekf_odom);

	#if DEBUG_ODOM
		// cout<<" The Kalman gain is:" << endl << Kt << endl;
		// cout<<" The mean_ns is:" << endl << mean_ns << endl;
		// cout<<" The cov_ns is:" << endl << cov_ns << endl;
		// cout<<" The g_ut is:" << endl << g_ut << endl;
		cout<<" The odometry before EKF:" << endl;
        ROS_INFO("Seq: [%d]", msg->header.seq);
		ROS_INFO("Position-> x: [%f], y: [%f], z: [%f]", camera_pose_wi_geo.translation.x,camera_pose_wi_geo.translation.y, camera_pose_wi_geo.translation.z);
		ROS_INFO("Orientation-> x: [%f], y: [%f], z: [%f], w: [%f]", camera_pose_wi_geo.rotation.x, camera_pose_wi_geo.rotation.y, camera_pose_wi_geo.rotation.z, camera_pose_wi_geo.rotation.w);
		cout<<" The odometry after EKF:" << endl;
        ROS_INFO("Seq: [%d]", ekf_odom.header.seq);
		ROS_INFO("Position-> x: [%f], y: [%f], z: [%f]", ekf_odom.pose.pose.position.x,ekf_odom.pose.pose.position.y, ekf_odom.pose.pose.position.z);
		ROS_INFO("Orientation-> x: [%f], y: [%f], z: [%f], w: [%f]", ekf_odom.pose.pose.orientation.x, ekf_odom.pose.pose.orientation.y, ekf_odom.pose.pose.orientation.z, ekf_odom.pose.pose.orientation.w);
		ROS_INFO("Vel-> Linear: [%f], Angular: [%f]", ekf_odom.twist.twist.linear.x,ekf_odom.twist.twist.angular.z);
    #endif
	// cout<<" The end of Odometry callback" << endl << endl;
}

int main(int argc, char **argv)
{
    ros::init(argc, argv, "ekf");
    ros::NodeHandle n("~");
	ros::Time::init();
	current_time = ros::Time::now();
    ros::Subscriber s1 = n.subscribe("imu", 1000, imu_callback);
    ros::Subscriber s2 = n.subscribe("tag_odom", 1000, odom_callback);
    odom_pub = n.advertise<nav_msgs::Odometry>("ekf_odom", 100);
    // Q imu covariance matrix; Rt visual odomtry covariance matrix
    Q.topLeftCorner(6, 6) = 0.01 * Q.topLeftCorner(6, 6);
    Q.bottomRightCorner(6, 6) = 0.01 * Q.bottomRightCorner(6, 6);
    Rt.topLeftCorner(3, 3) = 0.5 * Rt.topLeftCorner(3, 3);
    Rt.bottomRightCorner(3, 3) = 0.5 * Rt.bottomRightCorner(3, 3);
    Rt.bottomRightCorner(1, 1) = 0.1 * Rt.bottomRightCorner(1, 1);

    ros::spin();
}
