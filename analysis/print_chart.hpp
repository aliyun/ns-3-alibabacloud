//
// Created by sx00007 on 2/19/25.
//

#ifndef SIMAI_MASTER_PRINT_CHART_HPP
#define SIMAI_MASTER_PRINT_CHART_HPP

#include "trace-format.h"
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <iostream>

struct groupDataStruct{
    std::string trPath = "/etc/astra-sim/simulation/llama_hpn7_mix.tr";
    std::string title = "tp4";
    short tp = 4;
    short dp = 1;
    short pp = 1;
    short vpp = 1;
    short dataRate = 4000; //Gbps
    short linkDelay = 25; //ns
    short sendLat = 3000; //ns
    short numLayer = 4;
    u_int64_t attnComp = 275641;
    u_int64_t mlpComp = 440705;
    int switchNode = 4;
    int switchNodePort = 1;
    int switchNodeTotalRate = 16000; //Gbps
    bool actFlag = false;
    short nodeNum = 4;
};
struct xy{
    uint64_t xTime;
    uint64_t yCount = 0;
};
struct dataPieStruct{
    uint64_t totalTime;
    uint64_t compTime;
    uint64_t commTime;
    uint64_t dataTime;
    uint64_t sendLatTime;
    uint64_t linkDelay;
    uint64_t bandDelay;
    uint64_t forwardTime;
    uint64_t backwardTime;
    uint64_t igTime;
    uint64_t wgTime;
    uint64_t overlapTime;
};
struct dataPacketMsg{
    uint64_t packetNum = 0;
    uint64_t ackNum = 0;
    std::vector<uint64_t> data;
};
struct dataPacketStr{
    std::string packetNum;
    std::string ackNum;
    std::string data;
};

std::string outputFile = "chart.html";
std::ofstream outFile;

short groupCount = -1;
std::vector<groupDataStruct> groupDataVec;

std::vector<std::vector<xy>> timeToCount = {{},{}};
std::vector<std::string> dataVector;
std::vector<std::vector<std::string>> dataLineVec;
std::vector<dataPieStruct> dataPieVec;
std::vector<std::vector<uint64_t>> compData = {};
std::vector<uint64_t> commData;
bool commFlag = false;

dataPacketMsg dataPacket;
std::vector<dataPacketStr> dataPacketStrVec;

std::vector<uint64_t> windowTime = {0, 0};
short windowLength = 400; // 400ns
short windowCount = 10; // step = 400/10 = 40ns
std::vector<std::vector<uint64_t>> windowVec = {{},{}};

uint64_t totalTime = 0;
uint64_t tempTime = 0;
uint64_t beginTime = 0;
int maxValue = 0;
bool printTraceFlag = false;
bool printFlowFlag = false;
std::vector<bool> eventType = {0, 0, 1, 0};

uint64_t totalCount = 0;
uint64_t oneCount = 0;

static void windowComp(ns3::TraceFormat &tr, short num);
static char l3ProtToChar(uint8_t p);

