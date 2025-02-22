// -*- c++ -*-
/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2019 ifm electronic, gmbh
 */

#ifndef IFM3D_ROS2_CAMERA_NODE_HPP_
#define IFM3D_ROS2_CAMERA_NODE_HPP_

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <rclcpp/rclcpp.hpp>
#include <rclcpp_lifecycle/lifecycle_node.hpp>

#include <rcl_interfaces/msg/set_parameters_result.hpp>
#include <sensor_msgs/msg/compressed_image.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <sensor_msgs/msg/point_cloud2.hpp>

#include <ifm3d_ros2/visibility_control.h>

#include <ifm3d_ros2/msg/extrinsics.hpp>
#include <ifm3d_ros2/srv/dump.hpp>
#include <ifm3d_ros2/srv/config.hpp>
#include <ifm3d_ros2/srv/softon.hpp>
#include <ifm3d_ros2/srv/softoff.hpp>

#include <ifm3d/device/device.h>
#include <ifm3d/fg.h>

using TC_RETVAL = rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;

using ImageMsg = sensor_msgs::msg::Image;
using ImagePublisher = std::shared_ptr<rclcpp_lifecycle::LifecyclePublisher<ImageMsg>>;

using CompressedImageMsg = sensor_msgs::msg::CompressedImage;
using CompressedImagePublisher = std::shared_ptr<rclcpp_lifecycle::LifecyclePublisher<CompressedImageMsg>>;

using PCLMsg = sensor_msgs::msg::PointCloud2;
using PCLPublisher = std::shared_ptr<rclcpp_lifecycle::LifecyclePublisher<PCLMsg>>;

using ExtrinsicsMsg = ifm3d_ros2::msg::Extrinsics;
using ExtrinsicsPublisher = std::shared_ptr<rclcpp_lifecycle::LifecyclePublisher<ExtrinsicsMsg>>;

using DumpRequest = std::shared_ptr<ifm3d_ros2::srv::Dump::Request>;
using DumpResponse = std::shared_ptr<ifm3d_ros2::srv::Dump::Response>;
using DumpService = ifm3d_ros2::srv::Dump;
using DumpServer = rclcpp::Service<ifm3d_ros2::srv::Dump>::SharedPtr;

using ConfigRequest = std::shared_ptr<ifm3d_ros2::srv::Config::Request>;
using ConfigResponse = std::shared_ptr<ifm3d_ros2::srv::Config::Response>;
using ConfigService = ifm3d_ros2::srv::Config;
using ConfigServer = rclcpp::Service<ifm3d_ros2::srv::Config>::SharedPtr;

using SoftoffRequest = std::shared_ptr<ifm3d_ros2::srv::Softoff::Request>;
using SoftoffResponse = std::shared_ptr<ifm3d_ros2::srv::Softoff::Response>;
using SoftoffService = ifm3d_ros2::srv::Softoff;
using SoftoffServer = rclcpp::Service<ifm3d_ros2::srv::Softoff>::SharedPtr;

using SoftonRequest = std::shared_ptr<ifm3d_ros2::srv::Softon::Request>;
using SoftonResponse = std::shared_ptr<ifm3d_ros2::srv::Softon::Response>;
using SoftonService = ifm3d_ros2::srv::Softon;
using SoftonServer = rclcpp::Service<ifm3d_ros2::srv::Softon>::SharedPtr;

/**
   * provide legacy schema masks and lookup to buffer ids
   * (until interfaces changes)
 */
namespace ifm3d_legacy
{
  const std::uint16_t IMG_RDIS = (1 << 0);        // 2**0
  const std::uint16_t IMG_AMP = (1 << 1);         // 2**1
  const std::uint16_t IMG_RAMP = (1 << 2);        // 2**2
  const std::uint16_t IMG_CART = (1 << 3);        // 2**3
//  const std::uint16_t IMG_UVEC = (1 << 4);        // 2**4
//  const std::uint16_t EXP_TIME = (1 << 5);        // 2**5
//  const std::uint16_t IMG_GRAY = (1 << 6);        // 2**6
//  const std::uint16_t ILLU_TEMP = (1 << 7);       // 2**7
//  const std::uint16_t INTR_CAL = (1 << 8);        // 2**8
//  const std::uint16_t INV_INTR_CAL = (1 << 9);    // 2**9
//  const std::uint16_t JSON_MODEL = (1 << 10);     // 2**10
//  const std::uint16_t IMG_DIS_NOISE = (1 << 11);  // 2**11

