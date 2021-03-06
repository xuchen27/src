#include "ekf.h"

EKF::EKF()
{
	init = false;

	gravity = 9.72;
	g << 0,
		 0,
		 gravity;

	I3  = MatrixXd::Identity(3, 3);
	I15 = MatrixXd::Identity(15, 15);
}

EKF::~EKF() {}

bool EKF::isInit()
{
	return init;
}

VectorXd EKF::Get_State()
{
	return StateStack[StateStack.size() - 1].mean;
}

ros::Time EKF::Get_Time()
{
	return StateStack[StateStack.size() - 1].stamp;
}

void EKF::SetParam(double var_g, double var_a, double var_bg, double var_ba, double var_p, double var_q)
{
	Q.setZero(12, 12);
	R.setZero(6, 6);
	Wt.setZero(6, 6);
	Ct.setZero(6, 15);
	At.setZero(15, 15);
	Ft.setZero(15, 15);
	Ut.setZero(15, 12);
	Vt.setZero(15, 12);

	for(int i=0;i<3; i++){ Q(i,i) = var_g; }
	for(int i=3;i<6; i++){ Q(i,i) = var_a; }
	for(int i=6;i<9; i++){ Q(i,i) = var_bg;}
	for(int i=9;i<12;i++){ Q(i,i) = var_ba;}

	for(int i=0;i<3; i++){ R(i,i) = var_p;}
	for(int i=3;i<6; i++){ R(i,i) = var_q;}

	Wt.block(0, 0, 6, 6) = MatrixXd::Identity(6, 6);
	Ct.block(0, 0, 6, 6) = MatrixXd::Identity(6, 6);
}

void EKF::SetInit( VectorXd zt, ros::Time stamp)
{
	StateStack.clear();
	VectorXd mean_init = VectorXd::Zero(15);
	MatrixXd var_init  = MatrixXd::Zero(15, 15);

	mean_init.head(6)  = zt;
	rpy_ps(0) = zt(3);
	rpy_ps(1) = zt(4);
	rpy_ps(2) = zt(5);
	State state_init;
	state_init.mean = mean_init;
	state_init.var  = var_init;
	state_init.stamp= stamp;
	StateStack.push_back(state_init);
	init = true;
}