static short initChart(std::string configPath) {
    //init groupDataVec
    std::ifstream conf;
    conf.open(configPath);
    if (!conf) {
        print("Error opening configFile!\n");
        return 0;
    }
    while (!conf.eof()) {
        std::string key;
        conf >> key;
        if (key.compare("outputFile") == 0) {
            std::string v;
            conf >> v;
            outputFile = v;
        } else if (key.compare("printTrace") == 0) {
            short v;
            conf >> v;
            printTraceFlag = v;
        } else if (key.compare("eventType") == 0) {
            short v;
            conf >> v;
            eventType[0] = v;
            conf >> v;
            eventType[1] = v;
            conf >> v;
            eventType[2] = v;
            conf >> v;
            eventType[3] = v;
        } else if (key.compare("printFlow") == 0) {
            short v;
            conf >> v;
            printFlowFlag = v;
        } else if (key.compare("group") == 0) {
            groupDataStruct groupData;
            groupDataVec.push_back(groupData);
            groupCount++;
        } else if (key.compare("trPath") == 0) {
            std::string v;
            conf >> v;
            groupDataVec[groupCount].trPath = v;
        } else if (key.compare("title") == 0) {
            std::string v;
            conf >> v;
            groupDataVec[groupCount].title = v;
        } else if (key.compare("tp") == 0) {
            short v;
            conf >> v;
            groupDataVec[groupCount].tp = v;
        } else if (key.compare("dp") == 0) {
            short v;
            conf >> v;
            groupDataVec[groupCount].dp = v;
        } else if (key.compare("pp") == 0) {
            short v;
            conf >> v;
            groupDataVec[groupCount].pp = v;
        } else if (key.compare("vpp") == 0) {
            short v;
            conf >> v;
            groupDataVec[groupCount].vpp = v;
        } else if (key.compare("dataRate") == 0) {
            short v;
            conf >> v;
            groupDataVec[groupCount].dataRate = v;
        } else if (key.compare("linkDelay") == 0) {
            short v;
            conf >> v;
            groupDataVec[groupCount].linkDelay = v;
        }  else if (key.compare("sendLat") == 0) {
            short v;
            conf >> v;
            groupDataVec[groupCount].sendLat = v;
        } else if (key.compare("numLayer") == 0) {
            short v;
            conf >> v;
            groupDataVec[groupCount].numLayer = v;
        } else if (key.compare("attnComp") == 0) {
            int v;
            conf >> v;
            groupDataVec[groupCount].attnComp = v;
        } else if (key.compare("mlpComp") == 0) {
            uint64_t v;
            conf >> v;
            groupDataVec[groupCount].mlpComp = v;
        } else if (key.compare("switchNode") == 0) {
            uint64_t v;
            conf >> v;
            groupDataVec[groupCount].switchNode = v;
        } else if (key.compare("switchNodePort") == 0) {
            int v;
            conf >> v;
            groupDataVec[groupCount].switchNodePort = v;
        } else if (key.compare("switchNodePortNum") == 0) {
            int v;
            conf >> v;
            groupDataVec[groupCount].switchNodeTotalRate = groupDataVec[groupCount].dataRate*v;
        } else if (key.compare("actFlag") == 0) {
            short v;
            conf >> v;
            groupDataVec[groupCount].actFlag = v;
        } else if (key.compare("nodeNum") == 0) {
            short v;
            conf >> v;
            groupDataVec[groupCount].nodeNum = v;
        }
    }

    short size = groupDataVec.size();
    outFile.open(outputFile);
    if (!outFile) {
        std::cerr << "无法打开文件以写入！" << std::endl;
        return 0;
    }
    //init head
    outFile <<  R"(
    <!DOCTYPE html>
    <html>
    <head>
      <meta charset="UTF-8">
      <!-- 使用echart外部资源 -->
      <script src="https://cdn.jsdelivr.net/npm/echarts@5.3.2/dist/echarts.min.js"></script>
    <style>
     #chartPieContainer {
        display: flex;
        justify-content: center;
        align-items: center;
        gap: 50px;
        width: 100%;
     }
    </style>
    </head>
    <body>
    )";
    
    //init lineChart
    for (short i = 0; i < size; ++i) {
        outFile << "<div id=\"lineChart" << i << "\" style=\"width: 3000px; height:500px;\"></div>\n";
    }
    
    //init pieChart
    outFile << "    <div id=\"chartPieContainer\">\n";
    for (short i = 0; i < size; ++i) {
        outFile << "<div id=\"pieChart" << i << "\" style=\"width: 600px; height:550px;\"></div>\n";
    }
    outFile << "    </div>\n";
    //init barChart
    outFile << R"(    <select id="intervalSelect">
        <option value="1000">1000</option>
        <option value="2000">2000</option>
        <option value="3000">3000</option>
        <option value="4000">4000</option>
    </select>)";
    outFile << "<div id=\"barChart\" style=\"width: 100%; height:500px;\"></div>\n";
    
    //init function
    outFile << R"(<script>
    // 计算总数
    function getTotalValue(data) {
        let sum = 0;
        data.forEach(item => {
            sum += item.value || 0;
        });
        return sum;
    }

    // 递归计算百分比并格式化数据
    function formatDataWithPercentage(data, total) {
        return data.map(item => {
            let percent = ((item.value / total) * 100).toFixed(2) + '%'; // 计算百分比
            let newItem = {
                ...item,
                label: {
                    show: true,
                    formatter: `${item.name}\n (${percent})`,
                    rotate: 0  // 让标签保持水平
                }
            };
            if (item.children) {
                newItem.children = formatDataWithPercentage(item.children, total);
            }
            return newItem;
        });
    }
    </script>)";
    groupCount = -1;
    return size;
}