  std::map<std::uint16_t, ifm3d::buffer_id> schema_mask_buffer_id_map{
    {IMG_RDIS, ifm3d::buffer_id::RADIAL_DISTANCE_IMAGE},
    {IMG_AMP, ifm3d::buffer_id::NORM_AMPLITUDE_IMAGE},
    {IMG_RAMP, ifm3d::buffer_id::AMPLITUDE_IMAGE},
    {IMG_CART, ifm3d::buffer_id::XYZ}
  };

  ifm3d::FrameGrabber::BufferList buffer_list_from_schema_mask(const std::uint16_t mask)
  {
    ifm3d::FrameGrabber::BufferList buffer_list;

    for (auto& [schema_mask, buffer_id]: schema_mask_buffer_id_map) {
      if ((mask & schema_mask) == schema_mask)
      {
        buffer_list.emplace_back(buffer_id);
      }
    }
    return buffer_list;
  }
}  // namespace ifm3d_legacy

namespace ifm3d_ros2
{
/**
 * Managed node that implements an ifm3d camera driver for ROS 2 software
 * systems.
 *
 * This camera node is implemented as a lifecycle node allowing for
 * management by an external process or tool. State transitions (edges in the
 * managed node FSM graph) are handled by the `on_XXX()` callback functions
 * implemented herein.
 */
class IFM3D_ROS2_PUBLIC CameraNode : public rclcpp_lifecycle::LifecycleNode
{
public:
  /**
   * Instantiates the LifecycleNode. At the completion of the ctor, the
   * following initialization (beyond calling the parent ctor) has been done:
   *
   * - A named logger for this node has been initialized
   * - tf frame names have been initialzed based on the node name
   * - All parameters have been declared and a `set` callback has been
   *   registered
   * - All publishers have been created.
   */
  explicit CameraNode(const std::string& node_name, const rclcpp::NodeOptions& opts);

  /**
   * Delegates construction to the above ctor.
   */
  explicit CameraNode(const rclcpp::NodeOptions& opts);

  /**
   * RAII deallocations. As of this writing, given that all structures are
   * handled by various types of smart pointers no "real work" needs to be
   * done here. However, for debugging purposes we emit a log message so we
   * know when the dtor has actually been called and hence when all
   * deallocations actually occur.
   */
  ~CameraNode() override;

  /**
   * Implements the "configuring" transition state
   *
   * The following operations are performed:
   *
   * - Parameters are parsed and held locally in instance variables.
   * - If requested, the camera clock is synchronized to the system clock
   * - The core ifm3d data structures (camera, framegrabber, stlimage buffer)
   *   are initialized and ready to stream data based upon the requested
   *   schema mask.
   */
  TC_RETVAL on_configure(const rclcpp_lifecycle::State& prev_state) override;

  /**
   * Implements the "activating" transition state
   *
   * The following operations are performed:
   *
   * - The `on_activate()` method is called on all publishers
   * - A new thread is started that will continuous publish image data from
   *   the camera.
   */
  TC_RETVAL on_activate(const rclcpp_lifecycle::State& prev_state) override;

  /**
   * Implements the "deactivating" transition state
   *
   * The following operations are performed:
   *
   * - The thread that implements the "publish loop" is stopped
   * - All publishers can their `on_deactivate()` method called
   */
  TC_RETVAL on_deactivate(const rclcpp_lifecycle::State& prev_state) override;

