cmake_minimum_required(VERSION 2.8.3)
project(multisense_ros)

find_package(Boost REQUIRED COMPONENTS thread)

find_package(catkin REQUIRED COMPONENTS roscpp
                                        std_msgs
                                        genmsg
                                        tf
                                        multisense_lib
                                        rosbag
                                        sensor_msgs
                                        geometry_msgs
                                        image_transport
                                        angles
                                        cv_bridge
                                        dynamic_reconfigure
                                        image_geometry
                                        message_generation)

set(CMAKE_C_FLAGS "-O2")
set(CMAKE_CXX_FLAGS "-O2")

generate_dynamic_reconfigure_options(cfg/multisense.cfg)

add_message_files(DIRECTORY msg
                  FILES
                  DeviceInfo.msg
                  RawCamConfig.msg
                  RawImuData.msg
                  RawLidarData.msg
                  RawCamCal.msg
                  RawCamData.msg
                  RawLidarCal.msg
                  Histogram.msg
                  DeviceStatus.msg
                  StampedPps.msg)

generate_messages(DEPENDENCIES sensor_msgs)

catkin_package(INCLUDE_DIRS   include
               CATKIN_DEPENDS roscpp
                              std_msgs
                              genmsg
                              tf
                              multisense_lib
                              rosbag
                              sensor_msgs
                              angles
                              cv_bridge
                              dynamic_reconfigure
                              image_geometry
                              image_transport
                              message_runtime
                              message_generation
               LIBRARIES      ${PROJECT_NAME})

include_directories(include
                    ${catkin_INCLUDE_DIRS}
                    ${Boost_INCLUDE_DIRS}
                    ${multisense_lib_INCLUDE_DIRS})

## Multisense Library


add_library(${PROJECT_NAME} src/camera.cpp
                          src/imu.cpp
                          src/laser.cpp
                          src/pps.cpp
                          src/status.cpp
                          src/reconfigure.cpp)

add_dependencies(${PROJECT_NAME} ${PROJECT_NAME}_gencfg)
add_dependencies(${PROJECT_NAME} ${PROJECT_NAME}_generate_messages_cpp)
add_dependencies(${PROJECT_NAME} multisense_lib)

target_link_libraries(${PROJECT_NAME} ${catkin_LIBRARIES}
                                      ${Boost_LIBRARIES}
                                      ${multisense_lib_LIBRARIES})


set_target_properties(${PROJECT_NAME}
  PROPERTIES LINK_FLAGS "-L${PROJECT_SOURCE_DIR}/../multisense_lib/sensor_api/bin/")
set_target_properties(${PROJECT_NAME}
  PROPERTIES COMPILE_FLAGS "-I${PROJECT_SOURCE_DIR}/../multisense_lib/sensor_api/source/LibMultiSense")

## ROS Driver

add_executable(ros_driver src/ros_driver.cpp)
target_link_libraries(ros_driver ${PROJECT_NAME}
                                 turbojpeg)
set_target_properties(ros_driver
  PROPERTIES COMPILE_FLAGS "-I${PROJECT_SOURCE_DIR}/../multisense_lib/sensor_api/source/LibMultiSense")

add_dependencies(ros_driver ${PROJECT_NAME}_generate_messages_cpp)

## ROS Driver_Merritt

add_executable(ros_driver_merritt src/ros_driver_merritt.cpp)
target_link_libraries(ros_driver_merritt ${PROJECT_NAME}
                                 turbojpeg)
set_target_properties(ros_driver_merritt
  PROPERTIES COMPILE_FLAGS "-I${PROJECT_SOURCE_DIR}/../multisense_lib/sensor_api/source/LibMultiSense")

add_dependencies(ros_driver_merritt ${PROJECT_NAME}_generate_messages_cpp)


## Raw Snapshot

add_executable(raw_snapshot src/raw_snapshot.cpp)
add_dependencies(raw_snapshot ${PROJECT_NAME}_generate_messages_cpp)
add_dependencies(raw_snapshot multisense_lib)
target_link_libraries(raw_snapshot ${catkin_LIBRARIES}
                                   ${Boost_LIBRARIES}
                                   ${multisense_lib_LIBRARIES})

set_target_properties(raw_snapshot
  PROPERTIES COMPILE_FLAGS "-I${PROJECT_SOURCE_DIR}/../multisense_lib/sensor_api/source/LibMultiSense")


## Color Laser Point Cloud


add_executable(color_laser_publisher src/color_laser.cpp)
target_link_libraries(color_laser_publisher ${catkin_LIBRARIES}
                                            ${Boost_LIBRARIES})

## Install
## Mark executables and/or libraries for installation
install(TARGETS ${PROJECT_NAME} ros_driver ros_driver_merritt raw_snapshot color_laser_publisher
  ARCHIVE DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
  LIBRARY DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
  RUNTIME DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION}
)

## Mark cpp header files for installation
install(DIRECTORY include/${PROJECT_NAME}/
  DESTINATION ${CATKIN_PACKAGE_INCLUDE_DESTINATION}
  FILES_MATCHING PATTERN "*.h"
  PATTERN ".svn" EXCLUDE
)

install (DIRECTORY ${CATKIN_DEVEL_PREFIX}/${CATKIN_PACKAGE_SHARE_DESTINATION}/docs
         DESTINATION ${CATKIN_PACKAGE_SHARE_DESTINATION}
)

