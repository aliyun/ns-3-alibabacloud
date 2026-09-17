# This script compiles the customized ns-3 executable with CLEM-specific modules enabled.

rm -rf ./build
# clean the shared memory virtual files
rm -rf /dev/shm/shm_nccl_ns3_*
rm -rf ./cmake-cache
./ns3 clean
./ns3 configure -d default
./ns3 build
