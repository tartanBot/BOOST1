#include <ros/ros.h>
#include <log4cxx/logger.h>
#include <std_msgs/String.h>
#include <std_msgs/Float32MultiArray.h>
#include <std_msgs/MultiArrayLayout.h>
#include <message_filters/subscriber.h>
#include <message_filters/synchronizer.h>
#include <message_filters/sync_policies/approximate_time.h>
#include <tf2_ros/transform_listener.h>
#include <tf2_ros/message_filter.h>
#include <string>
#include <math.h>
#include <sstream> 
#include <iostream>
#include <stdio.h>      /* printf */
#include <stdlib.h>
#include <nav_msgs/Odometry.h>
#include <sensor_msgs/PointCloud2.h>
#include <sensor_msgs/JointState.h>

#include <pcl_conversions/pcl_conversions.h>
#include <pcl/point_types.h>
#include <pcl/PCLPointCloud2.h>
#include <pcl/conversions.h>
#include <pcl_ros/transforms.h>

#include <pcl/io/ply_io.h>
#include <pcl/common/transforms.h>
#include <pcl/filters/statistical_outlier_removal.h>

#include <boost/thread/thread.hpp>
#include <boost/circular_buffer.hpp>
#include <pcl/common/common_headers.h>
#include <pcl/features/normal_3d.h>

#include <pcl/filters/conditional_removal.h>
#include <pcl/filters/radius_outlier_removal.h>
#include <pcl/filters/passthrough.h>

#include <pcl/filters/voxel_grid.h>
#include <pcl/filters/filter.h>
#include <pcl/registration/icp.h>
#include <pcl/registration/icp_nl.h>
#include <pcl/registration/transforms.h>

#include "image_stitch/CloudStitch.h"
#include <string.h>
#include "std_msgs/Int16.h"
#include "std_msgs/Int32.h"
#include "std_srvs/Trigger.h"
#include <unistd.h>
#include "Vec3.h"

#include <pcl_ros/point_cloud.h>
#include <pcl/point_types.h>
#include <visualization_msgs/Marker.h>
#include <exception>

#include "image_stitch/AlphaCentroid.h"
#include "arm_executive/ArmInitStitch.h"
#include "gripper/GripperClose.h"

using namespace message_filters;
using namespace geometry_msgs;
using namespace sensor_msgs;

typedef boost::circular_buffer<sensor_msgs::PointCloud2> circular_buffer;
typedef boost::circular_buffer<nav_msgs::Odometry> circular_buffer_odom;

struct MyPointType
{
  PCL_ADD_POINT4D;                  // preferred way of adding a XYZ+padding
  int layer;
  float weight;
  EIGEN_MAKE_ALIGNED_OPERATOR_NEW   // make sure our new allocators are aligned
} EIGEN_ALIGN16;                    // enforce SSE padding for correct memory alignment

POINT_CLOUD_REGISTER_POINT_STRUCT (MyPointType,           // here we assume a XYZ + "test" (as fields)
                                   (float, x, x)
                                   (float, y, y)
                                   (float, z, z)
                                   (float, layer, layer)
                                   (float, weight, weight)
)

class Stitch
{

// BEG .h
public:
	Stitch();
	void filterStitch(boost::circular_buffer<sensor_msgs::PointCloud2> ringbuffer, boost::circular_buffer<nav_msgs::Odometry> odom_buffer);
	void pairAlign (const pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_src, const pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_tgt, const pcl::PointCloud<pcl::PointXYZRGB>::Ptr output, Eigen::Matrix4f &final_transform, bool downsample);
	void callback (const boost::shared_ptr<const nav_msgs::Odometry>& odomPtr, const boost::shared_ptr<const sensor_msgs::PointCloud2>& cloudPtr, const boost::shared_ptr<const sensor_msgs::JointState>& jointPtr);
	//bool cloudStitch(image_stitch::CloudStitch::Request  &req, image_stitch::CloudStitch::Response &res);
	bool cloudStitch(std_srvs::Trigger::Request  &req, std_srvs::Trigger::Response &res);
	void slice(const pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_full, Eigen::Matrix4f GlobalTransform_odom);
	std::vector<std::vector<float> > hotspotfilterFunction(pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud, int n);
	void cloudWriter(const pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud, const char* name, bool towrite);

// ---	
private:
	ros::NodeHandle mNode;
	ros::Publisher cloudPub;
	ros::Publisher markerPub;
	ros::Publisher zPub;
	circular_buffer cb;
	circular_buffer_odom cb_odom;
	Eigen::Matrix4f GlobalTransform, pairTransform;
	Eigen::Matrix4f GlobalTransform_odom;	

	pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_full;
	pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_filtered;
	pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_filtered_old;

	const static bool cloudWrite = true;

	int total_green;
	double arm_height;
	float centroid_x;
	float centroid_y;
	const static float cam_x_offset = 0.255; //.265
	const static float cam_z_offset = 0.015;	
	const static float arm_extended = 0.565;

	//---------Hardcoded Centroids at each layer--------
	const static int num_centroids_layer = 35;

	//---------Number of Images------------
	const static int num_images = 5;

	const static float offset_HACK = 0.0;

	//--------Stitching Variables-----------
	const static float pairAlign_leafsize = 0.01;
	const static float pairAlign_tform_eps = 1e-5;
	const static float pairAlign_correspondenceDistance = 0.05;
	const static float pairAlign_correspondenceDistance_cutoff = 0.01;

	//--------RGB Filtering Variables-------- 	
	const static int rMax = 255;
	const static int rMin = 15; //68; //99; //15; //31;
	const static int gMax = 255;
	const static int gMin = 15; //78; //115; //15; //22;
	const static int bMax = 255;
	const static int bMin = 15; //44; //56; //15; //22;

	//----------Heat Map Filtering----------
	//const static int noise_threshold = 400;//40;
	//const static int hotspot_threshold = 800;
	int noise_threshold;
	int hotspot_threshold;

