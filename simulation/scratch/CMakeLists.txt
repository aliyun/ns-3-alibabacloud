# set(ASTRA_PATH /workspace/astra-sim/)
# set(ASTRA_LIB_PATH "/workspace/astra-sim/extern/network_backend/ns3-interface/simulation/scratch")
# message(STATUS "The value of MY_VARIABLE is: ${ASTRA_PATH}")
# include_directories(${ASTRA_PATH})
# link_libraries("${ASTRA_LIB_PATH}/libAstraSim.a")

set(ASTRA_PATH "../../../astra-sim-alibabacloud/")
include_directories(${ASTRA_PATH}  "${ASTRA_PATH}/astra-sim/network_frontend/ns3")

file(GLOB ASTRA_SIM_SOURCE_FILES
	"${ASTRA_PATH}/astra-sim/system/*.cc"
	"${ASTRA_PATH}/astra-sim/workload/*.cc"
	"${ASTRA_PATH}/astra-sim/system/collective/*.cc"
	"${ASTRA_PATH}/astra-sim/system/topology/*.cc"
	"${ASTRA_PATH}/astra-sim/system/memory/*.cc"
	"${ASTRA_PATH}/astra-sim/system/fast-backend/*.cc"
	"${ASTRA_PATH}/astra-sim/system/scheduling/*.cc")

list(FILTER ASTRA_SIM_SOURCE_FILES EXCLUDE  REGEX ".*BootStrapnet\\.cc")
list(FILTER ASTRA_SIM_SOURCE_FILES EXCLUDE  REGEX ".*SimAiFlowModelRdma\\.cc")
list(FILTER ASTRA_SIM_SOURCE_FILES EXCLUDE  REGEX ".*PhyMultiThread\\.cc")

add_library(AstraSim ${ASTRA_SIM_SOURCE_FILES})

set(astra-sim-ns3-libs-list
  "libapplications"
  "libcore"
  "libcsma"
  "libinternet"
  "libpoint-to-point"
  "libnetwork"
)
list(JOIN astra-sim-ns3-libs-list " " astra-sim-ns3-libs)

add_executable(AstraSimNetworkNs3 "${ASTRA_PATH}/astra-sim/network_frontend/ns3/AstraSimNetwork.cc")
target_link_libraries(AstraSimNetworkNs3 ${astra-sim-ns3-libs-list} AstraSim)