static std::string getTrPath() {
    groupCount++;
    dataVector.clear();
    tempTime = 0;
    beginTime = 0;
    for (auto& item : windowVec) {
        item.clear();
    }
    windowTime[0] = 0;
    windowTime[1] = 0;
    commFlag = false;
    maxValue = 0;
    for (auto& item : timeToCount) {
        item.clear();
    }
    std::vector<std::string> dataLine;
    dataLineVec.push_back(dataLine);
    return groupDataVec[groupCount].trPath;
}

static void dataCollection(ns3::TraceFormat &tr) {
    if (printTraceFlag) {
        switch (tr.l3Prot){
            case 0x6:
            case 0x11:
                printf("%lu n:%u %u:%u %u %s ecn:%x %08x %08x %hu %hu %c %u %lu %u %hu(%hu)", tr.time, tr.node, tr.intf, tr.qidx, tr.qlen, EventToStr((ns3::Event)tr.event), tr.ecn, tr.sip, tr.dip, tr.data.sport, tr.data.dport, l3ProtToChar(tr.l3Prot), tr.data.seq, tr.data.ts, tr.data.pg, tr.size, tr.data.payload);
                break;
            case 0xFC: // ACK
                printf("%lu n:%u %u:%u %u %s ecn:%x %08x %08x %u %u %c 0x%02X %u %u %lu %hu", tr.time, tr.node, tr.intf, tr.qidx, tr.qlen, EventToStr((ns3::Event)tr.event), tr.ecn, tr.sip, tr.dip, tr.ack.sport, tr.ack.dport, l3ProtToChar(tr.l3Prot), tr.ack.flags, tr.ack.pg, tr.ack.seq, tr.ack.ts, tr.size);
                break;
            case 0xFD: // NACK
                printf("%lu n:%u %u:%u %u %s ecn:%x %08x %08x %u %u %c 0x%02X %u %u %lu %hu", tr.time, tr.node, tr.intf, tr.qidx, tr.qlen, EventToStr((ns3::Event)tr.event), tr.ecn, tr.sip, tr.dip, tr.ack.sport, tr.ack.dport, l3ProtToChar(tr.l3Prot), tr.ack.flags, tr.ack.pg, tr.ack.seq, tr.ack.ts, tr.size);
                break;
            case 0xFE: // PFC
                printf("%lu n:%u %u:%u %u %s ecn:%x %08x %08x %c %u %u %u %hu", tr.time, tr.node, tr.intf, tr.qidx, tr.qlen, EventToStr((ns3::Event)tr.event), tr.ecn, tr.sip, tr.dip, l3ProtToChar(tr.l3Prot), tr.pfc.time, tr.pfc.qlen, tr.pfc.qIndex, tr.size);
                break;
            case 0xFF: // CNP
                printf("%lu n:%u %u:%u %u %s ecn:%x %08x %08x %c %u %u %u %u %u", tr.time, tr.node, tr.intf, tr.qidx, tr.qlen, EventToStr((ns3::Event)tr.event), tr.ecn, tr.sip, tr.dip, l3ProtToChar(tr.l3Prot), tr.cnp.fid, tr.cnp.qIndex, tr.cnp.ecnBits, tr.cnp.seq, tr.size);
                break;
            case 0x0: // QpAv
                printf("%lu n:%u %u:%u %s %08x %08x %u %u", tr.time, tr.node, tr.intf, tr.qidx, EventToStr((ns3::Event)tr.event), tr.sip, tr.dip, tr.qp.sport, tr.qp.dport);
                break;
            default:
                printf("%lu n:%u %u:%u %u %s ecn:%x %08x %08x %x %u", tr.time, tr.node, tr.intf, tr.qidx, tr.qlen, EventToStr((ns3::Event)tr.event), tr.ecn, tr.sip, tr.dip, tr.l3Prot, tr.size);
                break;
        }
        printf("\n");
    }
    if (tr.node != groupDataVec[groupCount].switchNode) {
        return;
    }
    if (printFlowFlag && tr.event == 2) {
        {
            totalCount += tr.data.payload;
            xy temp;
            temp.xTime = tr.time;
            temp.yCount = totalCount;
            timeToCount[0].push_back(temp);
        }
        if (tr.intf == groupDataVec[groupCount].switchNodePort) {
            oneCount += tr.data.payload;
            xy temp;
            temp.xTime = tr.time;
            temp.yCount = oneCount;
            timeToCount[1].push_back(temp);
        }
    }
    if (!eventType[tr.event]) return;
    if (tr.size== 60) {
        dataPacket.ackNum++;
    } else {
        dataPacket.packetNum++;
    }
    if (dataPacket.data.size() < (tr.size/1000+1)) {
        short temp = dataPacket.data.size();
        short length = tr.size/1000+1;
        for (int i = temp; i < length; ++i) {
            dataPacket.data.push_back(0);
        }
    }
    if (maxValue < tr.size) {
        maxValue = tr.size;
    }
    dataPacket.data[tr.size/1000]++;
    if (!commFlag && beginTime!= 0 && tr.time - tempTime > 5000) {
        commFlag = true;
        commData.push_back(tempTime - beginTime + groupDataVec[groupCount].sendLat);
    }
    if (!printFlowFlag) {
        if (beginTime == 0 && tr.size != 0) {
            beginTime = tr.time;
        }
    
        tempTime = tr.time;
        
        windowComp(tr, 0);
        if (tr.intf != groupDataVec[groupCount].switchNodePort) {
            return;
        }
        windowComp(tr, 1);
    }
}