	//---------Sphere/Cylinder Logic--------
	const static float z_threshold = 0.015; //0.02 //this determines the angle of the stalk
	const static float x_threshold = 0.015; //0.02 //this determines the angle of the stalk
	const static float RANSAC_threshold = 0.025;

	//------------Cloud Slicing------------
	const static int rMin_slice = 68; //99; 
	const static int gMin_slice = 78; //115;
	const static int bMin_slice = 44; //56;
	const static int rMax_slice = 240; //99; 
	const static int gMax_slice = 240; //115;
	const static int bMax_slice = 240; //56;	
	const static int num_slices = 8;
	const static float slice_leafsize = 0.001;
	//const static int slice_normal_radius = 0.01;
	const static float bottom_start = 0.3;
	const static float middle_start = 0.4;
	const static float top_start = 0.5;	
	const static int num_spheres = 100; //can't remember if this is the total number of spheres or for each layer...
	const static float far_reach_reduction = 0.4; //this is a ridiculous variable to prevent the arm reaching over to the camera region

	//------------Cylinder Detection----------
	const static float cylinder_dist_threshold = 0.001;	

	std::vector<std::vector<float> > centroids;
};
// END .h

Stitch::Stitch()
{
	int buffer = 10;
	//this->cloudPub = mNode.advertise<std_msgs::String>("stitched_cloud", buffer);
	this->cloudPub = mNode.advertise<pcl::PointCloud<pcl::PointXYZRGB> > ("stitched_cloud", 1);
	this->markerPub = mNode.advertise<visualization_msgs::Marker>("visualization_marker", 1);
	this->zPub = mNode.advertise<sensor_msgs::JointState>("/joint_commands", 1);
	this->cb = circular_buffer(num_images);
	this->cb_odom = circular_buffer_odom(num_images);
	GlobalTransform = Eigen::Matrix4f::Identity ();
	GlobalTransform_odom = Eigen::Matrix4f::Identity ();	

	cloud_filtered.reset(new pcl::PointCloud<pcl::PointXYZRGB>);
	cloud_filtered_old.reset(new pcl::PointCloud<pcl::PointXYZRGB>);
	cloud_full.reset(new pcl::PointCloud<pcl::PointXYZRGB>);
}

void Stitch::cloudWriter(const pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud, const char* name, bool towrite){
	if(towrite){
		pcl::PLYWriter writer;
	    writer.write<pcl::PointXYZRGB> (name, *cloud, false);
	}
}

void Stitch::pairAlign (const pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_src, const pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_tgt, const pcl::PointCloud<pcl::PointXYZRGB>::Ptr output, Eigen::Matrix4f &final_transform, bool downsample)
{

  	// Downsample for consistency and speed
  	// \note enable this for large datasets
  	pcl::PointCloud<pcl::PointXYZRGB>::Ptr src (new pcl::PointCloud<pcl::PointXYZRGB>);
  	pcl::PointCloud<pcl::PointXYZRGB>::Ptr tgt (new pcl::PointCloud<pcl::PointXYZRGB>);
  	pcl::VoxelGrid<pcl::PointXYZRGB> grid;
  	if (downsample)
  	{
    	grid.setLeafSize (pairAlign_leafsize, pairAlign_leafsize, pairAlign_leafsize);
   		grid.setInputCloud (cloud_src);
    	grid.filter (*src);

    	grid.setInputCloud (cloud_tgt);
    	grid.filter (*tgt);
  	}
  	else
  	{
    	src = cloud_src;
    	tgt = cloud_tgt;
  	} 

  	ROS_INFO_STREAM("Size of src: " << src->size());
  	ROS_INFO_STREAM("Size of tgt: " << tgt->size());  	

  	// Align
  	pcl::IterativeClosestPoint<pcl::PointXYZRGB, pcl::PointXYZRGB> reg;
  	reg.setTransformationEpsilon (pairAlign_tform_eps); //1e-6
  	// Set the maximum distance between two correspondences (src<->tgt) to 10cm
  	// Note: adjust this based on the size of your datasets
  	reg.setMaxCorrespondenceDistance (pairAlign_correspondenceDistance);  //originally 0.15

  	reg.setInputSource (src);
  	reg.setInputTarget (tgt);

  	// Run the same optimization in a loop and visualize the results
  	Eigen::Matrix4f Ti = Eigen::Matrix4f::Identity (), prev, targetToSource;
  
  	//PointCloudWithNormals::Ptr reg_result = points_with_normals_src;
  	pcl::PointCloud<pcl::PointXYZRGB>::Ptr reg_result = src;
  	//reg.setMaximumIterations (pairAlign_max_iterations);

  	int i=0;
  	while (reg.getMaxCorrespondenceDistance() > pairAlign_correspondenceDistance_cutoff)
  	{
    	i = i+1;

    	// save cloud for visualization purpose
    	src = reg_result;

    	// Estimate
    	reg.setInputSource (src);
    	reg.align (*reg_result);

    	//accumulate transformation between each Iteration
    	Ti = reg.getFinalTransformation () * Ti;

    	//if the difference between this transformation and the previous one
    	//is smaller than the threshold, refine the process by reducing
    	//the maximal correspondence distance
    	if (fabs ((reg.getLastIncrementalTransformation () - prev).sum ()) < reg.getTransformationEpsilon ())
      		reg.setMaxCorrespondenceDistance (reg.getMaxCorrespondenceDistance () - 0.01);

    	prev = reg.getLastIncrementalTransformation ();   
  	}

  
  	ROS_INFO_STREAM("Iterations: " << i);

  	// Get the transformation from target to source
  	targetToSource = Ti.inverse();

  	// Transform target back in source frame
  	pcl::transformPointCloud (*cloud_tgt, *output, targetToSource);

  	//add the source to the transformed target
  	//*output += *src;
  
  	final_transform = targetToSource;
 
 	std::cout << final_transform <<std::endl;
 }


