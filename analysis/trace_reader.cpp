#include <cstdio>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include "trace-format.h"
#include "trace_filter.hpp"
#include "sim-setting.h"
#include "print_chart.hpp"

using namespace ns3;
using namespace std;

int main(int argc, char** argv){
    if (argc != 2 && argc != 3){
        printf("Usage: ./trace_reader <trace_file> [filter_expr]\n");
        return 0;
    }
    short num = initChart(argv[1]);
    if (num == 0) {
        return 0;
    }
    for (int i = 0; i < num;) {
        std::string strChar = getTrPath();
        FILE* file = fopen(strChar.c_str(), "r");
    	if (file == NULL) {
    	    print("Error opening tr_file!\n");
    	    continue;
    	}
        SimSetting sim_setting;
        sim_setting.Deserialize(file);
        // read trace
        TraceFormat tr;
        while (tr.Deserialize(file) > 0){
            dataCollection(tr);
        }
        setChartData();
        i++;
    }
    printChart();
    return 0;
}