static void setLineData() {
    printf("dataLineVec:%d\n", dataLineVec.size());
    printf("timeToCount:%d\n", timeToCount[0].size());
    for (size_t ii = 0; ii < timeToCount.size(); ++ii) {
        std::ostringstream data;
        data << "[";
        for (size_t i = 0; i < timeToCount[ii].size(); ++i) {
            if (i != 0) {
                data << ",";
            }
            data << "[";
            data << std::fixed << std::setprecision(2) << timeToCount[ii][i].xTime;
            data << ",";
            data << std::fixed << std::setprecision(2) << timeToCount[ii][i].yCount;
            data << "]";
        }
        data << "]";
        dataLineVec[groupCount].push_back(data.str());
    }
}

static void setBarData() {
    dataPacketStr dataStr;
    dataStr.ackNum = std::to_string(dataPacket.ackNum);
    dataStr.packetNum = std::to_string(dataPacket.packetNum);
    std::ostringstream data;
    data << "[";
    for (size_t i = 0; i < dataPacket.data.size(); ++i) {
        if (i != 0) {
            data << ",";
        }
        data << dataPacket.data[i];
    }
    data << "]";
    dataStr.data = data.str();
    dataPacketStrVec.push_back(dataStr);
}

static void setPieData() {
    uint64_t attnComp = groupDataVec[groupCount].attnComp;
    uint64_t mlpComp = groupDataVec[groupCount].mlpComp;
    uint64_t numLayer = groupDataVec[groupCount].numLayer;
    uint64_t tp = groupDataVec[groupCount].tp;
    uint64_t compTime = numLayer * (attnComp + mlpComp);
    dataPieStruct dataPie;
    dataPie.totalTime = tempTime;
    dataPie.compTime = 3 * compTime;
    dataPie.commTime = dataPie.totalTime - dataPie.compTime;
    dataPie.sendLatTime = (numLayer * 2 + 4) * (tp - 1) * 2 * groupDataVec[groupCount].sendLat;
    dataPie.dataTime = dataPie.commTime - dataPie.sendLatTime;
    dataPie.forwardTime = compTime;
    dataPie.backwardTime = 2 * compTime;
    dataPie.igTime = compTime;
    dataPie.wgTime = compTime;
    uint64_t mlpOverlap = std::min(commData[groupCount], mlpComp);
    uint64_t attentionOverlap = std::min(commData[groupCount], attnComp);
    dataPie.overlapTime = numLayer * (mlpOverlap + attentionOverlap);
    dataPieVec.push_back(dataPie);
}