void Stitch::filterStitch(boost::circular_buffer<sensor_msgs::PointCloud2> ringbuffer, boost::circular_buffer<nav_msgs::Odometry> odom_buffer)
{

	for (int i=(num_images-1); i>-1; i--){  //iterates from 4 to zero
		ROS_DEBUG_STREAM("header: \n" << ringbuffer[i].header);
		ROS_DEBUG_STREAM("ODOMETRY: \n" << odom_buffer[i].pose.pose.position);
		double dist_traveled = sqrt(pow(odom_buffer[i].pose.pose.position.x-odom_buffer[(num_images-1)].pose.pose.position.x,2) + pow(odom_buffer[i].pose.pose.position.y-odom_buffer[(num_images-1)].pose.pose.position.y,2));
		ROS_INFO_STREAM("DIST TRAVELED: \n" << dist_traveled);

//----------Convert PointCloud2 to PCL------------------
	    pcl::PCLPointCloud2 pcl_pc2;
	    pcl_conversions::toPCL(ringbuffer[i],pcl_pc2);
	    pcl::PointCloud<pcl::PointXYZRGB>::Ptr temp_cloud(new pcl::PointCloud<pcl::PointXYZRGB>);
	    pcl::fromPCLPointCloud2(pcl_pc2,*temp_cloud);

//-----------Initialize Empty Point Clouds-------------
	    pcl::PointCloud<pcl::PointXYZRGB>::Ptr source(new pcl::PointCloud<pcl::PointXYZRGB>);
	    pcl::PointCloud<pcl::PointXYZRGB>::Ptr target(new pcl::PointCloud<pcl::PointXYZRGB>);
	    pcl::PointCloud<pcl::PointXYZRGB>::Ptr result(new pcl::PointCloud<pcl::PointXYZRGB>);

	    ROS_DEBUG_STREAM("Eigen");

//---------Create a conditional removal filter to remove black pixels---------
	    pcl::ConditionAnd<pcl::PointXYZRGB>::Ptr color_cond (new pcl::ConditionAnd<pcl::PointXYZRGB> ());
	    color_cond->addComparison (pcl::PackedRGBComparison<pcl::PointXYZRGB>::Ptr (new pcl::PackedRGBComparison<pcl::PointXYZRGB> ("r", pcl::ComparisonOps::LT, rMax)));
	    color_cond->addComparison (pcl::PackedRGBComparison<pcl::PointXYZRGB>::Ptr (new pcl::PackedRGBComparison<pcl::PointXYZRGB> ("r", pcl::ComparisonOps::GT, rMin)));
	    color_cond->addComparison (pcl::PackedRGBComparison<pcl::PointXYZRGB>::Ptr (new pcl::PackedRGBComparison<pcl::PointXYZRGB> ("g", pcl::ComparisonOps::LT, gMax)));
	    color_cond->addComparison (pcl::PackedRGBComparison<pcl::PointXYZRGB>::Ptr (new pcl::PackedRGBComparison<pcl::PointXYZRGB> ("g", pcl::ComparisonOps::GT, gMin)));
	    color_cond->addComparison (pcl::PackedRGBComparison<pcl::PointXYZRGB>::Ptr (new pcl::PackedRGBComparison<pcl::PointXYZRGB> ("b", pcl::ComparisonOps::LT, bMax)));
	    color_cond->addComparison (pcl::PackedRGBComparison<pcl::PointXYZRGB>::Ptr (new pcl::PackedRGBComparison<pcl::PointXYZRGB> ("b", pcl::ComparisonOps::GT, bMin)));

	    // build the filter
	    pcl::ConditionalRemoval<pcl::PointXYZRGB> condrem (color_cond);
	    condrem.setInputCloud (temp_cloud);
	    condrem.setKeepOrganized(true);
	      
	    // apply filter
	    condrem.filter (*cloud_filtered);
		ROS_DEBUG_STREAM("Cloud_Filtered size:" << cloud_filtered->size());

		//Passthrough filter	
	    pcl::PassThrough<pcl::PointXYZRGB> pass;
	    pass.setInputCloud (cloud_filtered);
	    pass.setFilterFieldName ("z");
	    pass.setFilterLimits (0, 0.8);
	    pass.filter (*cloud_filtered);

	    //Adjust cloud location based on wheel odometry
	    Eigen::Matrix4f odom_transform = Eigen::Matrix4f::Identity();
	    odom_transform (0,3) = -dist_traveled + offset_HACK*i; //TODO: change this to be x,y,z
	    pcl::transformPointCloud (*cloud_filtered, *cloud_filtered, odom_transform);

	    if(cloudWrite){
			std::stringstream ss2;
			ss2 << "testCloud" << i <<".ply";
			pcl::PLYWriter writer;
		    writer.write<pcl::PointXYZRGB> (ss2.str(), *cloud_filtered, false);
		}

	    std::vector<int> indices;
	    pcl::removeNaNFromPointCloud(*cloud_filtered,*cloud_filtered, indices);

		//ROS_INFO_STREAM("Cloud_Filtered after NAN removal:" << cloud_filtered->size());
	
	    if (i<(num_images-1))
	    {
	        source = cloud_filtered_old;
			target = cloud_filtered;

	        pcl::PointCloud<pcl::PointXYZRGB>::Ptr temp (new pcl::PointCloud<pcl::PointXYZRGB>);
	        ROS_INFO ("Aligning...");  

			//This takes in (old cloud, new cloud, empty cloud, empty matrix)
			//Converts empty cloud to stitched cloud, and pairTransform to the tf between the two clouds)      
			Stitch::pairAlign (source, target, temp, pairTransform, true);
	        
	        //transform current pair into the global transform
	        //Takes in (stitched cloud, cloud to recieve, and global eigen matrix) 
	        pcl::transformPointCloud (*temp, *result, GlobalTransform);

	        //update the global transform
	        GlobalTransform = GlobalTransform * pairTransform;

	        //update the global transform with odom
	        GlobalTransform_odom = GlobalTransform * odom_transform;

	        ROS_DEBUG_STREAM("Global Tform: " << GlobalTransform_odom);	        

			*cloud_full+=*result;
	    }
	  
	    copyPointCloud(*cloud_filtered, *cloud_filtered_old); 
		ROS_DEBUG_STREAM("cloud_filtered_old size: " << cloud_filtered_old->size());

	    temp_cloud->clear();

	    /*
		if (i == 0 && cloudWrite){
			ROS_DEBUG("Saving the stitched cloud! _stitcher10");
			pcl::PLYWriter writer;
			writer.write<pcl::PointXYZRGB> ("StitchedCloud_new.ply", *cloud_full, false);
			ROS_DEBUG_STREAM("Cloud size: " << cloud_full->size());
		}
		*/
		if(i == 0){
			cloudWriter(cloud_full, "StitchedCloud_new.ply", cloudWrite);
		}
	}
	ROS_DEBUG("Exiting filterStitch");
}


