#include <ros/ros.h>

#include <message_filters/subscriber.h>
#include <message_filters/synchronizer.h>
#include <message_filters/sync_policies/approximate_time.h>

#include <geometry_msgs/PoseWithCovarianceStamped.h>

#include <geometry_msgs/PoseArray.h>

#include <nav_msgs/OccupancyGrid.h>

#include <sensor_msgs/LaserScan.h>

#include <tf2_ros/transform_listener.h>
#include <tf2_geometry_msgs/tf2_geometry_msgs.h>

#include <mcl_helper/scan_simulator.h>

using namespace mcl_helper;
ScanSimulator scan_sim;
bool mapSet = false;

ros::Publisher pose_pub;

// MISSING: pose_array being generated and motion update

struct euler {
    double roll, pitch, yaw;
};

// convert geometry_msgs::Quaternion to roll pitch yaw
euler convert(const geometry_msgs::Quaternion& q) {
    tf2::Quaternion rot;
    tf2::convert(q, rot);
    tf2::Matrix3x3 m(rot);
    double roll, pitch, yaw;
    m.getRPY(roll, pitch, yaw);
    euler e;
    e.roll = roll;
    e.pitch = pitch;
    e.yaw = yaw;
    return e;
}

enum Mode {
    WEIGHTED_AVERAGE,
    HIGHEST_WEIGHT,
};

void getPose(const geometry_msgs::PoseArray::ConstPtr &pose_array, std::vector<double> particle_weights, Mode mode) {

    if(mode == Mode::WEIGHTED_AVERAGE) {
        // ROS_INFO("WEIGHTED_AVERAGE");
        // normalize weights
        double sum = 0;
        for(int i = 0; i < particle_weights.size(); i++) {
            sum += particle_weights[i];
        }
        for(int i = 0; i < particle_weights.size(); i++) {
            particle_weights[i] /= sum;
        }
        
        // get weighted average
        double x,y,z,roll,pitch,yaw;
        for(int i = 0; i < pose_array->poses.size(); i++) {
            x += pose_array->poses[i].position.x * particle_weights[i];
            y += pose_array->poses[i].position.y * particle_weights[i];
            // z += pose_array->poses[i].position.z * particle_weights[i];
            euler e = convert(pose_array->poses[i].orientation);
            yaw += e.yaw * particle_weights[i];
        }

        geometry_msgs::Pose pose;
        pose.position.x = x;
        pose.position.y = y;
        // pose.position.z = z;
        pose.orientation = tf2::toMsg(tf2::Quaternion(0, 0, yaw));
        // return pose;

        geometry_msgs::PoseWithCovarianceStamped pose_stamped;
        pose_stamped.header.stamp = ros::Time::now();
        pose_stamped.header.frame_id = "map";
        pose_stamped.pose.pose = pose;
        pose_pub.publish(pose_stamped);

    } else if(mode == Mode::HIGHEST_WEIGHT) {
        // ROS_INFO("HIGHEST_WEIGHT");
        int index = 0;
        for(int i = 0; i < particle_weights.size(); i++) {
            // ROS_INFO("%d: %f", i, particle_weights[i]);
            if(particle_weights[i] > particle_weights[index]) {
                index = i;
            }
        }
        // ROS_INFO("Highest weight: %f", particle_weights[index]);
        // return pose_array->poses[index];
        geometry_msgs::PoseWithCovarianceStamped pose_stamped;
        pose_stamped.header.stamp = ros::Time::now();
        pose_stamped.header.frame_id = "map";
        pose_stamped.pose.pose = pose_array->poses[index];
        pose_pub.publish(pose_stamped);
    }
}

void realScanCallback(const sensor_msgs::LaserScan::ConstPtr &scan, const geometry_msgs::PoseArray::ConstPtr &pose_array) {
    if(!mapSet) {
        return;
    }
    
    std::vector<double> particle_weights;
    particle_weights.resize(pose_array->poses.size());
    for(int i = 0; i < pose_array->poses.size(); i++) {
        sensor_msgs::LaserScan simulated_scan;
        simulated_scan.header.stamp = scan->header.stamp;
        simulated_scan.header.frame_id = "scanner_simulated";
        simulated_scan.angle_min = scan->angle_min;
        simulated_scan.angle_max = scan->angle_max;
        simulated_scan.angle_increment = scan->angle_increment;
        simulated_scan.range_min = scan->range_min;
        simulated_scan.range_max = scan->range_max;
        scan_sim.simulateScan(pose_array->poses[i], simulated_scan);

        double sum = 0;
        int invalids = 0;
        for(int j = 0; j < simulated_scan.ranges.size(); j++) {
            // ROS_INFO("simulated_scan.ranges[%d]: %f; scan_ranges[%d]: %f", j, simulated_scan.ranges[j], j, scan->ranges[j]);
            if(simulated_scan.ranges[j] > simulated_scan.range_max || simulated_scan.ranges[j] < simulated_scan.range_min || scan->ranges[j] > scan->range_max || scan->ranges[j] < scan->range_min) {
                invalids++;
                continue;
            }
            // ROS_INFO("diff: %f", simulated_scan.ranges[j] - scan->ranges[j]);
            // ROS_INFO("pow: %f", std::pow((simulated_scan.ranges[j] - scan->ranges[j]) / 2, 2));
            sum += std::exp(std::pow((simulated_scan.ranges[j] - scan->ranges[j]) / 0.5, 2));
            // ROS_INFO("sum: %f", sum);
        }
        if(invalids >= simulated_scan.ranges.size() * 0.4) {
            particle_weights[i] = 0;
        } else {
            particle_weights[i] = sum / (simulated_scan.ranges.size() - invalids);
            // particle_weights[i] = 1 / particle_weights[i];  // Damit gegen 0 bei starkem Unterschied und gegen 1 bei starker Ähnlichkeit
        }
        
        
/*         if(particle_weights[i] < 0.00000001) {
            ros::shutdown();
            std::exit(0);
        } */
        
    }
    getPose(pose_array, particle_weights, Mode::HIGHEST_WEIGHT);
}




void mapCallback(const nav_msgs::OccupancyGrid::ConstPtr &map) {
    scan_sim.setMap(*map);
    mapSet = true;
}

int main(int argc, char **argv) {

    ros::init(argc, argv, "mcl_combined");

    ros::NodeHandle nh;

/*     if( ros::console::set_logger_level(ROSCONSOLE_DEFAULT_NAME, ros::console::levels::Warn) ) {
        ros::console::notifyLoggerLevelsChanged();
    } */
    
    // subscriber
    ros::Subscriber map_sub = nh.subscribe("/map", 1000, &mapCallback);

    message_filters::Subscriber<sensor_msgs::LaserScan> scan_sub(nh, "/scan", 1000);
    message_filters::Subscriber<geometry_msgs::PoseArray> pose_array_sub(nh, "/mcl/poses", 1000);

    typedef message_filters::sync_policies::ApproximateTime<sensor_msgs::LaserScan, geometry_msgs::PoseArray> MySyncPolicy;
    message_filters::Synchronizer<MySyncPolicy> sync(MySyncPolicy(1000), scan_sub, pose_array_sub);
    sync.registerCallback(boost::bind(&realScanCallback, _1, _2));

    pose_pub = nh.advertise<geometry_msgs::PoseWithCovarianceStamped>("/mcl/assumed_pose", 1000);

    ros::spin();

    return 0;

}