static void setChartData() {
    setLineData();
    setBarData();
    setPieData();
}

static void print2Line() {
    short ii = -1;
    for (auto &item: dataLineVec) {
        ii++;
        outFile <<  R"(
  <script>
    // 初始化 ECharts 实例
)";
        outFile << "    var lineChart" << std::to_string(ii) << " = echarts.init(document.getElementById('lineChart" << std::to_string(ii) << "'));\n";

        // 示例数据，数据点格式为 [x, y]
        outFile << "        var data" << std::to_string(ii*2) << " = " << item[0] << ";\n";
        outFile << "        var data" << std::to_string(ii*2+1) << " = " << item[1] << ";\n";
        outFile << "        var dataMax" << std::to_string(ii*2) << " = " << std::to_string(groupDataVec[ii].dataRate) << ";\n";
        outFile << "        var dataMax" << std::to_string(ii*2+1) << " = " << std::to_string(groupDataVec[ii].switchNodeTotalRate) << ";\n";
        // ECharts 配置项
        outFile <<  R"(var option = {
      visualMap: [
        {
          show: false,
          type: 'continuous',
          min: 0,
          max: 1800
        }
      ],
      title: {
)";
        outFile << "        text: '" << groupDataVec[ii].title << "\\ntest'";
        outFile << R"(      },
      tooltip: {
        trigger: 'axis'
      },
      legend: {
        data: ['one', 'total'],
        selected: { 'one': true }
      },
      xAxis: {
        type: 'value',
        name: '时间 (ns)'
      },
      yAxis: {
        type: 'value',
        name: '大小 (Gbps)'
      },
      dataZoom: [
        {
          type: 'slider',
          xAxisIndex: 0,
          start: 0,
          end: 100
        },
        {
          type: 'inside',
          xAxisIndex: 0,
          start: 0,
          end: 100
        }
      ],
      series: [
        {
          name: 'total',
          type: 'line',
)";
      outFile << "          data: data" << std::to_string(ii*2) << ",";
      outFile << R"(          lineStyle: {
            color: 'red'
          },
          smooth: false,
          connectNulls: false,
          symbol: 'none'
        },
        {
          name: 'one',
          type: 'line',
)";
        outFile << "          data: data" << std::to_string(ii*2+1) << ",";
        outFile << R"(          lineStyle: {
            color: 'blue'
          },
          smooth: false,
          connectNulls: false,
          symbol: 'none'
        }
      ]
    };

    // 设置配置项生成图表
)";
        outFile << "     lineChart" << std::to_string(ii) <<".setOption(option);\n";
        outFile << "     lineChart" << std::to_string(ii) << ".on('legendselectchanged', function(params) {\n";
        outFile << R"(       if (!params.selected['one'] || !params.selected['total']) {
)";
        outFile << "     lineChart" << std::to_string(ii) <<".setOption({\n";
        outFile << R"(            visualMap: [
            {
              show: false,
              type: 'continuous',
              min: 0,
)";
outFile << "              max: params.selected['one'] ? dataMax" << std::to_string(ii*2) << " : dataMax" << std::to_string(ii*2+1);
outFile << R"(            }
          ],
          series: [
            {
              name: 'one',
              lineStyle: {
                color: null
              },
              markLine: {
                silent: true,
                lineStyle: { color: '#333' },
                symbol: ['none', 'none'],
                data: [
)";
        for (short i = 2; i < 11; i = i + 2) {
            outFile << "                  { yAxis: dataMax" << std::to_string(ii*2) << " * " << std::to_string(i) << "/10, name: '" << std::to_string(i) <<"0%'}";
            if (i != 10) {
                outFile << ",";
            }
            outFile << "\n";
        }
        outFile << R"(                ],
                label: {
                  show: true,
                  position: 'end',
                  formatter: function (params) {
                    return params.name;
                  }
                }
              }
            },
            {
              name: 'total',
              lineStyle: {
                color: null
              },
              markLine: {
                silent: true,
                lineStyle: { color: '#333' },
                symbol: ['none', 'none'],
                data: [
)";
        for (short i = 2; i < 11; i = i + 2) {
            outFile << "                  { yAxis: dataMax" << std::to_string(ii*2+1) << " * " << std::to_string(i) << "/10, name: '" << std::to_string(i) <<"0%'}";
            if (i != 10) {
                outFile << ",";
            }
            outFile << "\n";
        }
        outFile << R"(                ],
                label: {
                  show: true,
                  position: 'end',
                  formatter: function (params) {
                    return params.name;
                  }
                }
              }
            }
          ]
        });
      } else {
)";
        outFile << "     lineChart" << std::to_string(ii) <<".setOption({\n";
        outFile << R"(          series: [
            {
              name: 'one',
              lineStyle: {
                color: 'red'
              },
              markLine: null
            },
            {
              name: 'total',
              lineStyle: {
                color: 'blue'
              },
              markLine: null
            }
          ]
        });
      }
    });

    // 自适应窗口大小变化
    window.addEventListener('resize', function () {
)";
        outFile << "     lineChart" << std::to_string(ii) <<".resize();\n";
        outFile << R"(    });
  </script>
    )";
    }

    return;
}