void Stitch::callback(const boost::shared_ptr<const nav_msgs::Odometry>& odomPtr, const boost::shared_ptr<const sensor_msgs::PointCloud2>& cloudPtr, const boost::shared_ptr<const sensor_msgs::JointState>& jointPtr)
{

	arm_height = jointPtr->position[5];

	ROS_INFO_STREAM("ODOM POSITION: " << odomPtr->pose.pose.position);		

	cb.push_back(*cloudPtr);
	cb_odom.push_back(*odomPtr);	

	ROS_INFO_STREAM("Size of cb: " << cb.size());

	if (cb.size() == num_images){
		std::clock_t begin = clock();
		Stitch::filterStitch(cb, cb_odom);
		//this->filterStitch(cb);

		// Publish stitched cloud
		ROS_DEBUG_STREAM("Time: " << ros::Time::now().toNSec());
  		//cloud_full->header.stamp = ros::Time::now().toNSec();
  		cloud_full->header.frame_id = "multisense/left_camera_optical_frame"; //"multisense/head";
  		for (size_t i = 0; i < cloud_full->points.size (); ++i)
    		cloud_full->points[i].x = cloud_full->points[i].x - 4.f*offset_HACK;
  		cloudPub.publish (cloud_full);

  		/*
  		if(cloudWrite){
			pcl::PLYWriter writer;
			writer.write<pcl::PointXYZRGB> ("StitchedCloud_debug.ply", *cloud_full, false);
		}
		*/
		cloudWriter(cloud_full, "StitchedCloud_debug.ply", cloudWrite);

  		ROS_DEBUG_STREAM("--------------DONE STITCHING!!---------------");
		std::clock_t stitching_time = clock();  		

		// Estimate stalk locations
		ROS_DEBUG("Entering slice function");		
		Stitch::slice(cloud_full, GlobalTransform_odom);

		ROS_DEBUG_STREAM("-------------------------DONE SLICING!!------------------------");
		std::clock_t end = clock();
  		double elapsed_secs = double(end - begin) / CLOCKS_PER_SEC;
		ROS_INFO_STREAM("-------------------------TOTAL ELAPSED TIME------------------------");  		
		ROS_INFO_STREAM("-------------------------" << elapsed_secs << " SECONDS-----------------------");  

		double stitch_secs = double(stitching_time - begin) / CLOCKS_PER_SEC;
		double detection_secs = double(end - stitching_time) / CLOCKS_PER_SEC;
		ROS_INFO_STREAM("STITCHING TIME: " << stitch_secs << ",   DETECTION TIME: " << detection_secs); 		

		// Close the gripper
		/*
		ros::ServiceClient gripperclient = mNode.serviceClient<gripper::GripperClose>("gripper_close");	
		gripper::GripperClose srv;
	  	srv.request.gripper_close_req = "close";
	  	if(gripperclient.call(srv))
	  	{
	    	ROS_DEBUG_STREAM("Response: " << srv.response.gripper_close_resp);
	  	}
	  	else
	  	{
	    	ROS_ERROR("Failed to call service");
	  	}

		sensor_msgs::JointState output;
	    output.header.stamp = ros::Time::now();
	    output.header.frame_id = "";
	    output.position.push_back(NAN);
        output.position.push_back(NAN);
        output.position.push_back(NAN);
        output.position.push_back(NAN);
        output.position.push_back(NAN);
        output.position.push_back(0);
        output.position.push_back(NAN);
        zPub.publish(output);
        sleep(3);
		*/
		// Clear all variables
		cb.clear();
		cloud_filtered->clear();
		cloud_filtered_old->clear();
		cloud_full->clear();	
		GlobalTransform = Eigen::Matrix4f::Identity ();
		GlobalTransform_odom = Eigen::Matrix4f::Identity ();								
	}
}