  /**
   * Implements the "cleaningup" transition state
   *
   * The following operations are performed:
   *
   * - The ifm3d core data structures (camera, framegrabber, stlimage buffer)
   *   have their dtors called
   */
  TC_RETVAL on_cleanup(const rclcpp_lifecycle::State& prev_state) override;

  /**
   * Implements the "shuttingdown" transition state
   *
   * The following operations are performed:
   *
   * - It is ensured that the publishing loop thread is stopped
   */
  TC_RETVAL on_shutdown(const rclcpp_lifecycle::State& prev_state) override;

  /**
   * Implements the "errorprocessing" transition state
   *
   * The following operations are performed:
   *
   * - The publish_loop thread is stopped (if running)
   * - The ifm3d core data structures (camera, framegrabber, stlimage buffer)
   *   have their dtors called
   */
  TC_RETVAL on_error(const rclcpp_lifecycle::State& prev_state) override;

protected:
  /**
   * Implementation of the Dump service.
   */
  void Dump(std::shared_ptr<rmw_request_id_t> request_header, DumpRequest req, DumpResponse resp);

  /**
   * Implementation of the Config service.
   */
  void Config(std::shared_ptr<rmw_request_id_t> request_header, ConfigRequest req, ConfigResponse resp);

  /**
   * Implementation of the SoftOff service.
   */
  void Softoff(std::shared_ptr<rmw_request_id_t> request_header, SoftoffRequest req, SoftoffResponse resp);

  /**
   * Implementation of the SoftOn service.
   */
  void Softon(std::shared_ptr<rmw_request_id_t> request_header, SoftonRequest req, SoftonResponse resp);

  /**
   * Callback that gets called when a parameter(s) is attempted to be set
   *
   * Some parameters can be changed on the fly while others, if changed,
   * require the node to reconfigure itself (e.g., because it needs to
   * switch the operating mode of the camera or connect to a different camera
   * etc.). In general, we take the new parameter values and set them into
   * the instance variables of this node. However, if a reconfiguration is
   * required, after looking at all the parameters, a state change that would
   * ultimately have the camera reinitialize is affected.
   */
  rcl_interfaces::msg::SetParametersResult set_params_cb(const std::vector<rclcpp::Parameter>& params);

  /**
   * Declares parameters and default values
   */
  void init_params();

  /**
   * Thread function that publishes data to clients
   */
  void publish_loop();

  /**
   * Utility function that makes a best effort to stop the thread publishing
   * loop.
   */
  void stop_publish_loop();

private:
  rclcpp::Logger logger_;
  // global mutex on ifm3d core data structures `cam_`, `fg_`, `im_`
  std::mutex gil_{};

  std::string ip_{};
  std::uint16_t xmlrpc_port_{};
  std::string password_{};
  std::uint16_t schema_mask_{};
  int timeout_millis_{};
  float timeout_tolerance_secs_{};
  float frame_latency_thresh_{};  // seconds
  bool sync_clocks_{};
  std::uint16_t pcic_port_{};

  DumpServer dump_srv_{};
  ConfigServer config_srv_{};
  SoftoffServer soft_off_srv_{};
  SoftonServer soft_on_srv_{};

  ifm3d::Device::Ptr cam_{};
  ifm3d::FrameGrabber::Ptr fg_{};

  ImagePublisher conf_pub_{};
  ImagePublisher distance_pub_{};
  ImagePublisher amplitude_pub_{};
  ImagePublisher raw_amplitude_pub_{};
  PCLPublisher cloud_pub_{};
  ExtrinsicsPublisher extrinsics_pub_{};
  CompressedImagePublisher rgb_pub_{};

  std::thread pub_loop_{};
  std::atomic_bool test_destroy_{};

  std::string camera_frame_{};
  std::string optical_frame_{};

  rclcpp_lifecycle::LifecycleNode::OnSetParametersCallbackHandle::SharedPtr set_params_cb_handle_{};
};  // end: class CameraNode

}  // namespace ifm3d_ros2

#endif  // IFM3D_ROS2_CAMERA_NODE_HPP_
