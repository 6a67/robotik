#include <nav_msgs/OccupancyGrid.h>
#include <sensor_msgs/LaserScan.h>
#include <geometry_msgs/PoseWithCovarianceStamped.h>
#include <sensor_msgs/LaserScan.h>
#include <sensor_msgs/PointCloud2.h>

#include <tf2_ros/transform_listener.h>
#include <tf2_geometry_msgs/tf2_geometry_msgs.h>
#include <tf/transform_listener.h>
#include <geometry_msgs/TransformStamped.h>
#include <laser_geometry/laser_geometry.h>
#include <tf2_sensor_msgs/tf2_sensor_msgs.h>

#include <message_filters/subscriber.h>
#include <message_filters/synchronizer.h>
#include <message_filters/sync_policies/approximate_time.h>

#include <ros/ros.h>

ros::Publisher map_pub;

// convert laserscan to map
void mapCallback(const geometry_msgs::PoseWithCovarianceStamped::ConstPtr &pose, const sensor_msgs::LaserScan::ConstPtr &laser)
{

    // laserscan to pointcloud
    laser_geometry::LaserProjection projector;
    sensor_msgs::PointCloud2 cloud;
    projector.projectLaser(*laser, cloud);

    // transform laserscan with pose
    tf2_ros::Buffer tfBuffer;
    tf2_ros::TransformListener tfListener(tfBuffer);
    geometry_msgs::TransformStamped transformStamped;
    while(ros::ok()) {
        try {
            transformStamped = tfBuffer.lookupTransform("odom_combined", cloud.header.frame_id, ros::Time(0));
            break;
        }
        catch (tf2::TransformException &ex) {
            ROS_WARN("%s",ex.what());
            ros::Duration(1.0).sleep();
            continue;
        }
    }

    tf2::doTransform(cloud, cloud, transformStamped);
   
    // create map
    nav_msgs::OccupancyGrid map;
    map.header.frame_id = "base_link";
    map.header.stamp = cloud.header.stamp;
    map.info.resolution = 0.1;
    map.info.width = laser->range_max * 2 / map.info.resolution;
    map.info.height = laser->range_max * 2/ map.info.resolution;
    map.info.origin.position.x = pose->pose.pose.position.x - laser->range_max;
    map.info.origin.position.y = pose->pose.pose.position.y - laser->range_max;
    map.info.origin.position.z = pose->pose.pose.position.z;
    map.info.origin.orientation.x = 0;
    map.info.origin.orientation.y = 0;
    map.info.origin.orientation.z = 0;
    map.info.origin.orientation.w = 1;
    // tf::quaternionTFToMsg(tf::createQuaternionFromRPY(0, 0, 90), map.info.origin.orientation);
    map.data.resize(map.info.width * map.info.height);

    // pointcloud to map
    float* points = reinterpret_cast<float*>(&cloud.data[0]);
    for (int i = 0; i < cloud.width; i += 5) {
        int x = (points[i] - map.info.origin.position.x) / map.info.resolution;
        ROS_INFO("hi");
        int y = (points[i + 1] - map.info.origin.position.y) / map.info.resolution;
        ROS_INFO("hi");
        if (x < 0 || x > map.info.width || y < 0 || y > map.info.height) {
            continue;
        }
        map.data[y * map.info.width + x] = 100;
    }

    // publish map
    map_pub.publish(map);
}

int main(int argc, char **argv) {

    ros::init(argc, argv, "fill_map");

    ros::NodeHandle nh;

    // publisher
    map_pub = nh.advertise<nav_msgs::OccupancyGrid>("map", 1000);


    // subscriber
    message_filters::Subscriber<geometry_msgs::PoseWithCovarianceStamped> ekf_sub(nh, "/robot_pose_ekf/odom_combined", 1);
    message_filters::Subscriber<sensor_msgs::LaserScan> laser_sub(nh, "/scan", 1);

    typedef message_filters::sync_policies::ApproximateTime<geometry_msgs::PoseWithCovarianceStamped, sensor_msgs::LaserScan> MySyncPolicy;
    message_filters::Synchronizer<MySyncPolicy> sync(MySyncPolicy(10), ekf_sub, laser_sub);
    sync.registerCallback(boost::bind(&mapCallback, _1, _2));


    ros::spin();

    return 0;
}