std::vector<std::vector<float> > Stitch::hotspotfilterFunction(pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud, int n)
{

	int gnu_counter = n;
	ROS_INFO_STREAM("gnu_counter: " << gnu_counter);
//-------------------Apply filtering based on normals and x-density-----------------------

	pcl::PointXYZRGB minPt, maxPt;
	pcl::getMinMax3D (*cloud, minPt, maxPt);

	ROS_DEBUG("got getMinMax3D");	

	float z_distr = maxPt.z-minPt.z;
	//float y_distr = maxPt.y-minPt.y; 
	float x_distr = maxPt.x-minPt.x; 

	int no_of_rows = int(x_distr*100)+1; 
	int no_of_cols = int(z_distr*100)+1;
	int initial_value = 0;

	std::vector<std::vector<int> > matrix;
	std::vector<std::vector<float> > hotspots;
	std::vector<std::vector<float> > hotspots_gnuplot; //this is for debugging!
	std::vector<std::vector<float> > matrix_normals;
	std::map <std::string, std::vector<int> > point_indices; //this is a map that will be filled with a string of the matrix location and the associated point indices
	ROS_DEBUG("Initialized vectors");
	
	matrix.resize(no_of_rows, std::vector<int>(no_of_cols, initial_value));
	//matrix_normals.resize(no_of_rows, std::vector<float>(no_of_cols, initial_value));
	hotspots.resize(num_centroids_layer, std::vector<float>(3, initial_value)); //this was 10!!
	hotspots_gnuplot.resize(num_centroids_layer, std::vector<float>(3, initial_value)); //this is for debugging!

	// Loop through every point in the cloud, adding to the histogram	
	for(int i=0; i<cloud->size(); i++)
	{
		int scatter_x = -int(((*cloud)[i].x*-100) + (minPt.x)*100); //this is the y-location of the point
		int scatter_z = -int(((*cloud)[i].z*-100) + (minPt.z)*100); //this is the z-location of the point

		matrix[scatter_x][scatter_z] = matrix[scatter_x][scatter_z]+1; //add a count to that cell location
		//matrix_normals[scatter_x][scatter_z] = matrix_normals[scatter_x][scatter_z]+std::abs((*cloud)[i].normal_y); //MJ UNSURE OF THIS!! //add z-normal to that cell location

		//HERE I KEEP TRACK OF WHICH POINT INDICES ARE ASSOCIATED WITH WHICH LOCATION
		std::stringstream ss;
		ss << scatter_x << scatter_z;
		point_indices[ss.str()].push_back(i);
	}

	ROS_DEBUG("Done with loop");

	std::ofstream outFile;
	std::ofstream otherFile;
	//std::stringstream gnu_name;
	//gnu_name << "heatmap_" << gnu_counter << ".dat";
	//const char *gnu_name = "string Literal";
	char gnu_name[12];   // array to hold the result.
	strcpy(gnu_name,"heatmap_"); // copy string one into the result.
	char integer_string[32];
	sprintf(integer_string, "%d", gnu_counter);
	strcat(gnu_name, integer_string); // append string two to the result.
	strcat(gnu_name, ".dat");
	ROS_DEBUG("Named the .dat file");

	//----just for debugging-----
	char other_name[25];   // array to hold the result.
	strcpy(other_name,"heatmap_other_"); // copy string one into the result.
	char other_integer_string[32];
	sprintf(other_integer_string, "%d", gnu_counter);
	strcat(other_name, other_integer_string); // append string two to the result.
	strcat(other_name, ".dat");
	otherFile.open(other_name);
	//-----end of debugging------

	outFile.open(gnu_name);

	float nanInsert = std::numeric_limits<float>::quiet_NaN();
	int I;
	int J;
	int counter = 0;
	ROS_DEBUG("Initialized variables");

	// This is just to fill in the .dat file that's useful for debugging
	for(int i=0; i<no_of_rows; i++)
	{
		for(int j=0; j<no_of_cols; j++) 
		{
			if(matrix[i][j]>0) {
				outFile << i << " " << j << " " << matrix[i][j] << "\n";
			}
			else{
				outFile << i << " " << j << " " << 0 << "\n";			
			}
		}
	}

	// THIS TAKES THE AVERAGE NORMAL VECTOR FOR EACH VOXEL AND THEN DECIDES TO KEEP OR TOSS THE ELEMENTS OF THE VOXEL
	for(int i=0; i<no_of_rows; i++)
	{
		for(int j=0; j<no_of_cols; j++) 
		{
			ROS_DEBUG("In the inner loop");

			if ((matrix[i][j]>hotspot_threshold)) { //&& (matrix_normals[i][j])<0.5) {  //i.e. find a random hotspot
				int hotspot_temp = matrix[i][j];

				ROS_INFO_STREAM("Initializing queue: " << i << " " << j << ", Value: " << matrix[i][j] << ", No_of_cols: " << no_of_cols << ", No_of_rows: " << no_of_rows);
				std::vector<std::vector<int> > queue;

				// Place hotspot in queue
				ROS_DEBUG("Placing hotspot in queue");
				std::vector<int> myvector;
  				myvector.push_back(i);
  				myvector.push_back(j);  		
    			queue.push_back(myvector); //the queue is initialized with the location of a hotspot (?)
    			myvector.clear();

				ROS_DEBUG("Initializing variables");
    			int new_i;
    			int new_j;
    			int loc_x=0;
    			int loc_z=0;
    			int whilecounter = 0;
    			float temp = matrix[i][j];

    			
    			// Start a while-loop clearing elements in a 3x3 grid around the queue elements
    			ROS_INFO("Above the while loop");
    			while(queue.size()){
					for(int k=-1; k<2; k++){  //this checks values at locations -1, 0, and 1
						for(int l=-1; l<2; l++){
							new_i = queue[0][0]; //new_i and new_j contain the location of the hotspot
							new_j = queue[0][1];
							if((k+new_i<no_of_rows)&&(k+new_i>=0)&&(l+new_j<no_of_cols)&&(l+new_j>=0)) {  // Check to make sure the square is within the boundary
								if(((k==-1)||(k==1)||(l==-1)||(l==1))&&(matrix[k+new_i][l+new_j]>noise_threshold)){  //RIGHT HERE IS THE NOISE THRESHOLD!! It also skips the hotspot itself
									myvector.push_back(new_i+k); 
									myvector.push_back(new_j+l);  				
					    			queue.push_back(myvector); //here I'm adding other neighbors to the queue
									myvector.clear();
					    			loc_x+=(k+new_i); //it appears that I'm summing up all of the point locations
									loc_z+=(l+new_j);
									matrix[k+new_i][l+new_j] = 0;
									whilecounter++; 
								}
								else{
					    			loc_x+=(new_i); //CHECK THIS. I MIGHT BE STUPID RIGHT NOW.
									loc_z+=(new_j);
									matrix[new_i][new_j] = 0;
									whilecounter++; 									
								}
								ROS_DEBUG("Inner for - in the while");
								/*
								// Save the largest value found in the region search (temp is the original hotspot location)
								if(matrix[k+new_i][l+new_j] > temp){
									temp = matrix[k+new_i][l+new_j];
									hotspots[counter][0] = minPt.z + float(l+new_j)/100; //here I reassign the hotspot location
									hotspots[counter][1] = minPt.y + float(k+new_i)/100;
									hotspots[counter][2] = matrix[k+new_i][l+new_j];
								}
								
								// Decimate the spot
								if (matrix[k+new_i][l+new_j] > noise_threshold){ //TODO: THIS CHECK ISN"T NEEDED!!!
									loc_x+=(k+new_i); //it appears that I'm summing up all of the point locations
									loc_z+=(l+new_j);
									matrix[k+new_i][l+new_j] = 0;
									whilecounter++; 
								}
								*/

								if(gnu_counter==2){
									//std::cout << "loc_x: " << loc_x << ", loc_z: " << loc_z << std::endl;
								}

							}
						}
					}
					queue.erase(queue.begin()); 

					//----------DEBUGGING!---------
					/*
					if(gnu_counter==2){
						std::cout << "Elements of queue: " << std::endl;						
						for(int t=0; t<queue.size(); t++){
							for(int u=0; u<queue[t].size(); u++){							
								std::cout << " " << queue[t][u];
							}
							std::cout << std::endl;
						}
					}
					*/
					//------------end debugging-----------------
    			}
    			std::cout << "loc_x: " << loc_x << ", loc_z: " << loc_z << std::endl;
    			std::cout << "whilecounter is: " << whilecounter << ", loc_x averaged is: " << (float(loc_x)/whilecounter)/100. << ", loc_z: " << (float(loc_z)/whilecounter)/100. << std::endl;
    			hotspots[counter][1] = minPt.x + (float(loc_x)/whilecounter)/100.; //...and then here i'm averaging those locations
    			hotspots[counter][0] = minPt.z + (float(loc_z)/whilecounter)/100.; 
				ROS_INFO_STREAM("Slice " << gnu_counter << ", hotspot loc x: " << (hotspots[counter][1] - minPt.x)*100 << ", z: " << (hotspots[counter][0] - minPt.z)*100);
				hotspots_gnuplot[counter][1] = int(float(loc_x)/whilecounter + 0.5); // the 0.5 is to make the int split the difference in rounding
    			hotspots_gnuplot[counter][0] = int(float(loc_z)/whilecounter + 0.5);
    			//outFile << loc_x/whilecounter << " " << loc_z/whilecounter << " " << 5000 << "\n";
				counter++;
			}
		}
	}


	//--------------_THIS IS JUST FOR DEBUGGING!!-------------
    int flag;
	for(int i=0; i<no_of_rows; i++){
		for(int j=0; j<no_of_cols; j++){
			for(int k=0; k<counter; k++){
				if(i==hotspots_gnuplot[k][1] && j==hotspots_gnuplot[k][0]) {
					flag=1;
					ROS_DEBUG_STREAM("-----FLAG SET-----, " << hotspots_gnuplot[k][1] << " " << hotspots_gnuplot[k][0]);
				}
			}
			if(flag==1) {
				otherFile << i << " " << j << " " << 5000 << "\n";
			}
			else{
				otherFile << i << " " << j << " " << 0 << "\n";				
			}
			flag = 0;
		}
	}
	//----------------end of debugging--------------

    outFile.close();
    otherFile.close();  //just for debugging!
 	return(hotspots);
}