void EKF::IMU_Propagation( VectorXd u, ros::Time stamp)
{
	Matrix3d G, Ginv, Rot, R_acc_dot, Ginv_w_dot;
	State state_ps = StateStack[StateStack.size() - 1];
	VectorXd mean_ps = state_ps.mean;
	MatrixXd var_ps  = state_ps.var;



	/**************************************************************************/
	/* updated to f(mu_t_1, u_t, 0), At(mu_t_1, u_t, 0), and Ut               */
	/* Generated by matlab ELEC6910P_Project2Phase2.m                         */
	/**************************************************************************/
/*
	double x21= mean_ps(3), x22= mean_ps(4), x23= mean_ps(5);
	double x31= mean_ps(6), x32= mean_ps(7), x33= mean_ps(8);
	double x41= mean_ps(9), x42= mean_ps(10),x43= mean_ps(11);
	double x51= mean_ps(12),x52= mean_ps(13),x53= mean_ps(14);
	// input u : acceleration, augular_velocity
	double am1= u(0), am2= u(1), am3= u(2);
	double wm1= u(3), wm2= u(4), wm3= u(5);

	VectorXd f_t_1(15);
	f_t_1 <<
		x31,
		x32,
		x33,
		wm1*cos(x22) - 0*cos(x22) - x41*cos(x22) - 0*sin(x22) + wm3*sin(x22) - x43*sin(x22),
		-(0*cos(x21) - wm2*cos(x21) + x42*cos(x21) - 0*cos(x22)*sin(x21) + wm3*cos(x22)*sin(x21) - x43*cos(x22)*sin(x21) + 0*sin(x21)*sin(x22) - wm1*sin(x21)*sin(x22) + x41*sin(x21)*sin(x22))/cos(x21),
		-(0*cos(x22) - wm3*cos(x22) + x43*cos(x22) - 0*sin(x22) + wm1*sin(x22) - x41*sin(x22))/cos(x21),
		cos(x21)*sin(x23)*(0 - am2 + x52) - (cos(x23)*sin(x22) + cos(x22)*sin(x21)*sin(x23))*(0 - am3 + x53) - (cos(x22)*cos(x23) - sin(x21)*sin(x22)*sin(x23))*(0 - am1 + x51),
		- (cos(x22)*sin(x23) + cos(x23)*sin(x21)*sin(x22))*(0 - am1 + x51) - (sin(x22)*sin(x23) - cos(x22)*cos(x23)*sin(x21))*(0 - am3 + x53) - cos(x21)*cos(x23)*(0 - am2 + x52),
		cos(x21)*sin(x22)*(0 - am1 + x51) - cos(x21)*cos(x22)*(0 - am3 + x53) - sin(x21)*(0 - am2 + x52) + gravity,
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
		0, 0, 0,                                                                                                                                 0,                                                 wm3*cos(x22) - 0*cos(x22) - x43*cos(x22) + 0*sin(x22) - wm1*sin(x22) + x41*sin(x22),                                                                                                                                                                             0, 0, 0, 0,                     -cos(x22),  0,                    -sin(x22),                                                0,                  0,                                                0,
		0, 0, 0,                              (0*cos(x22) - wm3*cos(x22) + x43*cos(x22) - 0*sin(x22) + wm1*sin(x22) - x41*sin(x22))/(cos(x21)*cos(x21)),                          -(sin(x21)*(0*cos(x22) - wm1*cos(x22) + x41*cos(x22) + 0*sin(x22) - wm3*sin(x22) + x43*sin(x22)))/cos(x21),                                                                                                                                                                             0, 0, 0, 0, -(sin(x21)*sin(x22))/cos(x21), -1, (cos(x22)*sin(x21))/cos(x21),                                                0,                  0,                                                0,
		0, 0, 0,                  -(sin(x21)*(0*cos(x22) - wm3*cos(x22) + x43*cos(x22) - 0*sin(x22) + wm1*sin(x22) - x41*sin(x22)))/(cos(x21)*cos(x21)),                                      (0*cos(x22) - wm1*cos(x22) + x41*cos(x22) + 0*sin(x22) - wm3*sin(x22) + x43*sin(x22))/cos(x21),                                                                                                                                                                             0, 0, 0, 0,             sin(x22)/cos(x21),  0,           -cos(x22)/cos(x21),                                                0,                  0,                                                0,
		0, 0, 0, cos(x21)*sin(x22)*sin(x23)*(0 - am1 + x51) - cos(x21)*cos(x22)*sin(x23)*(0 - am3 + x53) - sin(x21)*sin(x23)*(0 - am2 + x52), (cos(x23)*sin(x22) + cos(x22)*sin(x21)*sin(x23))*(0 - am1 + x51) - (cos(x22)*cos(x23) - sin(x21)*sin(x22)*sin(x23))*(0 - am3 + x53), (cos(x22)*sin(x23) + cos(x23)*sin(x21)*sin(x22))*(0 - am1 + x51) + (sin(x22)*sin(x23) - cos(x22)*cos(x23)*sin(x21))*(0 - am3 + x53) + cos(x21)*cos(x23)*(0 - am2 + x52), 0, 0, 0,                             0,  0,                            0,   sin(x21)*sin(x22)*sin(x23) - cos(x22)*cos(x23),  cos(x21)*sin(x23), - cos(x23)*sin(x22) - cos(x22)*sin(x21)*sin(x23),
		0, 0, 0, cos(x23)*sin(x21)*(0 - am2 + x52) - cos(x21)*cos(x23)*sin(x22)*(0 - am1 + x51) + cos(x21)*cos(x22)*cos(x23)*(0 - am3 + x53), (sin(x22)*sin(x23) - cos(x22)*cos(x23)*sin(x21))*(0 - am1 + x51) - (cos(x22)*sin(x23) + cos(x23)*sin(x21)*sin(x22))*(0 - am3 + x53), cos(x21)*sin(x23)*(0 - am2 + x52) - (cos(x23)*sin(x22) + cos(x22)*sin(x21)*sin(x23))*(0 - am3 + x53) - (cos(x22)*cos(x23) - sin(x21)*sin(x22)*sin(x23))*(0 - am1 + x51), 0, 0, 0,                             0,  0,                            0, - cos(x22)*sin(x23) - cos(x23)*sin(x21)*sin(x22), -cos(x21)*cos(x23),   cos(x22)*cos(x23)*sin(x21) - sin(x22)*sin(x23),
		0, 0, 0,                            cos(x22)*sin(x21)*(0 - am3 + x53) - cos(x21)*(0 - am2 + x52) - sin(x21)*sin(x22)*(0 - am1 + x51),                                                               cos(x21)*cos(x22)*(0 - am1 + x51) + cos(x21)*sin(x22)*(0 - am3 + x53),                                                                                                                                                                             0, 0, 0, 0,                             0,  0,                            0,                                cos(x21)*sin(x22),          -sin(x21),                               -cos(x21)*cos(x22),
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0;

	Ut <<
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0,                     -cos(x22),  0,                    -sin(x22), 0, 0, 0, 0, 0, 0,
		0, 0, 0, -(sin(x21)*sin(x22))/cos(x21), -1, (cos(x22)*sin(x21))/cos(x21), 0, 0, 0, 0, 0, 0,
		0, 0, 0,             sin(x22)/cos(x21),  0,           -cos(x22)/cos(x21), 0, 0, 0, 0, 0, 0,
		   sin(x21)*sin(x22)*sin(x23) - cos(x22)*cos(x23),  cos(x21)*sin(x23), - cos(x23)*sin(x22) - cos(x22)*sin(x21)*sin(x23), 0, 0, 0, 0, 0, 0, 0, 0, 0,
		 - cos(x22)*sin(x23) - cos(x23)*sin(x21)*sin(x22), -cos(x21)*cos(x23),   cos(x22)*cos(x23)*sin(x21) - sin(x22)*sin(x23), 0, 0, 0, 0, 0, 0, 0, 0, 0,
		                                cos(x21)*sin(x22),          -sin(x21),                               -cos(x21)*cos(x22), 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
		0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0;
*/

    Vector3d X2, X3, X4, X5;
    X2 = mean_ps.segment<3>(3); 
    X3 = mean_ps.segment<3>(6); 
    X4 = mean_ps.segment<3>(9); 
    X5 = mean_ps.segment<3>(12); 
    Vector3d rpy = X2;
    double fai = rpy(0), theta = rpy(1), sai = rpy(2);
    Vector3d w   = u.segment<3>(0) - X4;
    Vector3d acc = u.segment<3>(3) - X5;

	G <<    cos(theta),    0,     -cos(fai)*sin(theta),
            0,             1,           sin(fai),
            sin(theta),    0,      cos(fai)*cos(theta);

    Ginv<<   cos(theta),                       0,               sin(theta),
            (sin(fai)*sin(theta))/cos(fai),    1,              -(cos(theta)*sin(fai))/cos(fai),
            -sin(theta)/cos(fai),              0,               cos(theta)/cos(fai);

    Rot = rpy_to_R(rpy);
   
    Ginv_w_dot << 0,                                                                w(2)*cos(theta) - w(0)*sin(theta),                      0,
                -(w(2)*cos(theta) - w(0)*sin(theta))/pow(cos(fai),2),              (sin(fai)*(w(0)*cos(theta) + w(2)*sin(theta)))/cos(fai), 0,
                 (sin(fai)*(w(2)*cos(theta) - w(0)*sin(theta)))/pow(cos(fai),2),  -(w(0)*cos(theta) + w(2)*sin(theta))/cos(fai),            0;

    R_acc_dot  << sin(sai)*(acc(1)*sin(fai) + acc(2)*cos(fai)*cos(theta) - acc(0)*cos(fai)*sin(theta)), acc(2)*(cos(sai)*cos(theta) - sin(fai)*sin(sai)*sin(theta)) - acc(0)*(cos(sai)*sin(theta) + cos(theta)*sin(fai)*sin(sai)),  -acc(0)*(cos(theta)*sin(sai) + cos(sai)*sin(fai)*sin(theta)) - acc(2)*(sin(sai)*sin(theta) - cos(sai)*cos(theta)*sin(fai)) - acc(1)*cos(fai)*cos(sai),
                 -cos(sai)*(acc(1)*sin(fai) + acc(2)*cos(fai)*cos(theta) - acc(0)*cos(fai)*sin(theta)), acc(2)*(cos(theta)*sin(sai) + cos(sai)*sin(fai)*sin(theta)) - acc(0)*(sin(sai)*sin(theta) - cos(sai)*cos(theta)*sin(fai)),   acc(0)*(cos(sai)*cos(theta) - sin(fai)*sin(sai)*sin(theta)) + acc(2)*(cos(sai)*sin(theta) + cos(theta)*sin(fai)*sin(sai)) - acc(1)*cos(fai)*sin(sai),
                  acc(1)*cos(fai) - acc(2)*cos(theta)*sin(fai) + acc(0)*sin(fai)*sin(theta),           -cos(fai)*(acc(0)*cos(theta) + acc(2)*sin(theta)),                                                                            0;

    At.block(0, 6, 3, 3) =  I3;
    At.block(3, 3, 3, 3) = Ginv_w_dot;
    At.block(3, 9, 3, 3) = -G.inverse();
   	At.block(6, 3, 3, 3) = R_acc_dot;
   	At.block(6,12, 3, 3) = -Rot;

    Ut.block(3, 0, 3, 3) = -G.inverse();
    Ut.block(6, 3, 3, 3) = -Rot;
    Ut.block(9, 6, 3, 3) = I3;
    Ut.block(12,9, 3, 3) = I3;	

    VectorXd xdot = Eigen::VectorXd::Zero(15);
    xdot.segment(0, 3) = X3;
    xdot.segment(3, 3) = Ginv * w;
    xdot.segment(6, 3) = Rot * acc + g;

	double dt = (stamp - state_ps.stamp).toSec();

	Ft = I15 + dt * At;
    Vt = dt * Ut;

	// Create a new state
	State state_ns;
	// state_ns.mean = mean_ps + dt * f_t_1;
	state_ns.mean = mean_ps + dt * xdot;
	state_ns.var  = Ft * var_ps * Ft.transpose() + Vt * Q * Vt.transpose();
	state_ns.stamp= stamp;
	state_ns.u    = u;
	StateStack.push_back(state_ns);
}

