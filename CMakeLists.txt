cmake_minimum_required(VERSION 3.16)
# La carte visee : -DBOARD=isno-super (ESP32-S3) ou -DBOARD=m5-atom-lite (ESP32).
# Chacune a sa cible IDF : voir README, « Construire ».
set(BOARD "isno-super" CACHE STRING "carte visee : boards/<BOARD>.json")
set(PROJECT_VER "0.1.1")  # version au boot / app descriptor ; bumper a chaque release
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(openrftest)
