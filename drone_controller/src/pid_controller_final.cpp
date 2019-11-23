#include <ros/ros.h>


#include <stdio.h>
#include <math.h>

#include "mavros_msgs/PositionTarget.h"
#include "drone_controller/states.h"
#include "drone_controller/error.h"
#include "drone_controller/pid.h"

#include <mavros_msgs/CommandTOL.h> //Service for landing



PID pidx = PID(0.75, -0.75, 0.0015, 0.0002, 0.0); //max, min, kp, kd, ki (Max = 1m/s, Min = 1m/s, 
PID pidy = PID(0.75, -0.75, 0.003125, 0.0006251, 0.0);
//0.00234375 y 0.00046875
PID pidyth = PID(0.5, -0.5, 0.0055, 0.0011, 0.0);

#define FACTORZ  0.05 //Descend Factor

class Controller
{
  private: //Declaro las variables de mis publisher y subscribers

  ros::NodeHandle po_nh;
  ros::Subscriber sub;
  ros::Publisher pub;
  ros::Publisher pub1;
  ros::Time lastTime;
  ros::ServiceClient land_client;
  double imageW;
  double imageH;
  float zini;
  int flag;

  public:
  Controller(ros::NodeHandle ao_nh) : po_nh(ao_nh), flag(0) //Constructor de la clase Controller
  {
    pub = po_nh.advertise<mavros_msgs::PositionTarget>("/mavros/setpoint_raw/local",10) ; //Topico endiablado que me deja controlar velocidades y posiciones
    pub1 = po_nh.advertise<drone_controller::error>("/error",10) ;
    sub = po_nh.subscribe("/predicted_states", 10, &Controller::controllerCallBack, this); //Suscriber del tipo find_object_2d/ObjectsStamped que se suscribe a /objects
    land_client = po_nh.serviceClient<mavros_msgs::CommandTOL>("mavros/cmd/land"); //landing

    lastTime = ros::Time::now();
    imageW = 320/2;  //Image size
    imageH = 240/2;
    zini = 2.25; //Start altitude of search
  }

  void controllerCallBack(const drone_controller::states& msg) //Callback para el subscriber
  {

	//Image data
	double centX = (double) msg.Xc; //Parsing float into double
	double centY = (double) msg.Yc;
	double width = (double) msg.W;
	double heigh = (double) msg.H;
	double theta = (double) msg.Theta;

	//Velocities
	float Vx = 0;
	float Vy = 0;
	float Vthe = 0;
        float zpos = 0;

	//Z error
	float ErZ = abs(width-heigh); //Error in W and H of the images

	// Time since last call
        double timeBetweenMarkers = (ros::Time::now() - lastTime).toSec();
        lastTime = ros::Time::now();

        Vx = (float) -1 * pidx.calculate(imageW, centX, timeBetweenMarkers); //Setpoint, Process Variable, Sample time for Vx
	Vy = (float) pidy.calculate(imageH, centY, timeBetweenMarkers); //Setpoint, Process Variable, Sample time for Vy
	Vthe = (float) pidyth.calculate(0, theta, timeBetweenMarkers); //Setpoint, Process Variable, Sample time for Vy

	//Calculate Z descend based on W and H
	if(ErZ<3 && abs(Vx) < 0.05 && abs(Vy) < 0.05 && flag >= 3 && zini > 0.65) //If the error of W and H is less than 3 pixels
	{
	    zpos =  zini -  FACTORZ; //Descend Z based on the factor
	    flag = 0; 
	}
	else{
	    zpos = zini; //If there is more than 3 pixels of error, hold pos
	    flag = flag + 1;
	}

	//Drone service for automatic langind when it reaches an specific altitude
	if(zpos <= 0.65 && flag >= 20 &&  ErZ < 3 && abs(Vx) < 0.025 && abs(Vy) < 0.025) { //If the pos in z is less than this altitude
	mavros_msgs::CommandTOL land_cmd; //Set all the descend parameters to Zero
        land_cmd.request.yaw = 0;
        land_cmd.request.latitude = 0;
        land_cmd.request.longitude = 0;
        land_cmd.request.altitude = 0;

	//When it lands, everything goes to zero
        if (!(land_client.call(land_cmd) && land_cmd.response.success)){ //Publish the service of landing
        ROS_INFO("Landing");
	Vx = 0; //Set all the velocities and positions to 0
        Vy = 0;
	Vthe = 0;
	zpos = 0;

	drone_controller::error data;
	data.errorX = imageW - centX;
	data.errorY = imageH - centY;
	data.errorT = theta;
	data.errorS = ErZ;
	
	printf("Error at Vx, Vy, Theta and Z are (%f,%f,%f,%f) \n", data.errorX, data.errorY, data.errorT, data.errorS);
	pub1.publish(data);

	ros::shutdown(); //Shutdowm the node
        }
	}

	zini = zpos; //Zini update to dont crash the drone	

	//Pos object
        mavros_msgs::PositionTarget pos; //Creacion del objeto tipo setpoint_raw/local
	
	pos.coordinate_frame = mavros_msgs::PositionTarget::FRAME_BODY_NED;  //FRAME_LOCAL_NED to move the drone frame
  
        pos.header.stamp = ros::Time::now(); //Time header stamp
        pos.header.frame_id = "base_link"; //"base_link" frame to compute odom
        pos.type_mask = 1987; //Mask for Vx, Vy, Z pos and Yaw rate
        pos.position.z = zpos;//0.001*some_object.position_z;
        pos.velocity.x = Vx;//X(+) Drone left, X(-) Drone Right
        pos.velocity.y = Vy;//Y(+) Drone Backward, X(-) Drone Forward
        pos.yaw_rate = Vthe;//0.001*some_object.position_y;

	printf("PID Vx and Vy values at (%f,%f,%f) \n", Vx, Vy,zpos);
	pub.publish(pos);

        drone_controller::error data;
	data.errorX = imageW - centX;
	data.errorY = imageH - centY;
	data.errorT = theta;
	data.errorS = ErZ;
	
	printf("Error at Vx, Vy, Theta and Z are (%f,%f,%f,%f) \n", data.errorX, data.errorY, data.errorT, data.errorS);
	pub1.publish(data);
  }


};//End of class 


int main(int argc, char** argv)
{   

    ros::init(argc, argv, "controller_node"); //Nombre del nodo que uso para suscribirme y publicar la info
    ros::NodeHandle n;
    Controller cont(n);


    ros::spin();

    return 0;
}