static void printPie() {
    short i = -1;
    for (auto& item : dataPieVec) {
        i++;
        std::string ii = std::to_string(i);
        outFile << "<script>";
        outFile << "var pieChart" << ii << R"(= echarts.init(document.getElementById('pieChart)" << ii << R"(')))" << "\n";
        outFile << "var dataPie = [\n";
        outFile << R"(            {
                name: '无overlap\n通信时间',
                value:)";
        outFile << std::to_string(dataPieVec[i].commTime) << ",\n";
        outFile << "                children: [\n";
        outFile << "                  { name: '通信发起', value:"  << std::to_string(dataPieVec[i].sendLatTime) << "},\n";
        outFile << "                  { name: '数据传输', value:"  << std::to_string(dataPieVec[i].dataTime) << "}\n";
        outFile << "                ]\n";
        outFile << "            },\n";
        outFile << "            {\n";
        outFile << "                name: '计算时间',\n";
        outFile << "                value: " << std::to_string(dataPieVec[i].compTime) << ",\n";
        outFile << R"(               children: [
                {
                    name: '反向计算时间',)";
        outFile << "\n                    value: " << std::to_string(dataPieVec[i].backwardTime) << ",\n";
        outFile << "                    children: [\n";
        outFile << "                        { name: '反向ig时间', value: " << std::to_string(dataPieVec[i].igTime) << "},\n";
        outFile << R"(                        {
                            name: '反向wg时间',)";
        outFile << "\n                            value: " << std::to_string(dataPieVec[i].wgTime) << ",\n";
        outFile << "                            children: [\n";
        outFile << "                                { name: '反向overlap\\n的通信时间', value: " << std::to_string(dataPieVec[i].overlapTime) << "}\n";
        outFile << R"(                            ]
                        }
                    ]
                },)";
        outFile << "\n        { name: '前向计算时间', value: " << std::to_string(dataPieVec[i].forwardTime) << "}\n";
        outFile << R"(                            ]
            }
        ];)";

        outFile << "var totalValue = getTotalValue(dataPie);\n";

        outFile << "var formattedData" << std::to_string(i) << " = formatDataWithPercentage(dataPie, totalValue);\n";

        // 旭日图配置
        outFile << R"(    var option = {
        title: {)";

        outFile << "        text: '" << groupDataVec[i].title << "计算、通信时间\\n\\n总时间：" << std::to_string(dataPieVec[i].totalTime) << "ns',";
        outFile << R"(            left: 'center'
        },
        tooltip: {
            trigger: 'item',
            formatter: function (params) {
                let percent = ((params.value / totalValue) * 100).toFixed(2);
                return `${params.name}: ${params.value} (${percent}%)`;
            }
        },
        series: [
            {
                name: '分类',
                type: 'sunburst',  // 旭日图
                radius: ['0%', '100%'],
                label: {
                    show: true,
                    rotate: 0  // **关键：让文字水平展示**
                },)";
        outFile << "                data: formattedData" << std::to_string(i);
        outFile << R"(            }
        ]
    };)";

        outFile << "    pieChart" << ii << ".setOption(option);";

        outFile << "    pieChart" << ii << R"(.on('click', function (params) {
        if (params.data.children && params.data.children.length > 0) {
            let newChildrenData = formatDataWithPercentage(params.data.children, getTotalValue(params.data.children));
            let newData = [{
                name: params.data.name,
                label: {
                    show: true,
                    formatter: `${params.data.name}\n${params.data.value}`,
                    rotate: 0  // 让标签保持水平
                },
                value: params.data.value,
                children: newChildrenData
            }];)";
        outFile << "            pieChart" << ii << ".setOption({";
        outFile << R"(            series: [{ data: newData }]
            });
        } else {)";
        outFile << "            pieChart" << ii << ".setOption({";
        outFile << "            series: [{ data: formattedData" << std::to_string(i) << " }]";
        outFile << R"(            });
        }
    });
