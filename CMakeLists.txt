cmake_minimum_required(VERSION 3.16)

# Set partition table
set(PARTITION_TABLE_CSV "${CMAKE_CURRENT_LIST_DIR}/main/partitions.csv")

# Exclude matter_controller and zigbee components if not needed
set(EXCLUDE_COMPONENTS "matter_controller" "zigbee.disabled")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(sentinel-esp)