/*	Camera update and time stamp sequencing at the same function	*/
void EKF::Odom_Update( VectorXd zt, ros::Time stamp)
{
	State state_ps;
	vector< State >::iterator itr;
	vector< State >::iterator itr2;
	vector< State > StateStackFuture;

	// Sync the camera time stamp to IMU
	state_ps = StateStack[StateStack.size() - 1];
	for (itr = StateStack.begin(); itr != StateStack.end(); itr++) {
		if (itr->stamp >= stamp) {
			state_ps = *(itr - 1);
			StateStackFuture.assign(itr, StateStack.end());
			break;
		}
	}
	// Rebuild the state stack from the camera time
	StateStack.clear();

	VectorXd mean_ps = state_ps.mean;
	MatrixXd var_ps  = state_ps.var;
	MatrixXd K_step = MatrixXd::Zero(6, 6);
	MatrixXd Kt = MatrixXd::Zero(15, 6);
	K_step = Ct * var_ps * (Ct.transpose()) + Wt * R * (Wt.transpose());
	Kt = var_ps * (Ct.transpose()) * (K_step.inverse());

	// Check if the Euler angle cross the singularity
	for(int i=3;i<6;i++) {
		if(abs(rpy_ps(i-3)-zt(i))>M_PI){
			if(zt(i)<0) zt(i) = 2* M_PI + zt(i);
			else zt(i) = -2* M_PI + zt(i);
		}
	}
	rpy_ps(0) = zt(3);
	rpy_ps(1) = zt(4);
	rpy_ps(2) = zt(5);

	State state_ns;
	state_ns.mean = mean_ps + Kt * (zt - Ct * mean_ps);
	state_ns.var  = var_ps  - Kt * Ct * var_ps;
	state_ns.stamp= state_ps.stamp; // All time stamp are in sync with IMU!
	StateStack.push_back(state_ns);

	for (itr2 = StateStackFuture.begin(); itr2 != StateStackFuture.end(); itr2++)
	{
		VectorXd u_repropogate = itr2->u;
		ros::Time stamp_repropogate = itr2->stamp;
		EKF::IMU_Propagation(u_repropogate, stamp_repropogate);
	}
}