</script>)";
    }
}

static void printBar() {
    outFile << R"(
    <script>
        var barChart = echarts.init(document.getElementById('barChart'));
// 模拟数据
)";
    for (short i = 0; i < dataPacketStrVec.size(); ++i) {
        outFile << "        var   packetNum" << std::to_string(i+1) << " = " << dataPacketStrVec[0].data << ";\n";
    }

    outFile << R"(    // 初始化区间宽度
    var interval = 1000;

    // 更新函数
    function updateChart() {
)";
    for (short i = 0; i < dataPacketStrVec.size(); ++i) {
        outFile << "        var   histogram" << std::to_string(i+1) << " = new Array(Math.ceil(10000 / interval)).fill(0);\n";
    }


        // 数据桶化
    outFile << "        for (var index = 0; index < packetNum1.length; index++) {\n";
    for (short i = 0; i < dataPacketStrVec.size(); ++i) {
        outFile << "histogram" << std::to_string(i+1) << "[Math.floor(index * 1000/interval)] = histogram" << std::to_string(i+1)  << "[Math.floor(index * 1000/interval)] + packetNum" << std::to_string(i+1)  << "[index];\n";
    }
    outFile << R"(        }
        var xAxisData = [];
        for (var i = 0; i < histogram1.length; i++) {
            xAxisData.push(i * interval + ' - ' + ((i + 1) * interval));
        }

        var option = {
          title: {
            text: 'test'
          },
            tooltip: {
                trigger: 'axis',
                axisPointer: {
                    type: 'shadow'
                }
            },
            legend: {},
            grid: {
                bottom: '7%',
            },
            xAxis: [
                {
                    type: 'category',
                    data: xAxisData,
                    axisLabel: {
                        interval: 0
                    },
                    name: '大小 (byte)'
                }
            ],
            yAxis: [
                {
                    type: 'value'
                }
            ],
            series: [
)";
    for (short i = 0; i < dataPacketStrVec.size(); ++i) {
        outFile << "                {\n";
        outFile << "                    name: '" << groupDataVec[i].title << "',\n";
        outFile << R"(                    type: 'bar',
                    emphasis: {
                        focus: 'series'
                    },
)";
        outFile << "                   data: histogram" << std::to_string(i+1);
        outFile << "               }";
        if (i != dataPacketStrVec.size() - 1) {
            outFile << ",\n";
        } else {
            outFile << "\n";
        }
    }
    outFile << R"(            ]
        };
        barChart.setOption(option);
    }

    // 初始图表
    updateChart();

    // 监听区间选择变化
    document.getElementById('intervalSelect').addEventListener('change', function(event) {
            interval = parseInt(event.target.value);
            updateChart();
    });
    </script>
    )";
}

