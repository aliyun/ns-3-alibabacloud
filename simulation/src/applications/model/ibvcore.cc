#include "ibvcore.h"
#include <sys/time.h>
void CustomTimePrinter(std::ostream &os) {
	struct timeval tv;
	struct tm *tm_info;
	char time_str[200];
	char total_time_str[240];

	// 获取当前时间
	if (gettimeofday(&tv, NULL) != 0) {
		printf("Error in gettimeofday");
		return;
	}
	tm_info = localtime(&tv.tv_sec);
	strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
	// 打印时间，包括微秒
	sprintf(total_time_str, "[%s.%06ld]", time_str, tv.tv_usec);
	os << total_time_str;
}
    