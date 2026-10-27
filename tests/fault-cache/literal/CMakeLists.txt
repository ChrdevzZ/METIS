cmake_minimum_required(VERSION 3.24)
project(LiteralFaultPolicy NONE)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/received.txt" "${METIS_SANITIZERS}")