static void printChart() {

    print2Line();
//    for (short i = 0; i < groupDataVec.size(); ++i) {
//        print2Line();
//    }
    printPie();
    printBar();
    outFile << R"(  </body>
    </html>)";
    outFile.close();
    std::cout << "HTML 文件已生成，请打开 " << outputFile << " 查看效果。" << std::endl;
}

static void windowComp(ns3::TraceFormat &tr, short num) {
    if (windowVec[num].size() == 0) {
        for (short i = 0; i < windowCount; ++i) {
            windowVec[num].push_back(0);
        }
    }
    short step = windowLength/windowCount;
    uint64_t temp = windowTime[num] * step;

    if (temp > tr.time && temp - tr.time > step) return;
    if (temp > tr.time && temp - tr.time <= step) {
        windowVec[num][windowCount - 1] += tr.size;
        return;
    }
    short oneLinkTime = maxValue*8 / groupDataVec[groupCount].dataRate + groupDataVec[groupCount].linkDelay;
    if (tr.time >= temp && tr.time - temp > windowLength) {
        for (short i = 0; i < windowCount; ++i) {
            xy data;
            data.xTime = windowTime[num] * step;
            short j = 0;
            for (auto &item : windowVec[num]) {
                if (oneLinkTime > (j * step + step/2)) {
                    data.yCount = data.yCount + (j * step + step/2) * item / oneLinkTime;
                } else {
                    data.yCount += item;
                }
                j++;
            }
            data.yCount = 8 * data.yCount/windowLength;
            timeToCount[num].push_back(data);
            windowVec[num][i] = 0;
            windowTime[num]++;
        }
        xy data0;
        data0.xTime = windowTime[num] * step;
        data0.yCount = 0;
        timeToCount[num].push_back(data0);
        windowTime[num] = tr.time/step;
        xy data;
        data.xTime = windowTime[num] * step;
        data.yCount = 0;
        timeToCount[num].push_back(data);
        windowTime[num]++;
        windowVec[num][windowCount - 1] += tr.size;
    } else if (tr.time >= temp) {
        short count = (tr.time - step * windowTime[num]) / step + 1;
        for (short i = 0; i < count; ++i) {
            xy data;
            data.xTime = windowTime[num] * step;
            short j = 0;
            for (auto &item : windowVec[num]) {
                if (oneLinkTime > (j * step + step/2)) {
                    data.yCount = data.yCount + (j * step + step/2) * item / oneLinkTime;
                } else {
                    data.yCount += item;
                }
                j++;
            }
            for (short j = 1; j < windowCount; ++j) {
                windowVec[num][j - 1] = windowVec[num][j];
            }
            windowVec[num][windowCount - 1] = 0;
            data.yCount = 8 * data.yCount/windowLength;
            windowTime[num]++;
            timeToCount[num].push_back(data);
        }
        windowVec[num][windowCount - 1] = tr.size;
    }
}

static inline char l3ProtToChar(uint8_t p){
	switch (p){
		case 0x6:
			return 'T';
		case 0x11:
			return 'U';
		case 0xFC: // ACK
			return 'A';
		case 0xFD: // NACK
			return 'N';
		case 0xFE: // PFC
			return 'P';
		case 0xFF:
			return 'C';
		default:
			return 'X';
	}
}



#endif //SIMAI_MASTER_PRINT_CHART_HPP