void Stitch::slice(const pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_full, Eigen::Matrix4f GlobalTransform_odom){

	ROS_INFO("Calling ArmInitStitch client");
	ros::ServiceClient client = mNode.serviceClient<arm_executive::ArmInitStitch>("arm_init");

	//--------------------------------Filter Cloud-Full for Rectangular Unreachable Areas---------------
	ROS_DEBUG_STREAM("Size of cloud_full: " << cloud_full->size());
	std::clock_t begin = clock();
	pcl::PassThrough<pcl::PointXYZRGB> pass2;
    pass2.setInputCloud (cloud_full);
    pass2.setFilterFieldName ("x");
    pass2.setFilterLimits (-arm_extended-cam_x_offset, arm_extended-cam_x_offset-far_reach_reduction); //far_reach_reduction limits reaches over toward the camera
    pass2.filter (*cloud_full);
    std::cout << "Size of cloud_full: " << cloud_full->size() << std::endl;

	pcl::PassThrough<pcl::PointXYZRGB> pass3;
    pass3.setInputCloud (cloud_full);
    pass3.setFilterFieldName ("z");
    pass3.setFilterLimits (0, arm_extended+cam_z_offset);
    pass3.filter (*cloud_full);

	//-----------------------------Voxellize the cloud!!!----------------------
	//pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_full_xyz_vox (new pcl::PointCloud<pcl::PointXYZ>);
	pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_full_vox (new pcl::PointCloud<pcl::PointXYZRGB>);
	pcl::VoxelGrid<pcl::PointXYZRGB> sor;
	sor.setInputCloud(cloud_full);
	sor.setLeafSize(slice_leafsize, slice_leafsize, slice_leafsize);
	sor.filter(*cloud_full_vox); 

    //----------------------------Filter Cloud-Full to Remove Darker Pixels----------------------------------
    pcl::ConditionAnd<pcl::PointXYZRGB>::Ptr color_cond (new pcl::ConditionAnd<pcl::PointXYZRGB> ());
    color_cond->addComparison (pcl::PackedRGBComparison<pcl::PointXYZRGB>::Ptr (new pcl::PackedRGBComparison<pcl::PointXYZRGB> ("r", pcl::ComparisonOps::GT, rMin_slice)));
    color_cond->addComparison (pcl::PackedRGBComparison<pcl::PointXYZRGB>::Ptr (new pcl::PackedRGBComparison<pcl::PointXYZRGB> ("g", pcl::ComparisonOps::GT, gMin_slice)));
    color_cond->addComparison (pcl::PackedRGBComparison<pcl::PointXYZRGB>::Ptr (new pcl::PackedRGBComparison<pcl::PointXYZRGB> ("b", pcl::ComparisonOps::GT, bMin_slice)));
    color_cond->addComparison (pcl::PackedRGBComparison<pcl::PointXYZRGB>::Ptr (new pcl::PackedRGBComparison<pcl::PointXYZRGB> ("r", pcl::ComparisonOps::LT, rMax_slice)));
    color_cond->addComparison (pcl::PackedRGBComparison<pcl::PointXYZRGB>::Ptr (new pcl::PackedRGBComparison<pcl::PointXYZRGB> ("g", pcl::ComparisonOps::LT, gMax_slice)));
    color_cond->addComparison (pcl::PackedRGBComparison<pcl::PointXYZRGB>::Ptr (new pcl::PackedRGBComparison<pcl::PointXYZRGB> ("b", pcl::ComparisonOps::LT, bMax_slice)));    

    // build the filter
    pcl::ConditionalRemoval<pcl::PointXYZRGB> condrem (color_cond);
    condrem.setInputCloud (cloud_full_vox);
    condrem.setKeepOrganized(true);
      
    // apply filter
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_vox_dist_color (new pcl::PointCloud<pcl::PointXYZRGB>);	
    condrem.filter (*cloud_vox_dist_color);

    std::vector<int> indices;
	pcl::removeNaNFromPointCloud(*cloud_vox_dist_color,*cloud_vox_dist_color, indices);
	ROS_INFO_STREAM("cloud_vox_dist_color size:" << cloud_vox_dist_color->size());
	//if(cloudWrite){
	//	pcl::PLYWriter writer;
	//	writer.write ("cloud_vox_dist_color.ply", *cloud_vox_dist_color, false);
	//}

	int counter = 0;
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_vox_dist_color_new (new pcl::PointCloud<pcl::PointXYZRGB>);
    for (size_t i = 0; i < cloud_vox_dist_color->size (); ++i){
    	if((cloud_vox_dist_color->points[i].g-cloud_vox_dist_color->points[i].r)>5){
 			cloud_vox_dist_color_new->push_back(cloud_vox_dist_color->points[i]);//points[counter] = cloud_vox_dist_color->points[i];
 			counter++;   	
 		}	
    }
	cloudWriter(cloud_vox_dist_color_new, "cloud_vox_dist_color_new.ply", cloudWrite);

	total_green = 0;
	for (size_t i = 0; i < cloud_vox_dist_color_new->size (); ++i){
		total_green+=cloud_vox_dist_color_new->points[i].g;
	}

	//cloudWriter(cloud_vox_dist_color, "cloud_vox_dist_color.ply", cloudWrite);

	total_green = total_green/cloud_vox_dist_color_new->size();
	hotspot_threshold = cloud_vox_dist_color_new->size()/100;
	noise_threshold = cloud_vox_dist_color_new->size()/200;

	std::cout << "------------------------------------------------------------------------" << std::endl;
	std::cout << "Total points: " << cloud_vox_dist_color_new->size() << ", Total green: " << total_green << std::endl;
	std::cout << "Hotspot threshold: " << hotspot_threshold << ", Noise threshold: " << noise_threshold << std::endl;	
	std::cout << "------------------------------------------------------------------------" << std::endl;	

	//--------------------------------Start Slicing--------------------------------------

	std::vector<std::vector<std::vector<float> > > sphere_locs_3d (num_slices,std::vector<std::vector<float> >(num_centroids_layer,std::vector<float>(3, 0)));  // HARD CODING 25 SPHERES IN ONE LAYER!!!

	// Turn each cluster index into a point cloud
	std::vector<pcl::PointCloud<pcl::PointXYZRGB>::Ptr> multiCloud;

	pcl::PointCloud<MyPointType>::Ptr sphereCloud (new pcl::PointCloud<MyPointType>);
  	sphereCloud->width = num_spheres; //100;
  	sphereCloud->height = 1;
  	sphereCloud->is_dense = false;
	sphereCloud->points.resize (sphereCloud->width * sphereCloud->height);

  	int globalSphereCount = 0;
  	ROS_DEBUG("above slice loop");

    for(int n=0; n<num_slices; n++){
	    //-----------------------------------------Take a 20cm Slice------------------------------------------
	    pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_slice (new pcl::PointCloud<pcl::PointXYZRGB>);

	    // Create passthrough filter and slice the cloud
	    pcl::PassThrough<pcl::PointXYZRGB> pass;
	    pass.setInputCloud (cloud_vox_dist_color_new);
	    pass.setFilterFieldName ("y");
	    float height_bottom = bottom_start-0.1*(n);
	    float height_middle = middle_start-0.1*(n);
	    float height_top = top_start-0.1*(n);
	    pass.setFilterLimits (height_bottom, height_top);
	    std::cout << "above cloud slice filter" << std::endl;
	    pass.filter (*cloud_slice); 

	    if(cloudWrite){
			pcl::PLYWriter writer;
		    std::stringstream ss8;
		    ss8 << "slice" << n << ".ply";
		    writer.write (ss8.str(), *cloud_slice, false);
		}

		std::vector<int> indices3;
	    pcl::removeNaNFromPointCloud(*cloud_slice,*cloud_slice, indices3);
	
	    //-------------------------------------Heat Map / Filter It-----------------------------------

		std::vector<std::vector<float> > hotspots;

	    // filter one slice of the cloud
	    try{
	    	hotspots = Stitch::hotspotfilterFunction(cloud_slice, n); 
	    }
	    catch(std::exception &e){
	    	ROS_ERROR("Hotspotfilterfxn EXCEPTION");
	    }
	    
		uint32_t shape = visualization_msgs::Marker::SPHERE;
  		visualization_msgs::Marker marker_sphere;
	    std::stringstream ss9;
  		ss9 << "basic_shapes_" << n;

		marker_sphere.pose.orientation.x = 0;
		marker_sphere.pose.orientation.y = 0;
		marker_sphere.pose.orientation.z = 0;
		marker_sphere.pose.orientation.w = 1;
		marker_sphere.scale.x = 0.02;
	    marker_sphere.scale.y = 0.02;
	    marker_sphere.scale.z = 0.02;
	    marker_sphere.color.r = 1.0f;
	    marker_sphere.color.g = 0.0f;
	    marker_sphere.color.b = 0.0f;
	    marker_sphere.color.a = 0.5;
	    marker_sphere.lifetime = ros::Duration();  		

	    /*
	    if(n==2){
	    	for(int q=0; q<hotspots.size(); q++){
	    		std::cout << "hotspot x: "<< hotspots[q][0] << ", hotspot y: " << hotspots[q][1] << std::endl;
	    	}
	    }
	    */

	    //SPHERES UP THE ENTIRE STALK
	    for(int q=0; q<hotspots.size(); q++){
	    	if(hotspots[q][0]!=0){		
		    	ROS_INFO_STREAM("Slice: " << n); 
		    	ROS_INFO_STREAM("hotspots x: " << hotspots[q][0]); 
			    ROS_INFO_STREAM("hotspots y: " << hotspots[q][1]);

			    sphere_locs_3d[n][q][0] = hotspots[q][1];
	  			sphere_locs_3d[n][q][1] = height_middle;
	  			sphere_locs_3d[n][q][2] = hotspots[q][0];

		    	marker_sphere.header.frame_id = "/multisense/left_camera_optical_frame";
			    marker_sphere.header.stamp = ros::Time::now();
			    marker_sphere.ns = ss9.str();
			    marker_sphere.id = q;
			    ROS_DEBUG_STREAM("Sphere iterator: " << q);
			    marker_sphere.type = shape;
			    marker_sphere.action = visualization_msgs::Marker::ADD;
				marker_sphere.pose.position.x = hotspots[q][1];
	    		marker_sphere.pose.position.y = height_middle;
	    		marker_sphere.pose.position.z = hotspots[q][0];

				centroid_x = -(sphere_locs_3d[n][q][0]  + cam_x_offset); //centroids in arm_link coordinate frame
		    	centroid_y = -(sphere_locs_3d[n][q][2] + cam_z_offset); //TODO: MAKE THIS MAKE SENSE	

		    	//----------Search points within radius-------------
				pcl::KdTreeFLANN<pcl::PointXYZRGB> kdtree;

				kdtree.setInputCloud (cloud_vox_dist_color_new);

				pcl::PointXYZRGB searchPoint;

				searchPoint.x = hotspots[q][1];
				searchPoint.y = height_middle;
				searchPoint.z = hotspots[q][0];
		    	std::vector<int> pointIdxRadiusSearch;
				std::vector<float> pointRadiusSquaredDistance;

				float radius = 0.01;
				int green = 0;
				int greenavg = 0;

				//std::cout << "Neighbors within radius search at (" << hotspots[q][1] << " " << height_middle << " " << hotspots[q][0] << ") with radius=" << radius << std::endl;

				if ( kdtree.radiusSearch (searchPoint, radius, pointIdxRadiusSearch, pointRadiusSquaredDistance) > 0 ){
					std::cout << "Number of neighbors: " << pointIdxRadiusSearch.size () << std::endl;
					for (size_t i = 0; i < pointIdxRadiusSearch.size (); ++i){
						green+=cloud_vox_dist_color_new->points[ pointIdxRadiusSearch[i] ].g;
					}
					greenavg = green/pointIdxRadiusSearch.size();
					std::cout << "Green total: " << green << ", Green avg: " << greenavg << std::endl;
				}

				int greenthresh = total_green-20;

				if(sqrt(pow(centroid_x,2)+pow(centroid_y,2))<arm_extended && height_middle<(middle_start-0.2) && greenavg > greenthresh){
				    markerPub.publish(marker_sphere);
				    /*
				    ROS_INFO_STREAM("middle_start: " << height_middle);
				    
				    sensor_msgs::JointState output;
				    output.header.stamp = ros::Time::now();
				    output.header.frame_id = "";
				    output.position.push_back(NAN);
			        output.position.push_back(NAN);
			        output.position.push_back(NAN);
			        output.position.push_back(NAN);
			        output.position.push_back(NAN);
			        output.position.push_back(-24.1*height_middle);
			        output.position.push_back(NAN);
			        zPub.publish(output);
			        sleep(10);

					arm_executive::ArmInitStitch srv;
				  	srv.request.arm_init_req = "left";
				  	srv.request.arm_init_req_x = centroid_x;
				  	srv.request.arm_init_req_y = centroid_y;

				  	if (client.call(srv))
				  	{
				    	ROS_INFO_STREAM("Response: " << srv.response.arm_init_resp);
				  	}
				  	else
				  	{
				    	ROS_ERROR("Failed to call service");
				  	}  
				  	*/
				}
				else if(height_middle>=(middle_start-0.2)){}
				else if(sqrt(pow(centroid_x,2)+pow(centroid_y,2))>=arm_extended){
					ROS_ERROR_STREAM("Out of reach!! Reach: " << sqrt(pow(centroid_x,2)+pow(centroid_y,2)));
				}
				else
					ROS_ERROR_STREAM("Not green enough: " << greenavg);

			}
		} 

	
		//cout << "out of loop assigning points to sphereCloud" << endl;

  		ROS_DEBUG("Above sphere iterator");;

		// write just the filtered slice (no region growing yet)
    	//if(cloudWrite){
		//	pcl::PLYWriter writer;
		//    writer.write ("slice_filtered.ply", *cloud_slice, false);
		//}
		cloudWriter(cloud_slice, "slice_filtered.ply", cloudWrite);
	}
	ROS_DEBUG("end of slicing loop");
}


int main(int argc, char** argv)
{

	ros::init(argc, argv, "stitcher_server");

  	ros::NodeHandle nh;

	Stitch* pStitch = new Stitch;

	log4cxx::Logger::getLogger(ROSCONSOLE_DEFAULT_NAME)->setLevel(ros::console::g_level_lookup[ros::console::levels::Info]);


  	message_filters::Subscriber<nav_msgs::Odometry> odomSub(nh, "/wheel_encoder/odom", 20);
  	message_filters::Subscriber<sensor_msgs::PointCloud2> cloudSub(nh, "/multisense/image_points2_color", 20);
  	message_filters::Subscriber<sensor_msgs::JointState> jointSub(nh, "/joint_states", 20);

  	typedef sync_policies::ApproximateTime<nav_msgs::Odometry, sensor_msgs::PointCloud2, sensor_msgs::JointState> MySyncPolicy;

  	Synchronizer<MySyncPolicy> sync(MySyncPolicy(30), odomSub, cloudSub, jointSub);
  	sync.registerCallback(boost::bind(&Stitch::callback, pStitch, _1, _2, _3));

	ros::spin();
	
	return 0;
}
